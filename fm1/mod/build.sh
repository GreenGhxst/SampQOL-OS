#!/bin/sh
# Build fm1mod for the MPC Sample with the SDK's build script and pack the
# SD autorun zip. Needs: cargo with the aarch64-unknown-linux-gnu target
# (rustup target add aarch64-unknown-linux-gnu), aarch64-linux-gnu-gcc,
# llvm-objcopy, llvm-objdump, nm, python3, zip.
set -eu
here=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
export CARGO_TARGET_AARCH64_UNKNOWN_LINUX_GNU_LINKER="${CARGO_TARGET_AARCH64_UNKNOWN_LINUX_GNU_LINKER:-aarch64-linux-gnu-gcc}"
export CC_aarch64_unknown_linux_gnu="${CC_aarch64_unknown_linux_gnu:-aarch64-linux-gnu-gcc}"
# first 4 KiB of /usr/bin/MPC Sample, firmware 1.3.0.12 (same value SampQOL OS checks)
FW=44507c7a7d3029c411b2e3780ea286ca3e08b7e3ffd7b5047d52f73bf5299d1f
cd "$here"
sh ../sdk/build-mod.sh --manifest-path Cargo.toml --package fm1mod --name fm1mod \
    --output-dir "$here/build" --pack --firmware-sha256 "$FW" --display-name "FM1 Synth" "$@"
