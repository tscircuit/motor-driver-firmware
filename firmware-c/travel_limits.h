#ifndef TRAVEL_LIMITS_H
#define TRAVEL_LIMITS_H
#include <stdbool.h>
#include <stdint.h>
typedef struct { bool enabled; int64_t min, max; } travel_limits_t;
static inline bool travel_inside(travel_limits_t l, int64_t p) {
    return !l.enabled || (p >= l.min && p <= l.max);
}
/* Reserve every queued pulse, not just pulses already observed by the counter. */
static inline uint32_t travel_budget(travel_limits_t l, int64_t p, int direction,
                                     uint64_t queued, uint32_t requested) {
    if (!l.enabled) return requested;
    if (!travel_inside(l,p) || (direction != 1 && direction != -1)) return 0;
    uint64_t remaining = (uint64_t)(direction > 0 ? l.max-p : p-l.min);
    if (queued >= remaining) return 0;
    remaining -= queued;
    return remaining < requested ? (uint32_t)remaining : requested;
}
#endif
