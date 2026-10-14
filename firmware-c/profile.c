#include "profile.h"
#include <math.h>
#include <string.h>

int resolution_factor(const char *name) {
    const char *names[] = {"full", "half", "quarter", "eighth", "sixteenth"};
    for (int i = 0; i < 5; ++i)
        if (name && strcmp(name, names[i]) == 0)
            return 1 << i;
    return 0;
}
bool profile_validate(double speed, double acceleration, int factor, uint32_t steps) {
    return factor > 0 && factor <= 16 && (factor & (factor - 1)) == 0 && isfinite(speed) &&
           speed >= MIN_SPEED_SPS && speed * (16 / factor) <= MAX_PULSE_HZ &&
           isfinite(acceleration) && acceleration > 0 && steps <= MAX_MOVE_STEPS;
}
void profile_init(profile_t *p, double target, double acceleration, double initial, uint32_t count,
                  bool fixed) {
    *p = (profile_t){.target = target,
                     .acceleration = acceleration,
                     .initial = initial,
                     .count = count,
                     .fixed = fixed};
    if (count) {
        /* Symmetric finite trajectory in physical microsteps, including a
           nonzero low starting rate for legacy acceleration commands. */
        p->peak = fmin(target, sqrt(initial * initial + acceleration * count));
        p->ramp_distance = (p->peak * p->peak - initial * initial) / (2 * acceleration);
        p->duration =
            2 * (p->peak - initial) / acceleration + (count - 2 * p->ramp_distance) / p->peak;
    }
}
double profile_speed(const profile_t *p, uint32_t position) {
    if (p->braking)
        return sqrt(fmax(0, p->initial * p->initial - 2 * p->acceleration * position));
    double distance = p->count ? fmin(position, p->count - position) : position;
    if (!p->count && p->initial > p->target)
        return fmax(p->target,
                    sqrt(fmax(0, p->initial * p->initial - 2 * p->acceleration * distance)));
    return fmin(p->count ? p->peak : p->target,
                sqrt(p->initial * p->initial + 2 * p->acceleration * distance));
}
static double ramp_time(double distance, double initial, double acceleration) {
    if (!distance)
        return 0;
    /* Stable even for very small distances and large initial speeds. */
    return 2 * distance / (initial + sqrt(initial * initial + 2 * acceleration * distance));
}
double profile_time(const profile_t *p, uint32_t position) {
    if (!position)
        return 0;
    if (p->braking)
        return 2 * position / (p->initial + profile_speed(p, position));
    if (p->count) {
        if (position <= p->ramp_distance)
            return ramp_time(position, p->initial, p->acceleration);
        if (position >= p->count - p->ramp_distance)
            return p->duration - ramp_time(p->count - position, p->initial, p->acceleration);
        return (p->peak - p->initial) / p->acceleration + (position - p->ramp_distance) / p->peak;
    }
    double distance = fabs(p->target * p->target - p->initial * p->initial) / (2 * p->acceleration);
    if (position >= distance)
        return fabs(p->target - p->initial) / p->acceleration + (position - distance) / p->target;
    if (p->target >= p->initial)
        return ramp_time(position, p->initial, p->acceleration);
    return 2 * position /
           (p->initial + sqrt(fmax(0, p->initial * p->initial - 2 * p->acceleration * position)));
}
bool profile_segment(const profile_t *p, uint32_t generated, uint32_t *pulses, uint32_t *delay) {
    if (p->count && generated >= p->count)
        return false;
    double rate = profile_speed(p, generated + 1);
    uint32_t n = (uint32_t)fmax(1, fmin(64, floor(rate * 0.001)));
    if (p->count && n > p->count - generated)
        n = p->count - generated;
    double ticks;
    double cruise_distance =
        fabs(p->target * p->target - p->initial * p->initial) / (2 * p->acceleration);
    if (!p->braking && p->count && generated >= p->ramp_distance &&
        generated + n <= p->count - p->ramp_distance) {
        ticks = ceil(PULSE_CLOCK_HZ / p->peak);
    } else if (!p->braking && !p->count && generated >= cruise_distance) {
        ticks = ceil(PULSE_CLOCK_HZ / p->target);
    } else {
        double elapsed = profile_time(p, generated + n) - profile_time(p, generated);
        ticks = ceil(elapsed * PULSE_CLOCK_HZ / n);
    }
    if (!isfinite(ticks) || ticks > UINT32_MAX || ticks < 50)
        ticks = fmax(50, fmin(UINT32_MAX, ticks));
    *pulses = n;
    *delay = (uint32_t)ticks - 34;
    return true;
}
