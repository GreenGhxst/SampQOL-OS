#pragma once
// MPC Sample hardware boundary: source-derived protocol facts, not a claim
// of a verified device hookup. No ALSA/DRM ownership or stock-app injection.
#include "native_model.h"
#include <cstdint>
#include <string>
#include <vector>
namespace dm::native {
enum class InputResult { ignored, dispatched, disabled };
struct SurfaceEvent { std::uint8_t status=0, number=0, value=0; };
class SurfaceMapper {
public:
    explicit SurfaceMapper(bool enabled=false) : enabled_(enabled) {}
    void enable(bool on) { enabled_=on; }
    bool enabled() const { return enabled_; }
    // Incoming complete channel voice message from a future *verified*
    // local surface endpoint. Map only known channel-one buttons and wheel.
    InputResult route(const SurfaceEvent& event, Controller& controller) const;
private:
    bool enabled_;
};
struct DisplayInfo { bool available=false; std::string reason; std::vector<std::string> nodes; };
// Read-only candidate discovery. Never calls drmSetMaster, opens DRM
// devices for writing, changes tty, backlight, or stock service state.
DisplayInfo probe_display_candidates(const std::string& dri_directory="/dev/dri");
} // namespace dm::native
