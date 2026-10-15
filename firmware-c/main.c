#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef MOTOR_HOST_TEST
#define FIRMWARE_BUILD_ID "host-test"
#include "test_platform.h"
#else
#include "build_id.h"
#include "hardware/adc.h"
#include "hardware/i2c.h"
#include "hardware/pwm.h"
#include "hardware/resets.h"
#include "hardware/watchdog.h"
#include "pico/bootrom.h"
#include "pico/rand.h"
#include "pico/stdlib.h"
#include "pico/unique_id.h"
#include "tusb.h"
#endif
#include "board.h"
#include "current_capture.h"
#ifdef MOTOR_BOARD_DRV8452
#include "nema23.h"
#endif
#include "motor.h"
#include "settings.h"
#include "vendor/cJSON.h"

#define SENSOR_ADDRESS 0x48
#define SENSOR_CONFIG 0x60c0
#define SENSOR_MASK 0x7fd0
#define HEARTBEAT_US 1500000u
static const char *colors[] = {"off", "red", "green", "blue", "yellow", "cyan", "magenta", "white"};
static const uint8_t color_bits[] = {0, 1, 2, 4, 3, 6, 5, 7};
static const char *resolutions[] = {"full", "half", "quarter", "eighth", "sixteenth"};
static const char *modes[] = {"stopped", "prepared", "steps", "continuous", "braking", "test"};
static char device_id[17], session[33], stop_reason[96] = "Boot: motor disabled";
static uint64_t boot_us, heartbeat_us, last_sample, last_emit, test_until, restart_at;
#ifndef MOTOR_BOARD_DRV8452
static uint64_t power_at;
#endif
static bool heartbeat_seen, temperature_valid, alarm, sensor_initialized;
static double temperature;
static int pd_voltage = 5, led_selection = -1, led_bits, buzzer_frequency;
static uint16_t song_hz[48], song_ms[48];
static size_t song_length, song_index;
static uint64_t song_until;
static uint32_t diagnostic_pulses, diagnostic_elapsed;
static motor_mode_t previous_mode = STOPPED;
extern volatile bool usb_boot_requested;

static void stop(const char *reason) {
    motor_stop();
    song_length = 0;
    snprintf(stop_reason, sizeof(stop_reason), "%s", reason);
}
static bool power_ready(void) {
#ifdef MOTOR_BOARD_DRV8452
    return nema23_ready();
#else
    return pd_voltage >= 9 && time_us_64() >= power_at;
#endif
}
static bool driver_fault(void) { return !gpio_get(23); }
static bool read_reg(uint8_t reg, uint16_t *value) {
    uint8_t bytes[2];
    if (i2c_write_timeout_us(i2c1, SENSOR_ADDRESS, &reg, 1, true, 2000) != 1 ||
        i2c_read_timeout_us(i2c1, SENSOR_ADDRESS, bytes, 2, false, 2000) != 2)
        return false;
    *value = (bytes[0] << 8) | bytes[1];
    return true;
}
static bool write_reg(uint8_t reg, uint16_t value) {
    uint8_t bytes[] = {reg, value >> 8, value};
    return i2c_write_timeout_us(i2c1, SENSOR_ADDRESS, bytes, 3, false, 2000) == 3;
}
static bool sensor_verify(void) {
    uint16_t config, low, high;
    return read_reg(1, &config) && (config & SENSOR_MASK) == (SENSOR_CONFIG & SENSOR_MASK) &&
           read_reg(2, &low) && low == (SENSOR_LOW_C * 256) && read_reg(3, &high) &&
           high == (SHUTDOWN_C * 256);
}
static void sample(void) {
    uint16_t raw;
    temperature_valid = sensor_initialized && sensor_verify() && read_reg(0, &raw) && !(raw & 15);
    if (temperature_valid) {
        temperature = (int16_t)raw / 256.0;
        temperature_valid = temperature >= -40 && temperature <= 125;
    }
    if (!temperature_valid || temperature >= settings.threshold)
        alarm = true;
    else if (temperature < settings.threshold - 2)
        alarm = false;
    last_sample = time_us_64();
}
static void buzzer(int hz) {
    if (hz == buzzer_frequency)
        return;
    uint slice = pwm_gpio_to_slice_num(BUZZER_PIN);
    pwm_set_enabled(slice, false);
    gpio_put(BUZZER_PIN, 0);
    if (hz) {
        /* 1 MHz PWM tick; duty cycle 50%. */
        pwm_set_clkdiv(slice, 125.0f);
        pwm_set_wrap(slice, 1000000 / hz - 1);
        pwm_set_gpio_level(BUZZER_PIN, (1000000 / hz) / 2);
        pwm_set_enabled(slice, true);
    } else {
        pwm_set_gpio_level(BUZZER_PIN, 0);
    }
    buzzer_frequency = hz;
}
static void indicators(uint64_t now, motor_status_t s) {
    bool fault = alarm || driver_fault();
    led_bits = fault                ? ((now / 125000) % 2 ? 1 : 0)
               : led_selection >= 0 ? color_bits[led_selection]
               : s.mode == STOPPED  ? 2
                                    : 4;
    gpio_put(LED_RED_PIN, led_bits & 1);
    gpio_put(LED_GREEN_PIN, led_bits & 2);
    gpio_put(LED_BLUE_PIN, led_bits & 4);
    if (alarm) {
        song_length = 0;
        buzzer((now / 1000) % 1000 < 180 ? 2731 : 0);
        return;
    }
    if (song_length && now >= song_until) {
        if (++song_index >= song_length)
            song_length = 0;
        else
            song_until = now + (uint64_t)song_ms[song_index] * 1000;
    }
    buzzer(song_length ? song_hz[song_index] : now < test_until ? 2731 : 0);
}
static const char *color_name(void) {
    for (int i = 0; i < 8; ++i)
        if (color_bits[i] == led_bits)
            return colors[i];
    return "off";
}
static const char *resolution_name(int factor) {
    for (int i = 0; i < 5; ++i)
        if ((1 << i) == factor)
            return resolutions[i];
    return "full";
}
static void add_bool(cJSON *o, const char *k, bool v) { cJSON_AddBoolToObject(o, k, v); }
static void add_number(cJSON *o, const char *k, double v) { cJSON_AddNumberToObject(o, k, v); }
static void add_string(cJSON *o, const char *k, const char *v) { cJSON_AddStringToObject(o, k, v); }
static void null(cJSON *o, const char *k) { cJSON_AddNullToObject(o, k); }
/* Nonblocking bounded USB output: motion/protection never wait for a host
   reading CDC. A frame is either accepted whole, or dropped whole. */
