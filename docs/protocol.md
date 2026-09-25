# Serial protocol

Newline-delimited JSON at 115200 baud. Protocol version remains **3**; the new capability fields are additive. The transport drops whole lines longer than 512 characters and reads at most 128 characters per loop iteration.

| Command | Fields | Behavior |
| --- | --- | --- |
| `heartbeat` | none | Renew motion lease; no acknowledgement |
| `status` | `id` | Emit telemetry, then acknowledgement |
| `stop` | `id` | Release motor coils |
| `start` | `id`, `mode`, `resolution`, `direction`, `speed_sps`, optional `steps` | Start explicit motion after safety checks |
| `set_threshold` | `id`, `threshold_c` | Persist alarm threshold while stopped |
| `beep` | `id` | Play a 600 ms test tone |
| `set_device_name` | `id`, `name` | Persist USB name while stopped; acknowledge before reset after 750 ms |

`mode` is `steps` or `continuous`; `direction` is `1` or `-1`. Counts and speed use the selected resolution. A supported resolution is a key in `capabilities.resolutions`; its value is the number of increments per full motor step. For example `quarter: 4` means four increments per full step, only when supported by the connected driver.

```json
{"id":1,"cmd":"start","mode":"steps","resolution":"half","direction":1,"speed_sps":40,"steps":200}
```

An acknowledgement has `type: "ack"`, the supplied `id`, and `ok`. Failed commands include `error`; successful commands include the current `threshold_c`, `device_name`, and `restarting` state. Clients must wait for acknowledgement before displaying a setting as saved. A heartbeat does not start or resume motion.

Telemetry runs every 250 ms and retains existing protocol-3 fields (temperature/alarm, fault state, motion counts/state, current availability, USB naming, device ID, LED state). New identity fields are `board_id`, `mcu`, and `driver`.

`capabilities` contains:

- `resolutions`, `full_steps_per_revolution`.
- `min_speed_sps`, `max_speed_sps`, `max_steps`.
- `start_below_c`, `shutdown_c`, `threshold_min_c`, `threshold_max_c`.
- `heartbeat_timeout_ms`, `buzzer_hz`.
- `step_note`, `current_note`.

The dashboard uses these fields to populate options, limits, estimates and hardware labels. Older version-3 firmware without capabilities falls back to the original board values. `current_a: null` / `current_available: false` means no measurement; it is not zero current. `usb_name_supported: false` means the rename control is unavailable, without disabling motion commands.

Device renaming preserves VID/PID and serial number in the provided USB adapter. The connection drops during reset; reconnect after the device re-enumerates. No move is resumed after reconnect.
