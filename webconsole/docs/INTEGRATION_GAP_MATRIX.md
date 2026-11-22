# DISKMODE — 107-capability integration gap matrix

Approved baseline: corrected workbook **DISKMODE_Feature_Matrix_Corrected_2026-11-22.xlsx** (2026-11-22). Repo branch: `codex/diskmode-integrated-core`, audited baseline `c83c556`. This commit preserves the full approved scope in text. **Statuses below describe code evidence, not verified hardware functionality.** Existing tests were *reported* in documentation, not replayed during the initial audit.

Legend: **I** source implemented / host simulation reported; **P** partial / integration or testing still required; **M** missing; **X** intentionally excluded. **H** device integration or physical hardware evidence outstanding. **L** legacy scope gated, not a mandatory v1.1 requirement; **C** conditional future-only. Note: browser-tested features are not evidence of MPC testing.

| ID | Capability | Scope | Code | Device | Evidence / action |
|---:|---|---|:---:|:---:|---|
| 001 | Shared C++17 core modules | Approved | I | H | Native / architecture |
| 002 | Transport-independent service facade | Approved | P | — | Native / architecture |
| 003 | Native 320×240 application entrypoint | Approved | M | H | Native / architecture |
| 004 | Mock-input desktop UI harness | Approved | I | — | Native / architecture |
| 005 | AArch64 static binary for Web Console | Approved | P | H | Native / architecture |
| 006 | Physical MPC startup/launcher | Approved | M | H | Native / architecture |
| 007 | Clean shutdown and journaling replay | Approved | I | H | Native / architecture |
| 008 | Volume and folder browsing on hardware display | Approved | P | H | Native / architecture |
| 009 | File details on 320×240 screen | Approved | M | H | Native / architecture |
| 010 | Native multi-select | Approved | M | H | Native / architecture |
| 011 | Native copy/move/rename/new folder | Approved | M | H | Native / architecture |
| 012 | Native trash/restore | Approved | M | H | Native / architecture |
| 013 | Native search/favorites | Approved | M | H | Native / architecture |
| 014 | Native progress/error dialogs | Approved | M | H | Native / architecture |
| 015 | Physical buttons and encoder mappings | Approved | M | H | Native / architecture |
| 016 | Native offline operation without phone | Approved | M | H | Native / architecture |
| 017 | Directory listing, pagination, sorting and filters | Approved | I | — | File operations |
| 018 | Copy and move, including batch jobs | Approved | I | — | File operations |
| 019 | Rename and folder creation | Approved | I | — | File operations |
| 020 | Trash, restore and purge | Approved | I | — | File operations |
| 021 | Collision policies / overwrite decisions | Approved | I | — | File operations |
| 022 | Batch rename planning and apply | Approved | I | — | File operations |
| 023 | Job queue, progress and cancel | Approved | I | — | File operations |
| 024 | Interruption journal and temp-file cleanup | Approved | I | — | File operations |
| 025 | Protected path, symlink and traversal defense | Approved | I | — | File operations |
| 026 | SD card ownership/export blocking | Approved | P | H | File operations |
| 027 | Internal storage default read-only | Approved | I | — | File operations |
| 028 | Search indexing | Approved | I | — | Intelligence |
| 029 | Exact duplicate sample finder (SHA-256) | Approved | I | — | Intelligence |
| 030 | Favorites | Approved | I | — | Intelligence |
| 031 | Recents | Approved | I | — | Intelligence |
| 032 | Tags and virtual collections | Approved | I | — | Intelligence |
| 033 | File-type and extension search filters | Approved | I | — | Intelligence |
| 034 | Smart Inbox staging and commit | Approved | I | — | Intelligence |
| 035 | WAV metadata inspection | Approved | I | — | Intelligence |
| 036 | Waveform peaks for browser | Approved | I | — | Intelligence |
| 037 | Browser sample audition | Approved | I | — | Intelligence |
| 038 | SampQOL media-folder handoff | Approved | P | H | Intelligence |
| 039 | Kit folder presets and naming templates | Approved | M | — | Intelligence |
| 040 | Sample type/classification automation | Approved | M | — | Intelligence |
| 041 | Chunked resumable uploads | Approved | I | — | Transfers / USB |
| 042 | SHA-256 transfer verification | Approved | I | — | Transfers / USB |
| 043 | Atomic placement after uploads | Approved | I | — | Transfers / USB |
| 044 | Downloads with HTTP Range | Approved | I | — | Transfers / USB |
| 045 | USB-C NCM discovery/network status | Approved | P | H | Transfers / USB |
| 046 | USB mass-storage coexistence during NCM | X | X | H | Transfers / USB |
| 047 | USB removable host-drive explorer | X | X | H | Transfers / USB |
| 048 | Local HTTP API v1 | Approved | I | — | Web / security |
| 049 | Dual-pane browser file explorer | Approved | I | — | Web / security |
| 050 | Drag and drop with conflict dialog | Approved | I | — | Web / security |
| 051 | Phone/tablet responsive layouts | Approved | I | — | Web / security |
| 052 | iOS file picker/long press/touch resilience | Approved | P | H | Web / security |
| 053 | Installable offline-shell PWA | Approved | M | H | Web / security |
| 054 | Pairing and user sessions | Approved | I | — | Web / security |
| 055 | CSRF, origin/host and rebinding defenses | Approved | I | — | Web / security |
| 056 | TLS on development host | Approved | I | — | Web / security |
| 057 | Device-compatible TLS / iOS certificate trust | Approved | M | H | Web / security |
| 058 | Role permissions/session revocation/rate limit | Approved | I | — | Web / security |
| 059 | Simulated 320×240 frame source | Approved | I | — | Screen bridge |
| 060 | External file-fed RGB565/PPM viewer | Approved | I | H | Screen bridge |
| 061 | Actual native DISKMODE frame capture | Approved | M | H | Screen bridge |
| 062 | Aspect-correct authenticated phone viewer | Approved | P | H | Screen bridge |
| 063 | Remote tap/scroll/long-press mapping | Approved | M | H | Screen bridge |
| 064 | Virtual physical keys/knob/pad navigation | Approved | M | H | Screen bridge |
| 065 | Rate-limit/opt-in remote input/audit trail | Approved | M | H | Screen bridge |
| 066 | Stock SampQOL UI remote control | C | X | H | Screen bridge |
| 067 | Storage capacity dashboard | Approved | I | H | Storage / recovery |
| 068 | SD hardware health status | Approved | P | H | Storage / recovery |
| 069 | Simulation-only SD format workflow | Approved | I | — | Storage / recovery |
| 070 | Actual microSD formatting | C | M | H | Storage / recovery |
| 071 | Disconnection/reconnection/restart resilience | Approved | P | H | Storage / recovery |
| 072 | Safe uninstall/return to stock | Approved | M | H | Storage / recovery |
| 073 | Existing C++ unit test suite | Approved | I | — | Release / exclusions |
| 074 | Existing HTTP integration tests | Approved | I | — | Release / exclusions |
| 075 | Browser smoke automation | Approved | P | — | Release / exclusions |
| 076 | Automated CI for Codex branch | Approved | M | — | Release / exclusions |
| 077 | Integration gap matrix in repository | Approved | M | — | Release / exclusions |
| 078 | Reproducible release bundle and checksums | Approved | P | H | Release / exclusions |
| 079 | Physical MPC Sample smoke tests | Approved | M | H | Release / exclusions |
| 080 | VST2/VST3/CLAP hosting | X | X | — | Release / exclusions |
| 081 | Chop/trim/normalization/resampling DSP | X | X | — | Release / exclusions |
| 082 | Native back-navigation, breadcrumb and folder history | Approved | M | H | Native UX / boot |
| 083 | Native context/action menu for file operations | Approved | M | H | Native UX / boot |
| 084 | Native sort/filter and long-directory scrolling | Approved | M | H | Native UX / boot |
| 085 | Native file-operation confirmation and conflict resolution | Approved | M | H | Native UX / boot |
| 086 | Duplicate file/folder command (not duplicate detection) | L | P | H | Native UX / boot |
| 087 | Per-item batch failure, skip and retry reporting | Approved | P | H | Native UX / boot |
| 088 | Undo recent non-destructive organization actions | L | M | — | Native UX / boot |
| 089 | Dedicated boot-menu DISKMODE entry | L | M | H | Native UX / boot |
| 090 | Restart, Power Off and Return-to-Stock menu actions | L | M | H | Native UX / boot |
| 091 | Independent native process lifecycle and safe fallback | Approved | M | H | Native UX / boot |
| 092 | Smart Inbox duplicate check before destination commit | Approved | P | H | Sample import |
| 093 | Sample-compatible filename validation and safe name preview | Approved | M | H | Sample import |
| 094 | Non-destructive destination templates/presets for samples | Approved | M | H | Sample import |
| 095 | Dual-pane cross-volume transfer with ownership recheck | Approved | P | H | Web / transfer |
| 096 | Touch selection fallback when drag-and-drop unavailable | Approved | P | H | Web / transfer |
| 097 | Explicit view-only default and remote control enable toggle | Approved | P | H | Screen / hardware |
| 098 | Coordinate scaling and orientation-independent touch mapping | Approved | M | H | Screen / hardware |
| 099 | Touch drag gesture and long-press gesture arbitration | Approved | M | H | Screen / hardware |
| 100 | Multi-touch gesture support | L | M | H | Screen / hardware |
| 101 | Virtual transport/pad-bank controls | L | M | H | Screen / hardware |
| 102 | Frame reconnect and stale-frame indication | Approved | P | H | Screen / hardware |
| 103 | Remote input security audit and session-bound revocation | Approved | M | H | Screen / hardware |
| 104 | App/card ownership state exposed in native UI | Approved | M | H | Screen / hardware |
| 105 | Real device disk-full and media-removal fault tests | Approved | M | H | Screen / hardware |
| 106 | Hardware evidence log: exact firmware, runtime and test device | Approved | M | H | Screen / hardware |
| 107 | First-run pairing, config and rollback installation checklist | Approved | P | H | Screen / hardware |

