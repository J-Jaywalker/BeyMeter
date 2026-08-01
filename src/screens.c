#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "pico/time.h"
#include "screens.h"
#include "fb.h"
#include "splash_image.h"

// EVA Unit 01 palette — accurate RGB565 conversions from reference hex
#define COLOR_BG     0x0803U  // deep indigo-black background
#define COLOR_DPURP  0x72D3U  // dark violet body #765898
#define COLOR_LPURP  0x92FAU  // lighter purple highlight #965fd4
#define COLOR_NGRE   0x568AU  // neon green energy #52d053
#define COLOR_LNGRE  0x8EAAU  // lighter green accent #8bd450
#define COLOR_ORANGE 0xE3A1U  // chest/eye accent orange #e6770b
#define COLOR_RED    0xD141U  // danger red #d3290f
#define COLOR_LAVEND 0xB4BBU  // lavender — floating angle labels

#define R15  ((int16_t)((float)BUBBLE_TRAVEL * 15.0f / BUBBLE_MAX_DEG))
#define R30  ((int16_t)((float)BUBBLE_TRAVEL * 30.0f / BUBBLE_MAX_DEG))
#define R45  ((int16_t)BUBBLE_TRAVEL)

static const int16_t  s_ring_r[3]   = {R15, R30, R45};
static const uint16_t s_ring_col[3] = {COLOR_LPURP, COLOR_DPURP, COLOR_ORANGE};
static uint16_t s_prev_color = COLOR_NGRE;

// ── Armed corner-cross animation ──────────────────────────────────────────
#define A_LEN        7                      // cross arm half-length (px)
#define A_THK        2                      // cross arm half-thickness (px)
#define A_STRIP      (A_LEN * 2)            // 14 — strip width
#define A_BLINK_MS   200                    // blink phase duration (ms)
#define A_BLINK_PER   50                    // ms per half-cycle (~2 flickers)
#define A_ROT_MS     540                    // rotation phase duration (ms)
#define A_SEG        " ARMED "
#define A_SEG_W       42                    // 7 chars × 6 px (size-12 font)
#define A_FS          12                    // strip font size
#define A_LS_Y0      (A_LEN * 2 + 3)       // 17 — left strip top y
#define A_LS_Y1      (HEIGHT - A_LEN*2 - 3)// 223 — left strip bottom y
#define A_BS_X0      (A_LEN * 2 + 3)       // 17 — bottom strip left x
#define A_BS_X1      (WIDTH  - A_LEN*2 - 3)// 223 — bottom strip right x

typedef enum { AS_IDLE = 0, AS_BLINK, AS_ROTATE, AS_STABLE } arm_state_t;
static arm_state_t s_as    = AS_IDLE;
static uint32_t    s_as_t0 = 0;

// Rotation keyframes: arm-1 direction vector (adx, ady), indexed by step 0-4.
// Steps go ~45° (X) → ~34° → ~16° → −8° overshoot → 0° (+).
static const int8_t  s_rv[5][2]  = {{5,5},{6,4},{7,2},{7,-1},{A_LEN,0}};
static const uint16_t s_rt[5]    = {0, 80, 200, 340, 420};  // step start ms

// Cross centres: TL, BL, BR
static const int16_t s_acx[3] = {9,   9,   231};
static const int16_t s_acy[3] = {9, 231,   231};

static uint16_t arm_lerp_col(uint16_t c1, uint16_t c2, uint32_t t, uint32_t tmax) {
    if (t >= tmax) return c2;
    int r1=(c1>>11)&0x1F, r2=(c2>>11)&0x1F;
    int g1=(c1>>5) &0x3F, g2=(c2>>5) &0x3F;
    int b1= c1&0x1F,      b2= c2&0x1F;
    int ti=(int)t, tm=(int)tmax;
    return (uint16_t)(((r1+(r2-r1)*ti/tm)<<11)|((g1+(g2-g1)*ti/tm)<<5)|(b1+(b2-b1)*ti/tm));
}

