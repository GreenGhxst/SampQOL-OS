# Licences and redistribution

## This project

`mpc-vst-host` is MIT-licensed (`LICENSE`). Every source file here was
written for this project. The VST2 ABI header (`include/mpcvst/vst2_abi.h`)
is an independent description of the public VST 2.4 binary interface in this
project's own words and identifiers; it contains no Steinberg SDK text.

## Components studied but not incorporated

| Component | Licence | What was taken | What was not |
| --- | --- | --- | --- |
| Melodee (`melodee-sample`) and `sample-base` by Kerem Kiliç (Ellic Studio) | AGPL-3.0-only | Knowledge: the lifecycle order a VST2 host must follow, the device facts (128-frame block, glibc 2.39, GLIBCXX 3.4.32, no X11/GL), the shape of a 32-bit bridge | No code, headers, scripts, fonts or art. Incorporating any would place this project under the AGPL and require source distribution of the whole; its boot art is not licensed at all. |
| MPC-FM by Thomas Testaud | **none published** (all rights reserved by default) | Knowledge: the autorun → SampQOL → `setarch` stand-in → `LD_PRELOAD` chain, the build-id gate, the crash-guard rules, the `noexec` card | No code. `launcher/`, `src/` and `tests/` were read only. If a launcher is built here it is written from scratch against the described mechanism. |
| SampQOL OS by Snare Bes | proprietary freeware (its LICENSE) | Knowledge: install layout, boot chain, version | No files. Users download SampQOL from its official releases; this project never ships, mirrors or modifies `mpcqol.so` or its scripts. |
| Steinberg VST2 SDK | withdrawn; not redistributable | Nothing | The SDK is not used. The ABI is described independently (as FST and VeSTige do). "VST" is Steinberg's trademark. |
| inMusic MPC Sample firmware | proprietary (kernel parts GPL, see inMusic's open-source release) | Knowledge: `autorun.check` salt (public), `/usr/bin/MPC Sample` identity | No firmware files, no decompiled code, no offsets copied. |

## Third-party plug-ins

Plug-ins a user puts in `VST-Expansion/Plugins/` are the user's own, under
their own licences. None ship with this project. The mock plug-ins in
`plugins/` are this project's (MIT).

## Build toolchain

The device build uses GCC's aarch64 cross compiler and glibc headers
(GPL with the GCC Runtime Library Exception; LGPL). Binaries link
dynamically to the device's own libc, libm, libdl, libpthread, libstdc++
and libgcc_s; nothing from the toolchain is redistributed.

## What a release must carry

- `LICENSE` (MIT) and this file.
- Nothing from SampQOL, Melodee, MPC-FM, Steinberg or inMusic.
- Clear status labels (see `STATUS.md`): what is desktop-tested, what is
  built for aarch64, what has run on the MPC Sample, what is integrated
  with SampQOL. Simulated or emulated runs are never presented as hardware
  compatibility.
