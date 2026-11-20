#!/bin/sh
# SPDX-License-Identifier: MIT
# Build the SD card folder of the expansion from an aarch64 build:
#   sh package/mkpackage.sh build-arm64 dist
# Writes dist/VST-Expansion/ (the folder to copy to the card's root) and
# dist/mpc-vst-host-<version>-arm64.zip, plus a source zip. It contains only
# this project's own files: no SampQOL, no firmware, no third-party plug-ins.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
B="${1:?usage: mkpackage.sh BUILD_DIR OUT_DIR}"
OUT="${2:?usage: mkpackage.sh BUILD_DIR OUT_DIR}"
VERSION=$(sed -n 's/^#define MPCVST_VERSION "\(.*\)"$/\1/p' "$ROOT/include/mpcvst/version.h")
[ -n "$VERSION" ] || { echo "mkpackage: no version" >&2; exit 1; }

X="$OUT/VST-Expansion"
rm -rf "$X"
mkdir -p "$X/Plugins/Instruments" "$X/Plugins/Effects" "$X/Presets" "$X/State" "$X/Cache" "$X/Logs" "$X/Config" "$X/Host" "$X/Test-Plugins"
cp "$B/mpcvst" "$B/mpcvst-probe" "$X/Host/"
cp "$B/plugins/mock_instrument.so" "$B/plugins/mock_effect.so" "$X/Test-Plugins/"
cp "$ROOT/package/config.default.txt" "$X/Config/host.conf"
cp "$ROOT/package/README-CARD.txt" "$X/README.txt"
cp "$ROOT/tests/data/chord.midi.txt" "$X/Test-Plugins/chord.midi.txt"
cp "$ROOT/package/run-selftest.sh" "$X/Host/run-selftest.sh"
cp "$ROOT/LICENSE" "$X/LICENSE.txt"
cp "$ROOT/docs/LICENSES.md" "$X/THIRD-PARTY-NOTICES.txt"
: > "$X/Plugins/Instruments/.keep"
: > "$X/Plugins/Effects/.keep"
: > "$X/Presets/.keep" ; : > "$X/State/.keep" ; : > "$X/Cache/.keep" ; : > "$X/Logs/.keep"
printf '%s\n' "$VERSION" > "$X/Host/VERSION"
(cd "$X" && find . -type f ! -name SHA256SUMS -print0 | sort -z | xargs -0 sha256sum > SHA256SUMS)

mkdir -p "$OUT"
rm -f "$OUT/mpc-vst-host-$VERSION-arm64.zip"
(cd "$OUT" && zip -qr "mpc-vst-host-$VERSION-arm64.zip" VST-Expansion)
# Source zip from the committed tree, nothing from build folders.
if git -C "$ROOT" rev-parse >/dev/null 2>&1; then
    prefix="mpc-vst-host-$VERSION-src/"
    git -C "$ROOT" archive --format=zip --prefix="$prefix" -o "$OUT/mpc-vst-host-$VERSION-src.zip" HEAD -- . 2>/dev/null || \
        echo "mkpackage: source zip skipped (tree not committed yet)"
fi
(cd "$OUT" && sha256sum *.zip 2>/dev/null || true)
echo "mkpackage: $X (mpc-vst-host $VERSION)"