// Fills one thick arm segment in direction (adx, ady) centred at (cx, cy).
static void arm_draw_seg(int16_t cx, int16_t cy,
                          int16_t adx, int16_t ady, uint16_t col) {
    int32_t L2  = (int32_t)adx*adx + (int32_t)ady*ady;
    int32_t T2L = (int32_t)A_THK * A_THK * L2;
    int16_t b   = A_LEN + A_THK + 1;
    for (int16_t dy = -b; dy <= b; dy++) {
        int16_t rx0 = (int16_t)(cx + b + 1), rx1 = (int16_t)(cx - b - 1);
        for (int16_t dx = -b; dx <= b; dx++) {
            int32_t a = (int32_t)adx*dx + (int32_t)ady*dy;
            int32_t p = (int32_t)(-ady)*dx + (int32_t)adx*dy;
            if (a*a <= L2*L2 && p*p <= T2L) {
                int16_t px = (int16_t)(cx + dx);
                if (px < rx0) rx0 = px;
                if (px > rx1) rx1 = px;
            }
        }
        if (rx0 <= rx1) fb_hline(rx0, rx1, (int16_t)(cy + dy), col);
    }
}

// Draws a two-arm cross at any angle given arm-1 direction vector.
static void arm_draw_cross(int16_t cx, int16_t cy,
                            int16_t adx, int16_t ady, uint16_t col) {
    arm_draw_seg(cx, cy,  adx,  ady, col);
    arm_draw_seg(cx, cy, -ady,  adx, col);
}

// Fast + shape (axis-aligned): two fill_rects.
static void arm_draw_plus(int16_t cx, int16_t cy, uint16_t col) {
    fb_fill_rect(cx-A_LEN, cy-A_THK, cx+A_LEN, cy+A_THK, col);
    fb_fill_rect(cx-A_THK, cy-A_LEN, cx+A_THK, cy+A_LEN, col);
}

static void arm_draw_strips(uint16_t col, uint32_t now_ms) {
    uint16_t scroll = (uint16_t)((now_ms / 33) % (uint32_t)A_SEG_W);

    // Left strip bg (clears crosshair line that passes through)
    fb_fill_rect(0, A_LS_Y0, A_STRIP - 1, A_LS_Y1, COLOR_BG);
    // Vertical scrolling text (fb_string_vert: glyph-top faces right/inward)
    int16_t off_l = (int16_t)(scroll % A_SEG_W);
    for (int16_t y = (int16_t)(A_LS_Y0 + off_l - A_SEG_W);
         y < A_LS_Y1 + A_SEG_W; y += A_SEG_W)
        fb_string_vert(A_STRIP / 2, y, A_SEG, col, A_FS);

    // Bottom strip bg (clears crosshair line that passes through)
    fb_fill_rect(A_BS_X0, HEIGHT - A_STRIP, A_BS_X1, HEIGHT - 1, COLOR_BG);
    // Horizontal scrolling text
    int16_t off_b = (int16_t)(scroll % A_SEG_W);
    for (int16_t x = (int16_t)(A_BS_X0 + off_b - A_SEG_W);
         x < A_BS_X1 + A_SEG_W; x += A_SEG_W)
        fb_string(x, HEIGHT - A_STRIP + 1, A_SEG, col, A_FS);
}

static void arm_draw_overlay(bool locked, uint32_t now) {
    // State transitions
    if (locked  && s_as == AS_IDLE)  { s_as = AS_BLINK;  s_as_t0 = now; }
    if (!locked && s_as != AS_IDLE)  { s_as = AS_IDLE;   return; }
    if (s_as == AS_IDLE) return;

    uint32_t ph = now - s_as_t0;

    if (s_as == AS_BLINK && ph >= A_BLINK_MS) {
        s_as = AS_ROTATE; s_as_t0 = now; ph = 0;
    }
    if (s_as == AS_ROTATE && ph >= A_ROT_MS) {
        s_as = AS_STABLE;
    }

    uint16_t col;
    int16_t  adx, ady;
    bool     show_strips = false;

    if (s_as == AS_BLINK) {
        if ((ph / A_BLINK_PER) % 2 != 0) return;  // off half-cycle
        col = COLOR_ORANGE; adx = 5; ady = 5;

    } else if (s_as == AS_ROTATE) {
        // Find keyframe step
        int step = 4;
        for (int i = 0; i < 4; i++) {
            if (ph < (uint32_t)s_rt[i + 1]) { step = i; break; }
        }
        adx = (int16_t)s_rv[step][0];
        ady = (int16_t)s_rv[step][1];
        if (step < 4) {
            col = COLOR_ORANGE;
        } else {
            uint32_t lt = ph - (uint32_t)s_rt[4];
            uint32_t ld = A_ROT_MS - (uint32_t)s_rt[4];  // 120 ms
            col = arm_lerp_col(COLOR_ORANGE, COLOR_NGRE, lt, ld);
            show_strips = true;
        }

    } else {  // AS_STABLE
        col = COLOR_NGRE; adx = A_LEN; ady = 0;
        show_strips = true;
    }

    if (show_strips) arm_draw_strips(col, now);

    // Crosses — fast path when axis-aligned
    if (adx == A_LEN && ady == 0) {
        for (int i = 0; i < 3; i++) arm_draw_plus(s_acx[i], s_acy[i], col);
    } else {
        for (int i = 0; i < 3; i++) arm_draw_cross(s_acx[i], s_acy[i], adx, ady, col);
    }
}

