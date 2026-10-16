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


## Firmware estimator (1.0.11)

The DRV8825 firmware now drains the existing 100 kHz-per-winding ADC DMA ring through a cursor independent of USB export. Every raw pair contributes to millisecond mean, min/max and squared-current statistics. The existing seven waveform features use these complete 1 kHz bins; an additional raw-sample ripple RMS is reported for research. The ADC rate itself is unchanged. A fresh contiguous window is required after any data loss. At low speed the window expands to cover at least four electrical cycles (up to four seconds); the supported feature-analysis range is 4–320 full steps/sec. Speeds beyond the sampling/window limits return unknown rather than a fabricated score.

Full/half/quarter/eighth/sixteenth logical resolutions are converted to physical full-step speed. The board remains at 1/16 hardware microsteps. All advertised motor supply requests and both directions can produce an estimate. Outside 9 V requested, forward, 60 full steps/sec, the result is explicitly an **unvalidated extrapolation**. Extrapolation normalizes the spectral features to commanded electrical frequency and removes absolute-current magnitude from the distance metric; it cannot prove that the model remains accurate under those conditions. A distant feature vector is also labeled extrapolated/outlier. This remains observational and does not stop motion.

Telemetry reports `current_stall_valid`, `current_stall_score`, `current_stall_extrapolated`, `current_stall_outlier`, `current_stall_reason`, raw analyzed/dropped counts, window length, maximum processing duration and raw ripple RMS. Capability fields identify the 100 kHz raw / 1 kHz feature rates. `set_current_stream_enabled` controls opt-in USB waveform export (off at boot). Firmware analysis continues when export is disabled. Default operation sends only lightweight telemetry and scores; optional waveform export uses peak-preserving 10 ms bins and may reduce continuous-window availability at the lowest speeds. Hardware fault, stopped/braking, startup, missing-current, and unsupported bandwidth states remain unknown.

## Lossless diagnostic recording (1.0.12)

`begin_stall_trace` arms retention of the final contiguous 4096 millisecond bins. It is accepted only while stopped and does not start motion. At the end of the next move, the ring is retained rather than cleared. `get_stall_trace` with an integer `offset` returns up to 16 rows, `total`, and `sample_hz: 1000`. Each row contains `[device_time_us, mean_a, mean_b, mean_peak_to_peak, mean_variance]` in the estimator's existing current units. Download is accepted only while stopped, so it does not compete with ADC processing during motion. Beginning another move replaces the retained data. No extra sample buffer is allocated.

The recording is the final **contiguous** window, not necessarily the entire move. Startup/ramp changes or dropped samples reset the ring; check its timestamps and the change in `current_stall_samples_dropped`. Trace collection does not change the score, threshold, shutdown policy, or motion commands. Independently observed transition markers are needed to measure detection delay; button markers include observer reaction time and must not be treated as exact mechanical onset timestamps.
