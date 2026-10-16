# NEMA17 current-pattern experiment

The DRV8825 has no hardware torque-count register. The experimental dashboard indicator compares current waveform features with free-running and mechanically restrained recordings. It is read-only: it has no connection to motion, stop, current-limit or power commands.

## Scope

The included calibration is specific to controller `e66568714f117e21` and its attached motor at requested 9 V, forward, 60 full steps/sec (120 half steps/sec). Current regulation was unchanged. The motor is fixed at 1/16 hardware microsteps; logical resolution only groups pulses. Other devices, directions, speeds, temperatures outside 25–40 C, electrical faults, startup/braking, stale data and incomplete sample windows return unknown. Changing the attached motor or physical load invalidates this calibration even if the controller ID is unchanged.

The score is similarity to the restrained calibration, **not a probability**, torque measurement, or proof of a stationary shaft. Loaded-but-turning motion, different mounting, temperature drift and other operating conditions have not been validated. Existing firmware fault protections operate independently.

## Recordings and validation

Three four-second runs were collected per class at 9 V and 60 full steps/sec, with fixed 400 ms ramps. The user confirmed normal rotation during free-running recordings and a stationary shaft throughout restrained recordings. Each run sent 240 full-step commands; executed pulses are not shaft feedback. The first two complete runs per class were used for fitting; the third complete run per class was held out. Samples during the first second and final 400 ms were excluded.

Seven features are computed from 500 ms windows: radial current mean, radial coefficient of variation, normalized peak-to-peak ripple, ripple coefficient of variation, third/fifth harmonic ratios, and quadrature error. At least 350 valid millisecond bins are required, with no gap larger than 25 ms. Per-class centroids and pooled feature scaling form a nearest-centroid similarity score. Patterns far from both classes return unknown.

At a threshold of 65, all six windows from the held-out free-running run (scores 0.9–1.8) and four windows from the held-out restrained run (scores 73.9–88.7) were separated. These are correlated windows from only **two held-out runs**, not a reliable estimate of real-world error rates. Additional whole-run tests with independently observed loaded-but-turning motion and stall/release transitions are needed before relying on the indicator.

Raw recordings are retained in the local experiment workspace. The compact regression fixture contains two held-out waveform windows and ten held-out feature vectors. `tests/test_current_stall.cjs` checks feature parity, classification, bad data, unsupported settings, stale data and stopped/faulted behavior. No firmware update is required; this consumes the existing current stream.
