# DISKMODE — Master Codex Build Handoff v1.1
**Approved revision: 2026-11-20.** This document is the executable engineering brief for Codex. It supersedes any earlier brief that asked DISKMODE to recreate SampQOL audio editing or integrate VST2.

## Assignment and immutable source baseline
- Repo: GreenGhxst/SampQOL-OS
- Source branch: claude/diskmode-web-console-4k5tvw
- Source commit: a80ffd611bdc648205f902b7b72b6a48666922a0
- Development branch: codex/diskmode-integrated-core
- Existing application directory: webconsole/
- Preserve all source, history, third-party attributions, tests, routes, data formats and useful features. DO NOT start over, overwrite main, or modify the proprietary SampQOL deployment.
- Codex must first run baseline builds/tests; no claim of hardware verification is authorized by an existing report.

## Strict exclusions
1. Do **not** implement or duplicate auto-trim, normalization, resampling/format conversion, chopping, slice export or a second audio editor. SampQOL owns audio editing and native sample playback. Invoke SampQOL only when a stable, documented, legally permitted interface is actually verified. Unsupported actions stay in SampQOL.
2. Do **not** implement VST2, VST3, CLAP, FM-1 firmware emulation, a plugin rack, or MPC-FM audio hooks.
3. Do not copy code from AGPL Melodee/sample-base into the existing MIT webconsole without an explicit licensing decision. Public source can inform interfaces and tests.
4. No stock-app control injection, unsupported USB host mode, destructive SD formatting, fabricated screen mirroring, or fabricated iOS/PWA installability.
5. No modification or redistribution of SampQOL binaries and no modification of its signed autorun files.

## Product contract
DISKMODE is one Akai MPC Sample application with **two frontends using the same filesystem authority, service layer and jobs**:
- Independent native 320×240 file manager using verified MPC controls, functioning with no network or browser.
- Secure locally served Web Console, dual-pane explorer and Screen Bridge for an iPhone/iPad/computer over a verified USB-C NCM link; optional installable PWA after HTTPS, service-worker and iOS testing. No cloud or public publishing needed.

## Keep the actual existing modules
- `src/core/volume_registry.*`: removable/internal mounts and USB card-ownership detection.
- `src/core/path_auth.*`: fd-relative path validation, symlink refusal and protected paths.
- `src/core/file_ops.*`, `journal.*`, `job_queue.*`: copy/move/rename/mkdir/trash/restore, recovery and batch jobs.
- `transfer_manager.*`: resumable uploads, SHA-256 validation, ranged downloads.
- `intelligence.*`: index/search, exact duplicates, batch rename, favorites, recents, tags, virtual collections.
- `inbox.*` and `wav.*`: Smart Inbox, metadata, peaks, browser WAV audition. These are inspection and file-organization operations, **not duplicate DSP**.
- `screen.*`: explicit simulated/file frame sources and an unavailable native source. Existing stock screen capture is *not* implemented.
- `format_guard.*`: SIMULATION ONLY with mandatory gates. Do not enable actual formatting.
- `src/server/*` and `web/*`: authentication, pairing, sessions, TLS/CSRF policy, HTTP API v1, responsive browser interface.
- `tests/core/*` and `tests/http/test_api.py`, build tools and third-party license notices.

## Evidence that must NOT be overstated
Existing project documentation reports 29 C++ unit cases, 17 HTTP end-to-end cases, browser smoke checks, AArch64 cross builds and QEMU checks. Re-run independently. No physical MPC Sample run is verified. A reported dynamic AArch64 build required GLIBC_2.38 and may not run on the device; a reported fully static build ran under QEMU only. Existing remote screen control returns HTTP 501, screen frames are simulated or externally supplied, HTTPS device deployment is unfinished, and SD formatting is simulation-only. USB network interface/address, client-side configuration, realtime input, memory limits and boot chaining require hardware evidence.

## Implementation phases and gates
### Step 0 — Preserve and audit
1. Record both branch heads and inventory existing files, APIs and tests.
2. Add `webconsole/docs/INTEGRATION_GAP_MATRIX.md` listing every approved feature as implemented/simulated/compiled/emulated/hardware-verified/blocked, with source paths and tests.
3. Run `cmake -S webconsole -B webconsole/build -DCMAKE_BUILD_TYPE=Release`, `cmake --build webconsole/build -j` and `ctest --test-dir webconsole/build --output-on-failure`. Capture real output and failures.
4. Review security, authorization boundaries, upload recovery, ownership, race conditions and journaling. Fix defects with regression tests.
**Gate:** zero regressions; preserve all original sources.

### Phase 1 — Native MPC Sample file manager
- Refactor `dm_core` into a transport-independent local service API used directly by native UI and HTTP handlers (do not require HTTP loopback).
- Build native 320×240 volume browser, search, detail view, multi-select, copy, move, rename, folder creation, Trash/restore, favorites, progress and errors.
- Implement actual hardware input mappings only after checking hardware references and permissions. Desktop mock-input tests are separate from hardware tests.
- Enforce path-root authority, mount/ownership checks, internal read-only policy and journal recovery in shared core.
**Gate:** independent native UI builds for AArch64, desktop fake input tests pass; real hardware input remains pending until demonstrated.

