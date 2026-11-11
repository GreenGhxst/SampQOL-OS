#!/bin/sh
# Add fm1mix to YOUR OWN copy of a SampQOL OS "autorun" folder.
#
#   sh apply.sh <autorun folder> <fm1mix.so> [fm1.conf] [bank.syx]
#
# What it does:
#   1. keeps a copy of the original qol-run.sh and autorun.sh in autorun/fm1/orig
#   2. inserts the fm1mix lines into qol-run.sh (LD_PRELOAD gets a 2nd library)
#   3. writes the new sha256 of qol-run.sh into the PINS list of autorun.sh
#   4. rewrites autorun.check, which the stock firmware verifies before it
#      runs autorun.sh: sha256(autorun.json + autorun.sh + a fixed UUID)
#   5. copies fm1mix.so, fm1.conf and the optional bank into autorun/fm1
#   6. runs the same pin check as autorun.sh to prove the folder will start
#
# The SampQOL OS license allows you to modify it for your own use. Do not
# share the patched folder; share this script and a link to the release.
set -e

usage() { echo "usage: sh apply.sh <autorun folder> <fm1mix.so> [fm1.conf] [bank.syx]" >&2; exit 2; }
die() { echo "apply.sh: $*" >&2; exit 1; }

AUTORUN=${1:-}; SO=${2:-}; CONF=${3:-}; BANK=${4:-}
[ -n "$AUTORUN" ] && [ -n "$SO" ] || usage
[ -d "$AUTORUN" ] || die "$AUTORUN is not a folder"
[ -f "$SO" ] || die "$SO not found (run 'make' in the fm1 folder first)"
[ -z "$CONF" ] || [ -f "$CONF" ] || die "$CONF not found"
[ -z "$BANK" ] || [ -f "$BANK" ] || die "$BANK not found"
for f in autorun.sh autorun.json autorun.check qol-run.sh mpcqol.so; do
    [ -f "$AUTORUN/$f" ] || die "$AUTORUN/$f is missing: this is not a SampQOL OS autorun folder"
done
command -v sha256sum >/dev/null || die "sha256sum is needed"

RUN="$AUTORUN/qol-run.sh"; ASH="$AUTORUN/autorun.sh"; FM1="$AUTORUN/fm1"
mkdir -p "$FM1/orig"
[ -f "$FM1/orig/qol-run.sh" ] || cp "$RUN" "$FM1/orig/qol-run.sh"
[ -f "$FM1/orig/autorun.sh" ] || cp "$ASH" "$FM1/orig/autorun.sh"
[ -f "$FM1/orig/autorun.check" ] || cp "$AUTORUN/autorun.check" "$FM1/orig/autorun.check"
command -v python3 >/dev/null || die "python3 is needed to rewrite autorun.check"

# The stock firmware runs autorun.sh only when autorun.check matches.
check_value() {
    python3 -I - "$AUTORUN" <<'PY'
import hashlib, pathlib, sys
root = pathlib.Path(sys.argv[1])
blob = (root / "autorun.json").read_bytes() + (root / "autorun.sh").read_bytes()
blob += b"f7bed1c9-7e90-4731-9194-c996666b4331"
print(hashlib.sha256(blob).hexdigest())
PY
}
if [ "$(check_value)" != "$(tr -d '\r\n' < "$AUTORUN/autorun.check")" ]; then
    die "autorun.check does not match autorun.json + autorun.sh of this folder. Is this an unmodified SampQOL OS release? (SampQOL OS 0.2.0 matches)"
fi

if grep -q 'fm1mix' "$RUN"; then
    echo "qol-run.sh already has the fm1mix lines: only updating files and pins"
