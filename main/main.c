#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "libretro.h"
#include "esp_err.h"

static const char *TAG = "GBA_RUNNER";

// Forward declarations for your display / SD routines
extern void display_init(void);
extern void display_send_gba_frame(const uint16_t *frame_buffer);
extern esp_err_t sdcard_init(void);

// Libretro Video Callback: direct pipe to ILI9341
static void video_refresh_callback(const void *data, unsigned width, unsigned height, size_t pitch)
{
    if (data) {
        display_send_gba_frame((const uint16_t *)data);
    }
}

// Libretro Audio Callback
static size_t audio_batch_callback(const int16_t *data, size_t frames)
{
    // Return frames consumed (or route to I2S DMA)
    return frames;
}

static void audio_sample_callback(int16_t left, int16_t right)
{
    // Optional mono/stereo sample callback
}

// Libretro Input Callback
static void input_poll_callback(void)
{
    // Poll hardware gamepad / GPIO buttons here
}

static int16_t input_state_callback(unsigned port, unsigned device, unsigned index, unsigned id)
{
    // Return 1 if button pressed, 0 otherwise
    return 0;
}

// Libretro Environment Callback
static bool environment_callback(unsigned cmd, void *data)
{
    switch (cmd) {
        case RETRO_ENVIRONMENT_GET_CAN_DUPE:
            *(bool *)data = true;
            return true;

        case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
            enum retro_pixel_format fmt = *(enum retro_pixel_format *)data;
            return (fmt == RETRO_PIXEL_FORMAT_RGB565);
        }

        case RETRO_ENVIRONMENT_GET_VARIABLE:
        case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
            return false;

        default:
            return false;
    }
}

void gba_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Initializing display & peripherals...");
    display_init();

    if (sdcard_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SD card!");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Hooking libretro callbacks...");
    retro_set_environment(environment_callback);
    retro_set_video_refresh(video_refresh_callback);
    retro_set_audio_sample(audio_sample_callback);
    retro_set_audio_sample_batch(audio_batch_callback);
    retro_set_input_poll(input_poll_callback);

    ESP_LOGI(TAG, "Initializing GBA core...");
    retro_init();

    // Adjust this path to the test ROM on your SD card
    const char *rom_path = "/sdcard/game.gba";
    struct retro_game_info game_info = {
        .path = rom_path,
        .data = NULL,
        .size = 0,
        .meta = NULL
    };

    ESP_LOGI(TAG, "Loading ROM: %s", rom_path);
    if (!retro_load_game(&game_info)) {
        ESP_LOGE(TAG, "Failed to load ROM!");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Entering core execution loop...");
    while (1) {
        retro_run();
        // Yield to FreeRTOS watchdog / scheduler
        vTaskDelay(1);
    }
}

void app_main(void)
{
    // Pin emulator core task to CPU 1 with 32KB stack
    xTaskCreatePinnedToCore(gba_task, "gba_task", 32768, NULL, 5, NULL, 1);
}
