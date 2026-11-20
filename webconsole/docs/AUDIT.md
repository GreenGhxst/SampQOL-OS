# Phase 1 — Source and Hardware Audit

Status: **completed from source**, nothing in this document was verified on
an MPC Sample. Every claim below cites the file it was read from.

## 1. What exists in this repository

`GreenGhxst/SampQOL-OS` (fork of `snarebes/SampQOL-OS`, upstream HEAD
`cb7b9148`, 2026-11-18) contains only documentation:

| Path | Content |
|---|---|
| `README.md`, `CHANGELOG.md` | SampQOL OS user documentation and release notes |
| `index.html` | the SampQOL OS manual (static page with embedded images) |
| `LICENSE` | freeware licence: study/modify for own use allowed, redistribution of modified versions not allowed without permission |

**There is no DISKMODE source tree, no Screen Bridge v0.5, no File
Intelligence database, no Smart Inbox and no native frame adapter in this
repository or in either upstream project.** A case-insensitive search for
"DISKMODE", "Screen Bridge", "File Intelligence" and "Smart Inbox" over all
three repositories returns nothing. SampQOL OS itself is closed source
(`README.md` "Source code"): it ships as `mpcqol.so` plus autorun scripts in
a zip, and the manual states that the stock firmware ignores an `autorun`
folder whose files were changed.

Consequence for this specification: items that say "integrate the existing
…" cannot be implemented as integrations. The Web Console in `webconsole/`
is therefore built as the *shared services + server + frontend* layers of
the specified architecture, with the native DISKMODE application layer
absent. Nothing in SampQOL OS was modified, replaced or redistributed.

## 2. Upstream projects inspected

| Repository | Commit | Licence | Relevant for |
|---|---|---|---|
| `keremimo/sample-base` | `f18e620104fe579727c5f13f796016ba08e3a394` (2026-11-18) | AGPL-3.0-only; kernel modules GPL-2.0 | USB gadget, card sharing, display, control socket, SD boot |
| `keremimo/melodee-sample` | `f11466589c7d28a3dd386c15a30743a6abc23fa3` (2026-11-18) | AGPL-3.0-only | existing HTTP file manager, storage paths |
| `snarebes/SampQOL-OS` | `cb7b914839a4f9d7a76be785e5b19b18e4a5a68a` | proprietary freeware | SD autorun behaviour, user-visible storage names |

No code from either AGPL project was copied into `webconsole/`. The console
re-implements what it needs so that the AGPL does not attach to this tree
(see `docs/LICENSES.md`).

### 2.1 USB gadget and networking — `sample-base/src/drivers/usb_gadget.{h,cpp}`

* The gadget is built by the **stock** service `az0x-usb-gadget@ac50` from
  `/usr/lib/az0x-usb-gadget/ac50.scheme` and links `uac2.audio`,
  `ncm.network`, `midi.midi` into configuration `c.1`
  (`docs/HARDWARE.md:269-274`, `usb_gadget.h:6-9`).
* The network function is **NCM**. Neither repository contains RNDIS, ECM,
  `g_ether`, a DHCP server, `ip addr`, or any address assignment
  (grep over both trees). `192.168.155.1` appears only in `device.py:112`,
  `README.md:153`, `tools/capture_controls.py:314` and as a comment in
  `melodee-sample/src/melodee/web/web_server.h:15`.
* Any function change re-binds the UDC (`usb_gadget.cpp:101-121`) and drops
  audio, MIDI and the network link for about a second
  (`usb_gadget.h:13-15`). The stock *SD Card Access* mode swaps the whole
  gadget to `ac50-sd-access.scheme` (mass storage only), which removes NCM
  entirely (`HARDWARE.md:375-379`).
* DWC3 FIFO limits force NCM to stay last; a UVC webcam and the mass-storage
  drive cannot coexist (`usb_gadget.cpp:247-261`).

Web Console decisions: never touch configfs; read the interface list and
addresses at runtime (`src/core/network.cpp`); treat every disconnect as
expected (sessions survive; the UI reconnects); use port **8765**, because
the stock `az0x-webserver.service` owns port 80.

### 2.2 Card sharing and mount points — `sample-base/src/drivers/card_share.{h,cpp}`

* The card is found through the `edisksd` D-Bus daemon
  (`io.github.inmusicdev.edisksd`): first `Removable` volume whose `Media`
  starts with `flash_sd` (`card_share.cpp:28-45`).
* Export is configfs `mass_storage.sd` with `lun.0/file=/dev/mmcblk1`
  (whole disk, `usb_gadget.cpp:238-239`), after `sync()` and an edisksd
  `Unmount` of every mounted volume (`card_share.cpp:122-126`).
* Ownership state machine `Home → Lending → Lent → Returning`
  (`card_share.h:28`); external marker `/run/sample-base-card-share` exists
  only while sample-base runs.
* Mount points seen in code and tests: SD card at `/media/<fs-id>`
  (e.g. `/media/3265-3932`, exfat, `mmcblk1p1`; `tests/card_share.cpp:31`);
  internal eMMC is `mmcblk0`, `mmcblk0p11` mounted at `/content`
  (factory content); stock user data at `/data/UserContent/Samples` and
  `/data/UserContent/Projects/User`; autosave `/data/TempProjects`
  (`melodee-sample/src/melodee/shell/project_store.cpp:33-36`,
  `sample_library.cpp:357`). Which partition backs `/data` is not stated.

