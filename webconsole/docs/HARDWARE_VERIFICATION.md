# Hardware verification report

**No MPC Sample was available to this project. Nothing below was run on the
device.** The table separates what was done from what remains.

## Evidence levels used in this repository

| Level | Meaning |
|---|---|
| simulated | behaviour exercised against temporary directories and fake inventories |
| source-verified | claim read directly from upstream source files (cited in `docs/AUDIT.md`) |
| compiled | built for the target architecture with a cross toolchain |
| emulated | the AArch64 binary executed under `qemu-aarch64` user-mode emulation |
| hardware-tested | executed on an Akai MPC Sample — **none yet** |

## What was done (2026-11-20)

| Item | Level | Result |
|---|---|---|
| C++ unit tests (`core_tests`, 29 cases) on x86-64 host | simulated | pass |
| HTTP end-to-end suite (`tests/http/test_api.py`, 17 cases) against the real binary | simulated | pass |
| Browser smoke test (Chromium via Playwright: pair, browse, copy, conflict dialog, drag-and-drop, context menu, trash, upload, all views, phone and tablet layouts) | simulated | pass, no console errors |
| Cross-compile, `aarch64-linux-gnu-g++ 13`, `-DDM_WITH_TLS=OFF -DDM_STATIC_LIBSTDCXX=ON` | compiled | `ELF 64-bit ARM aarch64`, 3.8 MB; needs `GLIBC_2.38` symbols (`__isoc23_strto*`) because the Ubuntu 24.04 cross glibc is 2.39 — **may not run on the device if its glibc is older** |
| Cross-compile, `-DDM_FULLY_STATIC=ON` | compiled | statically linked, 4.7 MB, no glibc version dependency (linker warning about `getaddrinfo` in static binaries: only numeric addresses are used) |
| `core_tests` (static AArch64) under `qemu-aarch64-static` | emulated | 29/29 pass |
| `diskmode-webconsole --self-test` and `GET /api/v1/health`, `GET /` (static AArch64) under qemu | emulated | ok |
| Host resource use (x86-64, idle, four HTTP threads, two workers) | simulated | RSS ≈ 14 MB |

## Not performed (required before any release claim)

| Specification item | Status | How to verify |
|---|---|---|
| Runs on the MPC Sample | not tested | copy `build-arm64-static/diskmode-webconsole` + `web/` to the card, start from an autorun script (`docs/BUILD.md` §5), check `events.log` |
| Reachable over the verified USB-C link | not tested | read the NCM interface address from *SD Utilities → USB-C network*; confirm the host gets an address |
| Internal storage respects protected directories | source-verified only | confirm `/data/UserContent/*` and `/content` exist and are what the policy assumes; keep read-only until writes are proven safe next to `ac50.service` |
| microSD browsing and management | simulated | real exfat card at `/media/<id>`: list, copy, move, rename, trash/restore, upload, download; pull the card mid-copy and check the journal marks `interrupted` and removes `.dmtmp-*` |
| SD Card Access (host-owned card) blocks writes | source-verified | enable SD Card Access on the device, confirm every operation returns `busy` (the NCM link is expected to drop) |
| Physical controls remain functional | n/a | the console never touches input; verify pads/buttons while a screen stream runs |
| Screen Bridge shows device frames | **not possible under the stock app** | only a verified external capture (file source) or an in-process tap in a sample-base app can provide real frames |
| Formatting | simulation only | see `docs/FORMAT_AUDIT.md` |
| CPU / RAM / throughput on hardware | not measured | `top` while uploading a 500 MB file over NCM; measure fps of the screen viewer |
| HTTPS certificate trust on iPadOS | documented only | `docs/SECURITY.md` process |
| Clean stop/restart | emulated only | `SIGTERM` → journal flushed, index saved; restart replays journal |

## Known limitations carried into the hardware phase

* glibc: use the fully static build until the device's glibc version is known.
* The `flash_sd` media check fails closed without edisksd; formatting cannot
  become eligible on hardware with this build, by design.
* Audition plays in the browser only; no device-side playback.
* Remote control stays disabled (501).


## Phase 4 read-only evidence (not a hardware authorization)

Run `diskmode-native-diagnostics --evidence` to print one JSON report to stdout. It uses existing read-only kernel queries, enumerates DRM *device-node candidates* without claiming DRM master, reads up to 64 lines of `/proc/asound/cards` without opening MIDI endpoints, and uses the existing volume registry to report mount and USB-export ownership observations. No device paths are opened for write.

`hardware_verified`, `ownership_granted`, `recovery_verified`, `panel_verified`, and `mapping_verified` are always false. Storage `ownership_observation` is `usb-export-detected`, `unmounted`, or `unknown`; absence of a USB-export entry does **not** verify device ownership because configfs may be unreadable. Unmounted volumes report null filesystem/read-only values. An unreadable ALSA inventory reports a null control-surface hint. Reports omit mount paths, serial identifiers, and the kernel release/build fingerprint; only the machine architecture is retained. The report cannot grant a lease or bypass the default fail-closed `NoRecoveryGuard` / `RefuseOwnership` safeguards. Missing /proc or DRM information is reported as unavailable, not treated as readiness. Run without arguments for the existing blocked startup preflight and nonzero exit code. The evidence JSON is not a substitute for supervised physical validation.
