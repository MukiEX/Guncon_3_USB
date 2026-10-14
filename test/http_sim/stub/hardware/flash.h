#pragma once
#include <stdint.h>
#define FLASH_SECTOR_SIZE 4096
#define FLASH_PAGE_SIZE 256
#define PICO_FLASH_SIZE_BYTES 4096
extern uint8_t fake_flash[4096];
#define XIP_BASE ((uintptr_t)fake_flash)
void flash_range_erase(uint32_t o, uint32_t n);
void flash_range_program(uint32_t o, const uint8_t *d, uint32_t n);
