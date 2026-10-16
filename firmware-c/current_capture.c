#include "current_capture.h"
#include "hardware/adc.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"
#include <string.h>
#if !defined(MOTOR_BOARD_DRV8452) && !defined(MOTOR_BOARD_DRV8847)
#include "stall_estimator.h"
static uint64_t analysis_cursor;
void current_capture_analyze(void);
#endif
#define RING_WORDS 16384u
/* Multiple of the ring size, so each DMA restart preserves channel parity. */
#define BLOCK_WORDS 0xffffc000u
static volatile uint16_t ring[RING_WORDS] __attribute__((aligned(32768)));
static int channel;
static volatile uint64_t completed_words;
static uint64_t started_us, cursor, dropped;
static bool was_connected;
static void dma_complete(void) {
    if (dma_hw->ints1 & (1u << channel)) {
        dma_hw->ints1 = 1u << channel;
        completed_words += BLOCK_WORDS;
        dma_channel_set_write_addr(channel, (void *)ring, false);
        dma_channel_set_trans_count(channel, BLOCK_WORDS, true);
    }
}
static uint64_t pairs_written(void) {
    uint32_t saved = save_and_disable_interrupts();
    uint64_t words = completed_words + BLOCK_WORDS - dma_hw->ch[channel].transfer_count;
    restore_interrupts(saved);
    return words / 2;
}
void current_capture_init(void) {
    adc_init();
    adc_gpio_init(28);
    adc_gpio_init(29);
    adc_select_input(2);
    adc_set_round_robin((1u << 2) | (1u << 3));
    adc_fifo_setup(true, true, 1, true, false);
    adc_set_clkdiv((float)clock_get_hz(clk_adc) / (2 * CURRENT_SAMPLE_HZ) - 1);
    channel = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(channel);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_16);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    channel_config_set_ring(&c, true, 15);
    channel_config_set_dreq(&c, DREQ_ADC);
    dma_channel_configure(channel, &c, (void *)ring, &adc_hw->fifo, BLOCK_WORDS, false);
    /* Motor edge counting owns DMA IRQ 0; ADC uses the independent IRQ 1. */
    irq_set_exclusive_handler(DMA_IRQ_1, dma_complete);
    dma_channel_set_irq1_enabled(channel, true);
    irq_set_enabled(DMA_IRQ_1, true);
    dma_start_channel_mask(1u << channel);
    started_us = time_us_64();
    adc_run(true);
}
bool current_capture_snapshot(uint16_t raw[2]) {
    uint64_t n = pairs_written();
    if (!n)
        return false;
    unsigned i = ((n - 1) * 2) & (RING_WORDS - 1);
    raw[0] = ring[i];
    raw[1] = ring[i + 1];
    return !(raw[0] & 0x8000) && !(raw[1] & 0x8000) && raw[0] >= 16 && raw[0] <= 4079 &&
           raw[1] >= 16 && raw[1] <= 4079;
}
void current_capture_read(current_frame_t *f, bool connected) {
    memset(f, 0, sizeof(*f));
    uint64_t n = pairs_written(), end = n / CURRENT_BIN_PAIRS * CURRENT_BIN_PAIRS;
    f->captured = n;
    if (!connected || !was_connected) {
        cursor = end;
        dropped = 0;
    }
    was_connected = connected;
    if (!connected)
        return;
    /* Leave eight ms of ring headroom while copying bounded work. */
    if (end - cursor > CURRENT_MAX_BINS * CURRENT_BIN_PAIRS) {
        uint64_t next = end - CURRENT_MAX_BINS * CURRENT_BIN_PAIRS;
        dropped += next - cursor;
        cursor = next;
    }
    f->first_sample_us = started_us + cursor * (1000000u / CURRENT_SAMPLE_HZ);
    while (cursor + CURRENT_BIN_PAIRS <= end && f->count < CURRENT_MAX_BINS) {
        current_bin_t *bin = &f->bins[f->count++];
        for (unsigned j = 0; j < CURRENT_BIN_PAIRS; ++j) {
            unsigned i = ((cursor + j) * 2) & (RING_WORDS - 1);
            current_stats_add(&bin->winding[0], ring[i]);
            current_stats_add(&bin->winding[1], ring[i + 1]);
        }
        cursor += CURRENT_BIN_PAIRS;
    }
    f->dropped = dropped;
}
void current_capture_drop(unsigned bins) { dropped += bins * CURRENT_BIN_PAIRS; }

#if !defined(MOTOR_BOARD_DRV8452) && !defined(MOTOR_BOARD_DRV8847)
/* Independent of the USB export cursor. Bounded by the DMA ring, with explicit
   data-loss reporting rather than silently scoring overwritten samples. */
void current_capture_analyze(void) {
    uint64_t n=pairs_written();
    if(!stall_estimator_active()){analysis_cursor=n;return;}
    if(n-analysis_cursor>RING_WORDS/2-512){stall_estimator_gap(n-analysis_cursor);analysis_cursor=n;return;}
    while(analysis_cursor<n){unsigned i=(analysis_cursor*2)&(RING_WORDS-1);stall_estimator_sample(ring[i],ring[i+1],started_us+analysis_cursor*10);analysis_cursor++;}
}
#endif
