#include <stdio.h>
#include <math.h>
#include "screens.h"

/* ── Drawing helpers ─────────────────────────────────────────────── */

static void dashed_hline(int16_t x0, int16_t x1, int16_t y) {
    for (int16_t x = x0; x <= x1; x += 6)
        u8g2_DrawHLine(&u8g2, x, y, (x + 3 <= x1) ? 3 : x1 - x + 1);
}

static void dashed_vline(int16_t x, int16_t y0, int16_t y1) {
    for (int16_t y = y0; y <= y1; y += 6)
        u8g2_DrawVLine(&u8g2, x, y, (y + 3 <= y1) ? 3 : y1 - y + 1);
}

static void draw_centered(int16_t cx, int16_t cy, const char *s) {
    int16_t w        = u8g2_GetStrWidth(&u8g2, s);
    int16_t baseline = cy + (u8g2_GetAscent(&u8g2) + u8g2_GetDescent(&u8g2)) / 2;
    u8g2_DrawStr(&u8g2, cx - w / 2, baseline, s);
}

/* ── Battery icon ────────────────────────────────────────────────── */

static void draw_battery(uint8_t pct) {
    const int16_t bx = WIDTH - 14;
    const int16_t by = 1;

    u8g2_SetDrawColor(&u8g2, 0);
    u8g2_DrawBox(&u8g2, bx - 1, 0, WIDTH - bx + 1, 8);

    u8g2_SetDrawColor(&u8g2, 1);
    u8g2_DrawFrame(&u8g2, bx, by, 12, 6);
    u8g2_DrawBox(&u8g2, bx + 12, by + 2, 1, 2);

    int16_t fill_w = (int16_t)(10 * pct / 100);
    if (fill_w > 0)
        u8g2_DrawBox(&u8g2, bx + 1, by + 1, fill_w, 4);
}

/* ── Screen: spirit level bubble ─────────────────────────────────── */

void draw_bubble(float roll, float pitch, uint8_t bat) {
    u8g2_SetFont(&u8g2, u8g2_font_5x7_tf);

    u8g2_DrawFrame(&u8g2, 0, 0, WIDTH, HEIGHT);
    u8g2_DrawHLine(&u8g2, 2, HEIGHT / 2, WIDTH - 4);
    u8g2_DrawVLine(&u8g2, WIDTH / 2, 2, HEIGHT - 4);
    u8g2_DrawCircle(&u8g2, WIDTH / 2, HEIGHT / 2, 4, U8G2_DRAW_ALL);

    for (int deg = 15; deg <= (int)BUBBLE_MAX_DEG; deg += 15) {
        int16_t x_hs = (int16_t)((float)BUBBLE_TRAVEL_X * deg / BUBBLE_MAX_DEG);
        int16_t y_hs = (int16_t)((float)BUBBLE_TRAVEL_Y * deg / BUBBLE_MAX_DEG);
        int16_t cr   = y_hs / 3 < 2 ? 2 : y_hs / 3;
        u8g2_DrawRFrame(&u8g2, WIDTH / 2 - x_hs, HEIGHT / 2 - y_hs, x_hs * 2, y_hs * 2, cr);
    }

    float sx = BUBBLE_TRAVEL_X / BUBBLE_MAX_DEG;
    float sy = BUBBLE_TRAVEL_Y / BUBBLE_MAX_DEG;
    int16_t dot_x = WIDTH  / 2 - (int16_t)(pitch * sx);
    int16_t dot_y = HEIGHT / 2 - (int16_t)(roll  * sy);
    dot_x = (int16_t)fmaxf(BUBBLE_DOT_R + 2, fminf(WIDTH  - BUBBLE_DOT_R - 3, dot_x));
    dot_y = (int16_t)fmaxf(BUBBLE_DOT_R + 2, fminf(HEIGHT - BUBBLE_DOT_R - 3, dot_y));
    u8g2_DrawDisc(&u8g2, dot_x, dot_y, BUBBLE_DOT_R, U8G2_DRAW_ALL);

    char buf[8];
    int r = (int)roundf(roll), p = (int)roundf(pitch);
    snprintf(buf, sizeof(buf), "R%c%d", r >= 0 ? '+' : '-', abs(r));
    u8g2_DrawStr(&u8g2, 3, 8, buf);
    snprintf(buf, sizeof(buf), "P%c%d", p >= 0 ? '+' : '-', abs(p));
    u8g2_DrawStr(&u8g2, 3, HEIGHT - 2, buf);
    draw_battery(bat);
}

/* ── Screen: split roll / pitch gauge ───────────────────────────── */

