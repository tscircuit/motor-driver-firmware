#define MOTOR_HOST_TEST
#define main firmware_entry
#include "../../firmware-c/main.c"
#undef main
#include "nema23_test_platform.h"
#include <assert.h>
static uint8_t pd_reg, pd_status = 0x18, pd_current = 60, pd_written = 255;
static uint8_t spi_regs[64];
static bool spi_broken, pd_absent, pd_read_failed;
static int fake_pd_write(uint8_t address, const uint8_t *data, size_t n) {
    if (address != 0x22 || pd_absent)
        return -1;
    pd_reg = data[0];
    if (n == 2) {
        assert(!fake_pins[0] && !fake_pins[6] && !fake_pins[22]);
        pd_written = data[1];
        fake_pins[1] = 1;
    }
    return n;
}
static int fake_pd_read(uint8_t address, uint8_t *data, size_t n) {
    if (address != 0x22 || pd_absent || pd_read_failed)
        return -1;
    *data = pd_reg == 0x09 ? pd_status : pd_reg == 0x50 ? pd_current : 0;
    return n;
}
#include "fake_application.h"
void spi_init(int port, uint32_t hz) { assert(hz == 1000000); }
void spi_set_format(int port, int bits, int cpol, int cpha, int order) {
    assert(bits == 8 && cpol == 0 && cpha == 1);
}
int spi_write_read_blocking(int port, const uint8_t *tx, uint8_t *rx, size_t n) {
    assert(n == 2 && fake_pins[17] == 0 && fake_pins[6] && fake_pins[0]);
    rx[0] = spi_broken ? 0 : 0xc0;
    uint8_t reg = tx[0] & 0x3f;
    rx[1] = spi_regs[reg];
    if (!(tx[0] & 0x40))
        spi_regs[reg] = tx[1];
    if (reg == 0x06 && !(tx[0] & 0x40) && (tx[1] & 0x80)) {
        spi_regs[reg] &= 0x7f;
        spi_regs[0] = 0;
    }
    return 2;
}
void current_capture_init(void) {}
bool current_capture_snapshot(uint16_t raw[2]) {
    raw[0] = 2048;
    raw[1] = 2048;
    return true;
}
void current_capture_read(current_frame_t *f, bool connected) { memset(f, 0, sizeof(*f)); }
void current_capture_drop(unsigned bins) {}
static void check(const char *line, bool expected) {
    output_length = 0;
    command(line);
    char *last = strrchr(output, '\n');
    assert(last);
    *last = 0;
    // Commands below emit ACK only; status is checked independently.
    cJSON *o = cJSON_Parse(output);
    assert(o && cJSON_IsBool(cJSON_GetObjectItem(o, "ok")));
    assert(cJSON_IsTrue(cJSON_GetObjectItem(o, "ok")) == expected);
    cJSON_Delete(o);
}
static void setup(void) {
    memset(fake_pins, 0, sizeof(fake_pins));
    fake_pins[2] = fake_pins[23] = fake_pins[24] = 1;
    fake_us = 3000000;
    fake_connected = true;
    spi_broken = pd_absent = pd_read_failed = false;
    pd_status = 0x18;
    pd_current = 60;
    pd_written = 255;
    fake_motor.mode = STOPPED;
    fake_motor.enabled = false;
    memset(spi_regs, 0, sizeof(spi_regs));
    safe_gpio();
    nema23_init();
    assert(nema23_status().ignore_pd_power_good);
    assert(!nema23_status().stop_on_fault);
    nema23_set_fault_stop(true);
    nema23_set_pg_override(false); /* Exercise optional strict mode below. */
    sensor_initialized = true;
    fake_registers[1] = SENSOR_CONFIG;
    fake_registers[2] = 55 * 256;
    fake_registers[3] = 70 * 256;
    fake_registers[0] = 25 * 256;
    sample();
    assert(!fake_pins[0] && !fake_pins[6] && !fake_pins[8] && !fake_pins[22] && fake_pins[17]);
    assert(STEP_PIN == 20 && DIR_PIN == 21 && BUZZER_PIN == 3 && CURRENT_VOLTS_PER_AMP == .2);
}
static void request20(void) {
    pd_written = 255;
    check("{\"cmd\":\"set_pd_voltage\",\"voltage\":20}", true);
    fake_pins[1] = 0;
    nema23_poll(fake_us, true, 25);
    assert(pd_written == 4);
    fake_pins[1] = 0;
    assert(nema23_status().phase == POWER_CONTRACT && !fake_pins[0]);
}
static void arm(void) {
    check("{\"cmd\":\"arm_power\"}", true);
    assert(fake_pins[0] && !fake_pins[22]);
    nema23_poll(fake_us, true, 25);
    assert(nema23_ready() && !fake_pins[22] && !fake_pins[6]);
    assert(spi_regs[5] == 6 && spi_regs[4] == 0x8f && spi_regs[0x10] == 0x10);
}
static void test_current_overrides(void) {
    setup();
    assert(!nema23_status().ignore_overcurrent_shutdown && !nema23_status().ignore_winding_current_limit);
    check("{\"cmd\":\"set_overcurrent_shutdown_override\",\"enabled\":1}", false);
    check("{\"cmd\":\"set_winding_current_limit_override\"}", false);
    fake_motor.mode = CONTINUOUS;
    fake_motor.enabled = true;
    check("{\"cmd\":\"set_overcurrent_shutdown_override\",\"enabled\":true}", false);
    check("{\"cmd\":\"set_winding_current_limit_override\",\"enabled\":true}", false);
    fake_motor.mode = STOPPED;
    fake_motor.enabled = false;
    check("{\"cmd\":\"set_winding_current_limit_override\",\"enabled\":true}", true);
    assert(!nema23_status().ignore_overcurrent_shutdown && nema23_status().ignore_winding_current_limit);
    assert(!fake_pins[0] && !fake_pins[22]);
    request20();
    arm();
    assert(spi_regs[0x06] == 0x38 && spi_regs[0x0e] == 255);
    assert(nema23_status().current_peak_a == 1 && nema23_effective_current() == NEMA23_FULL_SCALE_A);
    assert(!nema23_prepare_motion());
    nema23_poll(fake_us, true, 60);
    assert(nema23_status().folded_back && spi_regs[0x0e] == nema23_current_code(NEMA23_FULL_SCALE_A / 2));
    nema23_poll(fake_us, true, 70);
    assert(!nema23_ready() && !fake_pins[0]);
    check("{\"cmd\":\"set_overcurrent_shutdown_override\",\"enabled\":true}", true);
    assert(nema23_status().ignore_winding_current_limit && nema23_status().ignore_overcurrent_shutdown);
    check("{\"cmd\":\"set_winding_current_limit_override\",\"enabled\":false}", true);
    assert(!nema23_status().ignore_winding_current_limit && nema23_status().ignore_overcurrent_shutdown);
    request20();
    arm();
    assert(spi_regs[0x06] == 0x3c && spi_regs[0x0e] == nema23_current_code(1));
    assert(!nema23_prepare_motion());
    spi_regs[0] = 0x88;
    fake_pins[23] = 0;
    assert(!driver_fault());
    nema23_poll(fake_us, true, 25);
    assert(nema23_ready());
    output_length = 0;
    status(false);
    cJSON *o = cJSON_Parse(output);
    assert(o && cJSON_IsTrue(cJSON_GetObjectItem(o, "driver_fault_asserted")));
    assert(cJSON_IsTrue(cJSON_GetObjectItem(o, "driver_overcurrent_reported")));
    assert(cJSON_IsTrue(cJSON_GetObjectItem(o, "overcurrent_shutdown_override")));
    assert(cJSON_IsFalse(cJSON_GetObjectItem(o, "winding_current_limit_override")));
    cJSON_Delete(o);
    spi_regs[0] = 0x8a; /* OCP plus thermal fault must still shut down. */
    assert(driver_fault());
    nema23_poll(fake_us, true, 25);
    assert(!nema23_ready() && !fake_pins[0]);
    assert(nema23_status().fault_snapshot_valid && nema23_status().fault_driver_register_valid);
    assert(nema23_status().fault_driver_register == 0x8a);
    assert(nema23_status().fault_gpio & (1u << 0));
    assert(!(nema23_status().fault_gpio & (1u << 23)));
    assert(nema23_status().fault_gpio & (1u << 24));
    for (int broken = 0; broken < 2; ++broken) {
        setup();
        nema23_set_overcurrent_override(true);
        request20();
        arm();
        assert(!nema23_prepare_motion());
        fake_pins[23] = 0;
        spi_regs[0] = broken ? 0x88 : 0; /* Unknown nFAULT or unreadable SPI. */
        spi_broken = broken;
        assert(driver_fault());
        nema23_poll(fake_us, true, 25);
        assert(!nema23_ready());
    }
    setup();
    nema23_set_overcurrent_override(true);
    nema23_set_overcurrent_override(false);
    request20();
    arm();
    assert(spi_regs[0x06] == 0x38 && spi_regs[0x0e] == nema23_current_code(1));
    setup();
    assert(!nema23_status().ignore_overcurrent_shutdown && !nema23_status().ignore_winding_current_limit);
}
static void start_observer(void) {
    setup();
    nema23_set_fault_stop(false);
    request20();
    arm();
    assert(spi_regs[6] == 0x3c && spi_regs[7] == 0x45);
    assert(spi_regs[8] == 0 && spi_regs[9] == 0x20);
    assert(!nema23_prepare_motion());
    fake_pins[22] = 1;
    nema23_observe_motion(true, 128);
    fake_us += 25000;
    nema23_poll(fake_us, true, 25);
    assert(spi_regs[7] == 0x55 && !nema23_status().stall_valid);
    nema23_observe_motion(true, 256);
}
static void test_reporting_mode(void) {
    setup();
    nema23_set_fault_stop(false);
    check("{\"cmd\":\"set_stop_on_detected_fault\",\"enabled\":1}", false);
    check("{\"cmd\":\"set_stall_threshold\",\"threshold\":0}", false);
    check("{\"cmd\":\"set_stall_threshold\",\"threshold\":3.5}", false);
    check("{\"cmd\":\"set_stall_threshold\",\"threshold\":50}", true);
    assert(nema23_status().stall_threshold == 50);
    start_observer();
    spi_regs[0x0a] = 100;
    fake_us += 25000;
    nema23_poll(fake_us, true, 25);
    assert(nema23_status().stall_valid && !nema23_status().stall_suspected);
    spi_regs[0x0a] = 0;
    for (int i = 0; i < 10; ++i) {
        fake_us += 25000;
        nema23_poll(fake_us, true, 25);
    }
    assert(nema23_status().stall_suspected && nema23_ready() && fake_pins[0] && fake_pins[22]);
    spi_regs[0x0a] = 100;
    fake_us += 25000;
    nema23_poll(fake_us, true, 25);
    assert(nema23_status().stall_valid && !nema23_status().stall_suspected);
    fake_pins[24] = 0;
    fake_us += 25000;
    nema23_poll(fake_us, true, 25);
    assert(nema23_ready() && fake_pins[0] && fake_pins[22]);
    assert(nema23_status().warning && !nema23_status().stall_valid && !nema23_status().stall_suspected);
    fake_pins[24] = 1;
    fake_pins[23] = 0;
    spi_regs[0] = 0x88;
    fake_us += 25000;
    nema23_poll(fake_us, true, 25);
    assert(nema23_ready() && !nema23_driver_fault() && nema23_status().warning);
    assert(!nema23_status().stall_valid);
    fake_pins[23] = 1;
    spi_regs[0] = 0;
    nema23_poll(fake_us, true, 70);
    assert(nema23_status().phase == POWER_LATCHED && !fake_pins[0]);
    start_observer();
    /* Exercise stop policy without changing hardware during motion. */
    nema23_set_fault_stop(true);
    request20();arm();assert(!nema23_prepare_motion());fake_pins[22] = 1;
    nema23_observe_motion(true,128);fake_us+=25000;nema23_poll(fake_us,true,25);
    nema23_observe_motion(true,256);spi_regs[0x0a]=0;
    for(int i=0;i<10 && nema23_ready();++i){fake_us+=25000;nema23_poll(fake_us,true,25);}
    assert(nema23_status().phase == POWER_LATCHED && !fake_pins[0] && !fake_pins[22]);
    assert(strstr(nema23_status().fault,"Suspected motor stall"));
    start_observer();
    spi_broken=true;fake_us+=25000;nema23_poll(fake_us,true,25);
    assert(!nema23_status().stall_valid && !nema23_status().stall_suspected);
    nema23_observe_motion(false,0);
    assert(!nema23_status().stall_valid);
    setup();
}
static void test_immediate_rearm(void) {
    setup();request20();arm();nema23_disarm();
    assert(nema23_rearm_remaining_ms()==0);
    assert(!nema23_set_current(2));
    assert(nema23_rearm_remaining_ms()==0);
    nema23_set_fuse_fault_override(true);
    assert(nema23_rearm_remaining_ms()==0);
    arm();
    assert(spi_regs[0x0e] == nema23_current_code(2));
}
static void test_current_change_without_rail_cycle(void) {
    setup();request20();arm();
    assert(!nema23_set_current(2));
    assert(fake_pins[0] && !fake_pins[6] && !fake_pins[22]);
    assert(!nema23_ready() && nema23_rearm_remaining_ms() == 0);
    arm();
    assert(spi_regs[0x0e] == nema23_current_code(2));
    assert(nema23_status().current_peak_a == 2);
    assert(nema23_set_current(5));
    assert(nema23_ready() && nema23_status().current_peak_a == 2);
    /* A current update cannot claim a voltage request was already sent. */
    check("{\"cmd\":\"set_pd_voltage\",\"voltage\":15}", true);
    assert(nema23_status().phase == POWER_REQUESTING);
    assert(!nema23_set_current(1.5));
    assert(nema23_status().phase == POWER_REQUESTING);
    assert(nema23_arm(true,25));
    nema23_poll(fake_us,true,25);
    assert(pd_written == 3 && nema23_status().phase == POWER_CONTRACT);
    fake_pins[1] = 0;
    arm();
    assert(spi_regs[0x0e] == nema23_current_code(1.5));
}
int main(void) {
    test_current_change_without_rail_cycle();
    test_immediate_rearm();
    test_reporting_mode();
    test_current_overrides();
    setup();
    nema23_set_pg_override(true);
    pd_status = pd_current = 0;
    check("{\"cmd\":\"set_pd_voltage\",\"voltage\":15}", true);
    pd_read_failed = true;
    nema23_poll(fake_us, true, 25);
    assert(pd_written == 3 && nema23_status().phase == POWER_CONTRACT);
    assert(!nema23_status().contract_valid && !fake_pins[0]);
    arm();
    fake_us += 5000000;
    nema23_poll(fake_us, true, 25);
    assert(nema23_ready() && !nema23_status().fault);
    setup();
    check("{\"cmd\":\"beep\",\"duration_ms\":2000}", true);
    indicators(fake_us, fake_motor);
    assert(buzzer_frequency == 2731 && test_until == fake_us + 1999000);
    fake_us = test_until - 1;
    indicators(fake_us, fake_motor);
    assert(buzzer_frequency == 2731);
    ++fake_us;
    indicators(fake_us, fake_motor);
    assert(buzzer_frequency == 0);
    check("{\"cmd\":\"beep\",\"duration_ms\":3001}", false);
    check("{\"cmd\":\"beep\",\"duration_ms\":\"bad\"}", false);
    setup();
    assert(!nema23_status().ignore_fuse_power_fault);
    check("{\"cmd\":\"set_fuse_power_fault_override\",\"enabled\":1}", false);
    check("{\"cmd\":\"set_fuse_power_fault_override\"}", false);
    check("{\"cmd\":\"set_fuse_power_fault_override\",\"enabled\":true}", true);
    assert(nema23_status().ignore_fuse_power_fault && !fake_pins[0] && !fake_pins[22]);
    request20();
    fake_pins[2] = 0;
    arm();
    assert(nema23_ready() && !gpio_get(2));
    fake_us += 100001;
    nema23_poll(fake_us, true, 25);
    assert(nema23_ready());
    output_length = 0;
    status(false);
    cJSON *override_status = cJSON_Parse(output);
    assert(override_status);
    assert(cJSON_IsTrue(cJSON_GetObjectItem(override_status, "power_fault_asserted")));
    assert(cJSON_IsTrue(cJSON_GetObjectItem(override_status, "fuse_power_fault_override")));
    cJSON_Delete(override_status);
    fake_motor.mode = CONTINUOUS;
    fake_motor.enabled = true;
    check("{\"cmd\":\"set_fuse_power_fault_override\",\"enabled\":false}", false);
    fake_motor.mode = STOPPED;
    fake_motor.enabled = false;
    fake_pins[24] = 0;
    nema23_poll(fake_us, true, 25);
    assert(!nema23_ready() && !fake_pins[0]);
    assert(strstr(nema23_status().fault, "interlock active"));
    assert(nema23_status().fault_snapshot_valid);
    assert(!(nema23_status().fault_gpio & (1u << 24)));
    assert(nema23_status().fault_gpio & (1u << 23));
    check("{\"cmd\":\"set_fuse_power_fault_override\",\"enabled\":false}", true);
    assert(!nema23_status().ignore_fuse_power_fault && !fake_pins[0]);
    setup();
    nema23_set_fuse_fault_override(true);
    request20();
    fake_pins[2] = fake_pins[24] = 0;
    check("{\"cmd\":\"arm_power\"}", true);
    fake_us += 1000000;
    nema23_poll(fake_us, true, 25);
    assert(nema23_status().phase == POWER_LATCHED && !fake_pins[0]);
    setup();
    nema23_set_fuse_fault_override(true);
    request20();
    fake_pins[2] = 0;
    arm();
    check("{\"cmd\":\"set_fuse_power_fault_override\",\"enabled\":false}", true);
    assert(!fake_pins[0] && !fake_pins[22] && !nema23_ready());
    nema23_set_fuse_fault_override(true);
    request20();
    arm();
    nema23_poll(fake_us, true, 70);
    assert(nema23_status().phase == POWER_LATCHED && !fake_pins[0]);
    setup();
    assert(!nema23_status().ignore_fuse_power_fault);
    setup();
    request20();
    fake_pins[2] = 0;
    fake_pins[24] = 0;
    check("{\"cmd\":\"arm_power\"}", true);
    nema23_poll(fake_us, true, 25);
    assert(nema23_status().phase == POWER_ENERGIZING && fake_pins[0] && !fake_pins[22]);
    fake_pins[2] = fake_pins[24] = 1;
    nema23_poll(fake_us, true, 25);
    assert(nema23_ready());
    fake_pins[2] = 0;
    nema23_poll(fake_us, true, 25);
    assert(nema23_status().phase == POWER_LATCHED && !fake_pins[0]);
    setup();
    request20();
    fake_pins[2] = fake_pins[24] = 0;
    check("{\"cmd\":\"arm_power\"}", true);
    fake_us += 1000000;
    nema23_poll(fake_us, true, 25);
    assert(nema23_status().phase == POWER_LATCHED && !fake_pins[0] && !fake_pins[22]);
    setup();
    check("{\"cmd\":\"arm_power\"}", false);
    check("{\"cmd\":\"set_pd_voltage\",\"voltage\":48}", false);
    request20();
    fake_registers[0] = 55 * 256;
    check("{\"cmd\":\"arm_power\"}", false);
    fake_registers[0] = 25 * 256;
    spi_regs[0] = 0x10; /* Latched power-up UVLO must clear only with explicit arm. */
    arm();
    assert(spi_regs[0] == 0);
    command("{\"cmd\":\"heartbeat\"}");
    check("{\"cmd\":\"jog\",\"direction\":1,\"resolution\":\"quarter\",\"speed_sps\":100,\"ramp_"
          "ms\":400}",
          true);
    assert(fake_pins[22] && fake_motor.enabled);
    nema23_poll(fake_us, true, 60);
    assert(nema23_status().folded_back && spi_regs[0x0e] == nema23_current_code(.5));
    fake_registers[0] = 70 * 256;
    sample();
    protect(fake_us, fake_motor);
    assert(!fake_pins[0] && !fake_pins[6] && !fake_pins[22] && !fake_motor.enabled &&
           nema23_status().phase == POWER_LATCHED);
    command("{\"cmd\":\"heartbeat\"}");
    assert(!fake_motor.enabled);
    check("{\"cmd\":\"arm_power\"}", false);
    setup();
    request20();
    pd_current = 0;
    pd_status = 0;
    arm();
    fake_us += 100001;
    nema23_poll(fake_us, true, 25);
    assert(nema23_ready() && !nema23_status().contract_valid);
    pd_absent = true;
    fake_us += 100001;
    nema23_poll(fake_us, true, 25);
    assert(nema23_ready());
    setup();
    request20();
    spi_broken = true;
    check("{\"cmd\":\"arm_power\"}", true);
    nema23_poll(fake_us, true, 25);
    assert(nema23_status().phase == POWER_LATCHED && !fake_pins[0] && !fake_pins[22]);
    setup();
    request20();
    arm();
    assert(!nema23_prepare_motion());
    spi_regs[5] = 0x0a;
    fake_us += 100001;
    nema23_poll(fake_us, true, 25);
    assert(nema23_status().phase == POWER_LATCHED && !fake_pins[0]);
    setup();
    request20();
    arm();
    fake_pins[1] = 1;
    nema23_poll(fake_us, true, 25);
    assert(!fake_pins[0] && !fake_pins[8] && nema23_status().phase == POWER_LATCHED);
    setup();
    request20();
    arm();
    fake_connected = false;
    protect(fake_us, fake_motor);
    assert(!fake_pins[0] && !fake_pins[22]);
    setup();
    fake_pins[1] = 1;
    nema23_poll(fake_us, true, 25);
    assert(!fake_pins[8]);
    check("{\"cmd\":\"set_current_limit\",\"peak_a\":5}", false);
    check("{\"cmd\":\"set_current_limit\",\"peak_a\":1.5}", true);
    assert(nema23_current_code(1.5) < nema23_current_code(3.965));
    check("{\"cmd\":\"set_threshold\",\"threshold_c\":70}", false);
    cJSON *o = cJSON_CreateObject();
    currents(o);
    double measured =
        cJSON_GetArrayItem(cJSON_GetObjectItem(o, "winding_current_a"), 0)->valuedouble;
    assert(fabs(measured - (2048 * 3.3 / 4095 - 1.65) / .2) < 1e-10);
    cJSON_Delete(o);
    setup();
    fake_pins[1] = 1;
    check("{\"cmd\":\"set_pd_power_good_override\",\"enabled\":1}", false);
    check("{\"cmd\":\"set_pd_power_good_override\",\"enabled\":true}", true);
    request20();
    fake_pins[1] = 1;
    arm();
    assert(nema23_ready() && nema23_status().ignore_pd_power_good);
    fake_us += 100001;
    nema23_poll(fake_us, true, 25);
    assert(nema23_ready());
    fake_pins[24] = 0;
    nema23_poll(fake_us, true, 25);
    assert(!nema23_ready() && !fake_pins[0]);
    setup();
    assert(!nema23_status().ignore_pd_power_good);
    puts("NEMA23: pins, immediate voltage requests/rearm, requested PD with non-blocking status, optional PG, explicit arm, SPI mode/readback, "
         "foldback, thermal/power/USB latches, bounds and 0.2 V/A scaling passed");
}
