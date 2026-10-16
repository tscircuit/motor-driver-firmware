#define MOTOR_HOST_TEST
#define main firmware_entry
#include "../../firmware-c/main.c"
#undef main
#include "nema23_test_platform.h"
#include <assert.h>
static uint8_t pd_reg, pd_status = 0x18, pd_current = 60, pd_written = 255;
static uint8_t spi_regs[64];
static bool spi_broken, pd_absent;
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
    if (address != 0x22 || pd_absent)
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
    spi_broken = pd_absent = false;
    pd_status = 0x18;
    pd_current = 60;
    pd_written = 255;
    fake_motor.mode = STOPPED;
    fake_motor.enabled = false;
    memset(spi_regs, 0, sizeof(spi_regs));
    safe_gpio();
    nema23_init();
    assert(nema23_status().ignore_pd_power_good);
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
    check("{\"cmd\":\"set_pd_voltage\",\"voltage\":20}", true);
    fake_us += NEMA23_DISCHARGE_US - 1;
    nema23_poll(fake_us, true, 25);
    assert(pd_written == 255);
    ++fake_us;
    nema23_poll(fake_us, true, 25);
    assert(pd_written == 4);
    nema23_poll(fake_us, true, 25);
    assert((!fake_pins[8] || nema23_status().ignore_pd_power_good) && !fake_pins[0]);
    fake_us += 2000000;
    fake_pins[1] = 0;
    nema23_poll(fake_us, true, 25);
    assert(nema23_status().phase == POWER_CONTRACT && !fake_pins[0]);
}
static void arm(void) {
    check("{\"cmd\":\"arm_power\"}", true);
    assert(fake_pins[0] && !fake_pins[22]);
    nema23_poll(fake_us, true, 25);
    assert(nema23_ready() && !fake_pins[22] && !fake_pins[6]);
    assert(spi_regs[5] == 6 && spi_regs[4] == 0x8f && spi_regs[0x10] == 0x10);
}
int main(void) {
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
    pd_current = 50;
    check("{\"cmd\":\"arm_power\"}", false);
    assert(!fake_pins[0]);
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
    puts("NEMA23: pins, 12s discharge, verified PD/current, optional PG, explicit arm, SPI mode/readback, "
         "foldback, thermal/power/USB latches, bounds and 0.2 V/A scaling passed");
}