// All four arrow glyphs, 6×6 px filled triangles — same visual weight as each other

static void draw_arr_up(int16_t x, int16_t y, uint16_t col) {
    fb_hline(x+2, x+3, y,   col);
    fb_hline(x+2, x+3, y+1, col);
    fb_hline(x+1, x+4, y+2, col);
    fb_hline(x+1, x+4, y+3, col);
    fb_hline(x,   x+5, y+4, col);
    fb_hline(x,   x+5, y+5, col);
}

static void draw_arr_down(int16_t x, int16_t y, uint16_t col) {
    fb_hline(x,   x+5, y,   col);
    fb_hline(x,   x+5, y+1, col);
    fb_hline(x+1, x+4, y+2, col);
    fb_hline(x+1, x+4, y+3, col);
    fb_hline(x+2, x+3, y+4, col);
    fb_hline(x+2, x+3, y+5, col);
}

static void draw_arr_right(int16_t x, int16_t y, uint16_t col) {
    fb_hline(x+4, x+5, y,   col);
    fb_hline(x+2, x+5, y+1, col);
    fb_hline(x,   x+5, y+2, col);
    fb_hline(x,   x+5, y+3, col);
    fb_hline(x+2, x+5, y+4, col);
    fb_hline(x+4, x+5, y+5, col);
}

static void draw_arr_left(int16_t x, int16_t y, uint16_t col) {
    fb_hline(x,   x+1, y,   col);
    fb_hline(x,   x+3, y+1, col);
    fb_hline(x,   x+5, y+2, col);
    fb_hline(x,   x+5, y+3, col);
    fb_hline(x,   x+3, y+4, col);
    fb_hline(x,   x+1, y+5, col);
}

static void draw_battery(uint8_t pct) {
    const int16_t bx = WIDTH - 30, by = 4;
    fb_hline(bx, bx + 24, by,      COLOR_LNGRE);
    fb_hline(bx, bx + 24, by + 12, COLOR_LNGRE);
    fb_vline(bx,      by, by + 12, COLOR_LNGRE);
    fb_vline(bx + 24, by, by + 12, COLOR_LNGRE);
    fb_fill_rect(bx + 25, by + 4, bx + 27, by + 9, COLOR_LNGRE);
    int16_t fw = (int16_t)(22 * pct / 100);
    if (fw > 1)
        fb_fill_rect(bx + 1, by + 1, bx + fw, by + 11, COLOR_NGRE);
}

static void draw_popup(float popup_pitch, float popup_roll) {
    const int16_t bx1 = 40, bx2 = 199, by1 = 70, by2 = 165;
    const int16_t cx = (bx1 + bx2) / 2;

    fb_fill_rect(bx1, by1, bx2, by2, COLOR_LPURP);
    fb_fill_rect(bx1 + 2, by1 + 2, bx2 - 2, by2 - 2, COLOR_BG);

    fb_string(cx - (13 * 6) / 2, by1 + 4, "LAUNCH ANGLES", COLOR_NGRE, 12);
    fb_fill_rect(bx1 + 2, by1 + 18, bx2 - 2, by1 + 19, COLOR_ORANGE);

    char pbuf[8], rbuf[8];
    int pi = (int)roundf(popup_pitch);
    int ri = (int)roundf(popup_roll);
    snprintf(pbuf, sizeof(pbuf), "%c%d", pi >= 0 ? '^' : 'v', abs(pi));
    snprintf(rbuf, sizeof(rbuf), "%c%d", ri >= 0 ? '>' : '<', abs(ri));
    int16_t plen = (int16_t)strlen(pbuf);
    int16_t rlen = (int16_t)strlen(rbuf);
    fb_string(cx - (plen * 12) / 2, by1 + 24, pbuf, COLOR_NGRE, 24);
    fb_string(cx - (rlen * 12) / 2, by1 + 52, rbuf, COLOR_NGRE, 24);
}