## Mandatory P0 order
1. Preserve baseline, rerun CMake/CTest + HTTP tests. No results yet claimed for new branch.
2. Add native controller, view model and mock-input tests. Drive it through `dm_core`, never bypass `FileOps` or volume policy.
3. Implement local transport-independent service facade exposing permitted volume listing/stat/jobs and action requests; keep server permissions unchanged.
4. Implement 320×240 renderer, context menus, selection and operations. Hardware input/display adaptation only after verified interfaces.
5. Confirm boot and recovery on a sacrificial card without modifying SampQOL signed autorun.
6. Verify device NCM/TLS, native frame source and opt-in remote input, storage ownership, real hardware tests and release A–E.

## Source evidence
- `webconsole/docs/CODEX_MASTER_BUILD_HANDOFF_v1.1.md` — controlling product contract and release gates
- `webconsole/README.md` and `webconsole/docs/API.md` — existing core, server and simulated UI features
- `webconsole/docs/AUDIT.md` — hardware interface analysis
- `webconsole/docs/HARDWARE_VERIFICATION.md` — previous host tests, cross-builds and missing physical validation
- `webconsole/src/core/*`, `src/server/*`, `web/*`, `tests/*` — authoritative sources; see associated workbook for per-row links, test evidence, gating and audit notes.

