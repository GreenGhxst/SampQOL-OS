// SPDX-License-Identifier: MIT
#include "mpcvst/state_file.h"
#include "mpcvst/vst2_plugin.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <unistd.h>

namespace mpcvst {

uint32_t crc32_of(const uint8_t* data, size_t size, uint32_t seed) {
    static uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        ready = true;
    }
    uint32_t crc = seed ^ 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

namespace {

void put_u32(std::vector<uint8_t>& out, uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
}

uint32_t get_u32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8 | static_cast<uint32_t>(p[2]) << 16 | static_cast<uint32_t>(p[3]) << 24;
}

std::string clean(const std::string& s) {
    std::string out;
    for (char c : s) out.push_back(c == '\n' ? ' ' : c);
    return out;
}

} // namespace

std::vector<uint8_t> encode_state(const PluginState& s) {
    std::ostringstream h;
    h << "plugin_path=" << clean(s.plugin_path) << '\n'
      << "plugin_name=" << clean(s.plugin_name) << '\n'
      << "unique_id=" << s.unique_id << '\n'
      << "plugin_version=" << s.plugin_version << '\n'
      << "pad=" << s.pad << '\n'
      << "track=" << s.track << '\n'
      << "midi_channel=" << s.midi_channel << '\n'
      << "transpose=" << s.transpose << '\n'
      << "program=" << s.program << '\n';
    for (const std::string& r : s.resource_files) h << "resource=" << clean(r) << '\n';
    for (const auto& kv : s.extra) h << "x." << clean(kv.first) << '=' << clean(kv.second) << '\n';
    std::string header = h.str();

    std::vector<uint8_t> out;
    out.insert(out.end(), {'M', 'V', 'X', 'S'});
    put_u32(out, 1);
    put_u32(out, static_cast<uint32_t>(header.size()));
    out.insert(out.end(), header.begin(), header.end());
    put_u32(out, static_cast<uint32_t>(s.body_kind));
    if (s.body_kind == StateBodyKind::Chunk) {
        put_u32(out, static_cast<uint32_t>(s.chunk.size()));
        out.insert(out.end(), s.chunk.begin(), s.chunk.end());
    } else if (s.body_kind == StateBodyKind::Parameters) {
        put_u32(out, static_cast<uint32_t>(s.params.size() * 4));
        for (float f : s.params) {
            uint32_t bits;
            std::memcpy(&bits, &f, 4);
            put_u32(out, bits);
        }
    } else {
        put_u32(out, 0);
    }
    put_u32(out, crc32_of(out.data(), out.size()));
    return out;
}

bool decode_state(const uint8_t* data, size_t size, PluginState& state, std::string& error) {
    if (size < 20 || std::memcmp(data, "MVXS", 4) != 0) { error = "not a state file"; return false; }
    if (crc32_of(data, size - 4) != get_u32(data + size - 4)) { error = "checksum mismatch (damaged file)"; return false; }
    uint32_t version = get_u32(data + 4);
    if (version != 1) { error = "unknown state version " + std::to_string(version); return false; }
    uint32_t hlen = get_u32(data + 8);
    size_t off = 12;
    if (hlen > size - off - 12) { error = "truncated header"; return false; }
    std::string header(reinterpret_cast<const char*>(data + off), hlen);
    off += hlen;
    uint32_t kind = get_u32(data + off); off += 4;
    uint32_t blen = get_u32(data + off); off += 4;
    if (blen > size - off - 4) { error = "truncated body"; return false; }

    PluginState s;
    size_t start = 0;
    while (start < header.size()) {
        size_t end = header.find('\n', start);
        if (end == std::string::npos) end = header.size();
        std::string line = header.substr(start, end - start);
        start = end + 1;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        auto num = [&]() { return static_cast<int>(std::strtol(v.c_str(), nullptr, 10)); };
        if (k == "plugin_path") s.plugin_path = v;
        else if (k == "plugin_name") s.plugin_name = v;
        else if (k == "unique_id") s.unique_id = num();
        else if (k == "plugin_version") s.plugin_version = num();
        else if (k == "pad") s.pad = num();
        else if (k == "track") s.track = num();
        else if (k == "midi_channel") s.midi_channel = num();
        else if (k == "transpose") s.transpose = num();
        else if (k == "program") s.program = num();
        else if (k == "resource") s.resource_files.push_back(v);
        else if (k.size() > 2 && k[0] == 'x' && k[1] == '.') s.extra.emplace_back(k.substr(2), v);
    }
    if (kind == 1) {
        s.body_kind = StateBodyKind::Chunk;
        s.chunk.assign(data + off, data + off + blen);
    } else if (kind == 2) {
        if (blen % 4) { error = "parameter body not a multiple of 4"; return false; }
        s.body_kind = StateBodyKind::Parameters;
        for (uint32_t i = 0; i < blen; i += 4) {
            uint32_t bits = get_u32(data + off + i);
            float f;
            std::memcpy(&f, &bits, 4);
            s.params.push_back(f);
        }
    } else if (kind == 0) {
        s.body_kind = StateBodyKind::None;
    } else {
        error = "unknown body kind " + std::to_string(kind);
        return false;
    }
    state = std::move(s);
    return true;
}

bool save_state_file(const std::string& path, const PluginState& state, std::string& error) {
    std::vector<uint8_t> bytes = encode_state(state);
    std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) { error = "cannot write " + tmp; return false; }
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!out) { error = "write failed"; return false; }
    }
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        error = std::string("rename failed: ") + std::strerror(errno);
        unlink(tmp.c_str());
        return false;
    }
    return true;
}

bool load_state_file(const std::string& path, PluginState& state, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) { error = "cannot open " + path; return false; }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.size() > (72u << 20)) { error = "state file too large"; return false; }
    return decode_state(bytes.data(), bytes.size(), state, error);
}

void capture_plugin_state(const Vst2Plugin& plugin, PluginState& s) {
    const PluginInfo& info = plugin.info();
    s.plugin_path = info.path;
    s.plugin_name = info.name;
    s.unique_id = info.unique_id;
    s.plugin_version = info.version;
    s.program = plugin.current_program();
    s.chunk.clear();
    s.params.clear();
    if (info.has_chunks && plugin.get_chunk(s.chunk)) {
        s.body_kind = StateBodyKind::Chunk;
        return;
    }
    s.body_kind = StateBodyKind::Parameters;
    for (int i = 0; i < plugin.num_params(); ++i) s.params.push_back(plugin.get_param(i));
}

bool apply_plugin_state(Vst2Plugin& plugin, const PluginState& s, std::string& error) {
    const PluginInfo& info = plugin.info();
    if (s.unique_id != 0 && info.unique_id != 0 && s.unique_id != info.unique_id) {
        error = "state belongs to another plug-in (id " + std::to_string(s.unique_id) + ", loaded " + std::to_string(info.unique_id) + ")";
        return false;
    }
    if (s.program >= 0 && s.program < plugin.num_programs()) plugin.set_program(s.program);
    if (s.body_kind == StateBodyKind::Chunk) {
        if (!plugin.set_chunk(s.chunk.data(), s.chunk.size())) {
            error = "plug-in refused its state chunk";
            return false;
        }
    } else if (s.body_kind == StateBodyKind::Parameters) {
        int n = static_cast<int>(s.params.size());
        if (n != plugin.num_params()) error = "parameter count differs (" + std::to_string(n) + " saved, " + std::to_string(plugin.num_params()) + " now); applied what matched";
        for (int i = 0; i < n && i < plugin.num_params(); ++i) plugin.set_param(i, s.params[static_cast<size_t>(i)]);
    }
    return true;
}

} // namespace mpcvst
