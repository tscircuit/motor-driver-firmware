# Native firmware targets

There are three supported firmware targets, one per retained hardware generation.
The first ordered NEMA17 generation is retired and has no target here.

| Directory | Build selector (`MOTOR_BOARD`) | Board identity | Status |
| --- | --- | --- | --- |
| [`nema17-drv8847/`](nema17-drv8847/README.md) | `nema17_drv8847` | `rp2040_drv8847` | Second ordered NEMA17 generation; maintenance support for deprecated driver |
| [`nema17-drv8825-1.0.42/`](nema17-drv8825-1.0.42/README.md) | `drv8825` | `rp2040_drv8825` | Latest NEMA17, PCB release 1.0.42; default target |
| [`nema23-drv8452-1.3.2/`](nema23-drv8452-1.3.2/README.md) | `nema23_drv8452` | `rp2040_drv8452_nema23` | NEMA23, PCB release 1.3.2 |

Numbers in directories identify published **PCB releases**, not firmware releases.
The exact published version of the second NEMA17 PCB has not been established;
its directory uses the confirmed DRV8847 identity rather than inventing a version.
Its connected legacy firmware reports this board ID and the same pin mapping.

Each target has its own board definition. DRV8847 also has its own four-input PIO
motion backend in its directory. The other targets retain the existing shared
STEP/DIR backend (`../motor.c`); NEMA23 power/SPI logic remains in `../nema23.c`.
Shared protocol, USB, settings and trajectory code live in the parent directory.
Do not select an image by motor size alone: GPIO functions and enable polarities
differ, even between the two NEMA17 boards.

Keep each build in a separate directory. The historical selectors and default
DRV8825 target are preserved; no dashboard or existing motor backend changes are
required to add DRV8847. `firmware/` retains the MicroPython migration reference.

All three targets use the same protocol 3 commands and capability discovery.
Unsupported hardware features must be absent/false, and unavailable measurements
must be null. Build fingerprints identify the actual source/configuration; PCB
release, firmware release and build fingerprint are separate identities.
