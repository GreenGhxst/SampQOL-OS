# Hardware-specific formatting audit

Real formatting is **disabled in this build**. `FormatGuard`
(`src/core/format_guard.cpp`) executes through a `FormatExecutor` whose only
implementation is `SimulatedExecutor`: it records the request and never
invokes a utility. The HTTP layer reports `simulation: true` on every
format response and the UI labels the wizard "SIMULATION ONLY".

## Preflight checks implemented (all must pass)

| Check | Source of truth | Hardware status |
|---|---|---|
| `volume_known` | volume registry | – |
| `policy_format_candidate` | volume rule `format_candidate` | – |
| `volume_is_removable` / `not_internal_storage` | rule kind | – |
| `block_device_discovered` | `/sys/block` inventory, disk derived from the mount's device | unverified: real inventory on the device |
| `device_is_removable_sd` | `/sys/block/<dev>/removable` **and** media `flash_sd` | **fails closed**: `flash_sd` is only known through edisksd (D-Bus), which this process does not query |
| `device_not_system` | no `/`, `/data`, `/content`, `/usr`, `/boot`, `/var`, `/system` mount on the disk | unverified partition map |
| `device_not_internal_emmc` | device name ≠ `mmcblk0` | assumption from sample-base tests |
| `not_application_medium` | `application_medium_mount` config | must be configured correctly |
| `no_active_jobs` | job queue items referencing the volume | – |
| `not_exported_to_usb_host` / `ownership_known` | configfs mass-storage LUNs | – |
| `filesystem_supported` | `format_fs` (default `exfat`) | – |
| `format_utility_verified` | passes only for the simulation executor | **no utility verified** (`mkfs.exfat` presence/version on the firmware unknown) |
| `unmount_remount_verified` | passes only for the simulation executor | edisksd `Unmount`/`Mount` sequence not exercised |
| `no_format_in_progress` | guard state | – |

Confirmation requires: administrator session, a single-use token issued by
a passing preflight (120 s, bound to volume + device + session), the same
volume id and device string again, `confirm_erase: true`, a supported
filesystem and a sane label. The preflight is re-run at confirm time; any
change (card exported, job started, device changed) refuses the operation.
A typed phrase is deliberately not the only gate.

## Simulation tests (all automated, see `tests/core/test_format.cpp`, `tests/http/test_api.py`)

* internal storage, system partitions, unknown devices, unverified USB
  devices, a busy card, an exported card, the application medium and
  unauthorized sessions are all refused;
* tokens are single use, session-bound, device-bound and expire;
* the simulated execution never contains `mkfs` and never touches a device.

## What a production executor must provide before the gate can open

1. A separate, setuid-free privileged helper started by the service manager,
   accepting only `--volume <id>` (validated against the same registry),
   never a device path or shell string from the client.
2. edisksd integration to confirm `Media = flash_sd`, unmount all volumes of
   the disk, format, re-read the partition table and remount; with the
   outcome verified by reading the new filesystem type.
3. An exclusive lock shared with the job queue and the transfer manager so
   no file operation can start during the format.
4. A documented recovery statement: an interrupted format leaves an
   unusable card; the console must say so and must not claim undo.
5. If the card hosts the console itself, formatting requires a separate
   recovery environment (specification §17) and remains refused here.

Until all five exist and have been tested on an MPC Sample with a
sacrificial card, `FormatGuard` must keep the simulation executor.
