#ifndef TEST_PLATFORM_H
#define TEST_PLATFORM_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef unsigned int uint;
#define GPIO_IN 0
#define GPIO_OUT 1
#define GPIO_FUNC_I2C 3
#define GPIO_FUNC_PWM 4
#define i2c1 1
#define RESETS_RESET_USBCTRL_BITS 1u
void reset_block(uint32_t mask);
extern uint64_t fake_us;
extern bool fake_connected;
extern int fake_pins[32];
extern uint16_t fake_registers[4];
extern char output[32768];
extern size_t output_length;
uint64_t time_us_64(void);
void sleep_us(uint32_t us);
void sleep_ms(uint32_t ms);
int gpio_get(uint pin);
void gpio_put(uint pin, bool value);
void gpio_init(uint pin);
void gpio_set_dir(uint pin, bool value);
void gpio_set_function(uint pin, uint function);
void gpio_pull_up(uint pin);
int i2c_write_timeout_us(int bus, uint8_t address, const uint8_t *data, size_t n, bool nostop,
                         uint32_t timeout);
int i2c_read_timeout_us(int bus, uint8_t address, uint8_t *data, size_t n, bool nostop,
                        uint32_t timeout);
void i2c_init(int bus, uint32_t rate);
void adc_init(void);
void adc_gpio_init(uint pin);
void adc_select_input(uint channel);
uint16_t adc_read(void);
uint pwm_gpio_to_slice_num(uint pin);
void pwm_set_enabled(uint slice, bool enabled);
void pwm_set_clkdiv(uint slice, float divisor);
void pwm_set_wrap(uint slice, uint16_t wrap);
void pwm_set_gpio_level(uint pin, uint16_t level);
void pico_get_unique_board_id_string(char *s, size_t n);
uint32_t get_rand_32(void);
void tusb_init(void);
void tud_task(void);
bool tud_cdc_connected(void);
uint32_t tud_cdc_write_available(void);
uint32_t tud_cdc_write(const void *data, uint32_t n);
uint32_t tud_cdc_write_char(char c);
void tud_cdc_write_flush(void);
uint32_t tud_cdc_available(void);
int tud_cdc_read_char(void);
void reset_usb_boot(uint32_t a, uint32_t b);
void watchdog_enable(uint32_t ms, bool pause);
void watchdog_update(void);
void watchdog_reboot(uint32_t a, uint32_t b, uint32_t c);
void tight_loop_contents(void);
#endif