void draw_gauge(float r_line, float p_line, int16_t r, int16_t p, uint8_t bat) {
    u8g2_SetFont(&u8g2, u8g2_font_profont17_tf);

    int16_t roll_y  = (((int16_t)(32.0f + r_line * ROLL_PPD) % HEIGHT) + HEIGHT) % HEIGHT;
    int16_t pitch_x = 65 + ((((int16_t)(31.0f - p_line * PITCH_PPD) % 63) + 63) % 63);

    u8g2_DrawVLine(&u8g2, 63, 0, HEIGHT);
    dashed_hline(0, 62, roll_y);
    dashed_vline(pitch_x, 0, HEIGHT - 1);

    u8g2_SetDrawColor(&u8g2, 0);
    u8g2_DrawBox(&u8g2,  9, 20, 44, 24);
    u8g2_DrawBox(&u8g2, 74, 20, 44, 24);
    u8g2_DrawBox(&u8g2, 20,  0, 23, 13);
    u8g2_DrawBox(&u8g2, 20, 51, 23, 13);
    u8g2_DrawBox(&u8g2, 65, 24, 11, 17);
    u8g2_DrawBox(&u8g2,119, 24,  9, 17);
    u8g2_SetDrawColor(&u8g2, 1);

    if (r > 0) u8g2_DrawTriangle(&u8g2, 31,  2, 23, 10, 39, 10);
    if (r < 0) u8g2_DrawTriangle(&u8g2, 31, 62, 23, 54, 39, 54);
    if (p > 0) u8g2_DrawTriangle(&u8g2, 67, 32, 73, 26, 73, 38);
    if (p < 0) u8g2_DrawTriangle(&u8g2,127, 32,121, 26,121, 38);

    char r_str[4], p_str[4];
    snprintf(r_str, sizeof(r_str), "%02d", abs(r));
    snprintf(p_str, sizeof(p_str), "%02d", abs(p));
    draw_centered(31, 32, r_str);
    draw_centered(96, 32, p_str);

    draw_battery(bat);
}

/* ── Screen: RPM mode placeholder ───────────────────────────────── */

void draw_rpm(void) {
    u8g2_SetFont(&u8g2, u8g2_font_profont17_tf);
    draw_centered(WIDTH / 2, 22, "RPM MODE");
    draw_centered(WIDTH / 2, 45, "TBD");
}

/* ── Screen: stats ───────────────────────────────────────────────── */

static void draw_beetle(void) {
    const int16_t ox = 110, oy = 1;

    u8g2_DrawLine(&u8g2, ox+5, oy+5, ox+2, oy+1);
    u8g2_DrawLine(&u8g2, ox+5, oy+5, ox+4, oy+2);
    u8g2_DrawLine(&u8g2, ox+9, oy+5, ox+12, oy+1);
    u8g2_DrawLine(&u8g2, ox+9, oy+5, ox+10, oy+2);

    u8g2_DrawDisc(&u8g2, ox+7, oy+7, 2, U8G2_DRAW_ALL);
    u8g2_DrawBox(&u8g2, ox+5, oy+10, 5, 3);
    u8g2_DrawRBox(&u8g2, ox+4, oy+13, 7, 8, 2);
    u8g2_DrawVLine(&u8g2, ox+7, oy+14, 6);

    u8g2_DrawLine(&u8g2, ox+5, oy+11, ox+2, oy+9);
    u8g2_DrawLine(&u8g2, ox+5, oy+13, ox+2, oy+12);
    u8g2_DrawLine(&u8g2, ox+5, oy+17, ox+2, oy+19);
    u8g2_DrawLine(&u8g2, ox+9, oy+11, ox+12, oy+9);
    u8g2_DrawLine(&u8g2, ox+9, oy+13, ox+12, oy+12);
    u8g2_DrawLine(&u8g2, ox+9, oy+17, ox+12, oy+19);
}

void draw_stats(void) {
    u8g2_SetFont(&u8g2, u8g2_font_5x7_tf);
    u8g2_DrawStr(&u8g2, 2,  8,  "BEYBEETLE V0.1");
    u8g2_DrawHLine(&u8g2, 0, 11, WIDTH);
    u8g2_DrawStr(&u8g2, 2, 22,  "BLADER: CHAMBER");
    u8g2_DrawStr(&u8g2, 2, 32,  "TOP SHOOT: N/A");
    u8g2_DrawStr(&u8g2, 2, 42,  "LAUNCHES: N/A");
    draw_beetle();
}

/* ── Menu overlay ────────────────────────────────────────────────── */

void draw_menu(int8_t held_item, float progress) {
    const char *labels[3] = { "ANGLE_MODE_", "RPM_MODE_", "STATS_" };

    u8g2_SetDrawColor(&u8g2, 0);
    u8g2_DrawBox(&u8g2, 0, 0, WIDTH, HEIGHT);
    u8g2_SetDrawColor(&u8g2, 1);
    u8g2_DrawFrame(&u8g2, 0, 0, WIDTH, HEIGHT);

    u8g2_SetFont(&u8g2, u8g2_font_profont12_tf);
    int16_t ascent  = u8g2_GetAscent(&u8g2);
    int16_t descent = u8g2_GetDescent(&u8g2);

    for (int i = 0; i < 3; i++) {
        int16_t iy       = 2 + i * MENU_ITEM_H;
        int16_t ih       = MENU_ITEM_H - 1;
        int16_t baseline = iy + ih / 2 + (ascent + descent) / 2;

        u8g2_SetDrawColor(&u8g2, 1);
        u8g2_DrawStr(&u8g2, 5, baseline, labels[i]);

        float item_prog = (i == held_item) ? progress : 0.0f;
        if (item_prog > 0.0f) {
            int16_t fw = (int16_t)((WIDTH - 2) * item_prog);
            u8g2_SetDrawColor(&u8g2, 2);
            u8g2_DrawBox(&u8g2, 1, iy, fw, ih);
            u8g2_SetDrawColor(&u8g2, 1);
        }
    }
}