void screen_init(void) {
    fb_clear(COLOR_BG);
    fb_flush();
}

void screen_splash(int step, int total, const char *label) {
    // Blit logo to framebuffer (pre-swapped RGB565, ready for DMA).
    // Already coloured COLOR_DPURP background / COLOR_NGRE artwork by
    // tools/convert_splash.py, so the bar sits straight on it — no strip,
    // no separator line between the loading area and the rest of the image.
    memcpy(g_fb, splash_image, sizeof(g_fb));

    // Bar is centred in the clear space below the logo (logo ends at y=213)
    const int16_t bx = 20, by = 217, bw = 200, bh = 18;   // rows 217..235

    fb_hline(bx,      bx + bw, by,      COLOR_ORANGE);
    fb_hline(bx,      bx + bw, by + bh, COLOR_ORANGE);
    fb_vline(bx,      by, by + bh,      COLOR_ORANGE);
    fb_vline(bx + bw, by, by + bh,      COLOR_ORANGE);

    // Bar fill — interior is bx+1..bx+bw-1, so full width is bw-1
    if (total > 0 && step > 0) {
        int16_t fw = (int16_t)((bw - 1) * step / total);
        if (fw > 0)
            fb_fill_rect(bx + 1, by + 1, bx + fw, by + bh - 1, COLOR_ORANGE);
    }

    // Status label inside the bar — dark, so it reads over both the orange
    // fill and the purple background of the not-yet-filled section
    if (label && *label) {
        int16_t tw = fb_bfont_width(label);
        fb_bfont_string(bx + (bw - tw) / 2,
                        by + 1 + (bh - 1 - BFONT_H) / 2,
                        label, COLOR_BG);
    }

    fb_flush();
}

