#!/bin/sh
# SPDX-License-Identifier: MIT
# Static checks of the aarch64 build. They need no device.
#   sh package/verify-arm64.sh build-arm64
set -eu
B="${1:?usage: verify-arm64.sh BUILD_DIR}"
fail() { echo "verify-arm64: FAIL: $*" >&2; exit 1; }
RE="${READELF:-aarch64-linux-gnu-readelf}"
command -v "$RE" >/dev/null 2>&1 || RE=readelf

for f in "$B/mpcvst" "$B/mpcvst-probe" "$B/plugins/mock_instrument.so" "$B/plugins/mock_effect.so"; do
    [ -f "$f" ] || fail "$f missing"
    "$RE" -h "$f" | grep -q 'AArch64' || fail "$f is not AArch64"
done
"$RE" -h "$B/plugins/mock_instrument.so" | grep -q 'DYN' || fail "mock_instrument.so is not a shared object"

# The MPC Sample (fw 1.3.0.12) ships glibc 2.39 and GCC 13's libstdc++
# (GLIBCXX 3.4.32), as surveyed by Melodee's tools/plugin_support/SURVEY.md.
# Our binaries must not ask for more.
for f in "$B/mpcvst" "$B/mpcvst-probe"; do
    g=$("$RE" -VW "$f" | grep -o 'GLIBC_[0-9.]*' | sed 's/GLIBC_//' | sort -uV | tail -1)
    [ "$(printf '%s\n2.39\n' "$g" | sort -V | tail -1)" = 2.39 ] || fail "$f needs GLIBC_$g (device has 2.39)"
    x=$("$RE" -VW "$f" | grep -o 'GLIBCXX_[0-9.]*' | sed 's/GLIBCXX_//' | sort -uV | tail -1)
    [ -z "$x" ] || [ "$(printf '%s\n3.4.32\n' "$x" | sort -V | tail -1)" = 3.4.32 ] || fail "$f needs GLIBCXX_$x (device has 3.4.32)"
    echo "  $(basename "$f"): $(wc -c < "$f") bytes, GLIBC <= $g, GLIBCXX <= ${x:-none}"
done
# Mock plug-ins export only the entry point.
ex=$("$RE" --dyn-syms -W "$B/plugins/mock_instrument.so" | awk '$5=="GLOBAL" && $7!="UND" {print $8}' | sort | tr '\n' ' ')
[ "$ex" = "VSTPluginMain " ] || fail "mock_instrument.so exports: $ex"
echo "verify-arm64: OK"
