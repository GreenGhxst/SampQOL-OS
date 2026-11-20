# Build and installation

## 1. Host build (Linux, development)

Requirements: CMake ≥ 3.16, a C++17 compiler, OpenSSL development headers
(for HTTPS), Python 3 (tests).

```sh
cd webconsole
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure      # C++ unit tests + Python HTTP suite
```

Run against simulated storage (nothing touches real devices):

```sh
tools/run-sim.sh                 # http://127.0.0.1:8765  pairing code DISKMODE
```

`tools/run-sim.sh` creates a fake card, internal and factory tree under
`$TMPDIR/diskmode-sim`, writes a `volumes.json` using the `directory`
rule option, and starts the server on loopback. Plain HTTP is accepted on
loopback only; see `docs/SECURITY.md`.

## 2. Options

| CMake option | Default | Meaning |
|---|---|---|
| `DM_WITH_TLS` | ON | link OpenSSL, enable `tls_cert`/`tls_key` |
| `DM_STATIC_LIBSTDCXX` | OFF | `-static-libstdc++ -static-libgcc` for the device |
| `DM_BUILD_TESTS` | ON | build `core_tests` and register the HTTP suite |

## 3. Cross-compiling for the MPC Sample (AArch64)

The device is a Rockchip RK3566 (Cortex-A55, 64-bit) running Linux 6.12
with glibc (versions unverified, see `docs/AUDIT.md` §2.6).

```sh
sudo apt install g++-aarch64-linux-gnu
cmake -S . -B build-arm64 -DCMAKE_TOOLCHAIN_FILE=tools/aarch64-linux-gnu.cmake \
      -DDM_WITH_TLS=OFF -DDM_STATIC_LIBSTDCXX=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-arm64 -j
```

Without TLS the binary depends only on glibc and libpthread. For HTTPS on the
device you need an OpenSSL built for the same sysroot:
`-DDM_WITH_TLS=ON -DOPENSSL_ROOT_DIR=<sysroot-openssl>`; linking against the
device's own `libssl.so.3` is possible if the firmware ships one (not
verified). The result of the cross-build performed in this repository is
recorded in `docs/HARDWARE_VERIFICATION.md`.

## 4. Files on the card

Proposed layout (nothing in this tree is placed inside SampQOL's signed
`autorun` folder, which the firmware rejects when modified):

```
<card>/
  autorun/                 SampQOL OS (unchanged, signed by its author)
  mpcqol.conf              SampQOL settings (protected from the console)
  diskmode/
    diskmode-webconsole    AArch64 binary
    web/                   frontend (index.html, app.js, explorer.js, app.css, icon.svg)
    config.json            see docs/CONFIG.md
    certs/                 webconsole-cert.pem, webconsole-key.pem (tools/gen-cert.sh)
  .diskmode/               application data: journal, index, staging, trash, inbox
```

`protected_paths` in the default policy hides `.diskmode` from listings and
refuses changes to `autorun` and `mpcqol.conf`.

## 5. Startup mechanism (documented, not exercised on hardware)

The MPC Sample stock firmware runs `<card>/autorun/autorun.sh` only when the
folder's `autorun.check` matches the author's salted hash
(`docs/AUDIT.md` §2.5). Two options exist; both need the card owner's
cooperation and a hardware test before either is recommended:

1. **A separate card for the console** with its own `autorun` folder that
   starts `diskmode/diskmode-webconsole --config diskmode/config.json &`
   and then starts the stock app (or SampQOL). Requires generating a valid
   `autorun.check`; the salt is documented in sample-base `device.py:29-45`.
2. **An extension hook in SampQOL OS** (the author would have to add it):
   SampQOL's `autorun.sh` could `exec` an optional `<card>/diskmode/start.sh`
   if present. This keeps SampQOL's signed folder intact.

In both cases the console must run as a service that does not block the
stock app, must be stopped before the card is lent to the USB host
(the NCM link disappears anyway), and must not be started twice (the
server refuses to bind an occupied port and exits with status 1).

Recovery: the journal (`.diskmode/journal.jsonl`) is replayed at startup;
operations without a terminal entry are marked `interrupted` and their
temporary files (`.dmtmp-*`) are deleted. Nothing destructive runs
automatically. Removing the card while the server runs makes every volume
`unavailable`; the server itself keeps running from RAM only if its binary
and data directory are not on that card (otherwise it dies with the card,
which is the documented limitation in the specification §17).

## 6. Resource notes

Measured on the host only (`docs/HARDWARE_VERIFICATION.md`): RSS about
14 MB idle with four HTTP worker threads and two job workers. Directory
listings are capped at 20 000 entries per request, uploads at 8 MB chunks,
screen frames at `screen_max_fps` per session, index scans sleep 200 µs every
64 files.
