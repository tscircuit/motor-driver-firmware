# NEMA23 / DRV8452 / PCB 1.3.2

Board-specific pins for the NEMA23 generation. The existing native STEP/DIR
backend (`../../motor.c`) and power/SPI module (`../../nema23.c`) are retained.
Select `MOTOR_BOARD=nema23_drv8452`. See [the NEMA23 guide](../../NEMA23.md) for
power arming, current limits, protection policy and validation evidence.

```sh
cmake -S firmware-c -B build/nema23-drv8452-1.3.2 -G Ninja \
  -DPICO_BOARD=pico -DMOTOR_BOARD=nema23_drv8452 -DPICO_SDK_PATH=/path/to/pico-sdk
cmake --build build/nema23-drv8452-1.3.2
```
