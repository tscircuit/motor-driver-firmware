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
int gpio_get(uint pin) {
#ifdef MOTOR_BOARD_DRV8452
    if (pin == 24)
        return fake_pins[pin] && fake_pins[0];
#endif
    return fake_pins[pin];
}
void gpio_put(uint pin, bool value) { fake_pins[pin] = value; }
void gpio_init(uint pin) {}
void gpio_set_dir(uint pin, bool value) {}
void gpio_set_function(uint pin, uint function) {}
void gpio_pull_up(uint pin) {}
int i2c_write_timeout_us(int bus, uint8_t address, const uint8_t *data, size_t n, bool nostop,
                         uint32_t timeout) {
#ifdef MOTOR_BOARD_DRV8452
    if (bus == i2c0)
        return fake_pd_write(address, data, n);
#endif
    selected_register = data[0];
    if (n == 3)
        fake_registers[data[0]] = (data[1] << 8) | data[2];
    return n;
}
int i2c_read_timeout_us(int bus, uint8_t address, uint8_t *data, size_t n, bool nostop,
                        uint32_t timeout) {
#ifdef MOTOR_BOARD_DRV8452
    if (bus == i2c0)
        return fake_pd_read(address, data, n);
#endif
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
#ifdef MOTOR_BOARD_DRV8452
    gpio_put(22, 0);
    gpio_put(6, 0);
#endif
}
bool motor_start(int factor, int direction, double speed, double acceleration, uint32_t steps,
                 bool fixed, bool defer, bool jogging) {
#ifdef MOTOR_BOARD_DRV8452
    assert(nema23_status().spi_valid && fake_pins[6] && fake_pins[0]);
    gpio_put(22, 1);
#endif
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
#ifdef MOTOR_BOARD_DRV8452
    gpio_put(22, 0);
    gpio_put(6, 0);
#endif
    return true;
}
motor_status_t motor_status(void) { return fake_motor; }
