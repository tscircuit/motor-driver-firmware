#include "motor.h"
#include "travel_limits.h"
#include "board.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "pico/multicore.h"
#include "pico/mutex.h"
#include "pico/stdlib.h"
#include "stepper.pio.h"
#include <math.h>

static PIO pio = pio0;
static uint step_sm, counter_sm, step_offset, counter_offset;
static int counter_dma;
static volatile uint32_t counter_value = UINT32_MAX;
static mutex_t lock;
static motor_status_t state = {
    .mode = STOPPED, .factor = 1, .direction = 1, .speed = 40, .acceleration = 100};
static profile_t profile;
static travel_limits_t limits;
static uint32_t base_count, generated, last_count, measured_count;
static uint64_t measured_at;

static uint64_t started_us, ready_us;
static bool fixed_profile;
static volatile bool core1_ready;
static uint32_t count(void) { return UINT32_MAX - counter_value; }
static void refresh_locked(void) {
    if (state.mode == STOPPED || state.mode == PREPARED)
        return;
    uint32_t current = count();
    uint32_t delta = current - last_count;
    last_count = current;
    state.executed_pulses += delta;
    if (state.mode != DIAGNOSTIC)
        state.position_microsteps += (int64_t)state.direction * delta;
    uint64_t now = time_us_64();
    if (now - measured_at >= 100000) {
        state.pulse_rate = (current - measured_count) * 1000000.0 / (now - measured_at);
        measured_count = current;
        measured_at = now;
    }
    state.elapsed_us = (uint32_t)(time_us_64() - started_us);
}
static void pause_locked(void) {
    pio_sm_set_enabled(pio, step_sm, false);
    pio_sm_set_pins_with_mask(pio, step_sm, 0, 1u << STEP_PIN);
    sleep_us(3); /* Let the independent counter/DMA observe the last edge. */
    refresh_locked();
    pio_sm_clear_fifos(pio, step_sm);
    pio_sm_restart(pio, step_sm);
    pio_sm_exec(pio, step_sm, pio_encode_jmp(step_offset));
    state.mode = STOPPED;
    state.profile_speed = 0;
    state.pulse_rate = 0;
}
static void halt_locked(void) {
    gpio_put(ARM_PIN,
#ifdef MOTOR_BOARD_DRV8452
             0
#else
             1
#endif
    ); /* Release immediately, before synchronizing PIO. */
    gpio_put(WAKE_PIN, 0);
    pause_locked();
    state.enabled = false;
}
bool motor_set_position(int64_t position) {
    mutex_enter_blocking(&lock);
    bool ok=state.mode==STOPPED;
    if(ok) state.position_microsteps=position;
    mutex_exit(&lock); return ok;
}
bool motor_set_travel_limits(bool enabled, int64_t min, int64_t max) {
    mutex_enter_blocking(&lock);
    bool ok=state.mode==STOPPED && (!enabled || (min<max && state.position_microsteps>=min && state.position_microsteps<=max));
    if(ok) limits=(travel_limits_t){enabled,min,max};
    mutex_exit(&lock); return ok;
}
void motor_stop(void) {
    mutex_enter_blocking(&lock);
    halt_locked();
    mutex_exit(&lock);
}
static void begin_locked(double initial, uint32_t target, bool fixed, bool defer, bool jogging,
                         bool test) {
    state.executed_pulses = 0;
    state.queue_underruns = 0;
    pio->fdebug = 1u << (PIO_FDEBUG_TXSTALL_LSB + step_sm);
    state.target_pulses = target;
    state.elapsed_us = 0;
    state.pulse_rate = 0;
    state.profile_speed = 0;
    generated = 0;
    base_count = count();
    last_count = measured_count = base_count;
    measured_at = time_us_64();
    int multiplier = 16 / state.factor;
    profile_init(&profile, state.speed * multiplier, state.acceleration * multiplier, initial,
                 target, fixed);
    fixed_profile = fixed;
    state.mode = test ? DIAGNOSTIC : defer ? PREPARED : target ? FINITE : CONTINUOUS;
    started_us = time_us_64();
    ready_us = started_us +
               (test            ? 0
                : state.enabled ? 3
                                : 3000) +
               (!test && !fixed && !jogging ? 100000 : 0);
    gpio_put(DIR_PIN, state.direction > 0);
    if (!test) {
#ifndef MOTOR_BOARD_DRV8452
        gpio_put(20, 1);
#endif
        gpio_put(WAKE_PIN, 1);
        gpio_put(ARM_PIN,
#ifdef MOTOR_BOARD_DRV8452
                 1
#else
                 0
#endif
        );
        state.enabled = true;
    }
    /* Enabled by core 1 after the driver's wake/setup interval. */
}
bool motor_start(int factor, int direction, double speed, double acceleration, uint32_t steps,
                 bool fixed, bool defer, bool jogging) {
    if (!profile_validate(speed, acceleration, factor, steps))
        return false;
    mutex_enter_blocking(&lock);
    if (state.mode != STOPPED) {
        mutex_exit(&lock);
        return false;
    }
    uint32_t wanted=steps ? steps*(16/factor) : 1;
    if(travel_budget(limits,state.position_microsteps,direction,0,wanted)<wanted) {
        mutex_exit(&lock); return false;
    }
    state.factor = factor;
    state.direction = direction;
    state.speed = speed;
    state.acceleration = acceleration;
    begin_locked(fixed ? 0 : fmin(10, speed) * (16 / factor), steps * (16 / factor), fixed, defer,
                 jogging, false);
    mutex_exit(&lock);
    return true;
}
bool motor_retarget(int factor, int direction, double speed, double acceleration, bool fixed) {
    if (!profile_validate(speed, acceleration, factor, 0))
        return false;
    mutex_enter_blocking(&lock);
    if (state.mode != CONTINUOUS && state.mode != BRAKING) {
        mutex_exit(&lock);
        return false;
    }
    refresh_locked();
    double initial = profile_speed(&profile, (uint32_t)state.executed_pulses);
    bool same = factor == state.factor && direction == state.direction;
    pause_locked();
    if(!travel_budget(limits,state.position_microsteps,direction,0,1)) {
        halt_locked(); mutex_exit(&lock); return false;
    }
    state.factor = factor;
    state.direction = direction;
    state.speed = speed;
    state.acceleration = acceleration;
    begin_locked(same    ? initial
                 : fixed ? 0
                         : fmin(10, speed) * (16 / factor),
                 0, fixed, false, true, false);
    mutex_exit(&lock);
    return true;
}
void motor_decelerate(void) {
    mutex_enter_blocking(&lock);
    if (state.mode == CONTINUOUS) {
        refresh_locked();
        double initial = profile_speed(&profile, (uint32_t)state.executed_pulses);
        double a = state.acceleration * (16 / state.factor);
        if (initial <= 0) {
            halt_locked();
            mutex_exit(&lock);
            return;
        }
        pause_locked();
        uint32_t tail = (uint32_t)fmax(1, ceil(initial * initial / (2 * a)));
        begin_locked(initial, tail, fixed_profile, false, true, false);
        profile = (profile_t){.initial = initial,
                              .target = 0,
                              .count = tail,
                              .acceleration = initial * initial / (2 * tail),
                              .braking = true};
        state.mode = BRAKING;
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
bool motor_test(uint32_t pulses, double hz) {
    if (!pulses || pulses > MAX_MOVE_STEPS || !isfinite(hz) || hz < 1 || hz > MAX_PULSE_HZ)
        return false;
    mutex_enter_blocking(&lock);
    if (state.mode != STOPPED || limits.enabled) {
        mutex_exit(&lock);
        return false;
    }
    state.factor = 16;
    state.speed = hz;
    state.acceleration = 1;
    begin_locked(hz, pulses, false, false, true, true);
    profile.peak = hz;
    profile.ramp_distance = 0;
    profile.duration = pulses / hz;
    mutex_exit(&lock);
    return true;
}
motor_status_t motor_status(void) {
    mutex_enter_blocking(&lock);
    refresh_locked();
    motor_status_t result = state;
    mutex_exit(&lock);
    return result;
}
static void core1(void) {
    multicore_lockout_victim_init();
    core1_ready = true;
    while (true) {
        mutex_enter_blocking(&lock);
        refresh_locked();
        if (state.mode != STOPPED && state.mode != PREPARED) {
            if (state.target_pulses && state.executed_pulses >= state.target_pulses) {
                halt_locked();
            } else if (time_us_64() >= ready_us) {
                if (pio->fdebug & (1u << (PIO_FDEBUG_TXSTALL_LSB + step_sm))) {
                    if (!state.target_pulses || generated < state.target_pulses)
                        ++state.queue_underruns;
                    pio->fdebug = 1u << (PIO_FDEBUG_TXSTALL_LSB + step_sm);
                }
                if (pio_sm_get_tx_fifo_level(pio, step_sm) <= 2 &&
                    (!state.target_pulses || generated < state.target_pulses)) {
                    uint32_t n, delay;
                    if (profile_segment(&profile, generated, &n, &delay)) {
                        if (state.target_pulses && n > state.target_pulses - generated)
                            n = state.target_pulses - generated;
                        n=travel_budget(limits,state.position_microsteps,state.direction,
                                        generated-state.executed_pulses,n);
                        if(!n) {
                            if(generated==state.executed_pulses) halt_locked();
                            mutex_exit(&lock); sleep_us(10); continue;
                        }
                        pio_sm_put(pio, step_sm, delay);
                        pio_sm_put(pio, step_sm, n - 1);
                        generated += n;
                        state.profile_speed =
                            ((double)PULSE_CLOCK_HZ / (delay + 34)) / (16 / state.factor);
                        if (!limits.enabled && !state.target_pulses && generated > 1000000000u) {
                            profile.initial = profile.target;
                            generated = 0;
                        }
                    }
                }
                pio_sm_set_enabled(pio, step_sm, true);
            }
        }
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
    step_sm = pio_claim_unused_sm(pio, true);
    counter_sm = pio_claim_unused_sm(pio, true);
    step_offset = pio_add_program(pio, &stepper_program);
    counter_offset = pio_add_program(pio, &pulse_counter_program);
    pio_gpio_init(pio, STEP_PIN);
    pio_sm_config c = stepper_program_get_default_config(step_offset);
    sm_config_set_set_pins(&c, STEP_PIN, 1);
    sm_config_set_clkdiv(&c, (float)clock_get_hz(clk_sys) / PULSE_CLOCK_HZ);
    pio_sm_init(pio, step_sm, step_offset, &c);
    pio_sm_set_consecutive_pindirs(pio, step_sm, STEP_PIN, 1, true);
    pio_sm_set_pins_with_mask(pio, step_sm, 0, 1u << STEP_PIN);
    c = pulse_counter_program_get_default_config(counter_offset);
    sm_config_set_in_pins(&c, STEP_PIN);
    sm_config_set_clkdiv(&c, (float)clock_get_hz(clk_sys) / PULSE_CLOCK_HZ);
    pio_sm_init(pio, counter_sm, counter_offset, &c);
    counter_dma = dma_claim_unused_channel(true);
    dma_channel_config dc = dma_channel_get_default_config(counter_dma);
    channel_config_set_read_increment(&dc, false);
    channel_config_set_write_increment(&dc, false);
    channel_config_set_dreq(&dc, pio_get_dreq(pio, counter_sm, false));
    dma_channel_configure(counter_dma, &dc, (void *)&counter_value, &pio->rxf[counter_sm],
                          UINT32_MAX, true);
    dma_channel_set_irq0_enabled(counter_dma, true);
    irq_set_exclusive_handler(DMA_IRQ_0, counter_dma_irq);
    irq_set_enabled(DMA_IRQ_0, true);
    pio_sm_set_enabled(pio, counter_sm, true);
    multicore_launch_core1(core1);
    while (!core1_ready)
        tight_loop_contents();
}