else
    # Anchors in qol-run.sh of SampQOL OS 0.2.0. Each must occur exactly once.
    A1='cp "$DIR/mpcqol.so" "$SO" 2>/dev/null'
    A2='      export LD_PRELOAD="$SO"'
    A3='    CODE=$?'
    for a in "$A1" "$A2" "$A3"; do
        n=$(grep -cF -- "$a" "$RUN" || true)
        [ "$n" = 1 ] || die "qol-run.sh does not look like SampQOL OS 0.2.0 (anchor '$a' found $n times). Patch by hand: see fm1/patch/README.md"
    done
    TMP="$RUN.fm1tmp"
    awk -v a1="$A1" -v a2="$A2" -v a3="$A3" '
    {
        if ($0 == a2) {
            print "      export LD_PRELOAD=\"$SO${FM1SO:+:$FM1SO}\""
            next
        }
        print
        if ($0 == a1) {
            print "# fm1mix: open FM1 DX7 synth mixed into the app (see fm1/README.md)"
            print "FM1SO="
            print "if [ -f \"$DIR/fm1/fm1mix.so\" ]; then"
            print "    cp \"$DIR/fm1/fm1mix.so\" /tmp/fm1mix.so 2>/dev/null && chmod 644 /tmp/fm1mix.so && FM1SO=/tmp/fm1mix.so"
            print "    export FM1MIX_DIR=\"$DIR/fm1\""
            print "fi"
        }
        if ($0 == a3) {
            print "    [ -n \"$FM1SO\" ] && cp /tmp/fm1mix.log \"$DIR/fm1/fm1mix.log\" 2>/dev/null"
        }
    }' "$RUN" > "$TMP"
    mv -f "$TMP" "$RUN"
    chmod 755 "$RUN" 2>/dev/null || true
fi

# New pin for qol-run.sh in autorun.sh.
NEW=$(sha256sum "$RUN" | cut -d' ' -f1)
n=$(grep -cE '^(PINS=")?[0-9a-f]{64}  qol-run.sh$' "$ASH" || true)
[ "$n" = 1 ] || die "autorun.sh has $n pin lines for qol-run.sh (expected 1)"
TMP="$ASH.fm1tmp"
sed -E "s/^(PINS=\")?[0-9a-f]{64}  qol-run.sh\$/\1$NEW  qol-run.sh/" "$ASH" > "$TMP"
mv -f "$TMP" "$ASH"
chmod 755 "$ASH" 2>/dev/null || true
printf '%s' "$(check_value)" > "$AUTORUN/autorun.check"

cp "$SO" "$FM1/fm1mix.so"
chmod 644 "$FM1/fm1mix.so"
if [ -n "$CONF" ]; then cp "$CONF" "$FM1/fm1.conf"
elif [ ! -f "$FM1/fm1.conf" ]; then
    here=$(cd "$(dirname "$0")" && pwd)
    [ -f "$here/../card/fm1.conf" ] && cp "$here/../card/fm1.conf" "$FM1/fm1.conf" || echo "note: no fm1.conf, defaults apply"
fi
[ -z "$BANK" ] || cp "$BANK" "$FM1/$(basename "$BANK")"

# Same check as autorun.sh does at start-up.
PINS=$(awk '/^PINS="/{sub(/^PINS="/,""); p=1} p{ if (sub(/"$/,"")) { print; exit } print }' "$ASH")
if (cd "$AUTORUN" && echo "$PINS" | sha256sum -c >/dev/null 2>&1); then
    echo "pin check of autorun.sh: OK"
else
    die "pin check FAILED: the MPC would start stock. Restore with revert.sh"
fi
echo "done:"
echo "  $RUN            - starts the app with mpcqol.so and fm1mix.so"
echo "  $ASH            - pins the new qol-run.sh"
echo "  $AUTORUN/autorun.check         - rewritten for the new autorun.sh (stock firmware check)"
echo "  $FM1/           - fm1mix.so, fm1.conf$( [ -n "$BANK" ] && printf ', %s' "$(basename "$BANK")" ), orig/"
echo "Copy the whole autorun folder to the root of the exFAT card."
