#ifndef STALL_ESTIMATOR_H
#define STALL_ESTIMATOR_H
#include <stdbool.h>
#include <stdint.h>
#define STALL_FEATURE_HZ 1000u
#define STALL_HISTORY 4096u
typedef struct { bool moving, healthy; int voltage, direction, factor; float speed, profile_speed, temperature; } stall_context_t;
typedef struct { bool valid, extrapolated, outlier; float score, ripple_rms, coverage; uint64_t analyzed, dropped; uint32_t window_ms, compute_max_us; const char *reason; } stall_result_t;
void stall_estimator_init(bool calibrated_device);
void stall_estimator_context(stall_context_t context, uint64_t now);
void stall_estimator_sample(uint16_t a, uint16_t b, uint64_t sample_us);
void stall_estimator_gap(uint64_t count);
void stall_estimator_evaluate(uint64_t now);
bool stall_estimator_active(void);
void stall_estimator_compute_time(uint32_t us);
stall_result_t stall_estimator_result(void);
/* Also accepts recorded millisecond bins for deterministic host replay tests. */
void stall_estimator_bin(float a,float b,float ripple,float variance,uint64_t at);
bool stall_estimator_features(const float (*samples)[4],unsigned count,float full_speed,float features[7]);
float stall_estimator_score(const float features[7],bool extrapolate,bool *outlier);
#endif