### Phase 2 — Smart Inbox and SampQOL-aware organization, no DSP
- Preserve WAV metadata, waveforms, audition in the **browser**, upload staging, hashes and duplicates.
- Add sample classification, compatible names, kit directory templates, preset destinations and non-destructive organization where missing.
- Verify which user-media folders are visible to SampQOL, and whether it offers an official API for refresh or actions. Absent proof, only organize files; do not promise audio editor integration.
- Preserve originals. No trimming, normalization, converting, resampling or chopping engine.
**Gate:** sample organization and transfer without altering audio contents.

### Phase 3 — Secure local Web Console and optional iOS PWA
- Maintain dual-pane file browser, same shared jobs/index, authentication, TLS, CSRF and server-side permission checks.
- Improve touch ergonomics: iOS Safari file picker, selection, drag-and-drop fallback, long press, upload interruption and reconnection.
- Detect NCM interface/IP at runtime. Never hardcode 192.168.155.1 as a proven address. Port 80 belongs to stock services; proposed Web Console port is 8765.
- Non-loopback writes require properly verified secure transport and device pairing. Do not expose publicly.
- Optional PWA only with tested manifest, trusted HTTPS, service worker caching non-sensitive app shell, and iOS Add to Home Screen behavior. No fake offline file operations.
**Gate:** browser automated tests; live USB-C verification on target when available.

### Phase 4 — Screen Bridge with touchscreen
- Screen Bridge must remain in scope. Existing `src/core/screen.cpp` provides only simulated frames or file-fed external frames; no stock app framebuffer capture.
- Develop a native DISKMODE renderer-owned frame source when verified; frame dimensions 320×240, throttled stream, aspect-correct display, reconnect after USB loss, strict CPU budget.
- Add authenticated opt-in **touch control for native DISKMODE screens** via verified input translation: screen coordinates, tap, long press, scroll, virtual MPC keys/encoder, rate limits and audit trail. Physical controls must keep working.
- Remote control of the **stock SampQOL UI** is separately gated. Never claim arbitrary stock touch input or screen mirroring works without a confirmed, permitted source/endpoint.
- If hardware implementation is unavailable, return explicit disabled/unavailable status, retain safe view-only behavior, and document blockers—not silent mock success.
**Gate:** UI tests plus actual native screen and input proof on hardware.

### Phase 5 — Storage and recoverability
- Protect stock `autorun/`, `mpcqol.conf`, `.diskmode/`, internal system mounts, and application storage. Default internal storage read-only.
- Block all SD operations while USB mass-storage export owns the card or mount ownership is unknown. Test disconnect, interruption, journaling, safe temporary-file cleanup and restoration.
- Formatting remains SIMULATION ONLY; no mkfs or privileged execution. Production requires verified removable media identity, edisksd, exclusive lock, out-of-card recovery environment, sacrificial-card testing, explicit approval.
- Deployment must not edit SampQOL's signed scripts. Find a verifiable separate autorun/launcher chain; disable safely on failures.
**Gate:** no destructive operation without all measured safety conditions.

### Phase 6 — Release and hardware gates
- Provide reproducible C++17/CMake build, static/dynamic AArch64 compatibility checks, tests, release source archive, checksums, notices, configuration, boot/install/recovery guide.
- Label each result as code implemented, test simulated, cross-compiled, QEMU-emulated or hardware-verified.
- Gate A: runs on physical MPC Sample.
- Gate B: authenticated local USB-C Web Console, safe card browsing and job operations tested.
- Gate C: native 320×240 UI usable without phone.
- Gate D: actual Screen Bridge and native DISKMODE touch tested; if unavailable, clearly mark feature incomplete and retain view-only/unavailable mode.
- Gate E: Smart Inbox and SampQOL file handoff where verified; no duplicate DSP.
- Never report complete hardware deployment until hardware tests pass.

## Known integration blockers to carry forward
- The original Claude branch is the source baseline; do not substitute upstream SampQOL main, which does not contain this Web Console.
- Existing USB gadget combines NCM network, audio and MIDI; stock SD Card Access exports mass storage and interrupts NCM. Concurrent SD ownership must fail closed.
- sample-base screenshot/control socket is in-process and **does not prove stock MPC Sample screen capture**.
- Stock MPC Sample physical display is not touch-sensitive. Remote touchscreen UI is an iPhone/iPad client translating input to supported native DISKMODE actions, not an alteration to stock display hardware.
- Unknown exact device glibc, practical memory/CPU, SD mount variants, network IP, SSL compatibility, physical input interface and SampQOL extension API.

## First Codex task and definition of done
Immediately add the gap matrix, replay baseline tests, and begin the standalone native file-manager with mock backend and testable shared service seams. Commit in reviewable stages on the new branch; preserve the original Claude source untouched until deliberately changed and tested. Report per-milestone diff, test evidence, device blockers and next action. Finish when advanced file management, Smart Inbox, local console, and verified Screen Bridge function together within all safety gates; no VST2 or redundant SampQOL DSP features.
