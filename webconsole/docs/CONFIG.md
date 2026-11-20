# Configuration

`diskmode-webconsole --config config.json` (command-line flags override the
file; `--help` lists them). All keys are optional except `data_dir` and
`auth.pairing_code` (a random code is generated and logged when absent,
which is only useful for development).

```json
{
  "bind_host": "0.0.0.0",
  "port": 8765,
  "tls_cert": "/media/3265-3932/diskmode/certs/webconsole-cert.pem",
  "tls_key":  "/media/3265-3932/diskmode/certs/webconsole-key.pem",
  "allow_plain_http": false,
  "data_dir": "/media/3265-3932/.diskmode",
  "web_root": "/media/3265-3932/diskmode/web",
  "application_medium_mount": "/media/3265-3932",
  "auth": {
    "pairing_code": "change-me",
    "admin_password_hash": "pbkdf2-sha256$100000$<salt>$<hash>",
    "idle_timeout_minutes": 30,
    "absolute_timeout_hours": 12,
    "max_failures": 5,
    "lockout_seconds": 60,
    "max_sessions": 32
  },
  "max_upload_bytes": 4294967296,
  "upload_chunk_max": 8388608,
  "workers": 2,
  "max_queue": 64,
  "screen_max_fps": 10,
  "screen_source": "simulated",
  "format_fs": ["exfat"],
  "allowed_hosts": [],
  "index_on_start": false,
  "log_level": "info",
  "volumes": [ ... ]
}
```

* `admin_password_hash`: produce with `diskmode-webconsole --hash-password 'secret'`.
  When empty, administrator features are disabled entirely.
* `screen_source`: `simulated`, `file:/run/diskmode/screen.rgb565`
  (320×240 RGB565 or binary PPM written by an external capture), or `none`.
* `allowed_hosts`: extra `Host` header values; IP literals, `localhost` and
  `*.local` are always accepted, everything else gets 421.

## Volume rules

When `volumes` is absent the MPC Sample default policy is used
(`VolumeRegistry::default_rules`). Each rule:

| Key | Meaning |
|---|---|
| `id` | stable identifier used by the API (`[A-Za-z0-9_-]{1,32}`) |
| `name` | user-visible name |
| `kind` | `removable`, `internal`, `usb`, `simulated` |
| `mount_point` | exact mount point to match, or |
| `mount_prefix` | first mount whose path starts with this prefix (e.g. `/media/`) |
| `fs_types` | optional filesystem filter |
| `subdir` | expose only this sub-directory of the mount |
| `directory` | `true`: a plain directory, not a mount (simulation and tests only) |
| `writable` | policy permission for writes (effective only when the mount is rw and device-owned) |
| `format_candidate` | may be offered to the (simulated) format wizard |
| `protected_paths` | relative prefixes that are never modified, deleted or indexed |

Default policy:

```json
[
 {"id":"sdcard","name":"microSD card","kind":"removable","mount_prefix":"/media/","fs_types":["exfat","vfat","msdos","fuseblk"],"writable":true,"format_candidate":true,"protected_paths":["autorun","mpcqol.conf",".diskmode"]},
 {"id":"internal-samples","name":"Internal: Samples","kind":"internal","mount_point":"/data","subdir":"UserContent/Samples","writable":false},
 {"id":"internal-projects","name":"Internal: Projects","kind":"internal","mount_point":"/data","subdir":"UserContent/Projects/User","writable":false},
 {"id":"factory-content","name":"Factory content (read-only)","kind":"internal","mount_point":"/content","writable":false}
]
```

Internal paths come from melodee-sample's source and are **not verified on
hardware**; they stay read-only until they are.
