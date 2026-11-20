// SPDX-License-Identifier: MIT
#include "mpcvst/offline_render.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>

namespace mpcvst {

bool parse_midi_script(const std::string& text, std::vector<ScriptEvent>& out, std::string& error) {
    std::istringstream in(text);
    std::string line;
    int line_no = 0;
    while (std::getline(in, line)) {
        ++line_no;
        size_t hash = line.find('#');
        if (hash != std::string::npos) line = line.substr(0, hash);
        std::istringstream ls(line);
        double t;
        std::string kind;
        if (!(ls >> t >> kind)) continue; // blank
        auto bad = [&](const char* why) { error = "line " + std::to_string(line_no) + ": " + why; return false; };
        if (t < 0) return bad("negative time");
        ScriptEvent e;
        e.seconds = t;
        int ch = 1, a = 0, b = 0;
        if (kind == "on" || kind == "off" || kind == "cc") {
            if (!(ls >> ch >> a)) return bad("expected channel and value");
            if (!(ls >> b)) b = kind == "off" ? 64 : 0;
            if (kind == "cc" && (ls.fail())) return bad("cc needs a value");
            if (ch < 1 || ch > 16 || a < 0 || a > 127 || b < 0 || b > 127) return bad("value out of range");
            e.data[0] = static_cast<uint8_t>((kind == "on" ? 0x90 : kind == "off" ? 0x80 : 0xB0) | (ch - 1));
            e.data[1] = static_cast<uint8_t>(a);
            e.data[2] = static_cast<uint8_t>(b);
            e.length = 3;
        } else if (kind == "bend") {
            if (!(ls >> ch >> a)) return bad("bend needs channel and value");
            if (ch < 1 || ch > 16 || a < -8192 || a > 8191) return bad("bend out of range");
            int v = a + 8192;
            e.data[0] = static_cast<uint8_t>(0xE0 | (ch - 1));
            e.data[1] = static_cast<uint8_t>(v & 0x7F);
            e.data[2] = static_cast<uint8_t>((v >> 7) & 0x7F);
            e.length = 3;
        } else if (kind == "pc") {
            if (!(ls >> ch >> a) || ch < 1 || ch > 16 || a < 0 || a > 127) return bad("pc needs channel and program");
            e.data[0] = static_cast<uint8_t>(0xC0 | (ch - 1));
            e.data[1] = static_cast<uint8_t>(a);
            e.length = 2;
        } else if (kind == "sustain") {
            std::string state;
            if (!(ls >> ch >> state) || ch < 1 || ch > 16) return bad("sustain needs channel and on|off");
            e.data[0] = static_cast<uint8_t>(0xB0 | (ch - 1));
            e.data[1] = 64;
            e.data[2] = state == "on" ? 127 : 0;
            e.length = 3;
        } else if (kind == "allnotesoff") {
            for (int c = 0; c < 16; ++c) {
                ScriptEvent x = e;
                x.data[0] = static_cast<uint8_t>(0xB0 | c);
                x.data[1] = 123;
                x.data[2] = 0;
                x.length = 3;
                out.push_back(x);
            }
            continue;
        } else {
            return bad("unknown event");
        }
        out.push_back(e);
    }
    std::stable_sort(out.begin(), out.end(), [](const ScriptEvent& a, const ScriptEvent& b) { return a.seconds < b.seconds; });
    return true;
}

RenderResult render_offline(PluginChain& chain, const RenderOptions& options) {
    RenderResult r;
    const EngineConfig& cfg = chain.config();
    const uint64_t total = static_cast<uint64_t>(std::max(0.0, options.seconds) * cfg.sample_rate);
    r.left.assign(static_cast<size_t>(total), 0.0f);
    r.right.assign(static_cast<size_t>(total), 0.0f);
    chain.transport().set_tempo(options.tempo);
    chain.transport().set_playing(options.playing);
    chain.transport().rewind();
    chain.perf().reset();

    std::vector<ScriptEvent> script = options.midi;
    std::stable_sort(script.begin(), script.end(), [](const ScriptEvent& a, const ScriptEvent& b) { return a.seconds < b.seconds; });
    size_t next_event = 0;

    std::vector<float> in_l(static_cast<size_t>(cfg.block_size)), in_r(static_cast<size_t>(cfg.block_size));
    const bool has_input = !options.input_left.empty();
    uint64_t pos = 0;
    double sum_sq = 0.0;
    while (pos < total) {
        const int frames = static_cast<int>(std::min<uint64_t>(static_cast<uint64_t>(cfg.block_size), total - pos));
        // Queue what falls in this block (ahead of time, as a sequencer would).
        const uint64_t block_end = pos + static_cast<uint64_t>(frames);
        while (next_event < script.size()) {
            const ScriptEvent& e = script[next_event];
            uint64_t frame = static_cast<uint64_t>(e.seconds * cfg.sample_rate);
            if (frame >= block_end) break;
            chain.midi().push(frame, e.data, e.length);
            ++next_event;
        }
        const float* il = nullptr; const float* ir = nullptr;
        if (has_input) {
            for (int i = 0; i < frames; ++i) {
                size_t k = static_cast<size_t>(pos) + static_cast<size_t>(i);
                in_l[static_cast<size_t>(i)] = k < options.input_left.size() ? options.input_left[k] : 0.0f;
                in_r[static_cast<size_t>(i)] = k < options.input_right.size() ? options.input_right[k] : in_l[static_cast<size_t>(i)];
            }
            il = in_l.data(); ir = in_r.data();
        }
        chain.process(il, ir, r.left.data() + pos, r.right.data() + pos, frames);
        for (int i = 0; i < frames; ++i) {
            float l = r.left[static_cast<size_t>(pos) + static_cast<size_t>(i)], rr = r.right[static_cast<size_t>(pos) + static_cast<size_t>(i)];
            r.peak = std::max(r.peak, std::max(std::fabs(l), std::fabs(rr)));
            sum_sq += 0.5 * (static_cast<double>(l) * l + static_cast<double>(rr) * rr);
        }
        pos += static_cast<uint64_t>(frames);
    }
    r.rms = total ? std::sqrt(sum_sq / static_cast<double>(total)) : 0.0;
    r.perf = chain.perf().snapshot();
    r.midi_dropped = chain.midi().dropped();
    if (chain.instrument()) r.bad_samples += chain.instrument()->bad_samples();
    for (auto& fx : chain.effects()) r.bad_samples += fx->bad_samples();
    return r;
}

namespace {
void put_u32(std::vector<uint8_t>& o, uint32_t v) { for (int i = 0; i < 4; ++i) o.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF)); }
void put_u16(std::vector<uint8_t>& o, uint16_t v) { o.push_back(static_cast<uint8_t>(v & 0xFF)); o.push_back(static_cast<uint8_t>(v >> 8)); }
uint32_t rd_u32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | static_cast<uint32_t>(p[3]) << 24; }
uint16_t rd_u16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | p[1] << 8); }
}

