#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include <string.h>

#define PIN_NUM_CS    10
#define PIN_NUM_RST   16
#define PIN_NUM_DC    15
#define PIN_NUM_MOSI  38
#define PIN_NUM_CLK   39
#define PIN_NUM_MISO  -1

#define LCD_WIDTH     320
#define LCD_HEIGHT    240

static spi_device_handle_t spi;

static void display_cmd(uint8_t cmd) {
    gpio_set_level(PIN_NUM_DC, 0);
    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &cmd,
    };
    spi_device_polling_transmit(spi, &t);
}

static void display_data(const uint8_t *data, int len) {
    if (len == 0) return;
    gpio_set_level(PIN_NUM_DC, 1);
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = data,
    };
    spi_device_polling_transmit(spi, &t);
}

void display_init(void) {
    gpio_set_direction(PIN_NUM_DC, GPIO_MODE_OUTPUT);
    gpio_set_direction(PIN_NUM_RST, GPIO_MODE_OUTPUT);
    gpio_set_direction(PIN_NUM_CS, GPIO_MODE_OUTPUT);

    gpio_set_level(PIN_NUM_CS, 1);
    gpio_set_level(PIN_NUM_DC, 1);

    // Hardware Reset
    gpio_set_level(PIN_NUM_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(PIN_NUM_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    spi_bus_config_t buscfg = {
        .miso_io_num = PIN_NUM_MISO,
        .mosi_io_num = PIN_NUM_MOSI,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_WIDTH * 20 * sizeof(uint16_t)
    };
    spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 26 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = PIN_NUM_CS,
        .queue_size = 7
    };
    spi_bus_add_device(SPI2_HOST, &devcfg, &spi);

    // ILI9341 Initialization
    display_cmd(0x01); // Software Reset
    vTaskDelay(pdMS_TO_TICKS(100));

    display_cmd(0x28); // Display OFF

    display_cmd(0x3A); // Interface Pixel Format: 16-bit
    uint8_t pix_fmt = 0x55;
    display_data(&pix_fmt, 1);

    display_cmd(0x36); // Memory Access Control: Landscape (MV=1, MX=0, MY=0, BGR=1)
    uint8_t madctl = 0x20; 
    display_data(&madctl, 1);

    display_cmd(0x11); // Sleep Out
    vTaskDelay(pdMS_TO_TICKS(120));

    display_cmd(0x29); // Display ON
    vTaskDelay(pdMS_TO_TICKS(20));
}

void display_send_frame(const uint16_t *framebuffer) {
    // Column address set (0 to 319)
    display_cmd(0x2A);
    uint8_t col_data[] = { 0, 0, (319 >> 8) & 0xFF, 319 & 0xFF };
    display_data(col_data, 4);

    // Page address set (0 to 239)
    display_cmd(0x2B);
    uint8_t row_data[] = { 0, 0, (239 >> 8) & 0xFF, 239 & 0xFF };
    display_data(row_data, 4);

    display_cmd(0x2C); // Write to RAM
    gpio_set_level(PIN_NUM_DC, 1);

    const int lines_per_chunk = 20;
    const size_t chunk_pixels = LCD_WIDTH * lines_per_chunk;
    const size_t total_chunks = LCD_HEIGHT / lines_per_chunk;

    for (size_t i = 0; i < total_chunks; i++) {
        spi_transaction_t t = {
            .length = chunk_pixels * 16,
            .tx_buffer = framebuffer + (i * chunk_pixels)
        };
        spi_device_transmit(spi, &t);
    }
}

// Send native 240x160 GBA frame centered on the 320x240 LCD (40px border on all sides)
void display_send_gba_frame(const uint16_t *gba_fb) {
    const uint16_t x_start = 40;
    const uint16_t x_end   = 40 + 240 - 1; // 279
    const uint16_t y_start = 40;
    const uint16_t y_end   = 40 + 160 - 1; // 199

    // Set active drawing window strictly to the 240x160 centered box
    display_cmd(0x2A);
    uint8_t col_data[] = { (x_start >> 8) & 0xFF, x_start & 0xFF, (x_end >> 8) & 0xFF, x_end & 0xFF };
    display_data(col_data, 4);

    display_cmd(0x2B);
    uint8_t row_data[] = { (y_start >> 8) & 0xFF, y_start & 0xFF, (y_end >> 8) & 0xFF, y_end & 0xFF };
    display_data(row_data, 4);

    display_cmd(0x2C); // RAM write
    gpio_set_level(PIN_NUM_DC, 1);

    // Push 160 scanlines in 16-line slices
    const int lines_per_chunk = 16;
    const size_t chunk_pixels = 240 * lines_per_chunk;
    const size_t total_chunks = 160 / lines_per_chunk;

    for (size_t i = 0; i < total_chunks; i++) {
        spi_transaction_t t = {
            .length = chunk_pixels * 16,
            .tx_buffer = gba_fb + (i * chunk_pixels)
        };
        spi_device_transmit(spi, &t);
    }
}
void display_clear_black(void) {
    uint16_t *line = (uint16_t *)heap_caps_malloc(320 * 20 * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!line) return;
    memset(line, 0, 320 * 20 * sizeof(uint16_t));

    display_cmd(0x2A);
    uint8_t col_data[] = { 0, 0, (319 >> 8) & 0xFF, 319 & 0xFF };
    display_data(col_data, 4);

    display_cmd(0x2B);
    uint8_t row_data[] = { 0, 0, (239 >> 8) & 0xFF, 239 & 0xFF };
    display_data(row_data, 4);

    display_cmd(0x2C);
    gpio_set_level(PIN_NUM_DC, 1);

    for (int i = 0; i < 240 / 20; i++) {
        spi_transaction_t t = { .length = 320 * 20 * 16, .tx_buffer = line };
        spi_device_transmit(spi, &t);
    }
    free(line);
}