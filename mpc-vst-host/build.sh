#!/bin/sh
# SPDX-License-Identifier: MIT
# Build and test the MPC Sample VST host.
#   ./build.sh            desktop build in build/, then the tests
#   ./build.sh arm        aarch64 build in build-arm64/ (tests under qemu when present)
#   ./build.sh all        both
#   ./build.sh package    aarch64 build, then the SD card folder in dist/
set -eu
cd "$(dirname "$0")"
what="${1:-host}"

host() {
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release >/dev/null
    cmake --build build -j"$(nproc)"
    (cd build && ctest --output-on-failure)
}

arm() {
    cmake -S . -B build-arm64 -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-linux-gnu.cmake >/dev/null
    cmake --build build-arm64 -j"$(nproc)"
    sh package/verify-arm64.sh build-arm64
    if command -v qemu-aarch64-static >/dev/null 2>&1 || command -v qemu-aarch64 >/dev/null 2>&1; then
        (cd build-arm64 && ctest --output-on-failure)
    else
        echo "qemu-aarch64 not found: aarch64 tests not run here (run them on the device: package/README)"
    fi
}

case "$what" in
    host) host ;;
    arm) arm ;;
    all) host; arm ;;
    package) arm; sh package/mkpackage.sh build-arm64 dist ;;
    *) echo "usage: $0 [host|arm|all|package]" >&2; exit 2 ;;
esac
