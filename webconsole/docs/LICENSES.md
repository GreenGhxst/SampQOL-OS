# Dependency and licence report

## This tree (`webconsole/`)

New code written for this project. It links against system libraries only.
Nothing from SampQOL OS (proprietary freeware, closed source) is included,
modified or redistributed; the console runs next to it on the same card.

## Vendored (compiled into the binary)

| Component | Version / commit | Licence | Notes |
|---|---|---|---|
| cpp-httplib (yhirose) | v0.18.3 `a7bc00e3` | MIT | `third_party/httplib/httplib.h`, HTTP/1.1 server, optional OpenSSL TLS |
| nlohmann/json | v3.11.3 `9cca280a` | MIT | `third_party/nlohmann/json.hpp` |

Licence texts are next to the headers (`third_party/httplib/LICENSE`,
`third_party/nlohmann/LICENSE.MIT`).

## System libraries

| Library | Licence | Use |
|---|---|---|
| glibc / libstdc++ | LGPL-2.1 / GPL-3.0 with runtime exception | statically linked in the device build (`DM_FULLY_STATIC`) |
| OpenSSL 3 | Apache-2.0 | TLS when `DM_WITH_TLS=ON` (host builds); not linked in the current device build |

SHA-256, HMAC and PBKDF2 are implemented in `src/util/sha256.cpp` /
`src/server/auth.cpp` (FIPS 180-4, RFC 2104, RFC 8018) to avoid an OpenSSL
dependency for hashing; vectors are checked in `tests/core/test_auth.cpp`.

## Reference material (read, not copied)

| Project | Licence | What was taken |
|---|---|---|
| `keremimo/sample-base` `f18e6201` | AGPL-3.0-only (kernel modules GPL-2.0) | facts about the USB gadget, card sharing, display and boot flow, cited in `docs/AUDIT.md` |
| `keremimo/melodee-sample` `f1146658` | AGPL-3.0-only | stock storage paths, DNS-rebinding and `autorun` refusal behaviour |

No source lines from these AGPL projects are present in `webconsole/`; the
AGPL therefore does not apply to this tree. Should a future integration
link against or copy sample-base code, the console would have to be
released under AGPL-3.0 as well.

## Frontend

`web/` is hand-written HTML/CSS/JS with no third-party code, fonts or icons
(the icons are Unicode glyphs and one inline SVG).
