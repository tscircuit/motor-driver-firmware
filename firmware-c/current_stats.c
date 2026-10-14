#include "current_capture.h"
void current_stats_add(current_stats_t *s, uint16_t raw) {
    if ((raw & 0x8000) || raw < 16 || raw > 4079)
        return;
    if (!s->count || raw < s->min)
        s->min = raw;
    if (!s->count || raw > s->max)
        s->max = raw;
    s->sum += raw;
    ++s->count;
}
