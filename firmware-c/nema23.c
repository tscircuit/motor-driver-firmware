#include "nema23.h"
#ifdef NEMA23_HOST_TEST
#include "nema23_test_platform.h"
#else
#include "hardware/i2c.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"
#endif
#include <math.h>
static nema23_status_t state;
static uint64_t deadline, last_contract, last_spi, isolated_at;
static int last_sent_voltage;
static bool saw_pg_high, require_pg_edge, clear_on_arm;
static uint8_t expected_current;
static bool pg_missing(void) { return !state.ignore_pd_power_good && gpio_get(1); }
static bool fuse_fault(void) { return !state.ignore_fuse_power_fault && !gpio_get(2); }
static void isolate(void) {
    gpio_put(22, 0);
    gpio_put(6, 0);
    gpio_put(0, 0);
    state.spi_valid = false;
    isolated_at = time_us_64();
}
static void latch(const char *reason) {
    isolate();
    gpio_put(8, 0);
    state.phase = POWER_LATCHED;
    state.fault = reason;
    state.contract_valid = false;
}
static bool read_pd_at(uint8_t address, uint8_t reg, uint8_t *value) {
    return i2c_write_timeout_us(i2c0, address, &reg, 1, true, 2000) == 1 &&
           i2c_read_timeout_us(i2c0, address, value, 1, false, 2000) == 1;
}
static bool pd_contract(void) {
    state.contract_valid = false;
    /* PG can stay deasserted until a software voltage request. Only probe
       following an explicit request; require PD status/current and actual
       eFuse/driver checks before allowing motion. Strict PG is optional. */
    if (pg_missing()) {
        gpio_put(8, 0);
        return false;
    }
    gpio_put(8, 1);
    uint8_t status, current;
    if (!state.pd_address) {
        if (read_pd_at(0x22, 0x09, &status))
            state.pd_address = 0x22;
        else if (read_pd_at(0x23, 0x09, &status))
            state.pd_address = 0x23;
    }
    if (!state.pd_address || !read_pd_at(state.pd_address, 0x09, &status) ||
        !read_pd_at(state.pd_address, 0x50, &current)) {
        gpio_put(8, 0);
        state.pd_address = 0;
        return false;
    }
    state.pd_status = status;
    state.current_units = current;
    /* Refuse BC/QC fallback; current is meaningful only for a PD contract.
       A 3 A contract covers the nominal 2.46 A eFuse input limit with margin. */
    state.contract_valid =
        (status & 0x08) && (state.requested_voltage != 28 || (status & 0x10)) && current >= 60;
    return state.contract_valid;
}
static bool spi_read(uint8_t reg, uint8_t *value) {
    uint8_t tx[2] = {(uint8_t)(0x40 | reg), 0}, rx[2];
    gpio_put(17, 0);
    int n = spi_write_read_blocking(spi0, tx, rx, 2);
    gpio_put(17, 1);
    sleep_us(1);
    state.driver_status = rx[0] & 0x3f;
    if (n != 2 || (rx[0] & 0xc0) != 0xc0)
        return false;
    *value = rx[1];
    return true;
}
static bool spi_write_verify(uint8_t reg, uint8_t value) {
    uint8_t tx[2] = {reg, value}, rx[2], actual;
    gpio_put(17, 0);
    int n = spi_write_read_blocking(spi0, tx, rx, 2);
    gpio_put(17, 1);
    sleep_us(1);
    return n == 2 && (rx[0] & 0xc0) == 0xc0 && spi_read(reg, &actual) && actual == value;
}
uint8_t nema23_current_code(double peak_a) {
    double steps = floor(peak_a / NEMA23_FULL_SCALE_A * 256);
    if (steps < 1)
        steps = 1;
    if (steps > 256)
        steps = 256;
    return (uint8_t)(steps - 1);
}
static bool verify_driver(void) {
    const uint8_t regs[] = {0x04, 0x05, 0x06, 0x07, 0x0c, 0x0e, 0x10};
    const uint8_t values[] = {0x8f, 0x06, 0x38, 0x4d, 0x10, expected_current, 0x10};
    for (unsigned i = 0; i < sizeof(regs); ++i) {
        uint8_t v;
        if (!spi_read(regs[i], &v) || v != values[i])
            return false;
    }
    uint8_t faults;
    return spi_read(0, &faults) && !faults && gpio_get(23);
}
const char *nema23_prepare_motion(void) {
    if (!nema23_ready())
        return "Motor power is not armed";
    gpio_put(22, 0);
    gpio_put(6, 1);
    sleep_us(1500);
    if (clear_on_arm) {
        /* Clear power-up/previous latched diagnostics only on an explicit arm,
           never as part of an ordinary motion start. CLR_FLT self-clears. */
        uint8_t tx[2] = {0x06, 0xb8}, rx[2], actual;
        gpio_put(17, 0);
        int n = spi_write_read_blocking(spi0, tx, rx, 2);
        gpio_put(17, 1);
        sleep_us(1);
        if (n != 2 || (rx[0] & 0xc0) != 0xc0 || !spi_read(0x06, &actual) || actual != 0x38) {
            latch("DRV8452 explicit fault clear failed");
            return state.fault;
        }
        clear_on_arm = false;
    }
    /* SPI mode 1. External VREF divider, fixed 1/16 rising-edge STEP,
       smart-tune ripple decay, latched OCP/OTSD, no automatic interpolation,
       torque automation or untuned sensorless stall detection. */
    expected_current = nema23_current_code(state.current_peak_a * (state.folded_back ? 0.5 : 1));
    const uint8_t regs[] = {0x06, 0x04, 0x05, 0x07, 0x0c, 0x0d, 0x0e, 0x0f, 0x10};
    const uint8_t values[] = {0x38, 0x0f, 0x06, 0x4d, 0x10, 0x00, expected_current, 0x20, 0x10};
    for (unsigned i = 0; i < sizeof(regs); ++i) {
        if (!spi_write_verify(regs[i], values[i])) {
            latch("DRV8452 SPI configuration failed");
            return state.fault;
        }
    }
    /* EN_OUT alone cannot energize coils: external ARM remains low. */
    if (!spi_write_verify(0x04, 0x8f) || !verify_driver()) {
        latch("DRV8452 fault or SPI readback failed");
        return state.fault;
    }
    state.spi_valid = true;
    last_spi = time_us_64();
    return NULL;
}
void nema23_init(void) {
    state = (nema23_status_t){.phase = POWER_OFF,
                              .requested_voltage = 5,
                              .current_peak_a = 1.0,
                              .ignore_pd_power_good = true};
    last_sent_voltage = 0;
    last_contract = last_spi = 0;
    isolate();
    gpio_put(8, 0);
    i2c_init(i2c0, 100000);
    gpio_set_function(4, GPIO_FUNC_I2C);
    gpio_set_function(5, GPIO_FUNC_I2C);
    gpio_pull_up(4);
    gpio_pull_up(5);
    spi_init(spi0, 1000000);
    spi_set_format(spi0, 8, SPI_CPOL_0, SPI_CPHA_1, SPI_MSB_FIRST);
    gpio_set_function(16, GPIO_FUNC_SPI);
    gpio_set_function(18, GPIO_FUNC_SPI);
    gpio_set_function(19, GPIO_FUNC_SPI);
    gpio_put(17, 1);
}
const char *nema23_request_voltage(int volts) {
    if (volts != 5 && volts != 9 && volts != 12 && volts != 15 && volts != 20 && volts != 28)
        return "Choose 5, 9, 12, 15, 20 or 28 V";
    isolate();
    gpio_put(8, 0);
    state.requested_voltage = volts;
    state.request_sent = false;
    state.contract_valid = false;
    state.phase = POWER_DISCHARGE;
    state.fault = NULL;
    deadline = time_us_64() + NEMA23_DISCHARGE_US;
    return NULL;
}
void nema23_set_pg_override(bool enabled) {
    nema23_disarm();
    state.ignore_pd_power_good = enabled;
}
void nema23_set_fuse_fault_override(bool enabled) {
    nema23_disarm();
    state.ignore_fuse_power_fault = enabled;
}
void nema23_disarm(void) {
    isolate();
    gpio_put(8, 0);
    state.phase = POWER_OFF;
    state.contract_valid = false;
}
const char *nema23_set_current(double peak_a) {
    if (!isfinite(peak_a) || peak_a < 0.1 || peak_a > NEMA23_FULL_SCALE_A)
        return "Peak winding current must be 0.1-3.965 A";
    /* Changes require a new explicit arm and never energize coils. */
    nema23_disarm();
    state.current_peak_a = peak_a;
    state.folded_back = false;
    if (state.request_sent && state.requested_voltage >= 9)
        state.phase = POWER_CONTRACT;
    return NULL;
}
const char *nema23_arm(bool valid, double temperature) {
    if (!valid || temperature >= 55)
        return "Arming requires a valid temperature below 55 C";
    if (!state.request_sent || state.requested_voltage < 9 || state.phase == POWER_DISCHARGE ||
        state.phase == POWER_NEGOTIATING)
        return "Request motor voltage and wait for a verified contract";
    if (time_us_64() - isolated_at < NEMA23_DISCHARGE_US)
        return "Wait for motor rail discharge before rearming";
    if (!pd_contract())
        return "PD power-good and a sufficient PD/EPR current contract are required";
    state.fault = NULL;
    state.folded_back = false;
    state.spi_valid = false;
    clear_on_arm = true;
    gpio_put(0, 1);
    state.phase = POWER_ENERGIZING;
    deadline = time_us_64() + 1000000;
    return NULL;
}
bool nema23_ready(void) {
    return state.phase == POWER_READY && state.contract_valid && !pg_missing() && !fuse_fault() &&
           gpio_get(24);
}
nema23_status_t nema23_status(void) { return state; }
const char *nema23_phase_name(void) {
    const char *names[] = {"Motor rail off",
                           "Discharging motor rail (12 s)",
                           "Negotiating PD contract",
                           "Ready to arm motor power",
                           "Waiting for protected rail",
                           "Motor power armed",
                           "Fault latched: re-arm required"};
    return names[state.phase];
}
void nema23_poll(uint64_t now, bool valid, double temperature) {
    if (pg_missing())
        gpio_put(8, 0);
    if (state.phase == POWER_DISCHARGE && now >= deadline) {
        /* In optional strict mode, wait for hardware PG before probing. */
        if (pg_missing())
            return;
        state.pd_address = 0;
        if (!pd_contract() && !state.pd_address) {
            latch("CH224A is not responding");
            return;
        }
        int volts = state.requested_voltage;
        uint8_t code = volts == 5    ? 0
                       : volts == 9  ? 1
                       : volts == 12 ? 2
                       : volts == 15 ? 3
                       : volts == 20 ? 4
                                     : 5;
        uint8_t tx[2] = {0x0a, code};
        require_pg_edge = last_sent_voltage != volts;
        saw_pg_high = false;
        if (i2c_write_timeout_us(i2c0, state.pd_address, tx, 2, false, 2000) != 2) {
            latch("PD voltage request failed");
            return;
        }
        last_sent_voltage = volts;
        state.request_sent = true;
        state.contract_valid = false;
        state.phase = POWER_NEGOTIATING;
        deadline = now + 5000000;
        last_contract = 0;
    }
    if (state.phase == POWER_NEGOTIATING) {
        if (pg_missing())
            saw_pg_high = true;
        if (now >= deadline) {
            latch("PD negotiation was not verified");
            return;
        }
        if ((!require_pg_edge || saw_pg_high || state.ignore_pd_power_good) && !pg_missing() &&
            now + 3000000 >= deadline && now - last_contract >= 100000) {
            last_contract = now;
            if (state.requested_voltage == 5) {
                uint8_t value;
                gpio_put(8, 1);
                if (read_pd_at(state.pd_address, 0x09, &value)) {
                    state.phase = POWER_OFF;
                    gpio_put(8, 0);
                }
            } else if (pd_contract())
                state.phase = POWER_CONTRACT;
        }
    }
    bool armed = state.phase == POWER_READY || state.phase == POWER_ENERGIZING;
    if (armed) {
        if (!valid || temperature >= 70) {
            latch(valid ? "70 C thermal shutdown" : "Temperature sensor unavailable");
            return;
        }
        if (pg_missing() || (state.phase == POWER_READY && fuse_fault())) {
            latch("PD power loss or eFuse fault");
            return;
        }
        if (state.phase == POWER_READY && (!gpio_get(24) || !gpio_get(23))) {
            latch("Driver or hardware interlock fault");
            return;
        }
        if (now - last_contract >= 100000) {
            last_contract = now;
            if (!pd_contract()) {
                latch("PD contract lost or insufficient current");
                return;
            }
        }
    }
    if (state.phase == POWER_ENERGIZING) {
        /* The off-state fault pin may stay low until POWER_ARM is raised.
           Check it after the protected rail comes up, or at startup timeout. */
        if (gpio_get(24) && !fuse_fault()) {
            state.phase = POWER_READY;
            if (!nema23_prepare_motion()) {
                gpio_put(22, 0);
                gpio_put(6, 0);
                state.spi_valid = false;
            }
        } else if (now >= deadline)
            latch(fuse_fault() ? "eFuse fault remains asserted after enable"
                               : "Protected rail failed power-good check");
    }
    if (state.phase == POWER_READY && gpio_get(6)) {
        if (temperature >= 60 && !state.folded_back) {
            state.folded_back = true;
            expected_current = nema23_current_code(state.current_peak_a * 0.5);
            if (!spi_write_verify(0x0e, expected_current)) {
                latch("Thermal current foldback failed");
                return;
            }
        }
        if (now - last_spi >= 100000) {
            last_spi = now;
            if (!verify_driver())
                latch("Driver fault or SPI configuration changed");
        }
    }
}
