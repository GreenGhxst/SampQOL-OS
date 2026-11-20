# Security model

## Threat model

The console is reachable only over the MPC Sample's USB-C network (NCM) or
loopback. The attacker of interest is software on the connected computer or
tablet: another browser tab (cross-site requests, DNS rebinding), a
malicious page, or a device plugged in later. The console never listens on
the public internet and must not be port-forwarded.

## Authentication and sessions (`src/server/auth.cpp`)

* **Pairing**: `POST /api/v1/auth/pair` with the device pairing code
  (configured, or generated and logged) creates a session with role
  `viewer` or `file_manager`. `admin` cannot be requested here.
* **Administrator**: `POST /api/v1/auth/elevate` with the administrator
  password (PBKDF2-HMAC-SHA256, 100 000 iterations, per-device salt).
  Elevation is per session and requires a trusted transport. When no hash
  is configured, administrator features do not exist.
* Sessions: 32-byte random id in an `HttpOnly; SameSite=Strict` cookie
  (`Secure` on HTTPS); 32-byte CSRF token returned in JSON only. Idle
  timeout 30 min, absolute 12 h, explicit logout, administrator revocation
  by opaque handle, activity log (last 50 events) per session.
* Rate limiting: per client address, 5 failures → 60 s lockout, for pairing
  and elevation alike. Comparisons are constant-time over digests.
* Pairing codes and session ids never appear in URLs, in logs (only the
  client address and role are logged) or in persistent browser storage; the
  frontend keeps the CSRF token in memory only.

## Request hardening (`src/server/web_console.cpp`)

* **Host check**: only IP literals, `localhost`, `*.local` and configured
  `allowed_hosts` are accepted (421 otherwise) — defeats DNS rebinding.
* **CSRF**: every state-changing request needs `X-DM-CSRF` equal to the
  session token, a `Sec-Fetch-Site` of `same-origin`/`none` when the browser
  sends it, and a matching `Origin` when present.
* **CSP** `default-src 'self'`, no inline scripts or styles, `frame-ancestors
  'none'`, `nosniff`, `Referrer-Policy: no-referrer`; `Cache-Control:
  no-store` on the API.
* Body limits: JSON bodies ≤ 1 MiB, upload chunks ≤ `upload_chunk_max`
  (8 MiB), total upload ≤ `max_upload_bytes`; read/write socket timeouts.
* Authorization is enforced server-side per volume id and relative path;
  client-supplied names are never used as filesystem authority
  (`src/core/path_auth.cpp`: component validation, `..`/absolute/backslash
  refusal, O_NOFOLLOW directory walking, symlink refusal, protected-path
  prefixes, fd-relative operations).

## Transport: HTTPS and certificate provisioning

Non-loopback sessions are "trusted" only over TLS. Without `tls_cert`/
`tls_key` the server binds to 127.0.0.1 unless `allow_plain_http` is set;
in that mode every non-loopback session is downgraded to `viewer` and all
modifying endpoints are refused — the specification's "refuse authenticated
file-management access over an untrusted connection".

Provisioning (documented process, not yet exercised on hardware):

1. On a computer run `tools/gen-cert.sh <outdir> 192.168.155.1`
   (use the address shown by *SD Utilities → USB-C network* once the real
   address is known; it is embedded as a SAN together with
   `mpc-sample.local`). The script prints the SHA-256 fingerprint.
2. Copy `webconsole-cert.pem` and `webconsole-key.pem` to
   `<card>/diskmode/certs/` and reference them in `config.json`.
3. Trust the certificate on each client. **Self-signed certificates are
   not trusted automatically**:
   * macOS: Keychain Access → import → *Always Trust*.
   * Windows: `certmgr.msc` → Trusted Root Certification Authorities.
   * iPadOS/iOS: open the `.pem` (AirDrop/Files) → Settings → Profile
     Downloaded → Install; then Settings → General → About → Certificate
     Trust Settings → enable full trust for the certificate. Without this
     second step Safari shows a warning and `fetch()` fails.
   * Android: Settings → Security → Install a certificate → CA certificate.
4. Compare the fingerprint shown by the browser with the one printed in
   step 1 before trusting.

Certificates are valid for five years and bound to the address; regenerate
when the device address changes.

## What the console refuses by design

* Browsing anything outside the configured volume roots; following symlinks.
* Writing to internal storage (policy default), `autorun`, `mpcqol.conf`,
  `.diskmode`, or any volume the USB host currently owns.
* Overwriting existing files unless the client explicitly selects
  *overwrite* for that operation.
* Executing uploaded files (they are written with mode 0644 and never run),
  remote input injection (501), any real formatting (simulation only).
* Serving the same session over an untrusted transport once it was created
  over a trusted one.
