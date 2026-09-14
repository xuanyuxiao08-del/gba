#pragma once
#include <stdint.h>

#define LCD_WIDTH   320
#define LCD_HEIGHT  240
#define GBA_WIDTH   240
#define GBA_HEIGHT  160

void display_init(void);
void display_send_frame(const uint16_t *framebuffer);
void display_send_gba_frame(const uint16_t *gba_fb);
void display_clear_black(void);