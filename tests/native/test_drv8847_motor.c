#define MOTOR_HOST_TEST
#define MOTOR_BOARD_DRV8847 1
#include "../../firmware-c/boards/nema17-drv8847/motor.c"
#include <stdio.h>
static void multicore_launch_core1(void (*entry)(void)) { core1_ready = true; }
static void service(void) {
    fake_pio.fdebug = 0; /* Hardware clears written-one TXSTALL bits. */
    mutex_enter_blocking(&lock);
    service_locked();
    mutex_exit(&lock);
}
/* Simulate one queued PIO phase completing, independently of CPU service. */
static uint8_t emit_phase(void) {
    assert(fake_enabled && fifo_length >= 2);
    uint32_t period = fifo[0] + 18;
    uint8_t mask = fifo[1];
    memmove(fifo, fifo + 2, (fifo_length - 2) * sizeof(*fifo));
    fifo_length -= 2;
    fake_us += period / 10;
    fake_gpio = (fake_gpio & ~BRIDGE_MASK) | ((uint32_t)mask << BRIDGE_BASE_PIN);
    *fake_dma_destination = --fake_x;
    fake_pc = 1;
    return mask;
}
static void finish(unsigned transitions) {
    for (unsigned i = 0; i < transitions; ++i) {
        service();
        emit_phase();
    }
    service();
    assert(state.mode == STOPPED && !state.enabled && !fake_enabled);
    assert(!(fake_gpio & (BRIDGE_MASK | (1u << ENABLE_PIN))));
}
int main(void) {
    motor_init();
    assert(!state.enabled && !(fake_gpio & BRIDGE_MASK));
    /* Compare directly with the historical Python bridge states. */
    const uint8_t half_forward[] = {5, 4, 6, 2, 10, 8, 9, 1};
    const uint8_t full_forward[] = {4, 2, 8, 1};
    for (int factor = 1; factor <= 2; ++factor) {
        unsigned n = factor == 1 ? 4 : 8;
        assert(motor_start(factor, 1, 40, 100, n, true, false, false));
        service();
        assert(fifo_length == 0 && !fake_enabled); /* 3 ms wake interval. */
        fake_us += 3000;
        for (unsigned i = 0; i < n; ++i) {
            service();
            assert(emit_phase() == (factor == 1 ? full_forward[i] : half_forward[i]));
        }
        service();
        assert(state.mode == STOPPED && state.executed_pulses == n && phase == 0);
        assert(state.position_microsteps == 64);
        assert(motor_start(factor, -1, 40, 100, n, true, false, false));
        fake_us += 3000;
        for (unsigned i = 0; i < n; ++i) {
            service();
            uint8_t expected = drv8847_mask((8 - (i + 1) * (2 / factor)) & 7);
            assert(emit_phase() == expected);
        }
        service();
        assert(state.mode == STOPPED && state.position_microsteps == 0);
    }
    assert(!motor_start(4, 1, 40, 100, 1, true, false, false));
    assert(!motor_start(2, 1, 20001, 100, 1, true, false, false));
    assert(!motor_start(2, 0, 40, 100, 1, true, false, false));
    assert(!motor_test(100, 1000));
    assert(motor_start(2, 1, 40, 100, 3, true, true, false));
    fake_us += 1000000;
    service();
    assert(state.mode == PREPARED && !fake_enabled && fifo_length == 0);
    assert((fake_gpio & BRIDGE_MASK) == ((uint32_t)drv8847_mask(phase) << BRIDGE_BASE_PIN));
    assert(!state.executed_pulses && motor_run_prepared());
    finish(3);
    int64_t position = state.position_microsteps;
    /* Queue future phases, cancel them, and reverse from the emitted phase. */
    assert(motor_start(2, 1, 150, 375, 0, true, false, true));
    fake_us += 3000;
    service();
    uint8_t emitted = emit_phase();
    service();
    service();
    assert(fifo_length == 4);
    assert(motor_retarget(2, -1, 150, 375, true));
    assert(state.position_microsteps == position + 8 && fifo_length == 0);
    assert(drv8847_mask(phase) == emitted);
    unsigned retained = phase;
    service();
    assert(emit_phase() == drv8847_mask((retained + 7) & 7));
    motor_stop();
    assert(state.position_microsteps == position && !state.enabled && !fake_enabled);
    assert(!(fake_gpio & (BRIDGE_MASK | (1u << ENABLE_PIN))));
    assert(motor_start(1, 1, 1000, 2500, 0, true, false, true));
    fake_us += 3000;
    for (unsigned i = 0; i < 10; ++i) { service(); emit_phase(); }
    motor_decelerate();
    assert(state.mode == BRAKING && state.target_pulses > 0);
    finish(state.target_pulses);
    /* Stop at every instruction boundary after OUT but before the RX count.
       The physical phase must be counted once, with no extra output. */
    for (unsigned pc = 7; pc <= 9; ++pc) {
        assert(motor_start(2, 1, 40, 100, 0, true, false, true));
        fake_us += 3000;
        service();
        position = state.position_microsteps;
        /* OUT has happened; count/push may still be pending. */
        fake_pc = pc;
        if (pc >= 8) --fake_x;
        if (pc == 9) fake_isr = fake_x;
        motor_stop();
        assert(state.executed_pulses == 1 && state.position_microsteps == position + 8);
        assert(!state.enabled && fifo_length == 0);
    }
    puts("DRV8847 motor: historical full/half phases, reverse, wake delay, prepared moves, exact counts, retarget cancellation, braking and Stop/count boundaries passed");
}
