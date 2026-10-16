#define MOTOR_HOST_TEST
#define MOTOR_BOARD_DRV8847 1
#define main firmware_entry
#include "../../firmware-c/main.c"
#undef main
#include <assert.h>
#include "fake_application.h"

static void check(const char *line, bool expected) {
    output_length = 0;
    output[0] = 0;
    command(line);
    char *last = strstr(output, "{\"type\":\"ack\"");
    assert(last);
    cJSON *frame = cJSON_Parse(last);
    assert(frame && cJSON_IsTrue(cJSON_GetObjectItem(frame, "ok")) == expected);
    cJSON_Delete(frame);
}
#define START "{\"cmd\":\"start\",\"mode\":\"steps\",\"resolution\":\"half\",\"steps\":10,\"direction\":1,\"speed_sps\":40,\"ramp_ms\":400}"
int main(int argc, char **argv) {
    fake_pins[24] = 1;
    /* Unrelated pins must never be driven as PD selectors or RGB outputs. */
    for (unsigned i = 0; i < 16; ++i)
        fake_pins[i] = 7;
    safe_gpio();
    for (unsigned i = 0; i < 16; ++i)
        assert(fake_pins[i] == 7);
    for (unsigned i = 18; i <= 22; ++i)
        assert(!fake_pins[i]);
    assert(!fake_pins[16] && !fake_pins[25]);
    sensor_initialized = true;
    sample();
    assert(temperature_valid && !alarm);
    check(START, false);
    command("{\"cmd\":\"heartbeat\"}");
    check(START, true); /* No PD selection needed or available. */
    assert(fake_motor.factor == 2 && fake_motor.target_pulses == 10);
    check("{\"cmd\":\"set_device_name\",\"name\":\"Moving\"}", false);
    check("{\"cmd\":\"set_threshold\",\"threshold_c\":60}", false);
    check("{\"cmd\":\"stop\"}", true);
    const char *invalid[] = {
        "{\"cmd\":\"set_pd_voltage\",\"voltage\":9}",
        "{\"cmd\":\"set_led\",\"color\":\"red\"}",
        "{\"cmd\":\"test_pulses\",\"pulses\":10,\"pulse_hz\":1000}",
        "{\"cmd\":\"set_current_limit\",\"peak_a\":1}",
        "{\"cmd\":\"arm_power\"}",
        "{\"cmd\":\"start\",\"mode\":\"continuous\",\"resolution\":\"quarter\",\"direction\":1,\"speed_sps\":40}",
        "{\"cmd\":\"start\",\"mode\":\"continuous\",\"direction\":true,\"speed_sps\":40}",
        "{\"cmd\":\"start\",\"mode\":\"continuous\",\"direction\":1,\"speed_sps\":20001}",
        "{\"cmd\":\"start\",\"mode\":\"steps\",\"steps\":1.5,\"direction\":1,\"speed_sps\":40}",
        "{\"cmd\":\"start\",\"mode\":\"continuous\",\"direction\":1,\"speed_sps\":1e999}",
        "{\"cmd\":\"start\",\"mode\":\"continuous\",\"direction\":1,\"speed_sps\":40,\"ramp_ms\":399}",
        "{bad}", "[]", "{\"cmd\":\"start\"} trailing"};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        check(invalid[i], false);
        assert(!fake_motor.enabled);
    }
    check("{\"cmd\":\"start\",\"mode\":\"steps\",\"resolution\":\"full\",\"steps\":10,\"direction\":-1,\"speed_sps\":40,\"defer\":true,\"ramp_ms\":400}", true);
    assert(fake_motor.mode == PREPARED && fake_motor.target_pulses == 10);
    fake_pins[24] = 0;
    check("{\"cmd\":\"run_move\"}", false);
    assert(!fake_motor.enabled);
    fake_pins[24] = 1;
    check(START, true);
    fake_us += 1500001;
    protect(fake_us, motor_status());
    assert(!fake_motor.enabled);
    command("{\"cmd\":\"heartbeat\"}");
    assert(!fake_motor.enabled); /* Lease renewal must not resume a move. */
    check(START, true);
    fake_pins[24] = 0;
    protect(fake_us, motor_status());
    assert(!fake_motor.enabled);
    check(START, false);
    fake_pins[24] = 1;
    fake_pins[23] = 0;
    check(START, false);
    fake_pins[23] = 1;
    fake_registers[1] ^= 0x100;
    check(START, false);
    fake_registers[1] ^= 0x100;
    fake_registers[0] = 60 * 256;
    check(START, false);
    fake_registers[0] = 25 * 256;
    check(START, true);
    fake_connected = false;
    protect(fake_us, motor_status());
    assert(!fake_motor.enabled);
    fake_connected = true;
    check(START, true);
    fake_registers[0] = 75 * 256;
    sample();
    protect(fake_us, motor_status());
    assert(!fake_motor.enabled);
    fake_registers[0] = 25 * 256;
    sample();
    check("{\"cmd\":\"jog\",\"resolution\":\"half\",\"direction\":-1,\"speed_sps\":150,\"ramp_ms\":400}", true);
    check("{\"cmd\":\"decelerate\"}", true);
    assert(fake_motor.mode == BRAKING);
    check("{\"cmd\":\"stop\"}", true);
    check("{\"cmd\":\"set_threshold\",\"threshold_c\":64}", true);
    check("{\"cmd\":\"play_song\",\"hex\":\"b8016400\"}", true);
    assert(song_length == 1);
    check("{\"cmd\":\"stop_song\"}", true);
    fake_motor.position_microsteps = 24;
    fake_motor.executed_pulses = 3;
    snprintf(device_id, sizeof(device_id), "drv8847-test");
    snprintf(session, sizeof(session), "drv8847-session");
    output_length = 0;
    status(false);
    cJSON *frame = cJSON_Parse(output);
    assert(frame && matches(string(frame, "board_id"), "rp2040_drv8847"));
    cJSON *c = cJSON_GetObjectItem(frame, "capabilities");
    assert(!cJSON_HasObjectItem(c, "led_colors") && !cJSON_HasObjectItem(c, "pd_voltages"));
    assert(cJSON_GetArraySize(cJSON_GetObjectItem(frame, "supported_resolutions")) == 2);
    assert(cJSON_IsFalse(cJSON_GetObjectItem(frame, "current_available")));
    assert(cJSON_IsNull(cJSON_GetObjectItem(frame, "current_a")));
    assert(cJSON_IsNull(cJSON_GetObjectItem(frame, "pd_voltage_requested")));
    double value;
    assert(number(frame, "position_full_steps", &value) && value == 1.5);
    assert(number(frame, "steps_executed", &value) && value == 3);
    cJSON_Delete(frame);
    if (argc > 1) {
        FILE *file = fopen(argv[1], "w");
        assert(file && fputs(output, file) >= 0);
        fclose(file);
    }
    puts("DRV8847 protocol: safe pins, full/half steps, prepared moves, ramps, safety shutdowns, settings and honest capabilities passed");
    return 0;
}
