# Second-generation NEMA17 / DRV8847

Maintenance firmware for the deprecated DRV8847 board. This is the second ordered
NEMA17 generation, not the retired first board and not the latest DRV8825 board.
Its precise registry/PCB release number is unknown. The GPIO mapping and eight
bridge states come from the repository's existing `rp2040_drv8847` profile and
driver, introduced in `0c9251b` and exercised by the original firmware history.

```sh
cmake -S firmware-c -B build/nema17-drv8847 -G Ninja \
  -DPICO_BOARD=pico -DMOTOR_BOARD=nema17_drv8847 \
  -DPICO_SDK_PATH=/path/to/pico-sdk
cmake --build build/nema17-drv8847
```

Use Pico SDK 2.3.1 and the toolchain described in the [native guide](../../README.md).
Flash only `build/nema17-drv8847/motorcontrol.uf2` to this board's programming USB
BOOTSEL volume. A DRV8825 or NEMA23 UF2 must never be installed on it.

| Signal | RP2040 GPIO |
| --- | --- |
| Bridge A1/A2/B1/B2 | 18/19/20/21 |
| Enable (high = enabled) | 22 |
| Driver nFAULT / TMP102 ALERT (active low) | 23/24 |
| Single status LED | 25 |
| TMP102 I2C1 SDA/SCL | 26/27 |
| Buzzer | 16 |

This chip is driven by four bridge inputs, not STEP/DIR. PIO emits the historical
eight-state phase sequence, clearing all inputs for 1 us between phases. Full
steps advance two phases; half steps advance one. The retained initial phase is
energized after a 3 ms wake delay; legacy finite starts also retain their 100 ms
alignment dwell. Fixed 400 ms ramps and jogging omit that dwell. Prepared moves
energize the retained phase without advancing until `run_move`.

Core 1 feeds per-phase timing and coil masks to PIO. DMA copies the PIO's emitted
phase counter to memory, including transitions pending at a Stop/retarget boundary.
Queued future phases are discarded on retarget, keeping the actually emitted
phase/commanded position. FIFO starvation holds the last phase and is reported as
`pulse_queue_underruns`. Stop and all safety shutdowns release enable and all four
inputs. Rates are limited to 0.01–20,000 selected increments/sec; this is a firmware
bound, not a validated mechanical operating speed or guaranteed achieved rate.
Period quantization is 100 ns, with a three-clock difference for the first phase
because no preceding count/push tail runs. The PIO count observes coil commands,
not shaft motion. `hardware_steps_executed` counts selected full/half transitions;
`coil_step_rate_hz` measures their rate. Position is normalized to full steps.

Protocol 3 retains the bench, single-motor and gantry interfaces: start, jog,
decelerate, fixed ramps, prepared GOTO, heartbeat, name/alarm persistence, tones,
songs and BOOTSEL entry. It advertises only `full:1` and `half:2`. There is no
software PD selection, RGB, current measurement, voltage measurement, encoder or
STEP-only pulse test. Commands for unavailable controls are rejected. Half steps
alternate one/two energized coils without current normalization, as in the old
firmware. Nominal hardware current trip is about 1 A, not a measured value.

GPIO22 and all bridge inputs boot low. Valid TMP102 readings below 60 C and a fresh
heartbeat are required to start; invalid/configuration-changed sensing, 75 C,
hardware ALERT, nFAULT, USB loss or a 1.5 s expired lease release the motor. A 2 s
watchdog covers a stalled application. Reconnect/renewal/reset never resumes a
move. Commanded position sessions reset on reboot. LED behavior remains off idle,
1 Hz during motion and 4 Hz for alarm/fault. Ordinary buzzer alarms do not stop a
move below the shutdown limit.

Before migrating, back up the complete MicroPython filesystem, including
`device.json` and `alarm.json`. Pass existing values as `MOTOR_INITIAL_NAME` and
`MOTOR_INITIAL_THRESHOLD`, or save them through the site after installation.
Native redundant settings sectors do not read MicroPython files. Firmware updates
preserve native saved settings. Use the board's 2 MiB flash layout.

Run `scripts/test_native.sh` and `node tests/test_drv8847_dashboard.cjs`. The latter
feeds real C protocol-harness telemetry into the unchanged single-motor page and
checks capability gating, jogging and exact half-step GOTO. Host tests cannot prove
electrical timing, actual motion, loaded accuracy or supply conditions; validate
the installed build on the connected board before claiming physical operation.

## Installed-board checks (2026-10-15)

Controller `e6658c344b809535` identified itself as `rp2040_drv8847` under the old
MicroPython firmware. Its complete 40-file filesystem was backed up before
installing build `bc3182f626237f17`; the existing name `Gantry X` and 55 C alarm
threshold were retained. USB required a reconnect after the initial UF2 copy.
The native application then reported the expected build/board, valid temperature
around 27 C, disabled startup and no driver or hardware thermal fault.

At 20 full steps/sec with fixed 400 ms ramps, the application completed 10 full
steps forward and 10 backward, reporting exactly 10 PIO coil transitions each
way. Two prepared half-step moves of four increments each also completed, with
no commanded position change before `run_move`. All four moves had zero reported
queue underruns and no sensor/driver/thermal fault. Commanded position returned
to zero and the motor was left disabled. These are emitted coil-command counts;
they do not independently establish shaft movement, torque, loaded accuracy or
electrical waveforms. The unchanged live site's page/scripts matched the source
used for the protocol/DOM compatibility tests.
