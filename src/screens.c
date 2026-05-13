#include <stdio.h>
#include <string.h>
#include <math.h>
#include "screens.h"

#define COLOR_BLACK   0x0000UL
#define COLOR_WHITE   0xFFFFUL
#define COLOR_DGRAY   0x2104UL
#define COLOR_GREEN   0x07E0UL
#define COLOR_YELLOW  0xFFE0UL
#define COLOR_RED     0xF800UL

#define R15  ((int16_t)((float)BUBBLE_TRAVEL * 15.0f / BUBBLE_MAX_DEG))
#define R30  ((int16_t)((float)BUBBLE_TRAVEL * 30.0f / BUBBLE_MAX_DEG))
#define R45  ((int16_t)BUBBLE_TRAVEL)

static const int16_t s_ring_r[3] = {R15, R30, R45};

/* ── Primitives ──────────────────────────────────────────────────── */

static void fill_circle(int16_t cx, int16_t cy, int16_t r, uint32_t color) {
    for (int16_t y = cy - r; y <= cy + r; y++) {
        if (y < 0 || y >= HEIGHT) continue;
        float dy = (float)(y - cy);
        int16_t dx = (int16_t)sqrtf((float)(r * r) - dy * dy);
        int16_t x0 = cx - dx, x1 = cx + dx;
        if (x0 < 0) x0 = 0;
        if (x1 >= WIDTH) x1 = WIDTH - 1;
        if (x0 > x1) continue;
        if (x0 < x1) {
            int16_t yt = y, yb = (y + 1 < HEIGHT) ? y + 1 : y - 1;
            if (yt > yb) { int16_t t = yt; yt = yb; yb = t; }
            st7789_fill_rect(&g_st7789, (uint16_t)x0, (uint16_t)(yt + Y_OFF),
                             (uint16_t)x1, (uint16_t)(yb + Y_OFF), color);
        } else {
            st7789_draw_point(&g_st7789, (uint16_t)x0, (uint16_t)(y + Y_OFF), color);
        }
    }
}

static void draw_hline(int16_t x0, int16_t x1, int16_t y, uint32_t color) {
    if (x0 > x1) { int16_t t = x0; x0 = x1; x1 = t; }
    if (x0 < 0) x0 = 0;
    if (x1 >= WIDTH) x1 = WIDTH - 1;
    int16_t y1 = (y + 1 < HEIGHT) ? y + 1 : y - 1;
    if (x0 >= x1 || y < 0 || y1 < 0 || y >= HEIGHT) return;
    if (y > y1) { int16_t t = y; y = y1; y1 = t; }
    st7789_fill_rect(&g_st7789, x0, y + Y_OFF, x1, y1 + Y_OFF, color);
}

static void draw_vline(int16_t x, int16_t y0, int16_t y1, uint32_t color) {
    if (y0 > y1) { int16_t t = y0; y0 = y1; y1 = t; }
    if (y0 < 0) y0 = 0;
    if (y1 >= HEIGHT) y1 = HEIGHT - 1;
    int16_t x1 = (x + 1 < WIDTH) ? x + 1 : x - 1;
    if (y0 >= y1 || x < 0 || x1 < 0 || x >= WIDTH) return;
    if (x > x1) { int16_t t = x; x = x1; x1 = t; }
    st7789_fill_rect(&g_st7789, x, y0 + Y_OFF, x1, y1 + Y_OFF, color);
}

/* ── Ring outlines (Bresenham midpoint algorithm) ────────────────── */

static void ring_px(int16_t x, int16_t y, uint32_t color) {
    if (x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT)
        st7789_draw_point(&g_st7789, (uint16_t)x, (uint16_t)(y + Y_OFF), color);
}

static void draw_ring(int16_t cx, int16_t cy, int16_t r, uint32_t color) {
    int16_t x = 0, y = r, d = 3 - 2 * r;
    while (y >= x) {
        ring_px(cx+x, cy+y, color); ring_px(cx-x, cy+y, color);
        ring_px(cx+x, cy-y, color); ring_px(cx-x, cy-y, color);
        ring_px(cx+y, cy+x, color); ring_px(cx-y, cy+x, color);
        ring_px(cx+y, cy-x, color); ring_px(cx-y, cy-x, color);
        if (d < 0) d += 4 * x + 6;
        else { d += 4 * (x - y) + 10; y--; }
        x++;
    }
}

