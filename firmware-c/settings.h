#ifndef MOTOR_SETTINGS_H
#define MOTOR_SETTINGS_H
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    char name[33];
    float threshold;
    uint32_t stall_stop_ms; /* zero disables automatic stopping */
    bool stall_beep;
    uint32_t stall_sensitivity; /* 0-100; higher triggers at a lower score */
    bool bounds_saved;
    int64_t travel_max; /* microsteps, relative to homed zero */
} settings_t;
extern settings_t settings;
void settings_load(void);
bool settings_has_saved(void);
bool settings_save(const settings_t *next);
bool settings_name_valid(const char *name);
#endif
