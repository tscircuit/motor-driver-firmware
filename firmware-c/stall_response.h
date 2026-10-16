#ifndef STALL_RESPONSE_H
#define STALL_RESPONSE_H
#include <stdbool.h>
#include <stdint.h>

/* Policy is separate from detection: unknown/uncertain readings reset the timer. */
typedef struct { bool active, timed_out; uint64_t since_us; } stall_response_t;
static inline void stall_response_update(stall_response_t *s, bool detected,
                                         uint64_t now, uint32_t timeout_ms) {
    if (!detected) {
        *s = (stall_response_t){0};
        return;
    }
    if (!s->active) {
        s->active = true;
        s->since_us = now;
    }
    s->timed_out = timeout_ms && now - s->since_us >= (uint64_t)timeout_ms * 1000;
}
static inline int stall_response_tone(const stall_response_t *s, bool enabled,
                                      uint64_t now) {
    /* Three fast, lower-pitched chirps, followed by a short pause. */
    uint64_t phase = (now - s->since_us) / 1000 % 700;
    return enabled && s->active && (phase < 70 || (phase >= 140 && phase < 210) ||
                                    (phase >= 280 && phase < 350)) ? 1800 : 0;
}
#endif
