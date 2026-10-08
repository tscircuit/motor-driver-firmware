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

## Ramped motion additions (protocol 3)

`start` accepts optional `acceleration_sps2` in selected increments/sec². Capabilities add `acceleration_supported`, `min_acceleration_sps2`, `max_acceleration_sps2`, and `default_acceleration_sps2`. Telemetry adds `acceleration_sps2`, `profile_speed_sps`, `late_steps`, and `max_step_lateness_us`. Profile speed is not a measured shaft speed. See README for alignment, final-phase dwell, immediate safety stops, and scheduler limitations.

## Buzzer byte songs

`{"cmd":"play_song","id":1,"hex":"b80164000000140070036400"}` plays A4 (440 Hz) for 100 ms, a 20 ms rest, then A5 (880 Hz) for 100 ms. `{"cmd":"stop_song","id":2}` cancels playback/test tone without silencing temperature protection. Successful commands use the normal acknowledgement format.

Binary `.bin` files contain repeated four-byte records: unsigned 16-bit frequency in Hz, then unsigned 16-bit duration in milliseconds, both little-endian. Frequency is 0 (rest) or 100–10000 Hz; duration is 20–5000 ms. Maximum 192 bytes / 48 records / 60 seconds. The dashboard validates the file locally and sends its hex encoding in one command, below the 512-character framing limit. MP3/WAV/MIDI decoding is not provided. Tunes are held in RAM and not saved to flash.

Playback is nonblocking, requires the motor stopped, and is rejected during alarms or pending resets. Starting a motor, stopping/disconnecting, a test tone or a temperature/sensor alarm cancels the tune. A tune does not resume after an alarm. An abrupt cable removal allows the bounded tune to finish if the board stays powered. The default variable-pitch adapter uses PWM; other boards can omit `buzzer.play(hz)` to report unsupported.

Capabilities: `song_supported`, `song_format: "u16le-hz-u16le-ms"`, `song_max_bytes`. Telemetry: `song_playing`, `song_note` (1-based, 0 idle), `buzzer_frequency_hz`. `buzzer_on` is false during rests. Download `dist/example-song.bin` for an original ascending example tune.

## Board speech alert

- `{"id":1,"cmd":"set_hot_alert","enabled":true}` saves the spoken-alert preference;
  `false` restores normal chirps. Boolean only; motor must be stopped and no restart pending.
- `{"id":2,"cmd":"test_hot_alert"}` plays “HOT HOT HOT” once. Requires stopped motor,
  no pending restart and no active temperature alarm. `stop_song` or `stop` cancels previews.
- Alarm playback repeats with a 2000 ms quiet gap after each phrase. Alarm clearance
  cancels playback. Off/error/unsupported speech uses the existing chirp alarm.
- Telemetry: `hot_alert_enabled`, `hot_speech_playing`, `hot_speech_error`.
  Capabilities: `hot_speech_supported`, `hot_speech_pause_ms`. Acknowledgments include
  `hot_alert_enabled`. `buzzer_frequency_hz` is 0 for speech (not a single tone);
  `buzzer_on` is true during the phrase. Preview is independent of the saved option.

## Smooth jog release

Updated firmware advertises `deceleration_supported: true`. Start a half-step jog with `start`, `mode: "continuous"`, `resolution: "half"`, `speed_sps` and `acceleration_sps2`. `{"cmd":"decelerate","id":3}` turns that jog into a bounded deceleration tail using the same acceleration magnitude. Telemetry reports `motion_mode: "braking"`, remaining steps and planned speed, then `stop_reason: "Jog release complete"`. Repeated deceleration commands do not extend the tail; issuing one while stopped does not start motion. It rejects a finite-step move.

The tail ends at the profile’s low starting rate before releasing coils. It is speed ramping, not current control. Keep sending heartbeats through deceleration. `stop`, faults and heartbeat expiry still disable outputs immediately. `decelerate` does not bypass any thermal, sensor or driver protection.

## Commanded position reference

`position_full_steps` accumulates signed commanded increments across all moves, normalized to full steps. Half-step output adds/subtracts 0.5; full-step output adds/subtracts 1. This is not an encoder measurement and does not count initial alignment or external motion. It starts at zero for each controller run.

`position_session` is a fresh random nonce for each controller run on the MicroPython platform. Clients must pair it with `device_id` before reusing saved targets. Unsupported platforms may report null. The gantry page saves three pairs of coordinates in local storage and requires matching device/session identities to execute a return.
