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
#define COLOR_GREY   0x7BEFU  // mid grey — spent launch mark

#define R15  ((int16_t)((float)BUBBLE_TRAVEL * 15.0f / BUBBLE_MAX_DEG))
#define R30  ((int16_t)((float)BUBBLE_TRAVEL * 30.0f / BUBBLE_MAX_DEG))
#define R45  ((int16_t)BUBBLE_TRAVEL)

static const int16_t  s_ring_r[3]   = {R15, R30, R45};
static const uint16_t s_ring_col[3] = {COLOR_LPURP, COLOR_DPURP, COLOR_ORANGE};

// ── Armed corner-cross animation ──────────────────────────────────────────
#define A_LEN        7                      // cross arm half-length (px)
#define A_THK        2                      // cross arm half-thickness (px)
#define A_STRIP      (A_LEN * 2)            // 14 — strip width
#define A_BLINK_MS   200                    // blink phase duration (ms)
#define A_BLINK_PER   50                    // ms per half-cycle (~2 flickers)
#define A_ROT_MS     540                    // rotation phase duration (ms)
#define A_SEG        " ARMED "
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

// Fills one thick arm segment centred at (cx, cy). (adx, ady) is a direction
// only — its magnitude is divided out, so len/thk set the arm geometry.
static void draw_cross_seg(int16_t cx, int16_t cy, int16_t adx, int16_t ady,
                           int16_t len, int16_t thk, uint16_t col) {
    int32_t L2 = (int32_t)adx*adx + (int32_t)ady*ady;
    int32_t A2 = (int32_t)len * len * L2;   // |along|  <= len
    int32_t P2 = (int32_t)thk * thk * L2;   // |across| <= thk
    int16_t b  = (int16_t)(len + thk + 1);
    for (int16_t dy = -b; dy <= b; dy++) {
        int16_t rx0 = (int16_t)(cx + b + 1), rx1 = (int16_t)(cx - b - 1);
        for (int16_t dx = -b; dx <= b; dx++) {
            int32_t a = (int32_t)adx*dx + (int32_t)ady*dy;
            int32_t p = (int32_t)(-ady)*dx + (int32_t)adx*dy;
            if (a*a <= A2 && p*p <= P2) {
                int16_t px = (int16_t)(cx + dx);
                if (px < rx0) rx0 = px;
                if (px > rx1) rx1 = px;
            }
        }
        if (rx0 <= rx1) fb_hline(rx0, rx1, (int16_t)(cy + dy), col);
    }
}

// Draws a two-arm cross at any angle given arm-1 direction vector.
static void draw_cross(int16_t cx, int16_t cy, int16_t adx, int16_t ady,
                       int16_t len, int16_t thk, uint16_t col) {
    draw_cross_seg(cx, cy,  adx,  ady, len, thk, col);
    draw_cross_seg(cx, cy, -ady,  adx, len, thk, col);
}

// Fast + shape (axis-aligned): two fill_rects.
static void arm_draw_plus(int16_t cx, int16_t cy, uint16_t col) {
    fb_fill_rect(cx-A_LEN, cy-A_THK, cx+A_LEN, cy+A_THK, col);
    fb_fill_rect(cx-A_THK, cy-A_LEN, cx+A_THK, cy+A_LEN, col);
}

