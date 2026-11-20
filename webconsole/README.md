# DISKMODE Web Console

A browser-based file-management console for the Akai MPC Sample, served by
a small C++17 service on the device and reached over the USB-C network.
No cloud, no account, no internet connection.

```
webconsole/
  src/core      shared services: volume registry, path authorization, file operations,
                journal, job queue, transfers, file intelligence, inbox, screen, format guard
  src/server    authentication/sessions, configuration, HTTP API (/api/v1)
  web/          responsive frontend (dual-pane explorer, inbox, search, screen, SD utilities)
  tests/        C++ unit tests and a Python end-to-end suite against the real binary
  tools/        run-sim.sh (simulated storage), gen-cert.sh, cross toolchain, source package
  docs/         AUDIT, BUILD, CONFIG, SECURITY, API, FORMAT_AUDIT, LICENSES, HARDWARE_VERIFICATION
  third_party/  cpp-httplib (MIT), nlohmann/json (MIT), pinned
```

Quick start on a development machine:

```sh
cmake -S . -B build && cmake --build build -j && ctest --test-dir build
tools/run-sim.sh        # open http://127.0.0.1:8765, pairing code DISKMODE
```

## Feature status

Every feature is labelled with the strongest evidence it has. **Nothing has
been hardware-tested**; see `docs/HARDWARE_VERIFICATION.md`.

| Area | Status | Notes |
|---|---|---|
| Pairing, roles (viewer / file manager / admin), sessions, CSRF, rate limits | complete (simulated) | `docs/SECURITY.md` |
| HTTPS | complete on host builds; device build currently without TLS | certificate process documented, not exercised on iPadOS |
| Volume registry, ownership detection, protected paths | complete (simulated); mount layout source-verified | internal volumes read-only by policy |
| Dual-pane explorer, drag-and-drop, conflicts, keyboard, phone/tablet layouts | complete (simulated, browser-tested) | |
| Copy / move / rename / mkdir / trash / restore / hash, journal recovery | complete (simulated) | cross-volume move = verified copy + delete |
| Chunked resumable uploads, downloads with Range | complete (simulated) | |
| Smart Inbox (staging, WAV metadata, waveform, duplicate check, commit) | complete (simulated) | audition in the browser only |
| Search index, collections, favorites, recents, tags, duplicates, batch rename | complete (simulated) | |
| Storage dashboard, network status, journal | complete (simulated) | media health reported as unsupported |
| Screen Bridge viewer | **simulated frames** or external capture file; native capture unavailable under the stock app | remote control disabled |
| SD formatting | **simulation only**; all safety gates implemented, no utility ever invoked | `docs/FORMAT_AUDIT.md` |
| Integration with DISKMODE native app, Screen Bridge v0.5, File Intelligence DB | **not possible**: no such source exists in this repository or upstream | `docs/AUDIT.md` §1 |
| Additional USB storage | unsupported state shown | hardware cannot host USB storage in NCM mode (source-verified) |

## Licence

New code in this directory is released under the MIT licence (see
`LICENSE` in this directory). Third-party components are listed in
`docs/LICENSES.md`. SampQOL OS, which the console is designed to run next
to, is a separate proprietary work and is not included.
