# Latest NEMA17 / DRV8825 / PCB 1.0.42

Board-specific pins for the current NEMA17 generation. The existing native
STEP/DIR motion implementation is `../../motor.c`; its behavior is unchanged.
This remains the default `MOTOR_BOARD=drv8825` target. See the
[native installation guide](../../README.md) for the existing controls and tests.

```sh
cmake -S firmware-c -B build/nema17-drv8825-1.0.42 -G Ninja \
  -DPICO_BOARD=pico -DMOTOR_BOARD=drv8825 -DPICO_SDK_PATH=/path/to/pico-sdk
cmake --build build/nema17-drv8825-1.0.42
```

The full/half/quarter/eighth/sixteenth logical increments group fixed 1/16 STEP
pulses. This target retains RGB, CH224K voltage requests and winding ADC capture.
Its pin meanings and active-low driver enable differ from the older DRV8847 PCB.
