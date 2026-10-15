#ifndef MOTOR_BOARD_H
#define MOTOR_BOARD_H
#ifdef MOTOR_BOARD_DRV8452
#define BOARD_NAME "RP2040 / NEMA23 / DRV8452"
#define BOARD_ID "rp2040_drv8452_nema23"
#define DRIVER_NAME "DRV8452"
#define STEP_PIN 20
#define DIR_PIN 21
#define WAKE_PIN 6
#define ARM_PIN 22
#define LED_RED_PIN 25
#define LED_GREEN_PIN 10
#define LED_BLUE_PIN 11
#define BUZZER_PIN 3
#define CURRENT_VOLTS_PER_AMP 0.2
#define START_BELOW_C 55
#define SHUTDOWN_C 70
#define SENSOR_LOW_C 55
#else
#define BOARD_NAME "RP2040 / DRV8825 / RGB"
#define BOARD_ID "rp2040_drv8825"
#define DRIVER_NAME "DRV8825"
#define STEP_PIN 18
#define DIR_PIN 19
#define WAKE_PIN 22
#define ARM_PIN 21
#define LED_RED_PIN 25
#define LED_GREEN_PIN 2
#define LED_BLUE_PIN 3
#define BUZZER_PIN 16
#define CURRENT_VOLTS_PER_AMP 1.0
#define START_BELOW_C 60
#define SHUTDOWN_C 75
#define SENSOR_LOW_C 60
#endif
#endif
