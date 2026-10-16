# NEMA17 observational stall model

The DRV8825 has no hardware torque-count register. This firmware compares the external coil-current measurements with labelled recordings. The resulting indicator is observational: it cannot command motion, change power, or stop the motor. A score is not a probability or shaft feedback.

## Data and validation

The 2026-10-16 study used controller `e66568714f117e21` and its attached motor. Twenty-four twelve-second runs covered 12.5, 25, 60 and 100 full steps/sec, both directions, and full/half/sixteenth logical step selections. Hardware microstepping remains fixed at 1/16; these logical settings group pulses rather than changing the driver mode. Supply was requested at 9 V, not independently measured during this batch. Temperature ranged from 32.1875 to 42.1875 C.

The user followed audible, timestamped fixture instructions and confirmed all free/locked states afterward. Each retained trace contains 4096 consecutive millisecond records, with no dropped raw pairs. Training uses only the steady initial state before the transition cue; cue times are not treated as exact mechanical onset times. There are no loaded-but-rotating controls and no independent motors in this data.

`tools/train_stall_model.py` explores six time-domain current features, averaging windows and regularized linear discriminants. Decision thresholds reject the free-running envelope instead of using the midpoint between class means. Window/feature/threshold selection occurs inside nested leave-one-complete-trial-out validation. The previous study's held-out runs were already examined, so this is retrospective nested validation, not a new blind test. `firmware-c/calibration/nema17-stall-20261016.json` records the dataset digest, selection and fold results. `tools/export_stall_model.py` exports coefficients and observed feature envelopes to `stall_model.h`.

The selected windows are 800 ms at 12.5 full steps/sec, 400 ms at 25, and 100 ms at 60 and 100. Worst outer-fold window error was about 61.5%, 10%, 0%, and 8.6%, respectively. These small, correlated data sets do not establish general real-world accuracy. The low-speed profile always reports uncertainty despite its fitted score. Other profiles retain an uncertainty band around their decision threshold. New mechanically observed trials, different temperatures/voltages and loaded-but-turning negatives remain necessary to establish broader reliability.

## Firmware 1.0.13

Every raw ADC pair contributes to 1 kHz current statistics before USB export; the raw rate is 100 kHz per winding. The estimator evaluates every 50 ms using bounded incremental work. Lightweight motion telemetry updates every 100 ms. At the 60/100 speed anchors the signal window is 100 ms; this is not a claim of 100 ms physical stall-detection latency.

The six features are mean radial current, radial coefficient of variation, normalized peak-to-peak ripple, ripple coefficient of variation, normalized raw ripple RMS, and mean absolute radial-current change normalized by full-step speed. No harmonic transform is required. Host/native tests replay recorded waveform windows and check feature parity, scores, raw accounting, gaps, unsupported settings and trace retention.

Only the calibrated controller is enabled. Missing current, stale/discontinuous data, acceleration/braking and electrical/sensor faults return unknown. Voltages other than a 9 V request and speeds outside 12.5–100 full steps/sec have no model. Intermediate speeds use coefficient interpolation and are explicitly uncertain. Temperatures outside the recorded range and patterns outside recorded feature envelopes are also uncertain. Changing the motor or load can invalidate the calibration even if the controller ID is unchanged.

Telemetry includes `current_stall_model`, `current_stall_state` (`free`, `suspected`, `uncertain`, `unknown`), score, reason, feature-window length, window-end timestamp, processing lag, raw analyzed/dropped counts and ripple RMS. The browser respects the uncertainty state even if the numeric score is high. The threshold line remains at 65; all values are a similarity index, not probabilities.

## Diagnostic recording

`begin_stall_trace` is accepted only while stopped and does not start motion. It retains the final contiguous 4096 millisecond bins when deceleration begins. `get_stall_trace` accepts an integer `offset` and returns up to 16 rows, `total`, `sample_hz: 1000`, and a device timestamp for host-clock alignment. Each row is `[device_time_us, mean_a, mean_b, mean_peak_to_peak, mean_variance]`. Reads are accepted only while stopped, so download does not compete with ADC processing during motion. A new move replaces the retained data. No second history buffer is allocated.

The retained data is the final contiguous window, not the entire move. Check timestamps and dropped-pair counts; startup, ramp changes or gaps reset the ring. USB waveform export remains opt-in through `set_current_stream_enabled` and is unnecessary for firmware stall evaluation.
