#ifndef CURRENT_CAPTURE_H
#define CURRENT_CAPTURE_H
#include <stdbool.h>
#include <stdint.h>
#define CURRENT_SAMPLE_HZ 100000u
#define CURRENT_BIN_PAIRS 100u
#define CURRENT_MAX_BINS 32u
typedef struct {
    uint32_t min, max, sum, count;
} current_stats_t;
typedef struct {
    current_stats_t winding[2];
} current_bin_t;
typedef struct {
    uint64_t first_sample_us, captured, dropped;
    unsigned count;
    current_bin_t bins[CURRENT_MAX_BINS];
} current_frame_t;
void current_capture_init(void);
bool current_capture_snapshot(uint16_t raw[2]);
void current_capture_read(current_frame_t *frame, bool connected);
void current_capture_drop(unsigned bins);
#if !defined(MOTOR_BOARD_DRV8452) && !defined(MOTOR_BOARD_DRV8847)
void current_capture_analyze(void);
#endif
void current_stats_add(current_stats_t *stats, uint16_t raw);
#endif
