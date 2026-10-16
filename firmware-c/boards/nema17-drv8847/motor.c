#include "motor.h"
#include "board.h"
#include "sequence.h"
#ifdef MOTOR_HOST_TEST
#include "drv8847_test_platform.h"
#else
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "pico/multicore.h"
#include "pico/mutex.h"
#include "pico/stdlib.h"
#include "bridge.pio.h"
#endif
#include <math.h>

static PIO pio = pio0;
static uint sm, offset;
static int counter_dma;
static volatile uint32_t counter_value = UINT32_MAX;
static mutex_t lock;
static motor_status_t state = {
    .mode = STOPPED, .factor = 1, .direction = 1, .speed = 40, .acceleration = 100};
static profile_t profile;
static uint32_t last_count, measured_count, generated;
static uint64_t started_us, ready_us, wake_us, measured_at;
static unsigned phase, initial_phase;
static bool fixed_profile, phase_energized;
static volatile bool core1_ready;

static uint32_t count(void) { return UINT32_MAX - counter_value; }
static void refresh_locked(void) {
    if (state.mode == STOPPED || state.mode == PREPARED)
        return;
    uint32_t current = count(), delta = current - last_count;
    last_count = current;
    state.executed_pulses += delta; /* Selected coil transitions, not STEP pulses. */
    state.position_microsteps += (int64_t)state.direction * delta * (16 / state.factor);
    phase = drv8847_phase(initial_phase, state.direction, state.factor,
                         (uint32_t)state.executed_pulses);
    uint64_t now = time_us_64();
    if (now - measured_at >= 100000) {
        state.pulse_rate = (current - measured_count) * 1000000.0 / (now - measured_at);
        measured_count = current;
        measured_at = now;
    }
    state.elapsed_us = (uint32_t)(now - started_us);
}
static void pause_locked(void) {
    pio_sm_set_enabled(pio, sm, false);
    /* A phase may have been output just before disabling, before its count
       instruction. Finish only the bounded count/push tail, never another out. */
    uint pc = pio_sm_get_pc(pio, sm);
    if (pc >= offset + 7 && pc <= offset + 9) {
        if (pc == offset + 7)
            pio_sm_exec(pio, sm, pio_encode_jmp_x_dec(offset + 8));
        if (pc <= offset + 8)
            pio_sm_exec(pio, sm, pio_encode_mov(pio_isr, pio_x));
        pio_sm_exec(pio, sm, pio_encode_push(false, false));
    }
    sleep_us(3); /* Allow DMA to consume the last completed phase count. */
    refresh_locked();
    pio_sm_clear_fifos(pio, sm);
    pio_sm_restart(pio, sm);
    pio_sm_exec(pio, sm, pio_encode_jmp(offset + drv8847_bridge_wrap_target));
    state.mode = STOPPED;
    state.profile_speed = state.pulse_rate = 0;
}
static void halt_locked(void) {
    gpio_put(ENABLE_PIN, 0);
    pause_locked();
    pio_sm_set_pins_with_mask(pio, sm, 0, BRIDGE_MASK);
    state.enabled = false;
}
void motor_stop(void) {
    mutex_enter_blocking(&lock);
    halt_locked();
    mutex_exit(&lock);
}
static void begin_locked(double initial, uint32_t steps, bool fixed, bool defer, bool jogging) {
    bool was_enabled = state.enabled;
    state.executed_pulses = state.target_pulses = state.queue_underruns = 0;
    state.target_pulses = steps;
    state.elapsed_us = 0;
    state.profile_speed = state.pulse_rate = 0;
    generated = 0;
    initial_phase = phase;
    last_count = measured_count = count();
    measured_at = started_us = time_us_64();
    profile_init(&profile, state.speed, state.acceleration, initial, steps, fixed);
    fixed_profile = fixed;
    state.mode = defer ? PREPARED : steps ? FINITE : CONTINUOUS;
    ready_us = started_us + (was_enabled ? 0 : 3000) +
               (!fixed && !jogging ? 100000 : 0);
    wake_us = started_us + (was_enabled ? 0 : 3000);
    phase_energized = false;
    pio->fdebug = 1u << (PIO_FDEBUG_TXSTALL_LSB + sm);
    if (!was_enabled) {
        pio_sm_set_pins_with_mask(pio, sm, 0, BRIDGE_MASK);
        gpio_put(ENABLE_PIN, 1);
        state.enabled = true;
        /* Energize retained phase only after the DRV8847 wake interval. */
    }
}
bool motor_start(int factor, int direction, double speed, double acceleration, uint32_t steps,
                 bool fixed, bool defer, bool jogging) {
    if (!profile_validate(speed, acceleration, factor, steps) ||
        (direction != 1 && direction != -1))
        return false;
    mutex_enter_blocking(&lock);
    if (state.mode != STOPPED) {
        mutex_exit(&lock);
        return false;
    }
    state.factor = factor;
    state.direction = direction;
    state.speed = speed;
    state.acceleration = acceleration;
    begin_locked(fixed ? 0 : fmin(10, speed), steps, fixed, defer, jogging);
    mutex_exit(&lock);
    return true;
}
bool motor_retarget(int factor, int direction, double speed, double acceleration, bool fixed) {
    if (!profile_validate(speed, acceleration, factor, 0) ||
        (direction != 1 && direction != -1))
        return false;
    mutex_enter_blocking(&lock);
    if (state.mode != CONTINUOUS && state.mode != BRAKING) {
        mutex_exit(&lock);
        return false;
    }
    pause_locked();
    double initial = profile_speed(&profile, (uint32_t)state.executed_pulses);
    bool same = factor == state.factor && direction == state.direction;
    state.factor = factor;
    state.direction = direction;
    state.speed = speed;
    state.acceleration = acceleration;
    begin_locked(same ? initial : fixed ? 0 : fmin(10, speed), 0, fixed, false, true);
    mutex_exit(&lock);
    return true;
}
void motor_decelerate(void) {
    mutex_enter_blocking(&lock);
    if (state.mode == CONTINUOUS) {
        pause_locked();
        double initial = profile_speed(&profile, (uint32_t)state.executed_pulses);
        if (initial <= 0) {
            halt_locked();
        } else {
            uint32_t tail = (uint32_t)fmax(1, ceil(initial * initial / (2 * state.acceleration)));
            begin_locked(initial, tail, fixed_profile, false, true);
            profile = (profile_t){.initial = initial, .count = tail,
                                  .acceleration = initial * initial / (2 * tail), .braking = true};
            state.mode = BRAKING;
        }
    }
    mutex_exit(&lock);
}
bool motor_run_prepared(void) {
    mutex_enter_blocking(&lock);
    bool ok = state.mode == PREPARED;
    if (ok) {
        state.mode = FINITE;
        started_us = time_us_64();
        if (ready_us < started_us)
            ready_us = started_us;
    }
    mutex_exit(&lock);
    return ok;
}
bool motor_test(uint32_t pulses, double hz) { return false; }
motor_status_t motor_status(void) {
    mutex_enter_blocking(&lock);
    refresh_locked();
    motor_status_t result = state;
    mutex_exit(&lock);
    return result;
}
static void service_locked(void) {
    refresh_locked();
    if (state.mode == STOPPED)
        return;
    if (!phase_energized && time_us_64() >= wake_us) {
        pio_sm_set_pins_with_mask(pio, sm, (uint32_t)drv8847_mask(phase) << BRIDGE_BASE_PIN,
                                  BRIDGE_MASK);
        phase_energized = true;
    }
    if (state.mode == PREPARED)
        return;
    if (state.target_pulses && state.executed_pulses >= state.target_pulses) {
        halt_locked();
        return;
    }
    if (time_us_64() < ready_us)
        return;
    if (generated && (pio->fdebug & (1u << (PIO_FDEBUG_TXSTALL_LSB + sm)))) {
        if (!state.target_pulses || generated < state.target_pulses)
            ++state.queue_underruns;
    }
    pio->fdebug = 1u << (PIO_FDEBUG_TXSTALL_LSB + sm);
    if (pio_sm_get_tx_fifo_level(pio, sm) <= 2 &&
        (!state.target_pulses || generated < state.target_pulses)) {
        /* Each phase has its own deadline. No Python loop or grouped fake
           microsteps: the eight-state bridge sequence is the hardware output. */
        double seconds = profile_time(&profile, generated + 1) - profile_time(&profile, generated);
        double ticks = ceil(seconds * PULSE_CLOCK_HZ);
        ticks = fmax(18, fmin(UINT32_MAX, ticks));
        uint32_t period = (uint32_t)ticks;
        unsigned next = drv8847_phase(initial_phase, state.direction, state.factor, generated + 1);
        pio_sm_put(pio, sm, period - 18);
        pio_sm_put(pio, sm, drv8847_mask(next));
        ++generated;
        state.profile_speed = PULSE_CLOCK_HZ / (double)period;
        if (!state.target_pulses && generated >= 1000000000u) {
            /* The cruising phase repeats after a multiple of eight steps. */
            initial_phase = drv8847_phase(initial_phase, state.direction, state.factor, generated);
            profile.initial = profile.target;
            generated = 0;
        }
    }
    pio_sm_set_enabled(pio, sm, true);
}
static void core1(void) {
    multicore_lockout_victim_init();
    core1_ready = true;
    while (true) {
        mutex_enter_blocking(&lock);
        service_locked();
        mutex_exit(&lock);
        sleep_us(10);
    }
}
static void counter_dma_irq(void) {
    if (dma_hw->ints0 & (1u << counter_dma)) {
        dma_hw->ints0 = 1u << counter_dma;
        dma_channel_set_trans_count(counter_dma, UINT32_MAX, true);
    }
}
void motor_init(void) {
    mutex_init(&lock);
    gpio_put(ENABLE_PIN, 0);
    sm = pio_claim_unused_sm(pio, true);
    offset = pio_add_program(pio, &drv8847_bridge_program);
    for (uint pin = BRIDGE_BASE_PIN; pin < BRIDGE_BASE_PIN + 4; ++pin)
        pio_gpio_init(pio, pin);
    pio_sm_config c = drv8847_bridge_program_get_default_config(offset);
    sm_config_set_set_pins(&c, BRIDGE_BASE_PIN, 4);
    sm_config_set_out_pins(&c, BRIDGE_BASE_PIN, 4);
    sm_config_set_out_shift(&c, true, false, 32);
    sm_config_set_clkdiv(&c, (float)clock_get_hz(clk_sys) / PULSE_CLOCK_HZ);
    pio_sm_init(pio, sm, offset, &c);
    pio_sm_set_consecutive_pindirs(pio, sm, BRIDGE_BASE_PIN, 4, true);
    pio_sm_set_pins_with_mask(pio, sm, 0, BRIDGE_MASK);
    /* Execute initial counter setup before the first motion. */
    pio_sm_exec(pio, sm, pio_encode_mov_not(pio_x, pio_null));
    pio_sm_exec(pio, sm, pio_encode_jmp(offset + drv8847_bridge_wrap_target));
    counter_dma = dma_claim_unused_channel(true);
    dma_channel_config dc = dma_channel_get_default_config(counter_dma);
    channel_config_set_read_increment(&dc, false);
    channel_config_set_write_increment(&dc, false);
    channel_config_set_dreq(&dc, pio_get_dreq(pio, sm, false));
    dma_channel_configure(counter_dma, &dc, (void *)&counter_value, &pio->rxf[sm], UINT32_MAX, true);
    dma_channel_set_irq0_enabled(counter_dma, true);
    irq_set_exclusive_handler(DMA_IRQ_0, counter_dma_irq);
    irq_set_enabled(DMA_IRQ_0, true);
    multicore_launch_core1(core1);
    while (!core1_ready)
        tight_loop_contents();
}
