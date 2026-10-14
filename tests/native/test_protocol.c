#define MOTOR_HOST_TEST
#define main firmware_entry
#include "../../firmware-c/main.c"
#undef main
#include <assert.h>

uint64_t fake_us = 3000000;
bool fake_connected = true;
volatile bool usb_boot_requested;
int fake_pins[32] = {[23] = 1};
uint16_t fake_registers[4] = {25 * 256, 0x60c0, 60 * 256, 75 * 256};
char output[32768];
size_t output_length;
static uint8_t selected_register;
static motor_status_t fake_motor = {.mode = STOPPED, .factor = 1};
settings_t settings = {.name = "Native test", .threshold = 65};
uint64_t time_us_64(void) { return fake_us; }
void sleep_us(uint32_t us) { fake_us += us; }
void sleep_ms(uint32_t ms) { fake_us += ms * 1000; }
int gpio_get(uint pin) { return fake_pins[pin]; }
void gpio_put(uint pin, bool value) { fake_pins[pin] = value; }
void gpio_init(uint pin) {}
void gpio_set_dir(uint pin, bool value) {}
void gpio_set_function(uint pin, uint function) {}
void gpio_pull_up(uint pin) {}
int i2c_write_timeout_us(int bus, uint8_t address, const uint8_t *data, size_t n, bool nostop,
                         uint32_t timeout) {
    selected_register = data[0];
    if (n == 3)
        fake_registers[data[0]] = (data[1] << 8) | data[2];
    return n;
}
int i2c_read_timeout_us(int bus, uint8_t address, uint8_t *data, size_t n, bool nostop,
                        uint32_t timeout) {
    data[0] = fake_registers[selected_register] >> 8;
    data[1] = fake_registers[selected_register];
    return n;
}
void i2c_init(int bus, uint32_t rate) {}
void adc_init(void) {}
void adc_gpio_init(uint pin) {}
void adc_select_input(uint channel) {}
uint16_t adc_read(void) { return 2048; }
uint pwm_gpio_to_slice_num(uint pin) { return 0; }
void pwm_set_enabled(uint slice, bool enabled) {}
void pwm_set_clkdiv(uint slice, float divisor) {}
void pwm_set_wrap(uint slice, uint16_t wrap) {}
void pwm_set_gpio_level(uint pin, uint16_t level) {}
void pico_get_unique_board_id_string(char *s, size_t n) { snprintf(s, n, "test-board"); }
uint32_t get_rand_32(void) { return 1; }
void tusb_init(void) {}
void reset_block(uint32_t mask) {}
void tud_task(void) {}
bool tud_cdc_connected(void) { return fake_connected; }
uint32_t tud_cdc_write_available(void) { return 8192; }
uint32_t tud_cdc_write(const void *data, uint32_t n) {
    assert(output_length + n < sizeof(output));
    memcpy(output + output_length, data, n);
    output_length += n;
    output[output_length] = 0;
    return n;
}
uint32_t tud_cdc_write_char(char c) { return tud_cdc_write(&c, 1); }
void tud_cdc_write_flush(void) {}
uint32_t tud_cdc_available(void) { return 0; }
int tud_cdc_read_char(void) { return -1; }
void reset_usb_boot(uint32_t a, uint32_t b) {}
void watchdog_enable(uint32_t ms, bool pause) {}
void watchdog_update(void) {}
void watchdog_reboot(uint32_t a, uint32_t b, uint32_t c) {}
void tight_loop_contents(void) {}
void settings_load(void) {}
bool settings_has_saved(void) { return true; }
bool settings_name_valid(const char *name) {
    size_t n = strlen(name);
    if (!n || n > 32 || name[0] == ' ' || name[n - 1] == ' ')
        return false;
    for (size_t i = 0; i < n; ++i)
        if ((unsigned char)name[i] < 32 || (unsigned char)name[i] > 126)
            return false;
    return true;
}
bool settings_save(const settings_t *next) {
    settings = *next;
    return true;
}
void motor_init(void) {}
void motor_stop(void) {
    fake_motor.mode = STOPPED;
    fake_motor.enabled = false;
}
bool motor_start(int factor, int direction, double speed, double acceleration, uint32_t steps,
                 bool fixed, bool defer, bool jogging) {
    fake_motor = (motor_status_t){.mode = defer   ? PREPARED
                                          : steps ? FINITE
                                                  : CONTINUOUS,
                                  .factor = factor,
                                  .direction = direction,
                                  .speed = speed,
                                  .acceleration = acceleration,
                                  .enabled = true,
                                  .target_pulses = steps * (16 / factor)};
    return true;
}
bool motor_retarget(int factor, int direction, double speed, double acceleration, bool fixed) {
    return motor_start(factor, direction, speed, acceleration, 0, fixed, false, true);
}
void motor_decelerate(void) { fake_motor.mode = BRAKING; }
bool motor_run_prepared(void) {
    if (fake_motor.mode != PREPARED)
        return false;
    fake_motor.mode = FINITE;
    return true;
}
bool motor_test(uint32_t pulses, double hz) {
    if (hz < 1 || hz > MAX_PULSE_HZ)
        return false;
    fake_motor.mode = DIAGNOSTIC;
    fake_motor.enabled = false;
    return true;
}
motor_status_t motor_status(void) { return fake_motor; }

