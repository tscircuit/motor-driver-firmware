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
    cJSON *caps = capabilities();
    assert(cJSON_IsFalse(cJSON_GetObjectItem(caps, "torque_count_supported")));
    assert(cJSON_IsFalse(cJSON_GetObjectItem(caps, "stall_detection_supported")));
    assert(strstr(string(caps, "stall_detection_note"), "DRV8825"));
    assert(cJSON_IsTrue(cJSON_GetObjectItem(caps, "current_stall_firmware_supported")));
    cJSON_Delete(caps);
    output_length = 0;
    status(false);
    cJSON *initial = cJSON_Parse(output);
    assert(initial && cJSON_IsNull(cJSON_GetObjectItem(initial, "stall_torque_count")));
    assert(cJSON_IsNull(cJSON_GetObjectItem(initial, "stall_suspected")));
    assert(cJSON_IsFalse(cJSON_GetObjectItem(initial, "stall_detection_valid")));
    cJSON_Delete(initial);
    check("{\"cmd\":\"set_stall_sensitivity\",\"sensitivity\":80}",true);
    assert(settings.stall_sensitivity==80);
    check("{\"cmd\":\"set_stall_sensitivity\",\"sensitivity\":101}",false);
    check("{\"cmd\":\"set_stall_sensitivity\",\"sensitivity\":35}",true);
    check("{\"cmd\":\"set_stall_response\",\"beeping_on\":true,\"stop_ms\":1000}", true);
    assert(settings.stall_beep && settings.stall_stop_ms == 1000);
    check("{\"cmd\":\"set_stall_response\",\"beeping_on\":1,\"stop_ms\":1000}", false);
    check("{\"cmd\":\"set_stall_response\",\"beeping_on\":true,\"stop_ms\":-1}", false);
    check("{\"cmd\":\"set_stall_response\",\"beeping_on\":true,\"stop_ms\":60001}", false);
    check("{\"cmd\":\"set_stall_response\",\"beeping_on\":true,\"stop_ms\":1.5}", false);
    check("{\"cmd\":\"set_stall_response\",\"beeping_on\":false,\"stop_ms\":0}", true);
    check("{\"cmd\":\"set_current_stream_enabled\",\"enabled\":false}",true);
    assert(!current_stream_enabled);
    check("{\"cmd\":\"set_current_stream_enabled\",\"enabled\":1}",false);
    check("{\"cmd\":\"set_current_stream_enabled\",\"enabled\":true}",true);
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
    check("{\"cmd\":\"set_stall_response\",\"beeping_on\":true,\"stop_ms\":1000}", false);
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
    settings.stall_beep = true;
    settings.stall_stop_ms = 1000;
    check(START, true);
    respond_to_stall(fake_us, fake_motor, true, true);
    indicators(fake_us, fake_motor);
    assert(buzzer_frequency == 1800);
    alarm = true;
    indicators(fake_us / 1000000 * 1000000, fake_motor);
    assert(buzzer_frequency == 2731); /* Overheat always wins. */
    alarm = false;
    fake_us += 999999;
    respond_to_stall(fake_us, fake_motor, true, true);
    assert(fake_motor.enabled);
    fake_us++;
    respond_to_stall(fake_us, fake_motor, true, true);
    assert(!fake_motor.enabled && matches(stop_reason, "Stall auto-stop timeout"));
    assert(!stall_response.active);
    settings.stall_stop_ms = 0;
    check(START, true);
    settings.stall_stop_ms = 1000;
    stall_result_t uncertain = {.valid = true, .state = "uncertain", .score = 90,
                                .window_end_us = fake_us};
    assert(current_stall_warning(uncertain, fake_us));
    uncertain.score = 20;
    assert(!current_stall_warning(uncertain, fake_us));
    uncertain.score = 90;
    assert(!current_stall_warning(uncertain, fake_us + 250000));
    uncertain.valid = false;
    assert(!current_stall_warning(uncertain, fake_us));
    uncertain.valid = true;
    respond_to_stall(fake_us, fake_motor, false, current_stall_warning(uncertain, fake_us));
    indicators(fake_us, fake_motor);
    assert(buzzer_frequency == 1800 && !stall_response.active);
    fake_us += 2000000;
    respond_to_stall(fake_us, fake_motor, false, true);
    assert(fake_motor.enabled && stall_beep_response.active && !stall_response.timed_out);
    settings.stall_beep = false;
    indicators(fake_us, fake_motor);
    assert(buzzer_frequency == 0);
    stop("Test complete");
    assert(!stall_beep_response.active);
    settings.stall_stop_ms = 0;
    command("{\"cmd\":\"heartbeat\"}");
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
