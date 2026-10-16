#define MOTOR_HOST_TEST
#include "../../firmware-c/settings.c"
#include <assert.h>
#include <stdio.h>
uint8_t fake_flash[8192];
static bool partial_write;
void flash_range_erase(uint32_t offset, size_t n) {
    assert(offset >= SETTINGS_BASE && offset + n <= SETTINGS_BASE + sizeof(fake_flash));
    memset(fake_flash + (offset - SETTINGS_BASE), 0xff, n);
}
void flash_range_program(uint32_t offset, const uint8_t *data, size_t n) {
    assert(offset >= SETTINGS_BASE && offset + n <= SETTINGS_BASE + sizeof(fake_flash));
    memcpy(fake_flash + (offset - SETTINGS_BASE), data, partial_write ? 16 : n);
}
int flash_safe_execute(void (*function)(void *), void *param, uint32_t timeout) {
    function(param);
    return PICO_OK;
}
static void reboot(void) {
    active = -1;
    sequence = 0;
    settings_load();
}
int main(void) {
    memset(fake_flash, 0xff, sizeof(fake_flash));
    reboot();
    assert(settings.threshold == 65);
    assert(settings.stall_beep && settings.stall_stop_ms == 0);
    /* Preserve existing names/temperature limits when upgrading old firmware. */
    legacy_record_t old = {.magic = LEGACY_SETTINGS_MAGIC, .sequence = 12,
                          .value = {.name = "Existing motor", .threshold = 63}};
    old.checksum = checksum_bytes(&old, offsetof(legacy_record_t, checksum));
    memcpy(fake_flash, &old, sizeof(old));
    reboot();
    assert(strcmp(settings.name, "Existing motor") == 0 && settings.threshold == 63);
    assert(settings.stall_beep && settings.stall_stop_ms == 0);
    settings_t first = {.name = "Gantry YY", .threshold = 64};
    assert(settings_save(&first));
    reboot();
    assert(strcmp(settings.name, "Gantry YY") == 0 && settings.threshold == 64);
    settings_t second = {.name = "Gantry XX", .threshold = 66};
    assert(settings_save(&second));
    reboot();
    assert(strcmp(settings.name, "Gantry XX") == 0 && settings.threshold == 66);
    /* Corrupt the newest record; boot falls back to the intact older sector. */
    fake_flash[active * FLASH_SECTOR_SIZE + 10] ^= 1;
    reboot();
    assert(settings.threshold == 64);
    partial_write = true;
    assert(!settings_save(&second));
    partial_write = false;
    reboot();
    assert(settings.threshold == 64 && strcmp(settings.name, "Gantry YY") == 0);
    assert(settings_save(&second));
    reboot();
    assert(settings.threshold == 66);
    second.stall_beep = true;
    second.stall_stop_ms = 1250;
    assert(settings_save(&second));
    reboot();
    assert(settings.stall_beep && settings.stall_stop_ms == 1250);
    second.stall_stop_ms = 60001;
    assert(!settings_save(&second));
    second.stall_stop_ms = 0;
    const char *invalid[] = {
        "", " leading", "trailing ", "123456789012345678901234567890123", "bad\nname", "bad\x7f"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
        assert(!settings_name_valid(invalid[i]));
    second.threshold = NAN;
    assert(!settings_save(&second));
    second.threshold = 75;
    assert(!settings_save(&second));
    puts("Native settings: redundant sectors, restart persistence, corruption fallback, "
         "partial-write recovery and validation passed");
}
