# DISKMODE native hardware interface gate — source-derived, not hardware-proven

Updated: 2026-11-22. Controlling product scope: `CODEX_MASTER_BUILD_HANDOFF_v1.1.md`.

## Verified upstream source references

- `keremimo/sample-base` at `f18e620104fe579727c5f13f796016ba08e3a394`, `src/drivers/display.cpp` and `display.h`: the MPC Sample panel is 320×240 (physical portrait panel rotated). DRM/KMS dumb buffer presentation uses 32-bit XRGB8888, discovers `/dev/dri/card*` and calls **`drmSetMaster`** after explicitly stopping the stock application. The sample-base driver modifies/rebinds the panel and must restore the previous CRTC / stock service on exit. This is **NOT safe** to invoke alongside active SampQOL/stock UI, and sample-base's code is AGPL-3.0-only.
- Same pinned project, `src/surface/controls.h`: measured non-pad control surface is MIDI channel **1** (status low nibble 0); wheel relative CC **100**: `1` clockwise, `127` counterclockwise, `0` neutral; Wheel Push note **111**; Minus note **55**; Plus note **54**; Top Left note **12**. Buttons are note-on 127 press and note-off 0 release. Pads are on MIDI channel 10.
- Same pinned project, `src/drivers/hardware_midi.h` and `docs/HARDWARE.md`: UART3 `Control Surface` is accessed through ALSA raw MIDI, endpoints discovered by device names and drivers (never fixed `hw:X` numbers). Raw-MIDI ownership while the stock application is active is not verified.

## Implemented in this branch

- `native/hardware_bridge.h/.cpp`: an opt-in, independent **MIDI message mapper** translates only recognized channel-1 events to existing native `Controller` actions. The actions for Minus, Plus and Top Left are **provisional application choices**, not claimed as stock MPC functions. Other commands, releases, pads, transport keys and unsolicited values are ignored. It defaults to **disabled**.
- `probe_display_candidates`: *read-only* discovery of character-device `/dev/dri/cardN` names, no opening as DRM master, mode takeover, SPI/panel driver installation, or device state changes. A node is **not proof** that a compatible panel/connector is available.
- `DisplayGate::present`: rejects every frame with a clear reason, including a valid 320×240 XRGB8888 frame; no stock display takeover, silent fallback or simulated success.
- Unit tests check disabled-by-default behavior, relative encoder direction, wrong MIDI channel, note releases, ignored transport/pad keys, provisional button mappings and display fail-closed behavior. Existing desktop PPM renderer remains usable for development.

## Still blocked before a physical display / input release

1. **Owner-exclusive display launch**: verify service state and permissions, negotiate exclusive DRM master *without disrupting stock audio or SampQOL*, measure buffer timing, restore CRTC and re-enable prior owner after crashes. No production DRM presenter exists yet. Never run sample-base's display code directly beside stock/SampQOL.
2. **ALSA Control Surface input**: identify actual port at runtime and ensure another application is not already consuming it. Do not hardcode the ALSA card number. No ALSA reader is wired to `SurfaceMapper` yet.
3. **Device-screen frame source**: native DISKMODE render target and display-owned frames must be connected to a properly owned backend (the desktop simulator currently owns only its own PPM output). This does not provide stock framebuffer capture.
4. **Boot and recovery**: startup and clean display ownership handover require a proven launcher that does not alter signed SampQOL autorun scripts; restore stock service on failure.
5. **Manual hardware acceptance**: confirm keypad/wheel/button MIDI capture, screen orientation and dimensions, latency, resource budget, clean shutdown and crash recovery on a sacrificial SD card.

## Explicit safety rule

Until all gates have evidence from the **physical MPC Sample**, production builds must keep `SurfaceMapper` disabled and `DisplayGate::present` fail-closed. No native display control injection, kernel module substitution, power controls, formatting or modification of signed SampQOL files is authorized. Cross-compiling the bridge is not proof of actual hardware operation.

References:
- https://github.com/keremimo/sample-base/blob/f18e620104fe579727c5f13f796016ba08e3a394/src/drivers/display.cpp
- https://github.com/keremimo/sample-base/blob/f18e620104fe579727c5f13f796016ba08e3a394/src/surface/controls.h
- https://github.com/keremimo/sample-base/blob/f18e620104fe579727c5f13f796016ba08e3a394/src/drivers/hardware_midi.h
