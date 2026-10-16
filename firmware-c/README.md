# Native RP2040 motor firmware

Native firmware runs directly on the RP2040 using the Raspberry Pi Pico SDK. No MicroPython runtime or filesystem is needed. There are [three distinct hardware targets](boards/README.md): older NEMA17/DRV8847 (`nema17_drv8847`), latest NEMA17/DRV8825 (`drv8825`, PCB 1.0.42), and NEMA23/DRV8452 (`nema23_drv8452`, PCB 1.3.2). The default remains **DRV8825 / RGB**; the build command below produces that image. Never flash it onto a different PCB. The [DRV8847 guide](boards/nema17-drv8847/README.md) covers its separate four-input coil backend; the STEP/ADC/RGB/PD details below describe DRV8825.

## Build and install

Use Pico SDK **2.3.1**, CMake, Ninja, and the `arm-none-eabi` GCC toolchain. Initialize the SDK's `lib/tinyusb` submodule. Dependencies are outside this repository; cJSON 1.7.19 is vendored with its MIT license.

```sh
export PICO_SDK_PATH=/path/to/pico-sdk
cmake -S firmware-c -B build/native -G Ninja -DPICO_BOARD=pico
cmake --build build/native
```

Copy `build/native/motorcontrol.uf2` to the board's RPI-RP2 BOOTSEL volume. Both bridge permission signals default to disabled and USB-PD selectors default to 5 V. Reconnect at 115200 baud using the existing dashboard, `/single-motor.html`, or `/gantry.html`.

For migration, stop and disconnect the dashboard, back up the complete MicroPython filesystem (especially `device.json` and `alarm.json`), and provide initial native defaults:

```sh
cmake -S firmware-c -B build/native -DMOTOR_INITIAL_NAME="Gantry Y" -DMOTOR_INITIAL_THRESHOLD=65
```

Native settings have independent storage; they do not read MicroPython files. Names and alarm thresholds can also be saved through the dashboard after migration. Subsequent native updates preserve settings. Two final flash sectors store alternating records with checksums and sequence numbers; a failed update retains the previous valid record. Flash writes only occur while stopped, and use Pico SDK's cross-core flash lockout. A linker assertion reserves these sectors. This build assumes the controller's 2 MiB flash.

The native CDC VID/PID is `2e8a:000a`. Hardware ID and serial text use the same lowercase flash unique ID as MicroPython. Startup holds the USB controller in reset during initialization, providing a deliberate detach interval before CDC enumeration after BOOTSEL updates. Setting the serial device to 1200 baud requests BOOTSEL; the `bootloader` JSON command is another maintenance path. Both stop the driver before entering the bootloader.

## Pulse timing and motion

PIO 0 state machine 0 generates STEP pulses independently of USB, I2C, ADC, flash allocation, and application execution. At a 10 MHz PIO clock, STEP high is 3 us and STEP low is at least 2 us. There is a configured input ceiling of 200,000 physical pulses/sec, rather than a MicroPython throughput ceiling. Target speed remains in the selected increment. Convert with `pulse_hz = speed_sps * 16 / resolution_factor`; for example 16,000 quarter steps/sec requests 64,000 physical pulses/sec.

Core 1 supplies short `[period, count]` segments to PIO. Segments contain at most 64 pulses and normally span about one millisecond during fast motion; low speeds use single-pulse segments. Periods are rounded up to whole 100 ns ticks, and segment transitions add four PIO clock cycles. Pulse periods and rate changes therefore have bounded quantization. There is no per-pulse Python or CPU GPIO loop. FIFO starvation holds STEP low; `pulse_queue_underruns` reports unexpected starvation. PIO state machine 1 independently observes rising STEP edges and decrements a counter. DMA continuously copies that count to RAM; DMA completion rearms the transfer for long runs. Position includes emitted pulses in partially consumed segments, and uses signed 1/16-step integer coordinates. It measures electrical STEP edges, **not shaft movement or skipped motor steps**.

Finite moves emit exact requested pulse counts and use trapezoidal/triangular ramps. Gantry commands retain fixed 400 ms ramps, `defer:true`, `run_move`, `jog`, `decelerate`, and emergency Stop. Retargeting discards queued future pulses, retains coil enable, preserves emitted position, and starts a new profile from the current commanded rate. Direction changes restart at the low rate. Deceleration is bounded to an exact finite tail. Commanded positions have a fresh random session after reboot, so previously saved dashboard slots require saving again.

`profile_speed_sps` is planned segment speed. `pulse_rate_hz` is sampled emitted pulses/sec over approximately 100 ms, and is still not measured shaft speed. Legacy `late_steps` and `max_step_lateness_us` are zero because their cooperative per-step scheduler no longer exists; use the pulse-rate and starvation fields for native timing diagnostics. Motor capability, charger behavior, current limits and supply droop still constrain physical movement.

## Safety and board controls

