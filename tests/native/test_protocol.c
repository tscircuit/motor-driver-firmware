#define MOTOR_HOST_TEST
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

void current_capture_init(void) {}
bool current_capture_snapshot(uint16_t raw[2]) {raw[0]=2048;raw[1]=2048;return true;}
void current_capture_read(current_frame_t *f, bool connected) {memset(f,0,sizeof(*f));}
void current_capture_drop(unsigned bins) {}
