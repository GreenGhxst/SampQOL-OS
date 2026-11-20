#include "wav.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <unistd.h>

namespace dm {

json WavInfo::to_json() const {
    return json{{"valid", valid}, {"supported", supported}, {"format", format}, {"channels", channels}, {"bits", bits},
                {"sample_rate", sample_rate}, {"frames", frames}, {"seconds", seconds}, {"note", note}};
}

namespace {
std::uint32_t le32(const unsigned char* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (std::uint32_t(p[3]) << 24); }
std::uint16_t le16(const unsigned char* p) { return std::uint16_t(p[0] | (p[1] << 8)); }
bool pread_all(int fd, void* buf, std::size_t n, std::uint64_t off) {
    auto* p = static_cast<unsigned char*>(buf);
    while (n) { ssize_t r = ::pread(fd, p, n, off); if (r <= 0) return false; p += r; n -= std::size_t(r); off += std::uint64_t(r); }
    return true;
}
} // namespace

WavInfo wav_probe(int fd) {
    WavInfo w;
    unsigned char hdr[12];
    if (!pread_all(fd, hdr, 12, 0) || std::memcmp(hdr, "RIFF", 4) || std::memcmp(hdr + 8, "WAVE", 4)) { w.note = "not a RIFF/WAVE file"; return w; }
    std::uint64_t off = 12; bool have_fmt = false, have_data = false;
    for (int guard = 0; guard < 64 && !(have_fmt && have_data); ++guard) {
        unsigned char ch[8];
        if (!pread_all(fd, ch, 8, off)) break;
        std::uint32_t sz = le32(ch + 4);
        if (!std::memcmp(ch, "fmt ", 4) && sz >= 16) {
            unsigned char f[40]; std::size_t n = std::min<std::size_t>(sz, 40);
            if (!pread_all(fd, f, n, off + 8)) break;
            w.format = le16(f); w.channels = le16(f + 2); w.sample_rate = le32(f + 4); w.bits = le16(f + 14);
            if (w.format == 0xFFFE && sz >= 40) w.format = le16(f + 24);   // WAVE_FORMAT_EXTENSIBLE subformat
            have_fmt = true;
        } else if (!std::memcmp(ch, "data", 4)) {
            w.data_offset = off + 8; w.data_bytes = sz; have_data = true;
        }
        off += 8 + sz + (sz & 1);
    }
    if (!have_fmt || !have_data) { w.note = "missing fmt or data chunk"; return w; }
    w.valid = true;
    std::uint32_t block = std::uint32_t(w.channels) * (w.bits / 8);
    if (block == 0 || w.channels == 0 || w.sample_rate == 0) { w.note = "bad format chunk"; return w; }
    w.frames = w.data_bytes / block;
    w.seconds = double(w.frames) / w.sample_rate;
    bool pcm = w.format == 1 && (w.bits == 8 || w.bits == 16 || w.bits == 24 || w.bits == 32);
    bool flt = w.format == 3 && w.bits == 32;
    w.supported = (pcm || flt) && w.channels <= 8;
    if (!w.supported) w.note = "format " + std::to_string(w.format) + "/" + std::to_string(w.bits) + "-bit not supported for preview";
    return w;
}

Status wav_peaks(int fd, const WavInfo& info, std::size_t buckets, std::vector<float>& peaks, std::uint64_t max_bytes) {
    if (!info.supported) return Status::fail(Err::unsupported, info.note);
    buckets = std::clamp<std::size_t>(buckets, 1, 4096);
    peaks.assign(buckets * 2, 0.f);
    std::uint32_t bps = info.bits / 8, block = info.channels * bps;
    std::uint64_t frames = info.frames;
    if (frames == 0) return Status::good();
    // Stride so very long files are sampled instead of fully read.
    std::uint64_t stride = 1;
    while (frames / stride * block > max_bytes) stride *= 2;
    std::vector<unsigned char> buf(1 << 20);
    std::uint64_t frame = 0;
    for (std::size_t b = 0; b < buckets; ++b) { peaks[b*2] = 1.f; peaks[b*2+1] = -1.f; }
    while (frame < frames) {
        std::uint64_t count = std::min<std::uint64_t>(buf.size() / block, frames - frame);
        if (!pread_all(fd, buf.data(), std::size_t(count * block), info.data_offset + frame * block)) break;
        for (std::uint64_t i = 0; i < count; i += stride) {
            const unsigned char* p = buf.data() + i * block;
            float mix = 0.f;
            for (unsigned c = 0; c < info.channels; ++c, p += bps) {
                float v = 0.f;
                if (info.format == 3) { std::memcpy(&v, p, 4); }
                else if (bps == 1) v = (int(p[0]) - 128) / 128.f;
                else if (bps == 2) v = std::int16_t(le16(p)) / 32768.f;
                else if (bps == 3) { std::int32_t s = (p[0] << 8) | (p[1] << 16) | (std::int32_t(p[2]) << 24); v = (s >> 8) / 8388608.f; }
                else if (bps == 4) v = std::int32_t(le32(p)) / 2147483648.f;
                mix += v;
            }
            mix /= info.channels;
            std::size_t bk = std::size_t((frame + i) * buckets / frames);
            if (bk >= buckets) bk = buckets - 1;
            peaks[bk*2] = std::min(peaks[bk*2], mix); peaks[bk*2+1] = std::max(peaks[bk*2+1], mix);
        }
        frame += count;
    }
    for (std::size_t b = 0; b < buckets; ++b) if (peaks[b*2] > peaks[b*2+1]) { peaks[b*2] = 0; peaks[b*2+1] = 0; }
    return Status::good();
}

} // namespace dm