Web Console decisions (`src/core/volume_registry.cpp`): volumes are policy
rules matched against `/proc/self/mounts` (SD: prefix `/media/`, exfat/vfat;
internal: `/data` + sub-directory, `/content`), never a fixed `/sdcard`.
Internal volumes default to read-only. Ownership is derived from configfs
`mass_storage.*/lun.*/file`: if the card's disk is exported, every operation
is refused with `busy`.

### 2.3 Display and screen capture — `display.cpp`, `screen_mirror.cpp`, `mirror_frame.cpp`

* The panel is an SPI ST7796S driven through a DRM dumb buffer (XRGB8888,
  `display.cpp:31-69`). There is **no framebuffer readback**: frames exist
  only as the `juce::Image` a sample-base process renders
  (`run_app.cpp:127-149`).
* Screen Mirror is a UVC webcam gadget (YUYV 320×240 / MJPEG via the hantro
  VEPU), one host consumer, and it blocks card sharing while streaming
  (`screen_mirror.h:10-18`, `usb_gadget.h:113-118`).
* A `screenshot` control command writes a PNG to `/run/sample-base/screen.png`.

Web Console decisions (`src/core/screen.h`): a `FrameSource` interface with
a clearly stamped **simulated** source and a **file** source (raw RGB565 or
binary PPM 320×240 written by an external, verified pipeline). Native
capture is reported as unavailable with the reason; frames carry
`X-DM-Frame-Simulated`. Remote input returns 501.

### 2.4 Control protocol — `runtime/control_socket.cpp`, `commands.cpp`

`AF_UNIX SOCK_DGRAM` socket at `/run/sample-base/control.sock`, text
commands, filesystem-permission auth (root only). Base commands include
`status`, `screenshot`, `card-share [on|off|force]`, `storage-mount OBJ`.
Input injection exists only in Melodee (`melodee-press`). Nothing of this
is reachable under the stock application, so the Web Console does not
depend on it.

### 2.5 Boot and autorun — `sample-base/install/*.sh`, SampQOL manual

The stock app runs `<card>/autorun/autorun.sh` when `autorun.check` equals
`sha256(autorun.json + autorun.sh + salt)` (`device.py:29-45`). sample-base
then runs `sd-worker.sh` as root through `systemd-run`, verifies the
firmware binary hash (1.3.0.12 only), copies the release to `/run`, writes a
transient `sample-base.service` and stops `ac50.service`. SampQOL OS uses
the same stock autorun mechanism but keeps the stock app running with
`mpcqol.so` loaded (manual, "Is it safe?").

Web Console decision: the console must be started by an autorun script that
the card owner controls. It cannot be appended to SampQOL's signed
`autorun` folder (the firmware rejects modified folders). See
`docs/BUILD.md` §5 for the two supported startup options and why neither
has been exercised on hardware.

### 2.6 Toolchain

sample-base builds natively inside an emulated `debian:bookworm-slim`
arm64 container (`tools/Dockerfile`, `build.sh`) with `-std=c++17 -O2
-pthread`, dynamically linked against the device's glibc. The device's
glibc/libstdc++ versions are not stated anywhere. The console therefore
offers `-DDM_STATIC_LIBSTDCXX=ON` and a TLS-less build so the binary needs
only glibc; see `docs/HARDWARE_VERIFICATION.md` for what was actually
compiled.

### 2.7 Existing HTTP server in melodee-sample (not reused)

`melodee-sample/src/melodee/web/` is a single-threaded, one-connection,
unauthenticated, plain-HTTP server on port 80 (stopping the stock web
server), SD-card only, with path checks in `card_files.cpp:38-66`. Its design
confirms two things the console adopts: refuse `autorun/` on the card, and
refuse non-IP/non-`.local` Host headers (DNS rebinding). It does not meet
the specification's authentication or TLS requirements, so it was not reused.

## 3. Storage ownership model

| State | Detected by | Console behaviour |
|---|---|---|
| device owns card (`Home`) | card mounted under `/media/`, disk not in any configfs `mass_storage` LUN | full policy access |
| exported to USB host (`Lent`) | disk present in a `mass_storage.*/lun.*/file` | all reads and writes refused (`busy`) |
| unmounted / removed | no matching mount, or root directory missing | `unavailable`; jobs on it fail per item |
| ownership unknown | mount present but probe failed | refused |
| application medium | mount equals `application_medium_mount` (configured) | normal file access, formatting refused |

Under the **stock** SD Card Access mode the NCM link disappears together
with the card, so a console session cannot even observe the transition; it
reconnects when the gadget returns and re-reads the volume table.

## 4. USB networking report

* Interface name, address and DHCP/link-local behaviour of the stock NCM
  function are **unverified**. `network_status_json()` lists every non-loopback
  IPv4/IPv6 address so the UI shows what is real.
* Proposed address `https://192.168.155.1:8765` is a *proposal*. Port 80 is
  taken by `az0x-webserver.service`.
* Throughput of NCM next to 18-channel UAC2 on USB 2.0 is unmeasured.

## 5. Unresolved hardware questions

1. Interface name and static address of the stock NCM function; who assigns
   the host side address.
2. Does the stock web server on port 80 matter for firmware updates, and is
   port 8765 free.
3. Which device backs `/data`; is writing to `/data/UserContent` while
   `ac50.service` runs safe (locking, rescans). Until answered, internal
   volumes stay read-only by policy.
4. Is the card always a single exfat partition at `/media/<id>`.
5. glibc / libstdc++ versions on the device.
6. Availability of edisksd D-Bus (needed for a real format helper and for
   the `flash_sd` media check, which this build cannot perform and
   therefore fails closed).
7. Whether any screen capture path exists while the stock app owns the
   panel (currently: none).
8. CPU and RAM headroom while the stock app runs audio.