bool write_wav(const std::string& path, const std::vector<float>& left, const std::vector<float>& right, double sample_rate,
               bool float32, std::string& error) {
    const uint32_t frames = static_cast<uint32_t>(left.size());
    const uint16_t channels = 2, bits = float32 ? 32 : 16, block_align = static_cast<uint16_t>(channels * bits / 8);
    const uint32_t data_bytes = frames * block_align;
    std::vector<uint8_t> o;
    o.reserve(44 + data_bytes + 14);
    o.insert(o.end(), {'R', 'I', 'F', 'F'});
    put_u32(o, 4 + (8 + (float32 ? 18 : 16)) + (float32 ? 12 : 0) + 8 + data_bytes);
    o.insert(o.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    put_u32(o, float32 ? 18 : 16);
    put_u16(o, float32 ? 3 : 1);
    put_u16(o, channels);
    put_u32(o, static_cast<uint32_t>(sample_rate));
    put_u32(o, static_cast<uint32_t>(sample_rate) * block_align);
    put_u16(o, block_align);
    put_u16(o, bits);
    if (float32) {
        put_u16(o, 0);
        o.insert(o.end(), {'f', 'a', 'c', 't'});
        put_u32(o, 4);
        put_u32(o, frames);
    }
    o.insert(o.end(), {'d', 'a', 't', 'a'});
    put_u32(o, data_bytes);
    for (uint32_t i = 0; i < frames; ++i) {
        float s[2] = {left[i], i < right.size() ? right[i] : left[i]};
        for (float v : s) {
            if (float32) {
                uint32_t bitsv; std::memcpy(&bitsv, &v, 4); put_u32(o, bitsv);
            } else {
                float c = std::max(-1.0f, std::min(1.0f, v));
                put_u16(o, static_cast<uint16_t>(static_cast<int16_t>(std::lrint(c * 32767.0f))));
            }
        }
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) { error = "cannot write " + path; return false; }
    out.write(reinterpret_cast<const char*>(o.data()), static_cast<std::streamsize>(o.size()));
    if (!out) { error = "write failed"; return false; }
    return true;
}

bool read_wav(const std::string& path, std::vector<float>& left, std::vector<float>& right, double& sample_rate, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) { error = "cannot open " + path; return false; }
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (d.size() < 12 || std::memcmp(d.data(), "RIFF", 4) != 0 || std::memcmp(d.data() + 8, "WAVE", 4) != 0) { error = "not a WAV file"; return false; }
    uint16_t format = 0, channels = 0, bits = 0;
    size_t pos = 12, data_off = 0, data_len = 0;
    while (pos + 8 <= d.size()) {
        uint32_t len = rd_u32(d.data() + pos + 4);
        if (std::memcmp(d.data() + pos, "fmt ", 4) == 0 && pos + 8 + 16 <= d.size()) {
            format = rd_u16(d.data() + pos + 8);
            channels = rd_u16(d.data() + pos + 10);
            sample_rate = rd_u32(d.data() + pos + 12);
            bits = rd_u16(d.data() + pos + 22);
            if (format == 0xFFFE && len >= 26) format = rd_u16(d.data() + pos + 8 + 24); // extensible: sub-format's first word
        } else if (std::memcmp(d.data() + pos, "data", 4) == 0) {
            data_off = pos + 8;
            data_len = std::min<size_t>(len, d.size() - data_off);
            break;
        }
        pos += 8 + len + (len & 1);
    }
    if (!data_off || channels == 0 || channels > 2) { error = "unsupported WAV (need mono or stereo PCM)"; return false; }
    const size_t frame_bytes = static_cast<size_t>(channels) * bits / 8;
    if (!frame_bytes) { error = "bad WAV format"; return false; }
    const size_t frames = data_len / frame_bytes;
    left.resize(frames); right.resize(frames);
    for (size_t i = 0; i < frames; ++i) {
        for (uint16_t c = 0; c < channels; ++c) {
            const uint8_t* p = d.data() + data_off + i * frame_bytes + c * bits / 8;
            float v = 0.0f;
            if (format == 3 && bits == 32) { uint32_t b = rd_u32(p); std::memcpy(&v, &b, 4); }
            else if (format == 1 && bits == 16) v = static_cast<int16_t>(rd_u16(p)) / 32768.0f;
            else if (format == 1 && bits == 24) { int32_t s = (p[0] << 8) | (p[1] << 16) | (static_cast<int32_t>(p[2]) << 24); v = static_cast<float>(s >> 8) / 8388608.0f; }
            else if (format == 1 && bits == 32) v = static_cast<int32_t>(rd_u32(p)) / 2147483648.0f;
            else { error = "unsupported sample format"; return false; }
            (c == 0 ? left : right)[i] = v;
        }
        if (channels == 1) right[i] = left[i];
    }
    return true;
}

} // namespace mpcvst
