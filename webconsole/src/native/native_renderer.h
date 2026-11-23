#pragma once
// Pure CPU 320x240 XRGB8888 renderer. No DRM/device ownership.
#include "native_model.h"
#include <cstdint>
#include <vector>
namespace dm::native {
class NativeRenderer {
public:
    static constexpr int width=320, height=240;
    // Byte order B,G,R,0xFF: little-endian XRGB8888 for the guarded KMS interface.
    using Frame=std::vector<std::uint8_t>;
    Frame render(const Controller& controller) const;
    Frame render(const Controller& controller,const std::vector<std::string>& review_lines) const;
};
} // namespace dm::native
