# NEMA23 DRV8452 native firmware

Target: [imrishabh18/nema-23-stepper-controller](https://tscircuit.com/imrishabh18/nema-23-stepper-controller#schematic), published **1.3.2**, release `ce2edb82-bc7a-4eb3-9cbd-280f173e6dcd`. Source pin mapping, `usb-pd-power.tsx`, `passives.tsx` and README were retrieved from that exact release. This is a separate image from the DRV8825 controller.

```sh
cmake -S firmware-c -B build/nema23 -G Ninja \
  -DPICO_BOARD=pico -DMOTOR_BOARD=nema23_drv8452 \
  -DPICO_SDK_PATH=/path/to/pico-sdk \
  -DMOTOR_INITIAL_NAME=NEMA23 -DMOTOR_INITIAL_THRESHOLD=60
cmake --build build/nema23
```

Install `build/nema23/motorcontrol.uf2` through the **programming USB** port's RPI-RP2 volume. The motor USB port is separate. The flash part is W25Q16 (2 MiB). Do not install the DRV8825 image on this board. Both targets share PIO motion, redundant saved name/alarm settings, USB protocol 3 and 100 kHz/channel ADC capture.

## Bring-up behavior

- POWER_ARM (GPIO0), ARM (22), WAKE (6), PD_BUS_ENABLE (8), STEP (20) and buzzer (3) boot low. SPI CS (17) boots high. Nothing starts automatically.
- DIR is GPIO21; SPI0 MISO/CS/SCK/MOSI are 16/17/18/19, mode 1 at 1 MHz. RGB red/green/blue are 25/10/11. TMP102 is I2C1 on 26/27 at 0x48. PD I2C0 is 4/5; PD_PG_N is 1, eFuse fault is 2 and the shared temperature/rail interlock is 24.
- INA240A1 and 10 mΩ shunts produce **0.2 V/A**, referenced to nominal 1.65 V. Both regular telemetry and high-rate graph frames use this scale. Readings remain uncalibrated winding currents, not supply currents.
- Default current is **1 A peak**, approximately 0.707 A sinusoidal RMS. `set_current_limit` accepts 0.1–3.965 A peak while stopped. The current DAC rounds down using the board's nominal external VREF divider and TI's 0.66 V/A transimpedance. The limit is not saved and returns to 1 A on reboot.
- DRV8452 is configured and read back with ARM low before every start. Hardware is fixed to 1/16, with full/half/quarter/eighth/sixteenth logical increments grouping pulses. Rising-edge STEP, smart-tune ripple decay and latched OCP/OTSD are explicit. Automatic interpolation, auto-torque, custom tables and untuned sensorless stall detection are disabled. Optional AS5600 is DNP and not used.
- TMP102 comparator thresholds are 70°C high / 55°C low and its configuration is verified. Starting/rearming requires valid temperature **below 55°C**. At 60°C, moving current folds back to half and stays reduced until explicit re-arm. At 70°C or on sensor, driver, SPI/configuration, PD or eFuse failure, ARM/WAKE/POWER_ARM are latched off. Heartbeat expiry or USB loss stops pulses and disarms power. Renewing a heartbeat does not restart motion.

## Power sequencing and limits

The **raw motor USB input defaults to 28 V in hardware**, independently of MCU firmware. The eFuse isolates it at boot; `pd_voltage_requested=5` initially is a software standby intention, with `pd_request_sent=false`, not a measured rail voltage or evidence that CH224A has received a 5 V request.

`set_pd_voltage` queues 5/9/12/15/20/28 V. ARM, WAKE and POWER_ARM remain low for a 12-second rail discharge interval before the write. The board's 220 µF / 10 kΩ nominal time constant is 2.2 seconds; the delay is not a voltage measurement and must be checked on physical bring-up. Voltage changes never restart motion.

**PG is ignored by default.** The earlier implementation waited for PD_PG_N before allowing the I2C request, which could prevent the charger from ever receiving its first software voltage request. Firmware now enables the buffered bus only after an explicit voltage request and validates CH224A communication, protocol status and available current. It does not need an initial 28 V EPR contract to request a supported lower voltage. The buffer starts disabled and is disabled on disarm, communication failure or fault.

CH224A addresses 0x22/0x23 are detected. Register 0x0A values 0–5 request fixed voltages. After two seconds settling, require PD protocol-active status (0x09 bit 3), EPR-active status (bit 4) for 28 V, and at least a **3 A available contract** from register 0x50. An ACK alone is not successful negotiation. No PPS/AVS or >28 V requests are sent. Software PG gating and its transition check are optional; `set_pd_power_good_override` with `enabled:false` restores strict PG checks until reboot. Changing this setting disarms the rail. `pd_power_good` remains the raw input and `pd_power_good_override` identifies whether it is ignored.

After the contract is verified, the user must explicitly issue `arm_power`. Firmware verifies temperature and fresh contract status, enables the eFuse, waits for the shared protected-rail interlock, wakes the driver and verifies SPI with ARM low. The driver is then returned to sleep while awaiting a motion command. Supply and interlock status are monitored while armed. Actual PD or protected-rail voltages are unavailable because neither has an MCU ADC path.

```json
{"cmd":"set_current_limit","peak_a":1}
{"cmd":"set_pd_voltage","voltage":20}
{"cmd":"status"}
{"cmd":"arm_power"}
{"cmd":"disarm_power"}
```

Wait for `power_state="Ready to arm motor power"` before arming. Existing motion commands still require fresh heartbeats. `disarm_power` stops motion and isolates the motor rail immediately. After a fault, require cooling below 55°C and the discharge interval, then a new explicit arm. Keep the peak current appropriate to the attached motor; the NEMA enclosure size alone does not specify its electrical rating.

## Evidence and remaining physical checks

Host tests exercise the production parser and power/driver module with simulated GPIO, I2C and SPI: disabled boot pins, discharge delay, contract-current and PG gates, explicit arm, readback failure, configuration corruption, foldback/shutdown, power/USB loss, bounds and current scale. CI cross-builds both board targets. These tests do not establish actual power delivery, SPI waveforms, current calibration, motor torque or physical travel.

On 2026-10-14, the first connected NEMA23 controller (`e6658c344b36892b`) enumerated as native C after flashing, reported valid TMP102 temperature (25.1°C), both current channels, no driver/eFuse fault, a 1 A peak limit and disabled motor/power rail. Motor USB power-good was absent. No rail arming or motor movement was requested during that check.

References: [TI DRV8452 datasheet Rev B](https://www.ti.com/lit/ds/symlink/drv8452.pdf), SPI format/register map, external VREF current scaling, mode selection and wake timing; [WCH CH224 datasheet](https://www.wch-ic.com/downloads/CH224DS1_PDF.html), I2C configuration/status/current registers; exact published board source and README above.

A second controller (`e6658c344b0a562a`), connected to a motor, was flashed on the same date. It reported healthy sensing at 26.1°C and approximately 99,967 ADC samples/sec per winding. Motor and power rail remained disabled; motor USB power-good was absent. The connected motor was not moved.

Driver-disabled electrical pulse checks on the second board emitted exact counts at 1k, 64k and 200k requested pulses/sec, with zero PIO queue underruns while current data streamed. Observed throughput at the 200k setting was approximately 199,588 pulses/sec. These checks left the driver asleep and the eFuse isolated; they are not shaft-motion or SPI/power-stage validation.

## Physical motion test (2026-10-14)

Controller `e6658c344b0a562a`, with its motor connected, was tested with PG ignored, a 15 V request, a reported 3 A PD contract and a 1 A peak current setting. The protected-rail/eFuse interlock and driver SPI readback passed. It completed ten full steps forward and ten full steps backward at 20 full steps/sec with 400 ms ramps (18° commanded each way), exactly 160 emitted pulses each direction, and about 0.7 A observed in an energized winding. Commanded position returned to zero. The user confirmed seeing the motor turn forward and back. The test ended stopped and disarmed. Actual rail voltage, calibrated current and loaded positional accuracy were not measured.