// Redraws only the arc pixels of ring (ox,oy,r) that fall within clip_r of (px,py)
static void restore_ring_arc(int16_t ox, int16_t oy, int16_t r,
                              int16_t px, int16_t py, int16_t clip_r) {
    int32_t cr2 = (int32_t)(clip_r + 1) * (clip_r + 1);
    int16_t x = 0, y = r, d = 3 - 2 * r;

#define ARC_PT(ax, ay) do { \
        int32_t _dx = (int32_t)(ax) - px, _dy = (int32_t)(ay) - py; \
        if (_dx*_dx + _dy*_dy <= cr2) ring_px((ax), (ay), COLOR_WHITE); \
    } while (0)

    while (y >= x) {
        ARC_PT(ox+x, oy+y); ARC_PT(ox-x, oy+y);
        ARC_PT(ox+x, oy-y); ARC_PT(ox-x, oy-y);
        ARC_PT(ox+y, oy+x); ARC_PT(ox-y, oy+x);
        ARC_PT(ox+y, oy-x); ARC_PT(ox-y, oy-x);
        if (d < 0) d += 4 * x + 6;
        else { d += 4 * (x - y) + 10; y--; }
        x++;
    }
#undef ARC_PT
}

/* ── Battery icon ────────────────────────────────────────────────── */

static void draw_battery(uint8_t pct) {
    const int16_t bx = WIDTH - 30, by = 4;
    st7789_fill_rect(&g_st7789, bx - 2, Y_OFF,      WIDTH - 1,  18 + Y_OFF,       COLOR_BLACK);
    draw_hline(bx, bx + 24, by,      COLOR_WHITE);
    draw_hline(bx, bx + 24, by + 12, COLOR_WHITE);
    draw_vline(bx,      by, by + 12, COLOR_WHITE);
    draw_vline(bx + 24, by, by + 12, COLOR_WHITE);
    st7789_fill_rect(&g_st7789, bx + 25, by + 4 + Y_OFF, bx + 27, by + 9 + Y_OFF, COLOR_WHITE);
    int16_t fw = (int16_t)(22 * pct / 100);
    if (fw > 1)
        st7789_fill_rect(&g_st7789, bx + 1, by + 1 + Y_OFF, bx + fw, by + 11 + Y_OFF, COLOR_WHITE);
}

/* ── Static background (call once at startup) ────────────────────── */

void screen_init(void) {
    st7789_fill_rect(&g_st7789, 0, Y_OFF, WIDTH - 1, HEIGHT - 1 + Y_OFF, COLOR_BLACK);

    const int16_t cx = WIDTH / 2, cy = HEIGHT / 2;

    draw_hline(0, WIDTH - 1, cy, COLOR_DGRAY);
    draw_vline(cx, 0, HEIGHT - 1, COLOR_DGRAY);

    for (int ri = 0; ri < 3; ri++)
        draw_ring(cx, cy, s_ring_r[ri], COLOR_WHITE);
}

/* ── Bubble display (call every frame) ───────────────────────────── */

