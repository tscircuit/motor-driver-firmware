#include "settings.h"
#include "board.h"
#include <math.h>
#include <string.h>
#ifdef MOTOR_HOST_TEST
#include "test_settings_platform.h"
#else
#include "hardware/flash.h"
#include "hardware/regs/addressmap.h"
#include "pico/flash.h"
#include "pico/stdlib.h"
#endif
#define SETTINGS_BASE (2u * 1024u * 1024u - 2u * FLASH_SECTOR_SIZE)
#define SETTINGS_MAGIC 0x4d435431u
#ifndef MOTOR_INITIAL_NAME
#define MOTOR_INITIAL_NAME "Motor bench"
#endif
#ifndef MOTOR_INITIAL_THRESHOLD
#define MOTOR_INITIAL_THRESHOLD 65
#endif

typedef struct {
    uint32_t magic, sequence;
    settings_t value;
    uint32_t checksum;
} record_t;
_Static_assert(sizeof(record_t) <= FLASH_PAGE_SIZE, "Settings record exceeds one flash page");
settings_t settings;
static uint32_t sequence;
static int active = -1;
static uint8_t page[FLASH_PAGE_SIZE];
static uint32_t destination;
static uint32_t checksum(const record_t *r) {
    const uint8_t *bytes = (const uint8_t *)r;
    uint32_t value = 2166136261u;
    for (size_t i = 0; i < offsetof(record_t, checksum); ++i)
        value = (value ^ bytes[i]) * 16777619u;
    return value;
}
bool settings_name_valid(const char *name) {
    size_t n = strlen(name);
    if (!n || n > 32 || name[0] == ' ' || name[n - 1] == ' ')
        return false;
    for (size_t i = 0; i < n; ++i)
        if ((unsigned char)name[i] < 32 || (unsigned char)name[i] > 126)
            return false;
    return true;
}
static bool valid(const record_t *r) {
    return r->magic == SETTINGS_MAGIC && memchr(r->value.name, 0, sizeof(r->value.name)) &&
           settings_name_valid(r->value.name) && isfinite(r->value.threshold) &&
           r->value.threshold >= 10 && r->value.threshold <= SHUTDOWN_C - 1 &&
           r->checksum == checksum(r);
}
void settings_load(void) {
    settings = (settings_t){.name = MOTOR_INITIAL_NAME, .threshold = MOTOR_INITIAL_THRESHOLD};
    for (int i = 0; i < 2; ++i) {
        const record_t *r = (const record_t *)(XIP_BASE + SETTINGS_BASE + i * FLASH_SECTOR_SIZE);
        if (valid(r) && (active < 0 || (int32_t)(r->sequence - sequence) > 0)) {
            settings = r->value;
            sequence = r->sequence;
            active = i;
        }
    }
}
bool settings_has_saved(void) { return active >= 0; }
static void write_flash(void *unused) {
    flash_range_erase(destination, FLASH_SECTOR_SIZE);
    flash_range_program(destination, page, FLASH_PAGE_SIZE);
}
bool settings_save(const settings_t *next) {
    if (!settings_name_valid(next->name) || !isfinite(next->threshold) || next->threshold < 10 ||
        next->threshold > SHUTDOWN_C - 1)
        return false;
    record_t r = {.magic = SETTINGS_MAGIC, .sequence = sequence + 1, .value = *next};
    r.checksum = checksum(&r);
    memset(page, 0xff, sizeof(page));
    memcpy(page, &r, sizeof(r));
    int target = active == 0 ? 1 : 0;
    destination = SETTINGS_BASE + target * FLASH_SECTOR_SIZE;
    if (flash_safe_execute(write_flash, NULL, 1000) != PICO_OK)
        return false;
    const record_t *written = (const record_t *)(XIP_BASE + destination);
    if (!valid(written))
        return false;
    settings = *next;
    sequence = r.sequence;
    active = target;
    return true;
}
