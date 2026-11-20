#!/bin/sh
# Produces a downloadable source package of the Web Console (no build output)
# plus its SHA-256. Usage: tools/make-source-package.sh [outdir]
set -eu
HERE=$(cd "$(dirname "$0")/.." && pwd)
OUT=${1:-$HERE/dist}; mkdir -p "$OUT"
VER=$(grep -o 'kVersion = "[^"]*"' "$HERE/src/server/web_console.cpp" | cut -d'"' -f2)
NAME="diskmode-webconsole-src-$VER"
tar --exclude='webconsole/build' --exclude='webconsole/build-*' --exclude='webconsole/dist' --exclude='*.log' --exclude='.gitignore' \
    -C "$HERE/.." --transform "s,^webconsole,$NAME," -czf "$OUT/$NAME.tar.gz" webconsole
( cd "$OUT" && sha256sum "$NAME.tar.gz" > "$NAME.tar.gz.sha256" && cat "$NAME.tar.gz.sha256" )
