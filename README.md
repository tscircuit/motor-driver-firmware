# Motor driver firmware

MicroPython motor-control firmware with a Web Serial dashboard. Motion and safety logic are separated from driver chips, board wiring, and MCU services so additional hardware can reuse the controller and protocol.

[Public dashboard](https://motorcontrol.tscircuit.com) · [Porting guide](docs/architecture.md) · [Serial protocol](docs/protocol.md)

## Supported hardware

The included hardware profile is **RP2040 / DRV8847**, for the [tscircuit RP2040 motor controller](https://tscircuit.com/imrishabh18/rp2040-motor-controller#files), with a TMP102 temperature sensor and a StepperOnline 17HE15-1504S motor (200 full steps/revolution).

The original firmware was exercised on this board with MicroPython 1.29.0. The modular refactor is covered by host-side regression tests, including a simulated second driver/platform; it has not yet been flashed and validated on hardware. Other driver chips and MCUs need an adapter/profile and hardware validation. The host simulator is a test double, not a supported physical driver.

Features include continuous or finite-step motion, direction and speed controls, full and half stepping on the DRV8847, temperature history, persistent buzzer threshold, a test tone, USB device naming, and status LED patterns. The dashboard reads driver resolutions, motor geometry, safety limits, and current availability from telemetry. It can select serial devices with other USB identifiers and retains compatibility with older protocol-3 firmware.

## Repository layout

```text
firmware/
  boot.py                    Safe outputs and optional USB identity
  main.py                    Small application entrypoint
  board_config.py            Selected board composition
  boards/rp2040_drv8847.py    Wiring, limits, sensor/driver/platform assembly
  core/controller.py         Motion, safety, commands, telemetry, LED/alarm policy
  core/runtime.py            Cooperative event loop and watchdog handling
  core/settings.py           Persistent alarm settings
  device_config.py           Persistent device name and validation
  drivers/drv8847.py          Chip-specific bridge sequence and wake/fault behavior
  sensors/tmp102.py          I2C sensor protocol and configuration verification
  platforms/micropython.py    Pins, I2C, PWM, clock, watchdog, serial, reset, unique ID
  platforms/usb_identity.py   Optional MicroPython USB descriptor customization
dist/                        Buildless Web Serial dashboard
scripts/build_firmware.py    Stage a complete firmware tree for one board
tests/                      Host-only controller, driver, USB/settings and UI tests
```

See the [porting guide](docs/architecture.md) for interfaces and the steps to add hardware. The reusable controller imports neither `machine` nor a concrete board/driver. It also runs on CPython with fake adapters.

## Install firmware

Install a suitable MicroPython build on the board first. Disconnect the dashboard and remove motor power for firmware maintenance. Install `mpremote` on your computer, then stage a fresh output directory:

```sh
python3 -m pip install mpremote
python3 scripts/build_firmware.py --board rp2040_drv8847
```

Copy **the entire staged directory contents** to the board filesystem root, keeping the subdirectories. Replace `PORT` with the programming serial port:

```sh
cd build/firmware
mpremote connect PORT fs cp -r core drivers sensors platforms boards : + fs cp device_config.py board_config.py main.py boot.py :
```

Press RUN/reset after the copy finishes. Runtime USB naming can cause an `mpremote` soft reset to disconnect the port; reconnect and retry if this happens. Do not resume motor use until all files have uploaded successfully. A running watchdog may interrupt a slow update: use a maintenance MicroPython boot without the application/watchdog before retrying. Never bypass protection in the production profile.

The staging command refuses a nonempty output directory to avoid mixing versions. Use a fresh `--output` directory for subsequent builds. Device files `alarm.json` and `device.json` are not included or overwritten; settings survive an ordinary upload. Legacy top-level `tmp102.py` from earlier versions is unused and may remain on the board. Do not upload only the old four-file layout.

For a new board, add its composition module and select it with `--board`; do not scatter pin changes through the controller. The checked-in `board_config.py` selects the RP2040/DRV8847 profile for source-tree installs.

## Current board wiring and limits

| Signal | RP2040 GPIO |
| --- | --- |
| DRV8847 A1 / A2 / B1 / B2 | 18 / 19 / 20 / 21 |
| Motor enable | 22 |
| Driver nFAULT / TMP102 ALERT | 23 / 24 |
| User LED | 25 |
| I2C1 SDA / SCL | 26 / 27 |
| Buzzer | 16 |

The DRV8847 profile allows 5–100 selected steps/sec and up to 100,000 steps per move. Full steps are 1.8°; half steps are 0.9°. Quarter steps require controllable intermediate winding currents and are not supported on this PCB. Half stepping alternates one/two energized coils without current normalization, so torque ripple and additional heating are possible. Step counts are commanded, not encoder measurements; alignment and missed steps affect actual position.

There is no MCU measurement path for current or motor supply voltage on this board. The nominal hardware current trip is about 1 A; this is neither a measured current nor a validated continuous thermal rating. The encoder may remain disconnected.

Motion requires an explicit start and a recent browser heartbeat. Coils release after 1.5 seconds without a heartbeat, on driver/thermal faults, on invalid temperature, or at 75°C. Starting requires a valid temperature below 60°C. A 2-second watchdog resets into disabled startup if firmware stalls. No motion resumes after reset, reconnect, stop, or fault. Stop releases holding torque. Use a secured unloaded motor for initial checks.

The alarm defaults to 65°C and accepts 10–74°C, with 2°C hysteresis. It persists in `alarm.json` and runs without the browser. The LED is off when idle, blinks at 1 Hz during motion, and at 4 Hz during a temperature/sensor alarm; the alarm takes priority. Manual test tones alone do not activate the alarm LED.

## USB device naming

In the dashboard, stop the motor, enter a **USB device name**, and choose **Save name & restart**. Reconnect and select the new name. Names accept 1–32 printable ASCII characters; surrounding spaces are removed. The name persists in `device.json` independently of the alarm threshold.

USB naming is a platform capability, not a requirement for motion control. The supplied MicroPython adapter uses [`machine.USBDevice`](https://docs.micropython.org/en/v1.29.0/library/machine.USBDevice.html) when available. It changes product/CDC labels while preserving VID/PID and hardware serial number. It does not change the BOOTSEL bootloader name. Some OS/browser caches require unplugging and reconnecting the programming cable. The dashboard disables renaming if the platform cannot apply it.

The dashboard receives temperature telemetry at 8 Hz (every 125 ms) and retains five minutes of history. Install the updated firmware to use this rate; older firmware continues reporting at 4 Hz. The TMP102 remains configured for 8 Hz conversions, with controller protection checks every 100 ms.

## Run and test locally

```sh
python3 -m unittest discover -s tests -v
node --check dist/app.js
node tests/test_dashboard.cjs
python3 -m http.server 8000 --directory dist
```

Open `http://localhost:8000` in desktop Chrome or Edge. Web Serial needs HTTPS or localhost, and only one client can own the port. The site talks directly to the selected board at 115200 baud; it has no remote-control server or telemetry upload. Keep the page visible while running; hiding it sends Stop. Visiting the public site does not start a motor.

CI runs host tests, JavaScript checks, and firmware staging. Host tests exercise forward/reverse coil sequences, finite motion, heartbeat expiry across clock wrap, sensor/thermal/driver failures, malformed commands, saved settings, USB rename/restart, output cleanup, and an unrelated simulated driver that advertises quarter steps. Physical validation is still required for any new adapter or board.


## Dashboard deployment

The Vercel project `tscircuit/motorcontrol` is connected to this repository. Production deployments from `main` serve only `dist/` at https://motorcontrol.tscircuit.com. `vercel.json` configures a static deployment with no install/build step. `.vercelignore` excludes firmware, tests, scripts, and docs from CLI uploads.

Cloudflare DNS: `motorcontrol` is a DNS-only CNAME to `9009e8f2bfd94482.vercel-dns-016.com`. Vercel manages HTTPS. For a manual production deployment, use `vercel deploy --prod --scope tscircuit` from the linked repository root. Local Vercel state and environment files are ignored by Git.
