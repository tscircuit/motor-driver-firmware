# Motor driver firmware

Public dashboard: https://seve-motor-temperature.seveibarts.chatgpt.site

Open in desktop Chrome or Edge, connect the programming USB port, click Connect board, and select MicroPython / Board in FS mode. Only one serial client can own the port.

Features: live five-minute temperature graph; 600 ms buzzer test; persistent buzzer threshold (10–74 C); continuous or finite-step motion; left/right; 5–100 selected steps per second; Stop. Choose full (1.8 degrees, 200/revolution) or half (0.9 degrees, 400/revolution) for the 17HE15-1504S. Counts and speed use the selected increment. Quarter stepping is unavailable on this PCB: no adjustable intermediate winding-current reference or current feedback reaches the MCU. Firmware rejects unsupported resolutions. Half stepping alternates one/two coils without current normalization; torque ripple and additional heating are possible. Counts are commanded rather than encoder-measured; initial alignment and missed steps affect actual position. Current is unavailable because this PCB has no MCU current measurement path.

Motion must be requested explicitly after boot, reconnect, stop or fault. Browser sends heartbeats every 400 ms only while visible. Firmware releases coils after 1.5 seconds without heartbeat; hiding the page or disconnecting also sends Stop. Driver fault, invalid temperature, hardware ALERT, or 75 C temperature stops motion. A start requires a valid temperature below 60 C. A 2-second watchdog resets into motor-disabled startup if firmware stalls. This board's nominal hardware current trip is about 1 A; it is not a measured value or validated continuous thermal rating.

Use an unloaded secured motor for initial use. Keep the tab visible while running. Stop releases holding torque. Motor motion is never started by visiting the public site; Web Serial requires the visitor to choose their own attached device. No remote-control server or telemetry upload exists.

Firmware source: the four Python files in firmware/. Temperature and buzzer settings remain active without the browser; motion does not.

Validation: JS syntax; live hardware telemetry; threshold validation and alarm behavior; four-step moves in both directions; invalid speed rejected; continuous motion stopped after missing heartbeat (8 commanded steps at 5 steps/sec); explicit Stop; motor-disabled boot. Eight half steps in each direction, quarter-resolution rejection, half-step continuous heartbeat timeout and explicit Stop passed on hardware. Eight-state sequence, reverse traversal, bridge input states and unchanged full-step sequence passed code checks. Browser UI and live telemetry preview previously verified. Optional WebMCP read-tool execution was unavailable.

User LED on GPIO25: off while idle, 1 Hz during motion, 4 Hz while the temperature/sensor alarm is active. Alarm takes priority over motion. Manual test tone alone does not activate the alarm LED. Timing logic verified; live GPIO telemetry toggled during a six-half-step move and a temporary temperature alarm; original threshold restored and motor left stopped. Optical LED brightness was not measured.


## Hardware and installation

Designed for the [RP2040 motor controller](https://tscircuit.com/imrishabh18/rp2040-motor-controller#files) with a DRV8847 driver and TMP102 sensor. Tested with MicroPython 1.29.0 and a StepperOnline 17HE15-1504S motor.

| Signal | RP2040 GPIO |
| --- | --- |
| Driver inputs A1 / A2 / B1 / B2 | 18 / 19 / 20 / 21 |
| Motor enable | 22 |
| Driver nFAULT / TMP102 ALERT | 23 / 24 |
| User LED | 25 |
| I2C1 SDA / SCL | 26 / 27 |
| Buzzer | 16 |

Install MicroPython on the RP2040 first. Disconnect the dashboard's serial connection and remove motor power before updating firmware. Install `mpremote` on your computer, then upload all four firmware files to the board's filesystem root (replace `PORT` with its serial device):

```sh
python3 -m pip install mpremote
mpremote connect PORT fs cp firmware/device_config.py :device_config.py
mpremote connect PORT fs cp firmware/tmp102.py :tmp102.py
mpremote connect PORT fs cp firmware/main.py :main.py
mpremote connect PORT fs cp firmware/boot.py :boot.py
```

Press RUN/reset to start the firmware. An existing firmware watchdog can interrupt file transfers; if it does, disable that watchdog by starting from a fresh MicroPython installation before copying. Erasing the board filesystem also removes saved settings. Keep motor power disconnected until all four files are installed.

The default buzzer threshold is 65°C. Changes made in the dashboard persist in `alarm.json` on the board; that device-specific file is not part of this repository.

## Run the dashboard locally

No frontend build or dependencies are required:

```sh
python3 -m http.server 8000 --directory dist
```

Open `http://localhost:8000` in desktop Chrome or Edge. Web Serial requires a secure context (HTTPS or localhost). The dashboard communicates directly with the connected board using newline-delimited JSON, protocol version 3, at 115200 baud.


## Rename the USB device

Connect the board, stop the motor, and enter a **USB device name** in the dashboard's Device identity panel. Choose **Save name & restart**, then reconnect and select the new name. Names accept 1–32 printable ASCII characters; leading/trailing spaces are removed. The name is stored in `device.json` on the board, independently of the alarm threshold.

`boot.py` applies the USB product and CDC interface strings before enumeration using MicroPython's [`machine.USBDevice`](https://docs.micropython.org/en/v1.29.0/library/machine.USBDevice.html) API. The default name is **Motor bench**. VID/PID and the hardware serial number stay unchanged, preserving device identity and compatibility. OS/browser label caches may require unplugging and reconnecting the programming USB cable. The RP2040 BOOTSEL bootloader name is not changed.

All four firmware files are required, followed by a hardware reset. The dashboard disables renaming when firmware lacks boot-time USB naming support. The serial command is `{"cmd":"set_device_name","id":1,"name":"Left motor"}`; a successful acknowledgement precedes a deferred hardware reset. Renaming is rejected during motion, and motion is rejected while reset is pending. No movement resumes automatically. Telemetry includes `device_name`, `usb_name`, `usb_name_supported`, `usb_name_error`, `device_id`, and `restarting` while keeping protocol version 3 backward-compatible.

Host-side validation: `python3 -m unittest discover -s tests -v` and `node tests/test_dashboard.cjs` (plus `node --check dist/app.js`).
