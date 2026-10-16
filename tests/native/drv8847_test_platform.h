#ifndef DRV8847_TEST_PLATFORM_H
#define DRV8847_TEST_PLATFORM_H
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
typedef unsigned uint;
typedef struct { uint32_t fdebug, rxf[4]; } pio_hw_t;
typedef pio_hw_t *PIO;
static pio_hw_t fake_pio;
#define pio0 (&fake_pio)
#define PIO_FDEBUG_TXSTALL_LSB 0
#define DMA_IRQ_0 0
#define clk_sys 0
#define drv8847_bridge_wrap_target 1
static const int drv8847_bridge_program;
typedef int pio_sm_config;
typedef int dma_channel_config;
typedef struct { bool held; } mutex_t;
static struct { uint32_t ints0; } fake_dma;
#define dma_hw (&fake_dma)
static uint64_t fake_us;
static uint32_t fake_gpio, fake_x = UINT32_MAX, fake_isr;
static volatile uint32_t *fake_dma_destination;
static uint fake_pc = 1;
static bool fake_enabled;
static uint32_t fifo[4];
static unsigned fifo_length;
enum { pio_x, pio_isr, pio_null };
static void mutex_init(mutex_t *m) { m->held = false; }
static void mutex_enter_blocking(mutex_t *m) { assert(!m->held); m->held = true; }
static void mutex_exit(mutex_t *m) { assert(m->held); m->held = false; }
static uint64_t time_us_64(void) { return fake_us; }
static void sleep_us(uint us) { fake_us += us; }
static void gpio_put(uint pin, bool on) {
    fake_gpio = (fake_gpio & ~(1u << pin)) | ((uint32_t)on << pin);
}
static void pio_sm_set_enabled(PIO p, uint sm, bool on) { fake_enabled = on; }
static uint pio_sm_get_pc(PIO p, uint sm) { return fake_pc; }
static uint pio_encode_jmp(uint pc) { return 0x100 | pc; }
static uint pio_encode_jmp_x_dec(uint pc) { return 0x200 | pc; }
static uint pio_encode_mov(uint to, uint from) { return 0x300; }
static uint pio_encode_mov_not(uint to, uint from) { return 0x400; }
static uint pio_encode_push(bool iffull, bool block) { return 0x500; }
static void pio_sm_exec(PIO p, uint sm, uint instruction) {
    switch (instruction & 0xf00) {
    case 0x100: fake_pc = instruction & 0xff; break;
    case 0x200: --fake_x; fake_pc = instruction & 0xff; break;
    case 0x300: fake_isr = fake_x; break;
    case 0x400: fake_x = UINT32_MAX; break;
    case 0x500: *fake_dma_destination = fake_isr; break;
    default: assert(false);
    }
}
static void pio_sm_clear_fifos(PIO p, uint sm) { fifo_length = 0; }
static void pio_sm_restart(PIO p, uint sm) {}
static void pio_sm_set_pins_with_mask(PIO p, uint sm, uint32_t value, uint32_t mask) {
    fake_gpio = (fake_gpio & ~mask) | (value & mask);
}
static uint pio_sm_get_tx_fifo_level(PIO p, uint sm) { return fifo_length; }
static void pio_sm_put(PIO p, uint sm, uint32_t word) {
    assert(fifo_length < 4);
    fifo[fifo_length++] = word;
}
static void multicore_lockout_victim_init(void) {}
static void multicore_launch_core1(void (*entry)(void));
static void tight_loop_contents(void) {}
static uint pio_claim_unused_sm(PIO p, bool required) { return 0; }
static uint pio_add_program(PIO p, const void *program) { return 0; }
static void pio_gpio_init(PIO p, uint pin) {}
static pio_sm_config drv8847_bridge_program_get_default_config(uint off) { return 0; }
static void sm_config_set_set_pins(pio_sm_config *c, uint pin, uint count) {}
static void sm_config_set_out_pins(pio_sm_config *c, uint pin, uint count) {}
static void sm_config_set_out_shift(pio_sm_config *c, bool right, bool autopull, uint count) {}
static void sm_config_set_clkdiv(pio_sm_config *c, float divider) {}
static uint clock_get_hz(uint clock) { return 125000000; }
static void pio_sm_init(PIO p, uint sm, uint off, const pio_sm_config *c) {}
static void pio_sm_set_consecutive_pindirs(PIO p, uint sm, uint pin, uint count, bool out) {}
static int dma_claim_unused_channel(bool required) { return 0; }
static dma_channel_config dma_channel_get_default_config(int channel) { return 0; }
static void channel_config_set_read_increment(dma_channel_config *c, bool enabled) {}
static void channel_config_set_write_increment(dma_channel_config *c, bool enabled) {}
static void channel_config_set_dreq(dma_channel_config *c, uint dreq) {}
static uint pio_get_dreq(PIO p, uint sm, bool tx) { return 0; }
static void dma_channel_configure(int channel, const dma_channel_config *c, void *dst,
                                  const void *src, uint32_t count, bool start) {
    fake_dma_destination = dst;
}
static void dma_channel_set_irq0_enabled(int channel, bool enabled) {}
static void irq_set_exclusive_handler(uint irq, void (*handler)(void)) {}
static void irq_set_enabled(uint irq, bool enabled) {}
static void dma_channel_set_trans_count(int channel, uint32_t count, bool start) {}
#endif