void screen_render(float roll, float pitch, uint8_t bat, bool locked,
                   bool popup, float popup_pitch, float popup_roll) {
    fb_clear(COLOR_BG);

    const int16_t cx = WIDTH / 2, cy = HEIGHT / 2;
    uint16_t cross_color = locked ? COLOR_NGRE : COLOR_DPURP;

    // Crosshair
    fb_hline(0, WIDTH - 1, cy, cross_color);
    fb_vline(cx, 0, HEIGHT - 1, cross_color);

    // Reference rings — inner lpurp / mid dpurp / outer orange (danger zone)
    for (int i = 0; i < 3; i++)
        fb_draw_circle(cx, cy, s_ring_r[i], s_ring_col[i]);

    // Bubble position: pitch drives horizontal (left/right), roll drives vertical (fwd/back)
    float scale = (float)BUBBLE_TRAVEL / BUBBLE_MAX_DEG;
    float fdx = pitch * scale, fdy = roll * scale;
    float dist = sqrtf(fdx * fdx + fdy * fdy);
    if (dist > (float)BUBBLE_TRAVEL) { float s = (float)BUBBLE_TRAVEL / dist; fdx *= s; fdy *= s; }
    int16_t dot_x = cx + (int16_t)fdx;
    int16_t dot_y = cy + (int16_t)fdy;

    // Bubble colour: green → orange → red, with hysteresis
    float tilt = sqrtf(roll * roll + pitch * pitch);
    float lo = (s_prev_color == COLOR_NGRE) ? 5.5f : 4.5f;
    float hi = (s_prev_color == COLOR_RED)  ? 14.5f : 15.5f;
    uint16_t dot_color = tilt < lo ? COLOR_NGRE : tilt < hi ? COLOR_ORANGE : COLOR_RED;
    s_prev_color = dot_color;

    fb_fill_circle(dot_x, dot_y, BUBBLE_DOT_R, dot_color);

    // Floating angle labels — size 16 font, custom drawn arrows
    #define LBL_SIZE 16
    #define LBL_CW   8    // char width at size 16
    #define LBL_CH   16   // char height at size 16
    #define ARR_W    6    // arrow glyph width
    #define ARR_H    6    // arrow glyph height
    #define ARR_GAP  3    // gap between arrow and number
    #define LBL_GAP  (BUBBLE_DOT_R + 6)

    // Horizontal (pitch axis): arrow adjacent to dot, number on far side
    if (fabsf(pitch) >= 1.0f) {
        char nbuf[6];
        snprintf(nbuf, sizeof(nbuf), "%d", (int)roundf(fabsf(pitch)));
        int16_t nw  = (int16_t)(strlen(nbuf) * LBL_CW);
        int16_t ly  = dot_y - LBL_CH / 2;
        int16_t ayo = (LBL_CH - ARR_H) / 2;

        if (ly < 0) ly = 0;
        if (ly + LBL_CH >= HEIGHT) ly = HEIGHT - LBL_CH - 1;

        if (fdx >= 0) {
            // right of dot: [←arrow] [number]
            int16_t ax = dot_x + LBL_GAP;
            int16_t nx = ax + ARR_W + ARR_GAP;
            if (nx + nw >= WIDTH) { nx = WIDTH - nw - 1; ax = nx - ARR_W - ARR_GAP; }
            if (ax < 0) ax = 0;
            draw_arr_left(ax, ly + ayo, COLOR_LAVEND);
            fb_string(nx, ly, nbuf, COLOR_LAVEND, LBL_SIZE);
        } else {
            // left of dot: [number] [→arrow]
            int16_t ax = dot_x - LBL_GAP - ARR_W;
            int16_t nx = ax - ARR_GAP - nw;
            if (nx < 0) { nx = 0; ax = nx + nw + ARR_GAP; }
            if (ax + ARR_W >= WIDTH) ax = WIDTH - ARR_W - 1;
            draw_arr_right(ax, ly + ayo, COLOR_LAVEND);
            fb_string(nx, ly, nbuf, COLOR_LAVEND, LBL_SIZE);
        }
    }

    // Vertical (roll axis): two-row layout, arrow between dot and number in screen space
    //   above dot: [number] then [↑arrow] — up arrow is between number and dot below
    //   below dot: [↓arrow] then [number] — down arrow is between dot above and number
    if (fabsf(roll) >= 1.0f) {
        char nbuf[6];
        snprintf(nbuf, sizeof(nbuf), "%d", (int)roundf(fabsf(roll)));
        int16_t nw  = (int16_t)(strlen(nbuf) * LBL_CW);
        int16_t ax  = dot_x - ARR_W / 2;
        int16_t nx  = dot_x - nw / 2;

        if (ax < 0) ax = 0;
        if (ax + ARR_W >= WIDTH) ax = WIDTH - ARR_W - 1;
        if (nx < 0) nx = 0;
        if (nx + nw >= WIDTH) nx = WIDTH - nw - 1;

        if (fdy <= 0) {
            // above dot: number on top, up-arrow below pointing toward dot
            int16_t ly = dot_y - LBL_GAP - LBL_CH - ARR_GAP - ARR_H;
            if (ly < 0) ly = 0;
            fb_string(nx, ly, nbuf, COLOR_LAVEND, LBL_SIZE);
            draw_arr_up(ax, ly + LBL_CH + ARR_GAP, COLOR_LAVEND);
        } else {
            // below dot: down-arrow on top pointing toward dot, number below
            int16_t ly = dot_y + LBL_GAP;
            if (ly + ARR_H + ARR_GAP + LBL_CH >= HEIGHT)
                ly = HEIGHT - ARR_H - ARR_GAP - LBL_CH - 1;
            draw_arr_down(ax, ly, COLOR_LAVEND);
            fb_string(nx, ly + ARR_H + ARR_GAP, nbuf, COLOR_LAVEND, LBL_SIZE);
        }
    }

    // Battery
    draw_battery(bat);

    // Armed overlay — animated corner crosses + ARMED tape strips
    arm_draw_overlay(locked, to_ms_since_boot(get_absolute_time()));

    // Launch popup
    if (popup) draw_popup(popup_pitch, popup_roll);

    fb_flush();
}
