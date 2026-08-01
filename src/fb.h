#pragma once
#include <stdint.h>
#include "hw.h"

// 240x240 RGB565 framebuffer — stored byte-swapped for DMA_SIZE_8 SPI transfer
extern uint16_t g_fb[WIDTH * HEIGHT];

static inline void fb_set_pixel(int16_t x, int16_t y, uint16_t color) {
    if ((unsigned)x < WIDTH && (unsigned)y < HEIGHT)
        g_fb[(int)y * WIDTH + x] = __builtin_bswap16(color);
}

void fb_init(void);
void fb_clear(uint16_t color);
void fb_hline(int16_t x0, int16_t x1, int16_t y, uint16_t color);
void fb_vline(int16_t x, int16_t y0, int16_t y1, uint16_t color);
void fb_fill_rect(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color);
void fb_fill_circle(int16_t cx, int16_t cy, int16_t r, uint16_t color);
void fb_draw_circle(int16_t cx, int16_t cy, int16_t r, uint16_t color);
void fb_char(int16_t x, int16_t y, char c, uint16_t color, uint8_t size);
void fb_string(int16_t x, int16_t y, const char *s, uint16_t color, uint8_t size);
void fb_flush(void);
