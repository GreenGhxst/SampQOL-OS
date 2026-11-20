# Vendored third-party code

| Library | Version | Commit | License | File |
|---|---|---|---|---|
| cpp-httplib (yhirose) | v0.18.3 | a7bc00e3307fecdb4d67545e93be7b88cfb1e186 | MIT | `httplib/httplib.h` |
| nlohmann/json | v3.11.3 | 9cca280a4d0ccf0c08f47a99aa71d1b0e52f8d03 | MIT | `nlohmann/json.hpp` |

Both are single-header, compiled into the `diskmode-webconsole` binary. OpenSSL
(Apache-2.0, system library) is linked for TLS and SHA-256; it is not vendored.