## Guardrails
- Never claim hardware-tested without real MPC Sample evidence; stock screen capture and touch injection are not demonstrated.
- No VST hosting, duplicate audio DSP, or unsupported USB host / NCM coexistence.
- SD formatting remains simulation only; stock and SampQOL startup files stay untouched.
- Legacy-approved features remain tracked but do not silently become release gates.

## P0 implementation update — 2026-11-22

- **Commit `bf5ed15d`**: `src/native/native_model.{h,cpp}` — hardware-independent 320x240 navigation state, selectable folders, multi-select, history/back, scroll, refresh and error handling. No production renderer or device input adapter yet.
- **`src/native/native_core_service.{h,cpp}`**: read-only native adapter uses existing `VolumeRegistry` and `FileOps`; refreshes mount ownership before each directory load and refuses unavailable/USB-host-owned media. Mutations remain unwired pending shared jobs/confirmation safety.
- **`tests/core/test_native.cpp`**: five mock-input tests integrated into existing `core_tests`; `CMakeLists.txt` builds `dm_native`.
- **GitHub Actions run [45080563105](https://github.com/GreenGhxst/SampQOL-OS/actions/runs/45080563105)**: latest combined source built on ubuntu-24.04 and CTest passed. No MPC device used. No independent AArch64 cross-build in this run.
- Next: bind native UI to renderer/input via verified interfaces; implement shared confirmation/job-backed mutations and corresponding tests; then gated hardware validation.
