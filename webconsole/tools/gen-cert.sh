#!/bin/sh
# Generates a self-signed certificate for the Web Console's USB-C address.
# Browsers (and iPadOS in particular) do NOT trust this automatically: the
# certificate must be installed and trusted on each client, see
# docs/SECURITY.md "Certificate provisioning". Usage: tools/gen-cert.sh <outdir> [ip]
set -eu
OUT=${1:?outdir}; IP=${2:-192.168.155.1}
mkdir -p "$OUT"
openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes -days 1825 \
  -keyout "$OUT/webconsole-key.pem" -out "$OUT/webconsole-cert.pem" \
  -subj "/CN=DISKMODE Web Console" \
  -addext "subjectAltName=IP:$IP,DNS:mpc-sample.local,DNS:localhost,IP:127.0.0.1" \
  -addext "basicConstraints=critical,CA:TRUE" -addext "keyUsage=digitalSignature,keyEncipherment,keyCertSign" 2>/dev/null
chmod 600 "$OUT/webconsole-key.pem"
echo "certificate: $OUT/webconsole-cert.pem"; echo "key:         $OUT/webconsole-key.pem"
echo "fingerprint: $(openssl x509 -in "$OUT/webconsole-cert.pem" -noout -fingerprint -sha256 | cut -d= -f2)"
