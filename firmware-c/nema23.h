#ifndef NEMA23_H
#define NEMA23_H
#include <stdbool.h>
#include <stdint.h>
#define NEMA23_DISCHARGE_US 12000000u
#define NEMA23_FULL_SCALE_A (3.3 * 10000.0 / 12610.0 / 0.66)
typedef enum {
    POWER_OFF,
    POWER_DISCHARGE,
    POWER_NEGOTIATING,
    POWER_CONTRACT,
    POWER_ENERGIZING,
    POWER_READY,
    POWER_LATCHED
} power_phase_t;
typedef struct {
    power_phase_t phase;
    int requested_voltage;
    bool request_sent, contract_valid, folded_back, spi_valid;
    uint8_t pd_address, pd_status, driver_status, current_units;
    double current_peak_a;
    const char *fault;
} nema23_status_t;
void nema23_init(void);
void nema23_poll(uint64_t now, bool sensor_valid, double temperature);
const char *nema23_request_voltage(int volts);
const char *nema23_arm(bool sensor_valid, double temperature);
const char *nema23_set_current(double peak_a);
const char *nema23_prepare_motion(void);
void nema23_disarm(void);
bool nema23_ready(void);
nema23_status_t nema23_status(void);
const char *nema23_phase_name(void);
uint8_t nema23_current_code(double peak_a);
#endif
