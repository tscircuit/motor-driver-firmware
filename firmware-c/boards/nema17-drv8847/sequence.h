#ifndef DRV8847_SEQUENCE_H
#define DRV8847_SEQUENCE_H
#include <stdint.h>
/* Bit 0..3 drives GPIO18..21. Matches firmware/drivers/drv8847.py. */
static inline uint8_t drv8847_mask(unsigned phase) {
    const uint8_t states[] = {1, 5, 4, 6, 2, 10, 8, 9};
    return states[phase & 7];
}
static inline unsigned drv8847_phase(unsigned phase, int direction, int factor, uint32_t steps) {
    /* Unsigned wrap is also modulo eight, including reverse and long runs. */
    return (phase + (uint32_t)direction * (2u / factor) * steps) & 7;
}
#endif
