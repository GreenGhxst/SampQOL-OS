# Installation (standalone host, this release)

This release installs nothing into the MPC's firmware and changes nothing
about SampQOL OS. It is a folder on the card plus command line tools.

## What you need

- An Akai MPC Sample with firmware 1.3.0 (app build 1.3.0.12), optionally
  with SampQOL OS 0.1.5 installed the official way.
- Root SSH on the device to *run* anything from this release today (the
  stock app does not start our tools; see `SAMPQOL_INTEGRATION.md`).
- A computer with CMake, a C/C++17 compiler, and for the device build the
  `gcc-aarch64-linux-gnu` / `g++-aarch64-linux-gnu` packages (Debian or
  Ubuntu). `qemu-user-static` is optional (runs the device tests on the PC).

## Build

```sh
cd mpc-vst-host
./build.sh            # desktop build + tests in build/
./build.sh arm        # aarch64 build in build-arm64/, static checks, tests under QEMU if present
./build.sh package    # aarch64 build, then dist/VST-Expansion/ and zips
```

## Put it on the card

1. Copy `dist/VST-Expansion` to the root of the microSD card (next to
   `MPC-Sample`, and next to your SampQOL `autorun` folder if you have one).
2. Put aarch64 Linux VST2 plug-ins (`.so`) into
   `VST-Expansion/Plugins/Instruments` or `.../Effects`. Windows, macOS,
   x86 Linux and 32-bit ARM builds are listed by the scanner with the
   reason they do not load.
3. Nothing happens at boot. The MPC starts as before (stock, or SampQOL if
   its `autorun` folder is there).

## Run the self-test on the device

Over root SSH (the card is mounted `noexec`, the script copies the tools
to RAM first):

```sh
sh "/media/<card>/VST-Expansion/Host/run-selftest.sh"
```

It scans `Test-Plugins/`, renders a chord with the mock instrument and
effect to `Logs/selftest.wav`, benchmarks 0, 1, 2 and 4 instances and
writes everything to `Logs/selftest.log`. Copy that log into an issue if
something fails: it is the first hardware evidence this project asks for.

## Use the host by hand

```sh
H=/tmp/mpcvst; mkdir -p $H; cp /media/<card>/VST-Expansion/Host/mpcvst* $H; chmod 755 $H/*
X=/media/<card>/VST-Expansion
$H/mpcvst scan --worker $H/mpcvst-probe --cache $X/Cache/registry.txt $X/Plugins/Instruments $X/Plugins/Effects
$H/mpcvst info  /path/to/plugin.so
$H/mpcvst render /path/to/synth.so --midi $X/Test-Plugins/chord.midi.txt --seconds 4 --out $X/Logs/out.wav
$H/mpcvst bench  /path/to/synth.so --instances 4
```

Plug-ins on the card cannot be `dlopen`ed from a `noexec` mount: copy them
to RAM too, or mount the card `exec` for the session. The scanner tells you
when a load failed for that reason (`dlopen failed: ... failed to map segment`).

## Settings

`VST-Expansion/Config/host.conf` (see the comments in it). A file named
`VST-Expansion/Config/DISABLE` turns the whole expansion off for any future
runtime adapter; the standalone tools ignore it on purpose so you can still
test.

## Uninstall

Delete `VST-Expansion`. There is nothing else.