static bool emit(cJSON *o) {
    char *text = cJSON_PrintUnformatted(o);
    bool ok = false;
    if (text) {
        size_t n = strlen(text);
        cJSON *kind = cJSON_GetObjectItemCaseSensitive(o, "type");
        size_t reserve = cJSON_IsString(kind) && strcmp(kind->valuestring, "ack") == 0 ? 0
                         : cJSON_IsString(kind) && strcmp(kind->valuestring, "current_samples") == 0
                             ? 3500
                             : 768;
        if (tud_cdc_connected() && n + 1 + reserve <= tud_cdc_write_available()) {
            tud_cdc_write(text, n);
            tud_cdc_write_char('\n');
            tud_cdc_write_flush();
            ok = true;
        }
        free(text);
    }
    cJSON_Delete(o);
    return ok;
}
static void currents(cJSON *o) {
    double readings[2];
    uint16_t raw[2] = {0};
    bool valid = current_capture_snapshot(raw);
    cJSON *array = cJSON_CreateArray();
    for (int i = 0; i < 2; ++i) {
        readings[i] = (raw[i] * 3.3 / 4095 - 1.65) / CURRENT_VOLTS_PER_AMP;
        cJSON_AddItemToArray(array, cJSON_CreateNumber(readings[i]));
    }
    if (valid) {
        cJSON_AddItemToObject(o, "winding_current_a", array);
        add_number(o, "current_a", hypot(readings[0], readings[1]));
    } else {
        cJSON_Delete(array);
        null(o, "winding_current_a");
        null(o, "current_a");
    }
    add_bool(o, "current_available", valid);
}
static void stream_current(void) {
    static current_frame_t frame;
    current_capture_read(&frame, tud_cdc_connected());
    if (!frame.count)
        return;
    cJSON *o = cJSON_CreateObject(), *bins = cJSON_CreateArray();
    add_string(o, "type", "current_samples");
    add_number(o, "protocol", 3);
    add_number(o, "first_sample_us", frame.first_sample_us);
    add_number(o, "window_us", 1000);
    add_number(o, "sample_rate_hz", CURRENT_SAMPLE_HZ);
    add_number(o, "current_volts_per_amp", CURRENT_VOLTS_PER_AMP);
    add_number(o, "samples_captured", frame.captured);
    add_number(o, "dropped_samples", frame.dropped);
    for (unsigned i = 0; i < frame.count; ++i) {
        int values[8];
        for (unsigned w = 0; w < 2; ++w) {
            current_stats_t s = frame.bins[i].winding[w];
            values[4 * w] = s.min;
            values[4 * w + 1] = s.max;
            values[4 * w + 2] = s.sum;
            values[4 * w + 3] = s.count;
        }
        cJSON_AddItemToArray(bins, cJSON_CreateIntArray(values, 8));
    }
    cJSON_AddItemToObject(o, "bins", bins);
    if (!emit(o))
        current_capture_drop(frame.count);
}
static cJSON *capabilities(void) {
    cJSON *c = cJSON_CreateObject(), *r = cJSON_CreateObject();
    for (int i = 0; i < 5; ++i)
        add_number(r, resolutions[i], 1 << i);
    cJSON_AddItemToObject(c, "resolutions", r);
    cJSON_AddItemToObject(c, "led_colors", cJSON_CreateStringArray(colors, 8));
#ifdef MOTOR_BOARD_DRV8452
    int volts[] = {5, 9, 12, 15, 20, 28};
    add_bool(c, "power_arm_required", true);
    add_bool(c, "pd_power_good_override_supported", true);
    add_bool(c, "current_limit_supported", true);
    add_number(c, "current_limit_min_a", 0.1);
    add_number(c, "current_limit_max_a", NEMA23_FULL_SCALE_A);
    add_number(c, "pd_settle_ms", 14000);
    add_number(c, "current_foldback_c", 60);
    add_string(
        c, "power_note",
        "Request voltage, wait for the verified contract, then arm motor power below 55 C. Changes "
        "discharge for 12 seconds. The raw PD input defaults to 28 V; it is isolated at boot. "
        "PD power-good is ignored by default; PD communication and driver/eFuse checks remain "
        "active.");
#else
    int volts[] = {5, 9, 12, 15, 20};
#endif
    cJSON_AddItemToObject(c, "pd_voltages",
                          cJSON_CreateIntArray(volts, sizeof(volts) / sizeof(volts[0])));
    add_number(c, "current_volts_per_amp", CURRENT_VOLTS_PER_AMP);
    add_number(c, "full_steps_per_revolution", 200);
    add_number(c, "min_speed_sps", MIN_SPEED_SPS);
    null(c, "max_speed_sps");
    add_number(c, "max_step_pulse_hz", MAX_PULSE_HZ);
    add_number(c, "max_steps", MAX_MOVE_STEPS);
    add_bool(c, "acceleration_supported", true);
    add_bool(c, "deceleration_supported", true);
    add_bool(c, "jog_update_supported", true);
    add_bool(c, "deferred_start_supported", true);
    add_number(c, "fixed_ramp_ms", 400);
    add_number(c, "min_acceleration_sps2", 10);
    add_number(c, "max_acceleration_sps2", 1000);
    add_number(c, "default_acceleration_sps2", 100);
    add_number(c, "start_below_c", START_BELOW_C);
    add_number(c, "shutdown_c", SHUTDOWN_C);
    add_number(c, "threshold_min_c", 10);
    add_number(c, "threshold_max_c", SHUTDOWN_C - 1);
    add_number(c, "heartbeat_timeout_ms", 1500);
    add_number(c, "buzzer_hz", 2731);
    add_bool(c, "song_supported", true);
    add_string(c, "song_format", "u16le-hz-u16le-ms");
    add_number(c, "song_max_bytes", 192);
    add_bool(c, "hardware_pulse_timing", true);
    add_number(c, "current_sample_hz", CURRENT_SAMPLE_HZ);
    add_bool(c, "current_stream_supported", true);
    add_bool(c, "pulse_test_supported", true);
    add_string(c, "step_note",
               "Fixed 1/16 hardware microsteps. PIO generates evenly spaced physical pulses at up "
               "to 200000 Hz; selected increments group pulses.");
    add_string(c, "current_note",
               "Nominal INA240 winding samples; uncalibrated, instantaneous, not supply current.");
    return c;
}
static void status(bool compact) {
    motor_status_t s = motor_status();
    cJSON *o = cJSON_CreateObject();
    add_string(o, "type", "telemetry");
    add_number(o, "protocol", 3);
    if (compact)
        add_bool(o, "compact", true);
    if (temperature_valid)
        add_number(o, "temperature_c", temperature);
    else
        null(o, "temperature_c");
    add_bool(o, "motor_enabled", s.enabled);
    add_string(o, "motion_mode", modes[s.mode]);
    if (temperature_valid)
        null(o, "sensor_error");
    else
        add_string(o, "sensor_error", "TMP102 unavailable or protection configuration changed");
    add_bool(o, "alarm_active", alarm);
    add_bool(o, "thermal_alert",
#ifdef MOTOR_BOARD_DRV8452
             nema23_status().phase == POWER_LATCHED || (power_ready() && !gpio_get(24))
#else
             false
#endif
    );
    add_bool(o, "driver_fault_asserted", driver_fault());
    add_number(o, "pulse_queue_underruns", s.queue_underruns);
    add_string(o, "step_resolution", resolution_name(s.factor));
    add_number(o, "steps_per_revolution", 200 * s.factor);
    add_number(o, "direction", s.direction);
    add_number(o, "speed_sps", s.speed);
    add_number(o, "acceleration_sps2", s.acceleration);
    add_number(o, "profile_speed_sps", s.profile_speed);
    add_number(o, "pulse_rate_hz", s.pulse_rate);
    add_number(o, "position_full_steps", s.position_microsteps / 16.0);
    add_string(o, "stop_reason", stop_reason);
    add_number(o, "pd_voltage_requested", pd_voltage);
    null(o, "pd_voltage_measured");
    add_bool(o, "motor_power_ready", power_ready());
#ifdef MOTOR_BOARD_DRV8452
    nema23_status_t board = nema23_status();
    add_string(o, "power_state", nema23_phase_name());
    add_string(o, "power_error", board.fault ? board.fault : "");
    add_bool(o, "pd_request_sent", board.request_sent);
    add_bool(o, "pd_power_good", !gpio_get(1));
    add_bool(o, "pd_power_good_override", board.ignore_pd_power_good);
    add_bool(o, "protected_rail_interlock", gpio_get(24));
    add_bool(o, "power_fault_asserted", !gpio_get(2));
    add_bool(o, "pd_contract_valid", board.contract_valid);
    add_number(o, "pd_contract_current_a", board.current_units * 0.05);
    add_number(o, "pd_protocol_status", board.pd_status);
    add_bool(o, "power_armed", board.phase == POWER_READY);
    add_number(o, "current_limit_peak_a", board.current_peak_a);
    add_bool(o, "current_foldback_active", board.folded_back);
    add_bool(o, "driver_spi_verified", board.spi_valid && gpio_get(WAKE_PIN));
#endif
    currents(o);
    add_string(o, "led_color", color_name());
    if (!compact) {
        add_string(o, "firmware", "native-c-pio");
        add_string(o, "firmware_version", "1.0.0");
        add_string(o, "firmware_build", FIRMWARE_BUILD_ID);
        add_number(o, "uptime_ms", (time_us_64() - boot_us) / 1000);
        add_string(o, "device_name", settings.name);
        add_string(o, "usb_name", settings.name);
        add_bool(o, "usb_name_supported", true);
        null(o, "usb_name_error");
        add_string(o, "device_id", device_id);
        add_string(o, "position_session", session);
        add_bool(o, "restarting", restart_at != 0);
        add_bool(o, "led_on", led_bits != 0);
        add_string(o, "led_mode", alarm ? "alarm" : s.mode != STOPPED ? "running" : "off");
        add_string(o, "led_selection", led_selection < 0 ? "auto" : colors[led_selection]);
        add_number(o, "threshold_c", settings.threshold);
        add_bool(o, "buzzer_on", buzzer_frequency != 0);
        add_number(o, "buzzer_frequency_hz", buzzer_frequency);
        add_bool(o, "song_playing", song_length != 0);
        add_number(o, "song_note", song_length ? song_index + 1 : 0);
        cJSON_AddItemToObject(o, "supported_resolutions", cJSON_CreateStringArray(resolutions, 5));
        add_number(o, "late_steps", 0);
        add_number(o, "max_step_lateness_us", 0);
        if (s.mode == FINITE || s.mode == BRAKING)
            add_number(o, "steps_remaining",
                       (s.target_pulses - s.executed_pulses) / (16 / s.factor));
        else
            null(o, "steps_remaining");
        add_number(o, "steps_executed", s.executed_pulses / (16 / s.factor));
        add_number(o, "hardware_pulses_executed", s.executed_pulses);
        add_number(o, "pulse_test_pulses", diagnostic_pulses);
        add_number(o, "pulse_test_elapsed_us", diagnostic_elapsed);
        add_string(o, "board", BOARD_NAME);
        add_string(o, "board_id", BOARD_ID);
        add_string(o, "mcu", "RP2040");
        add_string(o, "driver", DRIVER_NAME);
        cJSON_AddItemToObject(o, "capabilities", capabilities());
    }
    emit(o);
}
static void ack(cJSON *id, const char *error) {
    cJSON *o = cJSON_CreateObject();
    add_string(o, "type", "ack");
    cJSON_AddItemToObject(o, "id", id ? cJSON_Duplicate(id, true) : cJSON_CreateNull());
    add_bool(o, "ok", !error);
    if (error)
        add_string(o, "error", error);
    else {
        add_number(o, "threshold_c", settings.threshold);
        add_string(o, "device_name", settings.name);
        add_bool(o, "restarting", restart_at != 0);
    }
    emit(o);
}
static const char *string(cJSON *o, const char *key) {
    cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsString(v) ? v->valuestring : NULL;
}
static bool number(cJSON *o, const char *key, double *out) {
    cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    if (!cJSON_IsNumber(v) || !isfinite(v->valuedouble))
        return false;
    *out = v->valuedouble;
    return true;
}
static bool matches(const char *s, const char *value) { return s && strcmp(s, value) == 0; }
static bool recent_heartbeat(void) {
    return heartbeat_seen && time_us_64() - heartbeat_us <= 1000000;
}
static const char *motion_command(cJSON *c, bool jog) {
    motor_status_t s = motor_status();
    if (restart_at)
        return "Restart pending";
    if ((jog && (s.mode == FINITE || s.mode == PREPARED || s.mode == DIAGNOSTIC)) ||
        (!jog && s.mode != STOPPED))
        return "Stop current movement first";
    int factor = resolution_factor(string(c, "resolution"));
    if (!factor && !cJSON_HasObjectItem(c, "resolution"))
        factor = jog ? s.factor : 1;
    double speed, direction, acceleration = 100, count = 0;
    if (!number(c, "speed_sps", &speed) || !number(c, "direction", &direction) ||
        (direction != 1 && direction != -1))
        return "Invalid speed or direction";
    bool fixed = cJSON_HasObjectItem(c, "ramp_ms");
    if (fixed) {
        double ramp;
        if (!number(c, "ramp_ms", &ramp) || ramp != 400)
            return "Ramp duration is fixed at 400 ms";
        acceleration = speed / 0.4;
    } else if (cJSON_HasObjectItem(c, "acceleration_sps2") &&
               (!number(c, "acceleration_sps2", &acceleration) || acceleration < 10 ||
                acceleration > 1000))
        return "Acceleration must be 10-1000";
    bool finite = !jog && matches(string(c, "mode"), "steps");
    if (!jog && !finite && !matches(string(c, "mode"), "continuous"))
        return "Unknown motion mode";
    if (finite && (!number(c, "steps", &count) || count < 1 || count > MAX_MOVE_STEPS ||
                   floor(count) != count))
        return "Steps must be an integer from 1-100000";
    cJSON *defer_item = cJSON_GetObjectItemCaseSensitive(c, "defer");
    bool defer = cJSON_IsTrue(defer_item);
    if ((defer_item && !cJSON_IsBool(defer_item)) || (defer && (!finite || jog)))
        return "Only finite moves can be prepared";
    if (!profile_validate(speed, acceleration, factor, (uint32_t)count))
        return "Unsupported resolution or speed exceeds 200000 hardware pulses/sec";
    if (!recent_heartbeat())
        return "Waiting for browser heartbeat";
    if (!power_ready())
        return "Select motor power and wait two seconds";
    sample();
    if (!temperature_valid || temperature >= START_BELOW_C)
        return "Temperature must be valid and below the board start limit";
    if (driver_fault())
        return "Driver fault";
#ifdef MOTOR_BOARD_DRV8452
    if (s.mode == STOPPED) {
        const char *driver_error = nema23_prepare_motion();
        if (driver_error)
            return driver_error;
    }
#endif
    bool ok = (jog && s.mode != STOPPED)
                  ? motor_retarget(factor, (int)direction, speed, acceleration, fixed)
                  : motor_start(factor, (int)direction, speed, acceleration, (uint32_t)count, fixed,
                                defer, jog);
    if (!ok)
        return "Motion request rejected";
    song_length = 0;
    stop_reason[0] = 0;
    previous_mode = defer ? PREPARED : finite ? FINITE : CONTINUOUS;
    return NULL;
}
static int hex_digit(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}
static const char *load_song(const char *hex) {
    if (!hex || !strlen(hex) || strlen(hex) > 384 || strlen(hex) % 8)
        return "Use 1-48 four-byte notes";
    uint8_t data[192];
    size_t bytes = strlen(hex) / 2;
    for (size_t i = 0; i < bytes; ++i) {
        int a = hex_digit(hex[2 * i]), b = hex_digit(hex[2 * i + 1]);
        if (a < 0 || b < 0)
            return "Invalid song hex";
        data[i] = (a << 4) | b;
    }
    uint32_t total = 0;
    for (size_t i = 0; i < bytes / 4; ++i) {
        uint16_t hz = data[4 * i] | data[4 * i + 1] << 8,
                 ms = data[4 * i + 2] | data[4 * i + 3] << 8;
        if ((hz && (hz < 100 || hz > 10000)) || ms < 20 || ms > 5000)
            return "Invalid note frequency or duration";
        total += ms;
    }
    if (total > 60000)
        return "Song exceeds 60 seconds";
    for (size_t i = 0; i < bytes / 4; ++i) {
        song_hz[i] = data[4 * i] | data[4 * i + 1] << 8;
        song_ms[i] = data[4 * i + 2] | data[4 * i + 3] << 8;
    }
    song_length = bytes / 4;
    song_index = 0;
    song_until = time_us_64() + (uint64_t)song_ms[0] * 1000;
    test_until = 0;
    return NULL;
}
static void command(const char *line) {
    const char *end;
    cJSON *c = cJSON_ParseWithOpts(line, &end, true);
    if (!cJSON_IsObject(c)) {
        cJSON_Delete(c);
        ack(NULL, "Expected a JSON object");
        return;
    }
    const char *cmd = string(c, "cmd"), *error = NULL;
    cJSON *id = cJSON_GetObjectItemCaseSensitive(c, "id");
    motor_status_t s = motor_status();
    if (matches(cmd, "heartbeat")) {
        heartbeat_us = time_us_64();
        heartbeat_seen = true;
        cJSON_Delete(c);
        return;
    }
    if (matches(cmd, "stop"))
        stop("Stopped by user");
    else if (matches(cmd, "status"))
        status(false);
    else if (matches(cmd, "start") || matches(cmd, "jog"))
        error = motion_command(c, matches(cmd, "jog"));
    else if (matches(cmd, "run_move")) {
        sample();
        if (!recent_heartbeat() || !temperature_valid || temperature >= START_BELOW_C ||
            driver_fault() || !power_ready()) {
            stop("Prepared move failed safety check");
            error = "Prepared move failed safety check";
        } else if (!motor_run_prepared())
            error = "No prepared move";
        else
            previous_mode = FINITE;
    } else if (matches(cmd, "decelerate")) {
        if (s.mode == CONTINUOUS) {
            motor_decelerate();
            previous_mode = BRAKING;
        } else if (s.mode != STOPPED && s.mode != BRAKING)
            error = "Deceleration requires continuous motion";
    } else if (matches(cmd, "arm_power") || matches(cmd, "set_current_limit")) {
#ifdef MOTOR_BOARD_DRV8452
        if (s.mode != STOPPED || restart_at)
            error = "Stop motor first";
        else if (matches(cmd, "arm_power")) {
            sample();
            error = nema23_arm(temperature_valid, temperature);
        } else {
            double peak;
            if (!number(c, "peak_a", &peak))
                error = "Provide peak_a in amps";
            else
                error = nema23_set_current(peak);
        }
#else
        error = "Board does not support this command";
#endif
    } else if (matches(cmd, "set_pd_power_good_override")) {
#ifdef MOTOR_BOARD_DRV8452
        cJSON *enabled = cJSON_GetObjectItemCaseSensitive(c, "enabled");
        if (s.mode != STOPPED || restart_at)
            error = "Stop motor first";
        else if (!cJSON_IsBool(enabled))
            error = "enabled must be a boolean";
        else
            nema23_set_pg_override(cJSON_IsTrue(enabled));
#else
        error = "Board does not support this command";
#endif
    } else if (matches(cmd, "disarm_power")) {
#ifdef MOTOR_BOARD_DRV8452
        stop("Motor power disarmed");
        nema23_disarm();
#else
        error = "Board does not support this command";
#endif
    } else if (matches(cmd, "set_pd_voltage")) {
#ifdef MOTOR_BOARD_DRV8452
        double voltage;
        if (s.mode != STOPPED || restart_at)
            error = "Stop motor before changing voltage";
        else if (!number(c, "voltage", &voltage) || floor(voltage) != voltage || voltage < 5 ||
                 voltage > 28)
            error = "Invalid voltage";
        else {
            error = nema23_request_voltage((int)voltage);
            if (!error)
                pd_voltage = (int)voltage;
        }
#else
        double voltage;
        if (s.mode != STOPPED || restart_at)
            error = "Stop motor before changing voltage";
        else if (!number(c, "voltage", &voltage) ||
                 (voltage != 5 && voltage != 9 && voltage != 12 && voltage != 15 && voltage != 20))
            error = "Choose 5, 9, 12, 15 or 20 V";
        else {
            motor_stop();
            int code = voltage == 5    ? 0
                       : voltage == 9  ? 7
                       : voltage == 12 ? 3
                       : voltage == 15 ? 1
                                       : 5;
            gpio_put(4, 0);
            gpio_put(5, code & 2);
            gpio_put(6, code & 4);
            gpio_put(4, code & 1);
            pd_voltage = (int)voltage;
            power_at = time_us_64() + 2000000;
        }

#endif
    } else if (matches(cmd, "set_led")) {
        const char *color = string(c, "color");
        int next = -2;
        if (matches(color, "auto"))
            next = -1;
        for (int i = 0; i < 8; ++i)
            if (matches(color, colors[i]))
                next = i;
        if (next == -2)
            error = "Unsupported LED color";
        else
            led_selection = next;
    } else if (matches(cmd, "set_threshold") || matches(cmd, "set_device_name")) {
        if (s.mode != STOPPED || restart_at)
            error = "Stop motor before saving settings";
        else {
            settings_t next = settings;
            if (matches(cmd, "set_threshold")) {
                double threshold;
                if (!number(c, "threshold_c", &threshold) || threshold < 10 ||
                    threshold > SHUTDOWN_C - 1)
                    error = "Threshold must be below the board shutdown temperature";
                else
                    next.threshold = threshold;
            } else {
                const char *name = string(c, "name");
                if (!name || !settings_name_valid(name))
                    error = "Use 1-32 printable ASCII characters without surrounding spaces";
                else
                    snprintf(next.name, sizeof(next.name), "%s", name);
            }
            if (!error) {
                motor_stop();
                if (!settings_save(&next))
                    error = "Settings flash write failed";
                else if (matches(cmd, "set_device_name")) {
                    stop("Restarting to apply USB name");
                    restart_at = time_us_64() + 750000;
                }
            }
        }
    } else if (matches(cmd, "play_song")) {
        sample();
        if (s.mode != STOPPED || restart_at || alarm)
            error = "Stop motor; alarms take priority over songs";
        else
            error = load_song(string(c, "hex"));
    } else if (matches(cmd, "stop_song")) {
        song_length = 0;
        test_until = 0;
    } else if (matches(cmd, "beep")) {
        song_length = 0;
        test_until = time_us_64() + 600000;
    } else if (matches(cmd, "test_pulses")) {
        double pulses, hz;
        if (s.mode != STOPPED || restart_at)
            error = "Stop motor before pulse test";
        else if (!number(c, "pulses", &pulses) || pulses < 1 || pulses > MAX_MOVE_STEPS ||
                 floor(pulses) != pulses || !number(c, "pulse_hz", &hz))
            error = "Invalid pulse test";
        else if (!motor_test((uint32_t)pulses, hz))
            error = "Pulse test rate must be 1-200000 Hz";
        else {
            stop_reason[0] = 0;
            diagnostic_pulses = diagnostic_elapsed = 0;
            previous_mode = DIAGNOSTIC;
        }
    } else if (matches(cmd, "bootloader")) {
        stop("Entering bootloader");
        usb_boot_requested = true;
    } else
        error = "Unknown command";
    ack(id, error);
    cJSON_Delete(c);
}
static void protect(uint64_t now, motor_status_t s) {
#ifdef MOTOR_BOARD_DRV8452
    nema23_poll(now, temperature_valid, temperature);
    if (nema23_status().phase == POWER_LATCHED) {
        stop(nema23_status().fault);
        return;
    }
    if (!tud_cdc_connected() && nema23_status().phase != POWER_OFF) {
        nema23_disarm();
        stop("USB disconnected");
        return;
    }
    if (s.mode != STOPPED && heartbeat_seen && now - heartbeat_us > HEARTBEAT_US)
        nema23_disarm();
#endif
    if (s.mode != STOPPED) {
        if (!temperature_valid)
            stop("Temperature sensor error");
        else if (temperature >= SHUTDOWN_C)
            stop("Thermal shutdown");
        else if (driver_fault())
            stop("Driver fault");
        else if (!heartbeat_seen || now - heartbeat_us > HEARTBEAT_US)
            stop("Browser heartbeat lost");
        else if (!tud_cdc_connected())
            stop("USB disconnected");
    }
}
static void safe_gpio(void) {
#ifdef MOTOR_BOARD_DRV8452
    const int pins[] = {0, 6, 8, 10, 11, 25, 3, 17, 18, 19, 20, 21, 22};
    const int inputs[] = {1, 2, 7, 23, 24};
#else
    const int pins[] = {22, 21, 18, 19, 20, 4, 5, 6, 25, 2, 3, 16};
    const int inputs[] = {23};
#endif
    for (size_t i = 0; i < sizeof(pins) / sizeof(pins[0]); ++i) {
        gpio_init(pins[i]);
        gpio_put(pins[i],
#ifdef MOTOR_BOARD_DRV8452
                 pins[i] == 17
#else
                 pins[i] == 21
#endif
        );
        gpio_set_dir(pins[i], GPIO_OUT);
    }
    for (size_t i = 0; i < sizeof(inputs) / sizeof(inputs[0]); ++i) {
        gpio_init(inputs[i]);
        gpio_set_dir(inputs[i], GPIO_IN);
    }
}
int main(void) {
    safe_gpio();
    /* BOOTSEL leaves USB attached. Hold reset long enough for the host to
       observe a real disconnect before TinyUSB enumerates the CDC device. */
    reset_block(RESETS_RESET_USBCTRL_BITS);
    sleep_ms(250);
    boot_us = time_us_64();
    settings_load();
    pico_get_unique_board_id_string(device_id, sizeof(device_id));
    for (size_t i = 0; device_id[i]; ++i)
        if (device_id[i] >= 'A' && device_id[i] <= 'F')
            device_id[i] += 'a' - 'A';
    snprintf(session, sizeof(session), "%s-%08lx", device_id, (unsigned long)get_rand_32());
#ifdef MOTOR_BOARD_DRV8452
    nema23_init();
#endif
    motor_init();
    if (!settings_has_saved())
        settings_save(&settings);
    current_capture_init();
    i2c_init(i2c1, 100000);
    gpio_set_function(26, GPIO_FUNC_I2C);
    gpio_set_function(27, GPIO_FUNC_I2C);
    gpio_pull_up(26);
    gpio_pull_up(27);
    sensor_initialized = write_reg(1, SENSOR_CONFIG) && write_reg(2, SENSOR_LOW_C * 256) &&
                         write_reg(3, SHUTDOWN_C * 256) && sensor_verify();
    sleep_ms(300);
    sample();
    gpio_set_function(BUZZER_PIN, GPIO_FUNC_PWM);
    tusb_init();
    watchdog_enable(2000, true);
    char line[513];
    size_t length = 0;
    bool overflow = false;
    uint64_t connection_lost_at = 0, last_current_emit = 0;
    while (true) {
        tud_task();
        uint64_t now = time_us_64();
        if (usb_boot_requested) {
            stop("Entering bootloader");
            reset_usb_boot(0, 0);
        }
        if (restart_at && now >= restart_at) {
            stop("Restarting");
            watchdog_reboot(0, 0, 0);
            while (true)
                tight_loop_contents();
        }
        if (now - last_sample >= 100000)
            sample();
        motor_status_t s = motor_status();
        protect(now, s);
        if (!tud_cdc_connected()) {
            if (!connection_lost_at)
                connection_lost_at = now;
        } else
            connection_lost_at = 0;
        /* Bound input work per loop so a flooding host cannot starve protection. */
        for (int i = 0; i < 128 && tud_cdc_available(); ++i) {
            char ch = (char)tud_cdc_read_char();
            if (ch == '\n') {
                if (overflow)
                    ack(NULL, "Command exceeds 512 bytes");
                else if (length) {
                    line[length] = 0;
                    command(line);
                }
                length = 0;
                overflow = false;
            } else if (ch != '\r') {
                if (ch == '\0')
                    overflow = true;
                else if (length < 512 && !overflow)
                    line[length++] = ch;
                else
                    overflow = true;
            }
        }
        s = motor_status();
        if (previous_mode != STOPPED && s.mode == STOPPED) {
            if (!stop_reason[0])
                snprintf(stop_reason, sizeof(stop_reason), "%s",
                         previous_mode == DIAGNOSTIC ? "Pulse test complete"
                         : previous_mode == BRAKING  ? "Jog release complete"
                                                     : "Step move complete");
            if (previous_mode == DIAGNOSTIC) {
                diagnostic_pulses = s.executed_pulses;
                diagnostic_elapsed = s.elapsed_us;
            }
            status(false);
            last_emit = now;
        }
        previous_mode = s.mode;
        indicators(now, s);
        if (now - last_emit >= 250000) {
            status(s.mode != STOPPED && s.mode != PREPARED);
            last_emit = now;
        }
        if (now - last_current_emit >= 20000) {
            stream_current();
            last_current_emit = now;
        }
        watchdog_update();
        sleep_us(50);
    }
}
