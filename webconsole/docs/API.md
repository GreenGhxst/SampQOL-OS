# HTTP API v1

All endpoints are under `/api/v1`. Every request except `health`,
`auth/status` and `auth/pair` requires the session cookie; modifying
requests (POST/PUT/DELETE) also require `X-DM-CSRF`. Roles: V = viewer,
F = file_manager, A = admin. Errors are JSON `{ok:false, error, message}`
with `error` one of `invalid_argument` 400, `unauthenticated` 401, `csrf`/
`forbidden`/`read_only` 403, `not_found` 404, `exists`/`busy`/`cancelled`
409, `no_space` 507, `unsupported` 501, `unavailable` 503, `io` 500,
`partial` 207.

Paths are always `volume` (server id) + `path` (relative, `/`-separated).

| Method | Path | Role | Purpose |
|---|---|---|---|
| GET | `/health` | – | liveness |
| GET | `/auth/status` | – | authenticated?, transport trust, csrf |
| POST | `/auth/pair` `{code, role}` | – | create session (sets cookie) |
| POST | `/auth/elevate` `{password}` | V | become admin for this session |
| POST | `/auth/logout` | V | revoke own session |
| GET | `/session` · `/session/list` | V · A | own session · all sessions |
| POST | `/session/revoke` `{handle}` · `/session/revoke_others` | A | revocation |
| GET | `/volumes` · POST `/volumes/refresh` | V | volume registry |
| GET | `/files?volume&path&offset&limit&sort&desc&filter&kind&hidden` | V | incremental listing |
| GET | `/files/stat` · `/files/peaks?buckets` · `/files/hash` | V | metadata, WAV peaks, SHA-256 (job when > 64 MB) |
| GET | `/files/download?volume&path[&inline=1]` | V | streaming download, Range supported |
| POST | `/files/mkdir` `{volume,path}` · `/files/rename` `{volume,path,name}` | F | |
| POST | `/files/copy` · `/files/move` `{items[],dest{volume,path},conflict}` | F | batch job (202 `{job}`) |
| POST | `/files/delete` `{items[],permanent}` | F | trash (default) or permanent, job |
| GET | `/files/trash?volume` · POST `/files/trash/restore` `{volume,id}` · `/files/trash/purge` | V/F | recovery |
| POST | `/files/batch_rename/plan` · `/apply` `{volume,dir,names[],pattern,start}` | F | `{name}{ext}{n}{n2}{date}` |
| POST | `/transfers/upload` `{volume,dir,name,size,sha256?,inbox?}` | F | start (201 `{id,chunk_max}`) |
| PUT | `/transfers/upload/:id?offset=` (raw body) | F | sequential chunk; 400 with current `received` to resume |
| GET | `/transfers/upload/:id` · `/transfers` | V | state |
| POST | `/transfers/upload/:id/commit` `{conflict}` | F | verify + atomic placement, or inbox |
| DELETE | `/transfers/upload/:id` | F | abort |
| GET | `/jobs` · `/jobs/:id` · POST `/jobs/:id/cancel` | V | per-session jobs (admin: `?all=1`) |
| GET | `/search?q&kind&ext&volume&min&max&sort&desc&offset&limit` | V | index search |
| GET | `/search/status` · POST `/search/index` `{volume}` · `/search/duplicates` `{volume}` | V | jobs |
| GET/POST | `/collections`, `/collections/:id/rename|items|remove`, DELETE `/collections/:id` | V | virtual collections |
| GET/POST | `/favorites` `{volume,path,on}` · GET `/recents` · GET/POST `/tags` | V | |
| GET | `/inbox` · `/inbox/:id` · `/inbox/:id/peaks` · `/inbox/:id/audio` | V | staged samples |
| POST | `/inbox/:id/rename` · `/inbox/:id/commit` `{volume,path,conflict}` · DELETE `/inbox/:id` | F | |
| GET | `/storage/status` · `/storage/network` · `/storage/journal` · `/storage/usage?volume&path` | V | dashboard |
| GET | `/screen/status` · `/screen/frame` (RGB565LE, headers `X-DM-Frame-*`) | V | rate-limited per session |
| POST | `/screen/input` | F | always 501 (remote control disabled) |
| GET | `/admin/status` · `/admin/format/devices` | A | |
| POST | `/admin/format/preflight` `{volume}` · `/admin/format/confirm` `{token,volume,device,fs_type,label,confirm_erase}` | A | simulation only |
