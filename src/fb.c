#include <string.h>
#include <math.h>
#include "fb.h"
#include "hardware/dma.h"
#include "hardware/spi.h"
#include "driver_st7789_font.h"
#include "bb_font.h"

uint16_t g_fb[WIDTH * HEIGHT];

void fb_init(void) { }

void fb_clear(uint16_t color) {
    uint16_t sw = __builtin_bswap16(color);
    if (sw == 0x0000 || sw == 0xFFFF) {
        memset(g_fb, sw & 0xFF, sizeof(g_fb));
    } else {
        uint32_t w = ((uint32_t)sw << 16) | sw;
        uint32_t *p = (uint32_t *)g_fb;
        for (int i = 0; i < WIDTH * HEIGHT / 2; i++) *p++ = w;
    }
}

void fb_hline(int16_t x0, int16_t x1, int16_t y, uint16_t color) {
    if ((unsigned)y >= HEIGHT) return;
    if (x0 > x1) { int16_t t = x0; x0 = x1; x1 = t; }
    if (x0 < 0) x0 = 0;
    if (x1 >= WIDTH) x1 = WIDTH - 1;
    if (x0 > x1) return;
    uint16_t sw = __builtin_bswap16(color);
    uint16_t *row = g_fb + (int)y * WIDTH + x0;
    for (int16_t x = x0; x <= x1; x++) *row++ = sw;
}

void fb_vline(int16_t x, int16_t y0, int16_t y1, uint16_t color) {
    if ((unsigned)x >= WIDTH) return;
    if (y0 > y1) { int16_t t = y0; y0 = y1; y1 = t; }
    if (y0 < 0) y0 = 0;
    if (y1 >= HEIGHT) y1 = HEIGHT - 1;
    if (y0 > y1) return;
    uint16_t sw = __builtin_bswap16(color);
    for (int16_t y = y0; y <= y1; y++) g_fb[(int)y * WIDTH + x] = sw;
}