- Startup, reset, Stop, USB disconnect, heartbeat expiry and firmware restart release the driver. A 1.5 s heartbeat lease is required during motion; starting and running a prepared move require a heartbeat no older than 1 s.
- TMP102 configuration and protection thresholds are verified every sample. Motion requires valid temperature below 60 C to start; invalid sensing, 75 C or driver nFAULT stop it. GPIO24 is unconnected on this board; thermal protection uses the actual TMP102 readings.
- The 2 s hardware watchdog is fed only by the main protection/USB loop. If that loop hangs, reset returns the board to disabled startup. PIO runs autonomously between protection checks; the watchdog is the fallback for a stalled main loop.
- USB-PD 5/9/12/15/20 V requests, two-second settling, RGB colors, nominal INA240 samples, alarm hysteresis, PWM tones and four-byte buzzer songs retain protocol-3 compatibility. Voltage requests and LED choices are not saved and reset to 5 V/automatic.
- USB output is nonblocking and bounded. Telemetry drops whole frames when the host is slow and reserves space for acknowledgements. Input lines are capped at 512 bytes and input processing is bounded per loop. I2C operations have timeouts.

There are no homing switches, travel limits, encoder feedback, calibrated current or measured USB-PD voltage.

## Tests

Run `scripts/test_native.sh` for host motion and protocol tests. The protocol harness exercises production parsing and protection functions with fake hardware, including malformed/bool/nonfinite values, hardware rate limits, thermal/sensor/driver faults, communication loss, power settling, prepared starts, saved-setting restrictions and alarm priority. Host tests cannot prove PIO waveforms or physical motor motion.

For an electrical pulse check without moving the motor, use `test_pulses` while stopped. This command keeps nENBL asserted and nSLEEP low, drives STEP only, and counts its edges with the independent PIO counter:

```json
{"cmd":"heartbeat"}
{"cmd":"test_pulses","id":1,"pulses":64000,"pulse_hz":64000}
```

Continue heartbeats during the test. Completion reports `pulse_test_pulses` and `pulse_test_elapsed_us`. These diagnostics prove generated pulse count and overall timing while the driver is disabled; a scope/logic analyzer is needed to independently verify pulse widths and jitter, and a loaded mechanical test is needed for shaft performance. `stop` and all ordinary fault/lease protection apply during the test.

## Hardware validation (2026-10-14)

Both connected DRV8825/RGB controllers were flashed with this native application. The driver remained disabled during pulse tests. Their independent PIO counters observed exact requested counts at every tested rate, with zero pulse-queue underruns:

| Requested physical pulses/sec | Observed pulses/sec, board Y | Observed pulses/sec, board X |
| --- | --- | --- |
| 1,000 | approximately 999 | approximately 999 |
| 64,000 | approximately 63,656 | approximately 63,656 |
| 128,000 | approximately 126,436 | approximately 126,434 |
| 200,000 | approximately 199,676 | approximately 199,674 |

Rate differences include whole PIO tick quantization, four-cycle segment transitions and completion-observation overhead. These are electrical pulse tests; no shaft movement, loaded torque, physical positional accuracy, pulse widths or jitter were independently measured. USB disconnect and heartbeat expiry stopped pulse generation. Renewing the heartbeat did not restart it. Software restarts preserved saved names and alarm thresholds, returned power selection to disabled 5 V, and generated fresh position sessions. Temperatures were valid and no driver/sensor faults were reported.

Full telemetry includes `firmware_build`, a fingerprint of the compiled application sources, SDK version and initial settings. This lets a live device be checked against its build rather than only the fixed version string. Compact telemetry includes the active resolution, target speed and direction, so changing jog units is reflected during motion.

## NEMA23 / DRV8452 target

Build the separate `-DMOTOR_BOARD=nema23_drv8452` image for the published NEMA23 1.3.2 PCB. See [NEMA23.md](NEMA23.md) for exact wiring, current scale, explicit power arming, PD requirements, thermal limits and physical validation status.

### NEMA23 current controls

The dashboard exposes two independent, optional current overrides for the DRV8452 board. Both default to off after reboot. Changing either requires a stopped motor and disconnects motor power; it never arms or starts motion.

- **Ignore overcurrent fault shutdown** (`set_overcurrent_shutdown_override`, boolean `enabled`) selects DRV8452 OCP auto-retry using CTRL3.OCP_MODE and ignores only an OCP-only fault report in firmware. The hardware OCP trip remains active. Mixed faults, unreadable SPI, and an unexplained low nFAULT still shut down.
- **Disable peak winding-current limit** (`set_winding_current_limit_override`, boolean `enabled`) bypasses the configured current cap by programming the board's full-scale setting, approximately 3.965 A peak. The configured current is retained for when this override is turned off. Temperature foldback and thermal shutdown remain active.

Telemetry reports each override separately, `effective_current_peak_a`, the last read `driver_fault_register`, and `driver_overcurrent_reported`. `driver_fault_asserted` continues to report the raw nFAULT pin even when an OCP report is ignored. Older firmware hides unsupported controls.

Fault shutdowns retain `fault_gpio_snapshot` (GPIO bitmask) and `fault_driver_register` before isolating power. Check `fault_snapshot_valid` and `fault_driver_register_valid` before interpreting them. The retained snapshot is the most recent power trip since boot; live pins after shutdown describe the isolated board, not the original trigger. GPIO24 interlock loss and GPIO23 driver nFAULT have separate stop reasons.