void draw_bubble(float roll, float pitch, uint8_t bat) {
    static int16_t  prev_x     = WIDTH  / 2;
    static int16_t  prev_y     = HEIGHT / 2;
    static uint32_t prev_color = COLOR_GREEN;
    static uint8_t  prev_bat   = 255;
    static char     prev_rbuf[8] = "";
    static char     prev_pbuf[8] = "";

    const int16_t cx = WIDTH  / 2;
    const int16_t cy = HEIGHT / 2;
    const int16_t er = BUBBLE_DOT_R + 1;

    float scale  = (float)BUBBLE_TRAVEL / BUBBLE_MAX_DEG;
    int16_t dot_x = cx - (int16_t)(pitch * scale);
    int16_t dot_y = cy - (int16_t)(roll  * scale);
    dot_x = (int16_t)fmaxf(BUBBLE_DOT_R + 2, fminf(WIDTH  - BUBBLE_DOT_R - 3, dot_x));
    dot_y = (int16_t)fmaxf(BUBBLE_DOT_R + 2, fminf(HEIGHT - BUBBLE_DOT_R - 3, dot_y));

    float tilt = sqrtf(roll * roll + pitch * pitch);
    uint32_t dot_color;
    if (prev_color == COLOR_GREEN)
        dot_color = tilt < 5.5f  ? COLOR_GREEN  : tilt < 15.5f ? COLOR_YELLOW : COLOR_RED;
    else if (prev_color == COLOR_YELLOW)
        dot_color = tilt < 4.5f  ? COLOR_GREEN  : tilt < 15.5f ? COLOR_YELLOW : COLOR_RED;
    else
        dot_color = tilt < 4.5f  ? COLOR_GREEN  : tilt < 14.5f ? COLOR_YELLOW : COLOR_RED;

    bool moved   = (dot_x != prev_x || dot_y != prev_y);
    bool recolor = (dot_color != prev_color);

    bool will_erase   = (moved || recolor);
    bool bat_erased   = will_erase && (prev_x + er >= WIDTH - 32 && prev_y - er <= 18);
    bool r_lbl_erased = will_erase && (prev_x - er < 48 && prev_y - er < 18);
    bool p_lbl_erased = will_erase && (prev_x - er < 48 && prev_y + er > HEIGHT - 19);

    if (moved || recolor) {
        // 1. Erase old dot
        fill_circle(prev_x, prev_y, er, COLOR_BLACK);

        // 2. Draw new dot immediately — minimises the "no dot" window to just the erase time
        fill_circle(dot_x, dot_y, BUBBLE_DOT_R, dot_color);

        // 3. Restore background at OLD position
        int16_t hx0 = (int16_t)fmaxf(0,         prev_x - er);
        int16_t hx1 = (int16_t)fminf(WIDTH - 1, prev_x + er);
        int16_t vy0 = (int16_t)fmaxf(0,          prev_y - er);
        int16_t vy1 = (int16_t)fminf(HEIGHT - 1, prev_y + er);
        draw_hline(hx0, hx1, cy, COLOR_DGRAY);
        draw_vline(cx, vy0, vy1, COLOR_DGRAY);

        // Restore any ring arc that passed through the erase circle
        for (int ri = 0; ri < 3; ri++) {
            int32_t dx = prev_x - cx, dy = prev_y - cy;
            float dist = sqrtf((float)(dx * dx + dy * dy));
            if (fabsf(dist - (float)s_ring_r[ri]) <= (float)(er + 3))
                restore_ring_arc(cx, cy, s_ring_r[ri], prev_x, prev_y, er + 2);
        }

        // 4. If old and new are close, bg restoration may have overdrawn the new dot
        int32_t sep_x = dot_x - prev_x, sep_y = dot_y - prev_y;
        int32_t sum_r  = er + BUBBLE_DOT_R + 2;
        if (sep_x * sep_x + sep_y * sep_y < sum_r * sum_r)
            fill_circle(dot_x, dot_y, BUBBLE_DOT_R, dot_color);

        prev_x = dot_x;
        prev_y = dot_y;
        prev_color = dot_color;
    }

    char rbuf[8], pbuf[8];
    int r = (int)roundf(pitch), p = (int)roundf(roll);
    snprintf(rbuf, sizeof(rbuf), "R%c%d", r >= 0 ? '+' : '-', abs(r));
    snprintf(pbuf, sizeof(pbuf), "P%c%d", p >= 0 ? '+' : '-', abs(p));

    if (strcmp(rbuf, prev_rbuf) != 0 || r_lbl_erased) {
        st7789_fill_rect(&g_st7789, 0, Y_OFF, 48, 18 + Y_OFF, COLOR_BLACK);
        st7789_write_string(&g_st7789, 4, 2 + Y_OFF, rbuf, strlen(rbuf), COLOR_WHITE, ST7789_FONT_16);
        memcpy(prev_rbuf, rbuf, sizeof(rbuf));
    }
    if (strcmp(pbuf, prev_pbuf) != 0 || p_lbl_erased) {
        st7789_fill_rect(&g_st7789, 0, HEIGHT - 19 + Y_OFF, 48, HEIGHT - 1 + Y_OFF, COLOR_BLACK);
        st7789_write_string(&g_st7789, 4, HEIGHT - 18 + Y_OFF, pbuf, strlen(pbuf), COLOR_WHITE, ST7789_FONT_16);
        memcpy(prev_pbuf, pbuf, sizeof(pbuf));
    }

    if (bat != prev_bat || bat_erased) {
        draw_battery(bat);
        prev_bat = bat;
    }
}
