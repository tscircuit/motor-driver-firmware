#ifndef MOTOR_H
#define MOTOR_H
#include "profile.h"
#include <stdbool.h>
#include <stdint.h>
typedef enum { STOPPED, PREPARED, FINITE, CONTINUOUS, BRAKING, DIAGNOSTIC } motor_mode_t;
typedef struct {
    motor_mode_t mode;
    int direction, factor;
    double speed, acceleration, profile_speed, pulse_rate;
    bool enabled;
    uint32_t queue_underruns;
    uint64_t executed_pulses;
    uint32_t target_pulses, elapsed_us;
    int64_t position_microsteps;
} motor_status_t;
#ifndef MOTOR_BOARD_DRV8847
bool motor_set_travel_limits(bool enabled, int64_t min, int64_t max);
bool motor_set_position(int64_t position);
#endif
void motor_init(void);
void motor_stop(void);
bool motor_start(int factor, int direction, double speed, double acceleration, uint32_t steps,
                 bool fixed, bool defer, bool jogging);
bool motor_retarget(int factor, int direction, double speed, double acceleration, bool fixed);
void motor_decelerate(void);
bool motor_run_prepared(void);
bool motor_test(uint32_t pulses, double pulse_hz);
motor_status_t motor_status(void);
#endif
