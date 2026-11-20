// Screen Bridge adapter.
//
// Audit result (docs/AUDIT.md §3): under the stock MPC Sample application the
// panel is driven through a DRM dumb buffer with no readback, so no process
// can capture the live display. sample-base renders its own 320x240 image and
// can write a screenshot file; a future in-process tap would be the real
// path. This module therefore exposes a FrameSource interface with:
//   * SimulatedFrameSource  - test pattern; every frame is marked simulated
//   * FileFrameSource       - reads a raw RGB565 or binary PPM 320x240 file
//                             written by an external, verified pipeline
// Native capture is intentionally not claimed. Remote input is disabled.
#pragma once
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "../util/common.h"

namespace dm {

constexpr int kScreenW = 320, kScreenH = 240;

struct Frame {
    int width = kScreenW, height = kScreenH;
    std::vector<std::uint8_t> rgb565;   // little-endian, width*height*2 bytes
    std::int64_t ts_ms = 0;
    std::uint64_t seq = 0;
    std::string source;                 // "simulated" | "file" | "native"
    bool simulated = true;
};

class FrameSource {
public:
    virtual ~FrameSource() = default;
    virtual std::string name() const = 0;
    virtual bool available() const = 0;
    virtual std::string unavailable_reason() const = 0;
    virtual bool simulated() const = 0;
    virtual Status capture(Frame& out) = 0;
};

std::unique_ptr<FrameSource> make_simulated_frame_source();
// `path`: .rgb565 (153600 bytes) or binary PPM P6 320x240.
std::unique_ptr<FrameSource> make_file_frame_source(std::string path);
// Placeholder that reports why native capture is not available.
std::unique_ptr<FrameSource> make_unavailable_native_source(std::string reason);

// Validates a frame before it is sent to a client.
Status validate_frame(const Frame& f);

} // namespace dm
