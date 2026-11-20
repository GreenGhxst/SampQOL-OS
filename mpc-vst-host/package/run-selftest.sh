#!/bin/sh
# SPDX-License-Identifier: MIT
# Self-test of the standalone host on the device (or any aarch64 Linux):
# copies the binaries to RAM (the card is noexec), scans the test plug-ins,
# renders a chord, benchmarks, and leaves the results in Logs/.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
X="$(cd "$HERE/.." && pwd)"
RUN="${MPCVST_RUN:-/tmp/mpcvst-selftest}"
LOG="$X/Logs/selftest.log"
mkdir -p "$RUN" "$X/Logs" "$X/Cache"
cp "$HERE/mpcvst" "$HERE/mpcvst-probe" "$RUN/" && chmod 755 "$RUN/mpcvst" "$RUN/mpcvst-probe" || { echo "copy to RAM failed" | tee -a "$LOG"; exit 1; }
# Plug-ins must be executable too: copy them beside the host.
mkdir -p "$RUN/plugins" && cp "$X/Test-Plugins/"*.so "$RUN/plugins/"
{
    echo "===== mpcvst self-test $(date '+%F %T') ====="
    echo "host: $(uname -m) $(uname -r); version $(cat "$HERE/VERSION" 2>/dev/null)"
    echo "--- scan"
    "$RUN/mpcvst" scan --worker "$RUN/mpcvst-probe" --cache "$X/Cache/selftest-registry.txt" "$RUN/plugins" 2>&1
    echo "--- info"
    "$RUN/mpcvst" info "$RUN/plugins/mock_instrument.so" 2>&1
    echo "--- render"
    "$RUN/mpcvst" render "$RUN/plugins/mock_instrument.so" --effect "$RUN/plugins/mock_effect.so" \
        --midi "$X/Test-Plugins/chord.midi.txt" --seconds 2 --out "$X/Logs/selftest.wav" 2>&1
    echo "--- bench (offline; the numbers are this machine's)"
    "$RUN/mpcvst" bench "$RUN/plugins/mock_instrument.so" --instances 4 --seconds 3 2>&1
    echo "--- compat gate (expected: disabled, we are not inside the stock app)"
    "$RUN/mpcvst" compat 2>&1
    echo "===== end ====="
} 2>&1 | tee -a "$LOG"