static void arm_draw_strips(uint16_t col, uint32_t now_ms) {
    // Tile period = full advance of one segment in the custom font
    static int16_t seg_w = 0;
    if (seg_w == 0) seg_w = (int16_t)(fb_bfont_width(A_SEG) + 1);
    int16_t scroll = (int16_t)((now_ms / 33) % (uint32_t)seg_w);

    // Left strip bg (clears crosshair line that passes through)
    fb_fill_rect(0, A_LS_Y0, A_STRIP - 1, A_LS_Y1, COLOR_BG);
    // Vertical scrolling text — glyph-top faces right/inward, 13 px band
    // sits at x 1..13 so the outer screen edge stays clear
    for (int16_t y = (int16_t)(A_LS_Y0 + scroll - seg_w);
         y < A_LS_Y1 + seg_w; y += seg_w)
        fb_bfont_string_vert(A_STRIP - 1, y, A_SEG, col);
    // Trim the tiles that overhang the strip ends (nothing else lives there)
    fb_fill_rect(0, 0, A_STRIP - 1, A_LS_Y0 - 1, COLOR_BG);
    fb_fill_rect(0, A_LS_Y1 + 1, A_STRIP - 1, HEIGHT - 1, COLOR_BG);

    // Bottom strip bg (clears crosshair line that passes through)
    fb_fill_rect(A_BS_X0, HEIGHT - A_STRIP, A_BS_X1, HEIGHT - 1, COLOR_BG);
    // Horizontal scrolling text — 13 px band at y 226..238
    for (int16_t x = (int16_t)(A_BS_X0 + scroll - seg_w);
         x < A_BS_X1 + seg_w; x += seg_w)
        fb_bfont_string(x, HEIGHT - A_STRIP, A_SEG, col);
    fb_fill_rect(0, HEIGHT - A_STRIP, A_BS_X0 - 1, HEIGHT - 1, COLOR_BG);
    fb_fill_rect(A_BS_X1 + 1, HEIGHT - A_STRIP, WIDTH - 1, HEIGHT - 1, COLOR_BG);
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
        for (int i = 0; i < 3; i++)
            draw_cross(s_acx[i], s_acy[i], adx, ady, A_LEN, A_THK, col);
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

// ── Floating angle labels ─────────────────────────────────────────────────
#define LBL_SIZE 16
#define LBL_CW   8    // char width at size 16
#define LBL_CH   16   // char height at size 16
#define ARR_W    6    // arrow glyph width
#define ARR_H    6    // arrow glyph height
#define ARR_GAP  3    // gap between arrow and number

// Arrow + number pair on each axis, laid out around the point at (px, py).
// (fdx, fdy) is the point's offset from centre — it picks which side each
// label sits on. `gap` is the clearance from the point's own glyph.
static void draw_angle_labels(int16_t px, int16_t py, float fdx, float fdy,
                              float pitch, float roll, int16_t gap,
                              uint16_t col) {
    // Horizontal (pitch axis): arrow adjacent to the point, number on far side
    if (fabsf(pitch) >= 1.0f) {
        char nbuf[6];
        snprintf(nbuf, sizeof(nbuf), "%d", (int)roundf(fabsf(pitch)));
        int16_t nw  = (int16_t)(strlen(nbuf) * LBL_CW);
        int16_t ly  = py - LBL_CH / 2;
        int16_t ayo = (LBL_CH - ARR_H) / 2;

        if (ly < 0) ly = 0;
        if (ly + LBL_CH >= HEIGHT) ly = HEIGHT - LBL_CH - 1;

        if (fdx >= 0) {
            // right of point: [←arrow] [number]
            int16_t ax = px + gap;
            int16_t nx = ax + ARR_W + ARR_GAP;
            if (nx + nw >= WIDTH) { nx = WIDTH - nw - 1; ax = nx - ARR_W - ARR_GAP; }
            if (ax < 0) ax = 0;
            draw_arr_left(ax, ly + ayo, col);
            fb_string(nx, ly, nbuf, col, LBL_SIZE);
        } else {
            // left of point: [number] [→arrow]
            int16_t ax = px - gap - ARR_W;
            int16_t nx = ax - ARR_GAP - nw;
            if (nx < 0) { nx = 0; ax = nx + nw + ARR_GAP; }
            if (ax + ARR_W >= WIDTH) ax = WIDTH - ARR_W - 1;
            draw_arr_right(ax, ly + ayo, col);
            fb_string(nx, ly, nbuf, col, LBL_SIZE);
        }
    }

    // Vertical (roll axis): two-row layout, arrow between point and number
    //   above: [number] then [↑arrow] — up arrow points down toward the point
    //   below: [↓arrow] then [number] — down arrow points up toward the point
    if (fabsf(roll) >= 1.0f) {
        char nbuf[6];
        snprintf(nbuf, sizeof(nbuf), "%d", (int)roundf(fabsf(roll)));
        int16_t nw = (int16_t)(strlen(nbuf) * LBL_CW);
        int16_t ax = px - ARR_W / 2;
        int16_t nx = px - nw / 2;

        if (ax < 0) ax = 0;
        if (ax + ARR_W >= WIDTH) ax = WIDTH - ARR_W - 1;
        if (nx < 0) nx = 0;
        if (nx + nw >= WIDTH) nx = WIDTH - nw - 1;

        if (fdy <= 0) {
            int16_t ly = py - gap - LBL_CH - ARR_GAP - ARR_H;
            if (ly < 0) ly = 0;
            fb_string(nx, ly, nbuf, col, LBL_SIZE);
            draw_arr_up(ax, ly + LBL_CH + ARR_GAP, col);
        } else {
            int16_t ly = py + gap;
            if (ly + ARR_H + ARR_GAP + LBL_CH >= HEIGHT)
                ly = HEIGHT - ARR_H - ARR_GAP - LBL_CH - 1;
            draw_arr_down(ax, ly, col);
            fb_string(nx, ly + ARR_H + ARR_GAP, nbuf, col, LBL_SIZE);
        }
    }
}

// Maps pitch/roll to a point on the bubble field, clamped to the outer ring.
static void bubble_pos(float pitch, float roll, int16_t *px, int16_t *py,
                       float *ofdx, float *ofdy) {
    float scale = (float)BUBBLE_TRAVEL / BUBBLE_MAX_DEG;
    float fdx = pitch * scale, fdy = roll * scale;
    float dist = sqrtf(fdx * fdx + fdy * fdy);
    if (dist > (float)BUBBLE_TRAVEL) {
        float s = (float)BUBBLE_TRAVEL / dist;
        fdx *= s; fdy *= s;
    }
    *px = (int16_t)(WIDTH  / 2 + (int16_t)fdx);
    *py = (int16_t)(HEIGHT / 2 + (int16_t)fdy);
    *ofdx = fdx; *ofdy = fdy;
}

// ── Launch mark ───────────────────────────────────────────────────────────
// Dropped where the bubble sat at launch: flickers as a small +, twists into
// an X, then holds. Greys out (but stays put) once the next shot is armed, so
// the previous launch angles can be dialled in again.
#define M_LEN        6    // cross arm half-length — much smaller than A_LEN
#define M_THK        1    // cross arm half-thickness (3 px arms vs the corner
                          // crosses' 5) — X spans ~9 px against their 15
#define M_DOT_R      2    // small enough that the X arms read past it
#define M_GAP        (M_LEN + 5)   // label clearance
#define M_FLICK_MS   1200          // flicker phase length
#define M_FLICK_PER  60            // ms per half-cycle
#define M_MORPH_MS   320           // + → X twist length

// Twist keyframes: arm-1 direction, 0° (+) → 11° → 30° → 56° overshoot → 45° (X)
static const int8_t   s_mv[5][2] = {{1,0},{5,1},{7,4},{4,6},{1,1}};
static const uint16_t s_mt[5]    = {0, 60, 140, 230, 290};

static void draw_launch_mark(float mark_pitch, float mark_roll,
                             bool greyed, uint32_t age) {
    int16_t mx, my;
    float   fdx, fdy;
    bubble_pos(mark_pitch, mark_roll, &mx, &my, &fdx, &fdy);

    uint16_t col = greyed ? COLOR_GREY : COLOR_NGRE;
    int16_t  adx = 1, ady = 1;   // settled X

    if (!greyed) {
        if (age < M_FLICK_MS) {
            if ((age / M_FLICK_PER) % 2 != 0) return;   // off half-cycle
            adx = 1; ady = 0;                           // still a +
        } else if (age < M_FLICK_MS + M_MORPH_MS) {
            uint32_t ph = age - M_FLICK_MS;
            int step = 4;
            for (int i = 0; i < 4; i++)
                if (ph < (uint32_t)s_mt[i + 1]) { step = i; break; }
            adx = s_mv[step][0];
            ady = s_mv[step][1];
        }
    }

    draw_cross(mx, my, adx, ady, M_LEN, M_THK, col);
    fb_fill_circle(mx, my, M_DOT_R, col);
    // Spent mark keeps only the crosshair — the live readout owns the numbers
    if (!greyed)
        draw_angle_labels(mx, my, fdx, fdy, mark_pitch, mark_roll, M_GAP, col);
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
                   bool have_mark, float mark_pitch, float mark_roll,
                   uint32_t mark_t0) {
    fb_clear(COLOR_BG);

    const uint32_t now = to_ms_since_boot(get_absolute_time());
    const int16_t cx = WIDTH / 2, cy = HEIGHT / 2;
    uint16_t cross_color = locked ? COLOR_NGRE : COLOR_DPURP;

    // Crosshair
    fb_hline(0, WIDTH - 1, cy, cross_color);
    fb_vline(cx, 0, HEIGHT - 1, cross_color);

    // Reference rings — inner lpurp / mid dpurp / outer orange (danger zone)
    for (int i = 0; i < 3; i++)
        fb_draw_circle(cx, cy, s_ring_r[i], s_ring_col[i]);

    // Previous launch mark — drawn first so the live bubble sits on top of it
    if (have_mark) draw_launch_mark(mark_pitch, mark_roll, locked, now - mark_t0);

    // Bubble position: pitch drives horizontal (left/right), roll drives vertical (fwd/back)
    int16_t dot_x, dot_y;
    float   fdx, fdy;
    bubble_pos(pitch, roll, &dot_x, &dot_y, &fdx, &fdy);

    fb_fill_circle(dot_x, dot_y, BUBBLE_DOT_R, COLOR_ORANGE);

    // Live angle labels
    draw_angle_labels(dot_x, dot_y, fdx, fdy, pitch, roll,
                      BUBBLE_DOT_R + 6, COLOR_LAVEND);

    // Battery
    draw_battery(bat);

    // Armed overlay — animated corner crosses + ARMED tape strips
    arm_draw_overlay(locked, now);

    fb_flush();
}
