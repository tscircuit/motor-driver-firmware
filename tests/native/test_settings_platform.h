#ifndef SETTINGS_TEST_PLATFORM_H
#define SETTINGS_TEST_PLATFORM_H
#include <stddef.h>
#include <stdint.h>
#define FLASH_SECTOR_SIZE 4096u
#define FLASH_PAGE_SIZE 256u
#define PICO_OK 0
extern uint8_t fake_flash[8192];
#define XIP_BASE ((uintptr_t)fake_flash - (2u * 1024u * 1024u - 8192u))
void flash_range_erase(uint32_t offset, size_t n);
void flash_range_program(uint32_t offset, const uint8_t *data, size_t n);
int flash_safe_execute(void (*function)(void *), void *param, uint32_t timeout);
#endif
