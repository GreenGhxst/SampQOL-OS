#include "screen.h"
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace dm {

namespace {
inline std::uint16_t rgb565(int r, int g, int b) { return std::uint16_t(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)); }
inline void put(Frame& f, int x, int y, std::uint16_t c) {
    if (x < 0 || y < 0 || x >= f.width || y >= f.height) return;
    std::size_t i = (std::size_t(y) * f.width + x) * 2;
    f.rgb565[i] = std::uint8_t(c & 0xff); f.rgb565[i + 1] = std::uint8_t(c >> 8);
}

// 5x7 glyphs for the letters of "SIMULATED" and digits, enough to stamp the
// frame so nobody can mistake it for a device capture.
const char* glyph(char c) {
    switch (c) {
        case 'S': return "01111 10000 10000 01110 00001 00001 11110";
        case 'I': return "11111 00100 00100 00100 00100 00100 11111";
        case 'M': return "10001 11011 10101 10101 10001 10001 10001";
        case 'U': return "10001 10001 10001 10001 10001 10001 01110";
        case 'L': return "10000 10000 10000 10000 10000 10000 11111";
        case 'A': return "01110 10001 10001 11111 10001 10001 10001";
        case 'T': return "11111 00100 00100 00100 00100 00100 00100";
        case 'E': return "11111 10000 10000 11110 10000 10000 11111";
        case 'D': return "11110 10001 10001 10001 10001 10001 11110";
        case 'F': return "11111 10000 10000 11110 10000 10000 10000";
        case 'R': return "11110 10001 10001 11110 10100 10010 10001";
        case 'N': return "10001 11001 10101 10011 10001 10001 10001";
        case 'O': return "01110 10001 10001 10001 10001 10001 01110";
        case 'V': return "10001 10001 10001 10001 10001 01010 00100";
        case 'C': return "01110 10001 10000 10000 10000 10001 01110";
        case 'P': return "11110 10001 10001 11110 10000 10000 10000";
        case ' ': return "00000 00000 00000 00000 00000 00000 00000";
        default: return "11111 10001 10001 10001 10001 10001 11111";
    }
}
void text(Frame& f, int x, int y, const std::string& s, int scale, std::uint16_t c) {
    for (char ch : s) {
        const char* g = glyph(ch);
        for (int row = 0; row < 7; ++row)
            for (int col = 0; col < 5; ++col)
                if (g[row * 6 + col] == '1')
                    for (int sy = 0; sy < scale; ++sy) for (int sx = 0; sx < scale; ++sx) put(f, x + col * scale + sx, y + row * scale + sy, c);
        x += 6 * scale;
    }
}

class SimulatedFrameSource : public FrameSource {
public:
    std::string name() const override { return "simulated"; }
    bool available() const override { return true; }
    std::string unavailable_reason() const override { return ""; }
    bool simulated() const override { return true; }
    Status capture(Frame& f) override {
        f.width = kScreenW; f.height = kScreenH; f.rgb565.assign(std::size_t(kScreenW) * kScreenH * 2, 0);
        f.ts_ms = now_ms(); f.seq = ++seq_; f.source = "simulated"; f.simulated = true;
        for (int y = 0; y < kScreenH; ++y) for (int x = 0; x < kScreenW; ++x) {
            int band = x * 8 / kScreenW;
            int r = (band & 1) ? 90 : 20, g = (band & 2) ? 90 : 20, b = (band & 4) ? 120 : 30;
            if (y < 24 || y > kScreenH - 24) { r = g = b = 16; }
            put(f, x, y, rgb565(r, g, b));
        }
        int marker = int(seq_ % kScreenW);
        for (int y = 24; y < kScreenH - 24; ++y) put(f, marker, y, rgb565(255, 200, 0));
        text(f, 22, 60, "SIMULATED", 6, rgb565(255, 255, 255));
        text(f, 40, 120, "NOT A DEVICE", 4, rgb565(255, 120, 120));
        text(f, 94, 160, "CAPTURE", 4, rgb565(255, 120, 120));
        text(f, 8, 6, "DISKMODE SCREEN", 2, rgb565(180, 180, 180));
        return Status::good();
    }
private:
    std::uint64_t seq_ = 0;
};

class FileFrameSource : public FrameSource {
public:
    explicit FileFrameSource(std::string p) : path_(std::move(p)) {}
    std::string name() const override { return "file"; }
    bool available() const override { struct stat st{}; return ::stat(path_.c_str(), &st) == 0 && S_ISREG(st.st_mode); }
    std::string unavailable_reason() const override { return available() ? "" : "capture file not present: " + path_; }
    bool simulated() const override { return false; }
    Status capture(Frame& f) override {
        int fd = ::open(path_.c_str(), O_RDONLY | O_CLOEXEC);
        if (fd < 0) return Status::fail(Err::unavailable, "capture file not readable");
        std::vector<std::uint8_t> buf;
        std::uint8_t tmp[65536]; ssize_t n;
        while ((n = ::read(fd, tmp, sizeof tmp)) > 0 && buf.size() < (4u << 20)) buf.insert(buf.end(), tmp, tmp + n);
        ::close(fd);
        f.width = kScreenW; f.height = kScreenH; f.ts_ms = now_ms(); f.seq = ++seq_; f.source = "file"; f.simulated = false;
        const std::size_t need = std::size_t(kScreenW) * kScreenH * 2;
        if (buf.size() == need) { f.rgb565 = std::move(buf); return Status::good(); }
        if (buf.size() > 2 && buf[0] == 'P' && buf[1] == '6') {
            // P6 <w> <h> <max>\n then RGB triplets
            std::size_t i = 2; int vals[3]; int k = 0;
            while (k < 3 && i < buf.size()) {
                while (i < buf.size() && std::isspace(buf[i])) ++i;
                if (i < buf.size() && buf[i] == '#') { while (i < buf.size() && buf[i] != '\n') ++i; continue; }
                int v = 0; bool any = false;
                while (i < buf.size() && std::isdigit(buf[i])) { v = v * 10 + (buf[i] - '0'); ++i; any = true; }
                if (!any) break;
                vals[k++] = v;
            }
            ++i;
            if (k == 3 && vals[0] == kScreenW && vals[1] == kScreenH && vals[2] == 255 && buf.size() >= i + need / 2 * 3) {
                f.rgb565.assign(need, 0);
                for (int y = 0; y < kScreenH; ++y) for (int x = 0; x < kScreenW; ++x) {
                    const std::uint8_t* p = &buf[i + (std::size_t(y) * kScreenW + x) * 3];
                    put(f, x, y, rgb565(p[0], p[1], p[2]));
                }
                return Status::good();
            }
        }
        return Status::fail(Err::invalid_argument, "capture file is not a 320x240 RGB565 or P6 PPM image");
    }
private:
    std::string path_;
    std::uint64_t seq_ = 0;
};

class UnavailableNative : public FrameSource {
public:
    explicit UnavailableNative(std::string r) : reason_(std::move(r)) {}
    std::string name() const override { return "native"; }
    bool available() const override { return false; }
    std::string unavailable_reason() const override { return reason_; }
    bool simulated() const override { return false; }
    Status capture(Frame&) override { return Status::fail(Err::unavailable, reason_); }
private:
    std::string reason_;
};
} // namespace

std::unique_ptr<FrameSource> make_simulated_frame_source() { return std::make_unique<SimulatedFrameSource>(); }
std::unique_ptr<FrameSource> make_file_frame_source(std::string p) { return std::make_unique<FileFrameSource>(std::move(p)); }
std::unique_ptr<FrameSource> make_unavailable_native_source(std::string r) { return std::make_unique<UnavailableNative>(std::move(r)); }

Status validate_frame(const Frame& f) {
    if (f.width != kScreenW || f.height != kScreenH) return Status::fail(Err::invalid_argument, "frame is not 320x240");
    if (f.rgb565.size() != std::size_t(kScreenW) * kScreenH * 2) return Status::fail(Err::invalid_argument, "frame size mismatch");
    return Status::good();
}

} // namespace dm