void fb_fill_rect(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) {
    if (x0 > x1) { int16_t t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { int16_t t = y0; y0 = y1; y1 = t; }
    if (x0 < 0) x0 = 0; if (x1 >= WIDTH)  x1 = WIDTH  - 1;
    if (y0 < 0) y0 = 0; if (y1 >= HEIGHT) y1 = HEIGHT - 1;
    if (x0 > x1 || y0 > y1) return;
    uint16_t sw = __builtin_bswap16(color);
    for (int16_t y = y0; y <= y1; y++) {
        uint16_t *row = g_fb + (int)y * WIDTH + x0;
        for (int16_t x = x0; x <= x1; x++) *row++ = sw;
    }
}

void fb_fill_circle(int16_t cx, int16_t cy, int16_t r, uint16_t color) {
    uint16_t sw = __builtin_bswap16(color);
    for (int16_t y = cy - r; y <= cy + r; y++) {
        if ((unsigned)y >= HEIGHT) continue;
        float dy = (float)(y - cy);
        int16_t dx = (int16_t)sqrtf((float)(r * r) - dy * dy);
        int16_t x0 = cx - dx, x1 = cx + dx;
        if (x0 < 0) x0 = 0;
        if (x1 >= WIDTH) x1 = WIDTH - 1;
        if (x0 > x1) continue;
        uint16_t *row = g_fb + (int)y * WIDTH + x0;
        for (int16_t x = x0; x <= x1; x++) *row++ = sw;
    }
}

void fb_draw_circle(int16_t cx, int16_t cy, int16_t r, uint16_t color) {
    int16_t x = 0, y = r, d = 3 - 2 * r;
    while (y >= x) {
        fb_set_pixel(cx+x, cy+y, color); fb_set_pixel(cx-x, cy+y, color);
        fb_set_pixel(cx+x, cy-y, color); fb_set_pixel(cx-x, cy-y, color);
        fb_set_pixel(cx+y, cy+x, color); fb_set_pixel(cx-y, cy+x, color);
        fb_set_pixel(cx+y, cy-x, color); fb_set_pixel(cx-y, cy-x, color);
        if (d < 0) d += 4 * x + 6;
        else { d += 4 * (x - y) + 10; y--; }
        x++;
    }
}

void fb_char(int16_t px, int16_t py, char c, uint16_t color, uint8_t size) {
    if (c < ' ' || c > '~') return;
    uint8_t chr = (uint8_t)(c - ' ');
    uint8_t csize = (uint8_t)((size / 8 + ((size % 8) ? 1 : 0)) * (size / 2));
    int16_t x = px, y = py;
    for (uint8_t t = 0; t < csize; t++) {
        uint8_t temp;
        if      (size == 12) temp = gsc_st7789_ascii_1206[chr][t];
        else if (size == 16) temp = gsc_st7789_ascii_1608[chr][t];
        else                 temp = gsc_st7789_ascii_2412[chr][t];
        for (uint8_t t1 = 0; t1 < 8; t1++) {
            if (temp & 0x80) fb_set_pixel(x, y, color);
            temp <<= 1;
            y++;
            if ((y - py) == size) { y = py; x++; break; }
        }
    }
}

void fb_string(int16_t x, int16_t y, const char *s, uint16_t color, uint8_t size) {
    while (*s) {
        fb_char(x, y, *s++, color, size);
        x += size / 2;
    }
}

void fb_string_vert(int16_t x_center, int16_t y_top, const char *s,
                    uint16_t color, uint8_t size) {
    // Draws text rotated 90° CW: glyphs flow downward, glyph-top faces right.
    // Row 0 of each glyph lands at x = x_center + size/2 - 1 (inner/right).
    // Row (size-1) lands at x = x_center - size/2 (outer/left).
    uint8_t bpc  = (uint8_t)(size / 8 + (size % 8 ? 1 : 0));  // bytes per column
    uint8_t cols = (uint8_t)(size / 2);                        // columns = char advance width
    int16_t half = (int16_t)(cols - 1);
    int16_t y    = y_top;
    while (*s) {
        char c = *s++;
        if (c < ' ' || c > '~') { y += (int16_t)cols; continue; }
        uint8_t chr = (uint8_t)(c - ' ');
        for (uint8_t ci = 0; ci < cols; ci++) {
            int16_t sy = y + (int16_t)ci;
            int16_t ri = 0;
            for (uint8_t bi = 0; bi < bpc; bi++) {
                uint8_t temp;
                if      (size == 12) temp = gsc_st7789_ascii_1206[chr][ci * bpc + bi];
                else if (size == 16) temp = gsc_st7789_ascii_1608[chr][ci * bpc + bi];
                else                 temp = gsc_st7789_ascii_2412[chr][ci * bpc + bi];
                for (uint8_t b = 0; b < 8 && ri < (int16_t)size; b++, ri++) {
                    if (temp & 0x80)
                        fb_set_pixel(x_center + half - ri, sy, color);
                    temp <<= 1;
                }
            }
        }
        y += (int16_t)cols;
    }
}

// ── Variable-width custom font ────────────────────────────────────────────

void fb_bfont_char(int16_t px, int16_t py, char c, uint16_t color) {
    if (c < 32 || c > 126) return;
    int idx = (uint8_t)c - 32;
    int w = bfont_widths[idx];
    const uint8_t *d = bfont_data + bfont_offsets[idx];
    int total = w * ((BFONT_H + 7) / 8);
    int16_t x = px, y = py;
    for (int i = 0; i < total; i++) {
        uint8_t byte = d[i];
        for (int b = 0; b < 8; b++) {
            if (byte & 0x80) fb_set_pixel(x, y, color);
            byte <<= 1;
            y++;
            if ((y - py) == BFONT_H) { y = py; x++; break; }
        }
    }
}

int16_t fb_bfont_width(const char *s) {
    int16_t w = 0;
    while (*s) {
        int idx = (uint8_t)(*s++) - 32;
        if (idx >= 0 && idx < 95) w += bfont_widths[idx] + 1;
    }
    return w > 1 ? w - 1 : 0;
}

void fb_bfont_string(int16_t x, int16_t y, const char *s, uint16_t color) {
    while (*s) {
        fb_bfont_char(x, y, *s, color);
        int idx = (uint8_t)(*s++) - 32;
        if (idx >= 0 && idx < 95) x += bfont_widths[idx] + 1;
    }
}

// ── DMA flush ──────────────────────────────────────────────────────────────

static void send_cmd(uint8_t cmd) {
    gpio_put(DISPLAY_CS, 0);
    gpio_put(DISPLAY_DC, 0);
    spi_write_blocking(SPI_PORT, &cmd, 1);
    gpio_put(DISPLAY_CS, 1);
}

static void send_data(const uint8_t *buf, size_t len) {
    gpio_put(DISPLAY_CS, 0);
    gpio_put(DISPLAY_DC, 1);
    spi_write_blocking(SPI_PORT, buf, len);
    gpio_put(DISPLAY_CS, 1);
}

void fb_flush(void) {
    // Column address: 0 to 239
    static const uint8_t caset[] = {0x00, 0x00, 0x00, 0xEF};
    send_cmd(0x2A); send_data(caset, 4);

    // Row address: Y_OFF (80) to HEIGHT-1+Y_OFF (319)
    static const uint8_t raset[] = {0x00, 0x50, 0x01, 0x3F};
    send_cmd(0x2B); send_data(raset, 4);

    // RAMWR then stream framebuffer — keep CS asserted throughout
    send_cmd(0x2C);
    gpio_put(DISPLAY_CS, 0);
    gpio_put(DISPLAY_DC, 1);

    int chan = dma_claim_unused_channel(true);
    dma_channel_config cfg = dma_channel_get_default_config(chan);
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_8);
    channel_config_set_dreq(&cfg, DREQ_SPI0_TX);
    channel_config_set_read_increment(&cfg, true);
    channel_config_set_write_increment(&cfg, false);

    dma_channel_configure(chan, &cfg,
        &spi_get_hw(SPI_PORT)->dr,
        g_fb,
        WIDTH * HEIGHT * 2,
        true);

    dma_channel_wait_for_finish_blocking(chan);
    while (spi_get_hw(SPI_PORT)->sr & SPI_SSPSR_BSY_BITS);

    gpio_put(DISPLAY_CS, 1);
    dma_channel_unclaim(chan);
}
