#ifndef MOTOR_PROFILE_H
#define MOTOR_PROFILE_H
#include <stdbool.h>
#include <stdint.h>
#define PULSE_CLOCK_HZ 10000000u
#define MAX_PULSE_HZ 200000u
#define MIN_SPEED_SPS 0.01
#define MAX_MOVE_STEPS 100000u

typedef struct {
    double target, acceleration, initial;
    uint32_t count; /* zero = continuous */
    bool fixed, braking;
    double peak, ramp_distance, duration;
} profile_t;
void profile_init(profile_t *p, double target, double acceleration, double initial, uint32_t count,
                  bool fixed);
double profile_speed(const profile_t *p, uint32_t position);
double profile_time(const profile_t *p, uint32_t position);
bool profile_segment(const profile_t *p, uint32_t generated, uint32_t *pulses, uint32_t *delay);
int resolution_factor(const char *name);
bool profile_validate(double speed, double acceleration, int factor, uint32_t steps);
#endif