static void check(const char *line, bool expected) {
    output_length = 0;
    output[0] = 0;
    command(line);
    char *last = strstr(output, "{\"type\":\"ack\"");
    assert(last);
    cJSON *frame = cJSON_Parse(last);
    assert(frame);
    assert(cJSON_IsTrue(cJSON_GetObjectItem(frame, "ok")) == expected);
    cJSON_Delete(frame);
}
#define START                                                                                      \
    "{\"id\":1,\"cmd\":\"start\",\"mode\":\"steps\",\"steps\":800,\"resolution\":\"quarter\","     \
    "\"direction\":1,\"speed_sps\":16000,\"ramp_ms\":400}"
int main(void) {
    safe_gpio();
    sensor_initialized = true;
    sample();
    assert(temperature_valid && !alarm);
    check(START, false); /* No heartbeat. */
    command("{\"cmd\":\"heartbeat\"}");
    check(START, false); /* Safe 5 V startup. */
    check("{\"cmd\":\"set_pd_voltage\",\"voltage\":9}", true);
    assert(!fake_pins[22] && fake_pins[21]);
    check(START, false); /* Settling timer. */
    fake_us += 2000001;
    command("{\"cmd\":\"heartbeat\"}");
    fake_registers[0] = 60 * 256;
    check(START, false); /* Start threshold inclusive. */
    fake_registers[0] = 25 * 256;
    fake_registers[1] ^= 0x100;
    check(START, false); /* Sensor config tampering. */
    fake_registers[1] ^= 0x100;
    fake_pins[23] = 0;
    check(START, false); /* Driver fault. */
    fake_pins[23] = 1;
    check(START, true);
    output_length = 0;
    status(true);
    cJSON *compact_frame = cJSON_Parse(output);
    assert(compact_frame && matches(string(compact_frame, "step_resolution"), "quarter"));
    double reported_speed;
    assert(number(compact_frame, "speed_sps", &reported_speed) && reported_speed == 16000);
    cJSON_Delete(compact_frame);
    assert(fake_motor.factor == 4 && fake_motor.target_pulses == 3200 &&
           fake_motor.acceleration == 40000);
    check("{\"cmd\":\"set_pd_voltage\",\"voltage\":20}", false);
    check("{\"cmd\":\"set_threshold\",\"threshold_c\":60}", false);
    check("{\"cmd\":\"set_device_name\",\"name\":\"new\"}", false);
    check("{\"cmd\":\"jog\",\"direction\":1,\"speed_sps\":40}", false);
    check("{\"cmd\":\"stop\"}", true);
    assert(!fake_motor.enabled);
    const char *invalid[] = {
        "[]",
        "null",
        "{bad}",
        "{}",
        "{\"cmd\":true}",
        "{\"cmd\":\"start\",\"mode\":\"continuous\",\"direction\":true,\"speed_sps\":40}",
        "{\"cmd\":\"start\",\"mode\":\"continuous\",\"direction\":1,\"speed_sps\":true}",
        "{\"cmd\":\"start\",\"mode\":\"continuous\",\"direction\":1,\"speed_sps\":1e999}",
        "{\"cmd\":\"start\",\"mode\":\"continuous\",\"direction\":1,\"speed_sps\":50001,"
        "\"resolution\":\"quarter\"}",
        "{\"cmd\":\"start\",\"mode\":\"continuous\",\"direction\":1,\"speed_sps\":40,\"ramp_ms\":"
        "399}",
        "{\"cmd\":\"start\",\"mode\":\"steps\",\"direction\":1,\"speed_sps\":40,\"steps\":1.5}",
        "{\"cmd\":\"start\",\"mode\":\"steps\",\"direction\":1,\"speed_sps\":40,\"steps\":100001}",
        "{\"cmd\":\"start\",\"mode\":\"continuous\",\"direction\":1,\"speed_sps\":40,\"defer\":"
        "true}",
        "{\"cmd\":\"set_threshold\",\"threshold_c\":75}",
        "{\"cmd\":\"set_pd_voltage\",\"voltage\":10}",
        "{\"cmd\":\"set_led\",\"color\":\"unknown\"}",
        "{\"cmd\":\"play_song\",\"hex\":\"ffff1400\"}",
        "{\"cmd\":\"play_song\",\"hex\":\"xx\"}",
        "{\"cmd\":\"start\"} trailing"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        check(invalid[i], false);
        assert(!fake_motor.enabled);
    }
    check("{\"cmd\":\"start\",\"mode\":\"steps\",\"direction\":-1,\"resolution\":\"sixteenth\","
          "\"speed_sps\":64000,\"steps\":800,\"defer\":true,\"ramp_ms\":400}",
          true);
    assert(fake_motor.mode == PREPARED);
    fake_us += 1100000;
    check("{\"cmd\":\"run_move\"}", false);
    assert(fake_motor.mode == STOPPED);
    command("{\"cmd\":\"heartbeat\"}");
    check(START, true);
    check("{\"cmd\":\"stop\"}", true);
    check("{\"cmd\":\"test_pulses\",\"pulses\":64000,\"pulse_hz\":64000}", true);
    assert(!fake_motor.enabled);
    check("{\"cmd\":\"stop\"}", true);
    check("{\"cmd\":\"play_song\",\"hex\":\"b8016400\"}", true);
    assert(song_length == 1);
    check("{\"cmd\":\"stop_song\"}", true);
    assert(song_length == 0);
    check("{\"cmd\":\"set_led\",\"color\":\"cyan\"}", true);
    fake_registers[0] = 70 * 256;
    sample();
    indicators(fake_us, fake_motor);
    assert(led_bits != 6); /* Alarm overrides color. */
    fake_registers[0] = 25 * 256;
    sample();
    command("{\"cmd\":\"heartbeat\"}");
    check(START, true);
    fake_us += 1500001;
    protect(fake_us, fake_motor);
    assert(!fake_motor.enabled && matches(stop_reason, "Browser heartbeat lost"));
    command("{\"cmd\":\"heartbeat\"}");
    check(START, true);
    fake_connected = false;
    protect(fake_us, fake_motor);
    assert(!fake_motor.enabled && matches(stop_reason, "USB disconnected"));
    fake_connected = true;
    check(START, true);
    fake_registers[0] = 75 * 256;
    sample();
    protect(fake_us, fake_motor);
    assert(!fake_motor.enabled && matches(stop_reason, "Thermal shutdown"));
    fake_registers[0] = 25 * 256;
    sample();
    check(START, true);
    fake_pins[23] = 0;
    protect(fake_us, fake_motor);
    assert(!fake_motor.enabled && matches(stop_reason, "Driver fault"));
    fake_pins[23] = 1;
    check(START, true);
    fake_registers[2] = 0;
    sample();
    protect(fake_us, fake_motor);
    assert(!fake_motor.enabled && matches(stop_reason, "Temperature sensor error"));
    fake_registers[2] = 60 * 256;
    sample();
    check("{\"cmd\":\"status\"}", true);
    cJSON *frame = cJSON_ParseWithOpts(output, NULL, false);
    assert(frame);
    assert(matches(string(frame, "firmware"), "native-c-pio"));
    cJSON_Delete(frame);
    puts("Native protocol: heartbeat/power/temperature/fault gates, prepared moves, malformed "
         "commands, bounds, alarm priority and driver-disabled pulse test passed");
}
