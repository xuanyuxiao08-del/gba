#pragma once
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#define VROM_BLOCK_SIZE     (32 * 1024)
#define VROM_BLOCK_SHIFT    15
#define VROM_BLOCK_MASK     (VROM_BLOCK_SIZE - 1)
#define VROM_CACHE_BLOCKS   128  // 128 * 32 KB = 4 MB PSRAM cache

esp_err_t vrom_init(const char *filepath);
void vrom_close(void);

// Fast inlined fetch for 8-bit, 16-bit, and 32-bit cartridge reads
uint8_t  vrom_read8(uint32_t addr);
uint16_t vrom_read16(uint32_t addr);
uint32_t vrom_read32(uint32_t addr);

// Direct pointer access if emulator core handles sequential block reads
const uint8_t *vrom_get_ptr(uint32_t addr);