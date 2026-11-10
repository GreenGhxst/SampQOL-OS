#!/bin/sh
# Undo apply.sh: put back the original qol-run.sh and autorun.sh and remove
# the fm1 folder.   sh revert.sh <autorun folder>
set -e
AUTORUN=${1:-}
[ -n "$AUTORUN" ] && [ -d "$AUTORUN/fm1/orig" ] || { echo "usage: sh revert.sh <autorun folder>   (needs autorun/fm1/orig)" >&2; exit 2; }
cp "$AUTORUN/fm1/orig/qol-run.sh" "$AUTORUN/qol-run.sh"
cp "$AUTORUN/fm1/orig/autorun.sh" "$AUTORUN/autorun.sh"
rm -rf "$AUTORUN/fm1"
rm -f "$AUTORUN/logs/fm1mix.log"
echo "reverted: $AUTORUN is the original SampQOL OS folder again"
