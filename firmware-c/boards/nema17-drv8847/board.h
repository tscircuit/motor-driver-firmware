#ifndef DRV8847_BOARD_H
#define DRV8847_BOARD_H
/* Second ordered NEMA17 generation. Exact published PCB version is unknown. */
#define BOARD_NAME "RP2040 / NEMA17 / DRV8847"
#define BOARD_ID "rp2040_drv8847"
#define DRIVER_NAME "DRV8847"
#define BRIDGE_BASE_PIN 18
#define BRIDGE_MASK (15u << BRIDGE_BASE_PIN)
#define ENABLE_PIN 22
#define FAULT_PIN 23
#define ALERT_PIN 24
#define LED_PIN 25
#define BUZZER_PIN 16
#define START_BELOW_C 60
#define SHUTDOWN_C 75
#define SENSOR_LOW_C 60
/* Firmware throughput bound, not a mechanical speed guarantee. */
#define MAX_COIL_STEP_HZ 20000u
#endif
