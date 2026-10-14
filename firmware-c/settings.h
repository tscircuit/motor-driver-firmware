#ifndef MOTOR_SETTINGS_H
#define MOTOR_SETTINGS_H
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    char name[33];
    float threshold;
} settings_t;
extern settings_t settings;
void settings_load(void);
bool settings_has_saved(void);
bool settings_save(const settings_t *next);
bool settings_name_valid(const char *name);
#endif
