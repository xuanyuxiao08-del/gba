#include "vrom.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "GBA_VROM";

typedef struct {
    uint32_t block_index;
    uint32_t lru_counter;
} CacheSlotMeta;

static FILE *rom_file = NULL;
static size_t rom_total_size = 0;
static uint32_t total_blocks = 0;
static uint32_t global_lru = 0;

static uint8_t *cache_data_pool = NULL;
static CacheSlotMeta slot_meta[VROM_CACHE_BLOCKS];
static uint8_t *lookup_table[1024]; // Up to 32 MB ROM capacity

esp_err_t vrom_init(const char *filepath) {
    rom_file = fopen(filepath, "rb");
    if (!rom_file) {
        ESP_LOGE(TAG, "Failed to open ROM for paging: %s", filepath);
        return ESP_FAIL;
    }

    struct stat st;
    stat(filepath, &st);
    rom_total_size = st.st_size;
    total_blocks = (rom_total_size + VROM_BLOCK_SIZE - 1) / VROM_BLOCK_SIZE;

    ESP_LOGI(TAG, "Virtual ROM: %s | Size: %.2f MB | Blocks: %u",
             filepath, (float)rom_total_size / (1024.0f * 1024.0f), (unsigned int)total_blocks);

    // Allocate 4 MB contiguous block pool in PSRAM
    cache_data_pool = (uint8_t *)heap_caps_malloc(VROM_CACHE_BLOCKS * VROM_BLOCK_SIZE, MALLOC_CAP_SPIRAM);
    if (!cache_data_pool) {
        ESP_LOGE(TAG, "Failed to allocate 4MB PSRAM cache pool!");
        fclose(rom_file);
        return ESP_ERR_NO_MEM;
    }

    // Reset tables
    memset(lookup_table, 0, sizeof(lookup_table));
    for (int i = 0; i < VROM_CACHE_BLOCKS; i++) {
        slot_meta[i].block_index = 0xFFFFFFFF;
        slot_meta[i].lru_counter = 0;
    }

    // Pre-cache the first 16 blocks (first 512 KB: Cartridge Header, Interrupt Vectors, Engine Init)
    uint32_t prefetch_blocks = (total_blocks < 16) ? total_blocks : 16;
    ESP_LOGI(TAG, "Prefetching initial %u blocks into cache...", (unsigned int)prefetch_blocks);
    for (uint32_t b = 0; b < prefetch_blocks; b++) {
        vrom_get_ptr(b << VROM_BLOCK_SHIFT);
    }

    return ESP_OK;
}

void vrom_close(void) {
    if (rom_file) {
        fclose(rom_file);
        rom_file = NULL;
    }
    if (cache_data_pool) {
        free(cache_data_pool);
        cache_data_pool = NULL;
    }
}

// Fault handler: pulls missing 32 KB block from MicroSD into the oldest PSRAM slot
static uint8_t *vrom_load_block(uint32_t block_idx) {
    if (block_idx >= total_blocks) {
        return cache_data_pool; // Bounds fallthrough to safe memory
    }

    // 1. Find the Least Recently Used (LRU) slot
    int victim_slot = 0;
    uint32_t min_lru = slot_meta[0].lru_counter;
    for (int i = 1; i < VROM_CACHE_BLOCKS; i++) {
        if (slot_meta[i].block_index == 0xFFFFFFFF) {
            victim_slot = i;
            break;
        }
        if (slot_meta[i].lru_counter < min_lru) {
            min_lru = slot_meta[i].lru_counter;
            victim_slot = i;
        }
    }

    // 2. Invalidate previous owner in lookup table
    uint32_t old_block = slot_meta[victim_slot].block_index;
    if (old_block != 0xFFFFFFFF && old_block < total_blocks) {
        lookup_table[old_block] = NULL;
    }

    // 3. Read 32 KB from SD card directly into target slot
    uint8_t *dest = cache_data_pool + (victim_slot * VROM_BLOCK_SIZE);
    fseek(rom_file, block_idx * VROM_BLOCK_SIZE, SEEK_SET);
    fread(dest, 1, VROM_BLOCK_SIZE, rom_file);

    // 4. Update tracking
    slot_meta[victim_slot].block_index = block_idx;
    slot_meta[victim_slot].lru_counter = ++global_lru;
    lookup_table[block_idx] = dest;

    return dest;
}

const uint8_t *vrom_get_ptr(uint32_t addr) {
    addr &= 0x01FFFFFF; // Mask off Cartridge Base 0x08000000
    uint32_t block = addr >> VROM_BLOCK_SHIFT;
    uint32_t offset = addr & VROM_BLOCK_MASK;

    uint8_t *base = lookup_table[block];
    if (__builtin_expect((base == NULL), 0)) {
        base = vrom_load_block(block);
    }
    return base + offset;
}

uint8_t vrom_read8(uint32_t addr) {
    return *vrom_get_ptr(addr);
}

uint16_t vrom_read16(uint32_t addr) {
    const uint8_t *ptr = vrom_get_ptr(addr);
    return *(const uint16_t *)ptr;
}

uint32_t vrom_read32(uint32_t addr) {
    const uint8_t *ptr = vrom_get_ptr(addr);
    return *(const uint32_t *)ptr;
}