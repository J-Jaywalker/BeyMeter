#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "screens.h"
#include "fb.h"

#define COLOR_BLACK   0x0000U
#define COLOR_WHITE   0xFFFFU
#define COLOR_DGRAY   0x2104U
#define COLOR_ORANGE  0xFD20U
#define COLOR_GREEN   0x07E0U
#define COLOR_YELLOW  0xFFE0U
#define COLOR_RED     0xF800U

#define R15  ((int16_t)((float)BUBBLE_TRAVEL * 15.0f / BUBBLE_MAX_DEG))
#define R30  ((int16_t)((float)BUBBLE_TRAVEL * 30.0f / BUBBLE_MAX_DEG))
#define R45  ((int16_t)BUBBLE_TRAVEL)

static const int16_t s_ring_r[3] = {R15, R30, R45};
static uint16_t s_prev_color = COLOR_GREEN;

static void draw_battery(uint8_t pct) {
    const int16_t bx = WIDTH - 30, by = 4;
    fb_hline(bx, bx + 24, by,      COLOR_WHITE);
    fb_hline(bx, bx + 24, by + 12, COLOR_WHITE);
    fb_vline(bx,      by, by + 12, COLOR_WHITE);
    fb_vline(bx + 24, by, by + 12, COLOR_WHITE);
    fb_fill_rect(bx + 25, by + 4, bx + 27, by + 9, COLOR_WHITE);
    int16_t fw = (int16_t)(22 * pct / 100);
    if (fw > 1)
        fb_fill_rect(bx + 1, by + 1, bx + fw, by + 11, COLOR_WHITE);
}

static void draw_popup(float popup_pitch, float popup_roll) {
    const int16_t bx1 = 40, bx2 = 199, by1 = 70, by2 = 165;
    const int16_t cx = (bx1 + bx2) / 2;

    fb_fill_rect(bx1, by1, bx2, by2, COLOR_WHITE);
    fb_fill_rect(bx1 + 2, by1 + 2, bx2 - 2, by2 - 2, COLOR_BLACK);

    fb_string(cx - (13 * 6) / 2, by1 + 4, "LAUNCH ANGLES", COLOR_WHITE, 12);
    fb_fill_rect(bx1 + 2, by1 + 18, bx2 - 2, by1 + 19, COLOR_DGRAY);

    char pbuf[8], rbuf[8];
    int pi = (int)roundf(popup_pitch);
    int ri = (int)roundf(popup_roll);
    snprintf(pbuf, sizeof(pbuf), "%c%d", pi >= 0 ? '^' : 'v', abs(pi));
    snprintf(rbuf, sizeof(rbuf), "%c%d", ri >= 0 ? '>' : '<', abs(ri));
    int16_t plen = (int16_t)strlen(pbuf);
    int16_t rlen = (int16_t)strlen(rbuf);
    fb_string(cx - (plen * 12) / 2, by1 + 24, pbuf, COLOR_WHITE, 24);
    fb_string(cx - (rlen * 12) / 2, by1 + 52, rbuf, COLOR_WHITE, 24);
}

void screen_init(void) {
    fb_clear(COLOR_BLACK);
    fb_flush();
}

void screen_render(float roll, float pitch, uint8_t bat, bool locked,
                   bool popup, float popup_pitch, float popup_roll) {
    fb_clear(COLOR_BLACK);

    const int16_t cx = WIDTH / 2, cy = HEIGHT / 2;
    uint16_t cross_color = locked ? COLOR_ORANGE : COLOR_DGRAY;

    // Crosshair
    fb_hline(0, WIDTH - 1, cy, cross_color);
    fb_vline(cx, 0, HEIGHT - 1, cross_color);

    // Reference rings
    for (int i = 0; i < 3; i++)
        fb_draw_circle(cx, cy, s_ring_r[i], COLOR_WHITE);

    // Bubble position
    float scale = (float)BUBBLE_TRAVEL / BUBBLE_MAX_DEG;
    float fdx = -pitch * scale, fdy = -roll * scale;
    float dist = sqrtf(fdx * fdx + fdy * fdy);
    if (dist > (float)BUBBLE_TRAVEL) { float s = (float)BUBBLE_TRAVEL / dist; fdx *= s; fdy *= s; }
    int16_t dot_x = cx + (int16_t)fdx;
    int16_t dot_y = cy + (int16_t)fdy;

    // Bubble colour with hysteresis
    float tilt = sqrtf(roll * roll + pitch * pitch);
    float lo = (s_prev_color == COLOR_GREEN) ? 5.5f : 4.5f;
    float hi = (s_prev_color == COLOR_RED)   ? 14.5f : 15.5f;
    uint16_t dot_color = tilt < lo ? COLOR_GREEN : tilt < hi ? COLOR_YELLOW : COLOR_RED;
    s_prev_color = dot_color;

    fb_fill_circle(dot_x, dot_y, BUBBLE_DOT_R, dot_color);

    // Roll label — top-left
    char rbuf[8], pbuf[8];
    int r = (int)roundf(pitch), p = (int)roundf(roll);
    snprintf(rbuf, sizeof(rbuf), "R%c%d", r >= 0 ? '+' : '-', abs(r));
    snprintf(pbuf, sizeof(pbuf), "P%c%d", p >= 0 ? '+' : '-', abs(p));
    fb_string(2,  8,          "<",  COLOR_WHITE, 16);
    fb_string(12, 4,          rbuf, COLOR_WHITE, 24);
    fb_string(62, 8,          ">",  COLOR_WHITE, 16);

    // Pitch label — bottom-left
    fb_string(24, HEIGHT - 62, "^",  COLOR_WHITE, 16);
    fb_string(4,  HEIGHT - 44, pbuf, COLOR_WHITE, 24);
    fb_string(24, HEIGHT - 18, "v",  COLOR_WHITE, 16);

    // Battery
    draw_battery(bat);

    // Armed overlay
    if (locked) {
        fb_fill_rect(WIDTH - 76, HEIGHT - 30, WIDTH - 1, HEIGHT - 1, COLOR_YELLOW);
        fb_string(WIDTH - 58, HEIGHT - 23, "ARMED", COLOR_BLACK, 16);
    }

    // Launch popup
    if (popup) draw_popup(popup_pitch, popup_roll);

    fb_flush();
}
