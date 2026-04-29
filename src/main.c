#include <stdio.h>
#include <string.h>
#include <math.h>
#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/i2c.h"
#include "u8g2.h"
#include "ism330dhcx_reg.h"

#define I2C_PORT     i2c1
#define I2C_SDA      2
#define I2C_SCL      3
#define IMU_ADDR     0x6A
#define DISPLAY_ADDR 0x3C
#define BAT_ADDR     0x36

#define WIDTH   128
#define HEIGHT   64

/* ── Button GPIO pins ────────────────────────────────────────────────
   FeatherWing OLED: A=D9, B=D6, C=D5
   https://learn.adafruit.com/adafruit-feather-rp2040-pico/pinouts   */
#define BTN_A  9
#define BTN_B  8
#define BTN_C  7

/* ── IMU / EMA ───────────────────────────────────────────────────── */
#define ALPHA_TEXT  0.20f
#define ALPHA_LINE  0.08f
#define ROLL_PPD    (HEIGHT / 5.0f)
#define PITCH_PPD   (63.0f / 5.0f)

/* ── Bubble mode ─────────────────────────────────────────────────── */
#define BUBBLE_MAX_DEG  45.0f
#define BUBBLE_TRAVEL_X 60
#define BUBBLE_TRAVEL_Y 28
#define BUBBLE_DOT_R     3

/* ── Menu ────────────────────────────────────────────────────────── */
#define MENU_HOLD_MS  1000
#define MENU_ITEM_H   20

/* VIEW_BUBBLE / VIEW_GAUGE are sub-screens of ANGLE_MODE.
   A opens the menu. B = bubble, C = gauge (when menu closed).    */
typedef enum { VIEW_BUBBLE, VIEW_GAUGE, VIEW_RPM, VIEW_STATS } view_t;

/* ── u8g2 I²C callbacks ──────────────────────────────────────────── */

static uint8_t i2c_buf[256];
static uint8_t i2c_buf_len;

static uint8_t u8g2_byte_i2c(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr) {
    switch (msg) {
    case U8X8_MSG_BYTE_SEND:
        memcpy(i2c_buf + i2c_buf_len, arg_ptr, arg_int);
        i2c_buf_len += arg_int;
        break;
    case U8X8_MSG_BYTE_START_TRANSFER:
        i2c_buf_len = 0;
        break;
    case U8X8_MSG_BYTE_END_TRANSFER:
        i2c_write_blocking(I2C_PORT, u8x8_GetI2CAddress(u8x8) >> 1,
                           i2c_buf, i2c_buf_len, false);
        break;
    }
    return 1;
}

static uint8_t u8g2_gpio_delay(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr) {
    (void)u8x8; (void)arg_ptr;
    switch (msg) {
    case U8X8_MSG_DELAY_MILLI:   sleep_ms(arg_int); break;
    case U8X8_MSG_DELAY_10MICRO: sleep_us(10);      break;
    case U8X8_MSG_DELAY_100NANO: sleep_us(1);       break;
    }
    return 1;
}

/* ── ISM330DHCX platform wrappers ────────────────────────────────── */

static int32_t imu_write(void *hdl, uint8_t reg, const uint8_t *buf, uint16_t len) {
    (void)hdl;
    uint8_t tmp[8];
    tmp[0] = reg;
    memcpy(tmp + 1, buf, len);
    i2c_write_blocking(I2C_PORT, IMU_ADDR, tmp, len + 1, false);
    return 0;
}

static int32_t imu_read(void *hdl, uint8_t reg, uint8_t *buf, uint16_t len) {
    (void)hdl;
    i2c_write_blocking(I2C_PORT, IMU_ADDR, &reg, 1, true);
    i2c_read_blocking(I2C_PORT, IMU_ADDR, buf, len, false);
    return 0;
}

static void imu_delay_ms(uint32_t ms) { sleep_ms(ms); }

/* ── MAX17048 battery ────────────────────────────────────────────── */

static uint16_t bat_read_reg(uint8_t reg) {
    uint8_t buf[2];
    i2c_write_blocking(I2C_PORT, BAT_ADDR, &reg, 1, true);
    i2c_read_blocking(I2C_PORT, BAT_ADDR, buf, 2, false);
    return (uint16_t)(buf[0] << 8) | buf[1];
}

static uint8_t bat_percent(void) {
    uint8_t pct = bat_read_reg(0x04) >> 8;
    return pct > 100 ? 100 : pct;
}

static uint16_t bat_mv(void) {
    return (uint16_t)((bat_read_reg(0x02) >> 4) * 125 / 100);
}

/* ── Drawing helpers ─────────────────────────────────────────────── */

static u8g2_t u8g2;

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
/*
 * 30×12 body + 3×6 terminal nib in the top-right corner.
 * Text is drawn first in white, then an XOR fill box sweeps left-to-right
 * across the filled portion — the overlap inverts to black-on-white.
 */
static void draw_battery(uint8_t pct) {
    const int16_t bx = WIDTH - 14;  /* left edge of 12px body */
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

static void draw_bubble(float roll, float pitch, uint8_t bat) {
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

static void draw_gauge(float r_line, float p_line, int16_t r, int16_t p, uint8_t bat) {
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

static void draw_rpm(void) {
    u8g2_SetFont(&u8g2, u8g2_font_profont17_tf);
    draw_centered(WIDTH / 2, 22, "RPM MODE");
    draw_centered(WIDTH / 2, 45, "TBD");
}

/* ── Screen: stats ───────────────────────────────────────────────── */

static void draw_beetle(void) {
    const int16_t ox = 110, oy = 1;

    /* Mandibles — forked pincers */
    u8g2_DrawLine(&u8g2, ox+5, oy+5, ox+2, oy+1);
    u8g2_DrawLine(&u8g2, ox+5, oy+5, ox+4, oy+2);
    u8g2_DrawLine(&u8g2, ox+9, oy+5, ox+12, oy+1);
    u8g2_DrawLine(&u8g2, ox+9, oy+5, ox+10, oy+2);

    /* Head */
    u8g2_DrawDisc(&u8g2, ox+7, oy+7, 2, U8G2_DRAW_ALL);

    /* Thorax */
    u8g2_DrawBox(&u8g2, ox+5, oy+10, 5, 3);

    /* Elytra (wing covers) with centre seam */
    u8g2_DrawRBox(&u8g2, ox+4, oy+13, 7, 8, 2);
    u8g2_DrawVLine(&u8g2, ox+7, oy+14, 6);

    /* Legs — 3 per side */
    u8g2_DrawLine(&u8g2, ox+5, oy+11, ox+2, oy+9);
    u8g2_DrawLine(&u8g2, ox+5, oy+13, ox+2, oy+12);
    u8g2_DrawLine(&u8g2, ox+5, oy+17, ox+2, oy+19);
    u8g2_DrawLine(&u8g2, ox+9, oy+11, ox+12, oy+9);
    u8g2_DrawLine(&u8g2, ox+9, oy+13, ox+12, oy+12);
    u8g2_DrawLine(&u8g2, ox+9, oy+17, ox+12, oy+19);
}

static void draw_stats(void) {
    u8g2_SetFont(&u8g2, u8g2_font_5x7_tf);
    u8g2_DrawStr(&u8g2, 2,  8,  "BEYBEETLE V0.1");
    u8g2_DrawHLine(&u8g2, 0, 11, WIDTH);
    u8g2_DrawStr(&u8g2, 2, 22,  "BLADER: CHAMBER");
    u8g2_DrawStr(&u8g2, 2, 32,  "TOP SHOOT: N/A");
    u8g2_DrawStr(&u8g2, 2, 42,  "LAUNCHES: N/A");
    draw_beetle();
}

/* ── Menu overlay ────────────────────────────────────────────────── */
/*
 * Items: 0 = ANGLE_MODE_  (hold A)
 *        1 = RPM_MODE_    (hold B)
 *        2 = BACK_        (press C, immediate)
 *
 * Fill animation uses XOR draw mode so the text inverts as the bar
 * sweeps over it — no clipping needed.
 *
 * Progress curve: sqrtf(t) — fast at the start, eases in at the end.
 */
static void draw_menu(int8_t held_item, float progress) {
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

        /* White text */
        u8g2_SetDrawColor(&u8g2, 1);
        u8g2_DrawStr(&u8g2, 5, baseline, labels[i]);

        /* XOR fill sweeps over the item, inverting text colour */
        float item_prog = (i == held_item) ? progress : 0.0f;
        if (item_prog > 0.0f) {
            int16_t fw = (int16_t)((WIDTH - 2) * item_prog);
            u8g2_SetDrawColor(&u8g2, 2);   /* XOR */
            u8g2_DrawBox(&u8g2, 1, iy, fw, ih);
            u8g2_SetDrawColor(&u8g2, 1);
        }
    }
}

/* ── Main ────────────────────────────────────────────────────────── */

int main(void) {
    stdio_init_all();

    i2c_init(I2C_PORT, 400000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    const uint btn_pins[] = { BTN_A, BTN_B, BTN_C };
    for (int i = 0; i < 3; i++) {
        gpio_init(btn_pins[i]);
        gpio_set_dir(btn_pins[i], GPIO_IN);
        gpio_pull_up(btn_pins[i]);
    }

    u8g2_Setup_sh1107_i2c_64x128_f(&u8g2, U8G2_R1,
        u8g2_byte_i2c, u8g2_gpio_delay);
    u8g2_SetI2CAddress(&u8g2, DISPLAY_ADDR << 1);
    u8g2_InitDisplay(&u8g2);
    u8g2_SetPowerSave(&u8g2, 0);

    stmdev_ctx_t imu = {
        .write_reg = imu_write,
        .read_reg  = imu_read,
        .mdelay    = imu_delay_ms,
        .handle    = NULL,
    };

    ism330dhcx_reset_set(&imu, PROPERTY_ENABLE);
    uint8_t rst;
    do { ism330dhcx_reset_get(&imu, &rst); } while (rst);
    ism330dhcx_xl_data_rate_set(&imu, ISM330DHCX_XL_ODR_104Hz);
    ism330dhcx_xl_full_scale_set(&imu, ISM330DHCX_2g);

    float r_text = 0.0f, p_text = 0.0f;
    float r_line = 0.0f, p_line = 0.0f;

    uint8_t  bat      = bat_percent();
    uint16_t bat_mv_v = bat_mv();
    uint32_t bat_tick = 0;

    view_t   view      = VIEW_BUBBLE;
    bool     menu_open = false;
    int8_t   menu_held = -1;       /* 0=ANGLE_MODE, 1=RPM_MODE */
    uint32_t hold_start = 0;

    bool prev_a = false, prev_b = false, prev_c = false;

    for (;;) {
        uint32_t now = to_ms_since_boot(get_absolute_time());

        /* ── Sensor ─────────────────────────────────────────────── */
        int16_t raw[3];
        ism330dhcx_acceleration_raw_get(&imu, raw);

        float ax =  (float)raw[0];
        float ay =  (float)raw[2];
        float az = -(float)raw[1];

        float r_raw = fmaxf(-45.0f, fminf(45.0f,
            atan2f(ay, az) * (180.0f / (float)M_PI)));
        float p_raw = fmaxf(-45.0f, fminf(45.0f,
            atan2f(-ax, sqrtf(ay*ay + az*az)) * (180.0f / (float)M_PI)));

        r_text = ALPHA_TEXT * r_raw + (1.0f - ALPHA_TEXT) * r_text;
        p_text = ALPHA_TEXT * p_raw + (1.0f - ALPHA_TEXT) * p_text;
        r_line = ALPHA_LINE * r_raw + (1.0f - ALPHA_LINE) * r_line;
        p_line = ALPHA_LINE * p_raw + (1.0f - ALPHA_LINE) * p_line;

        int16_t r = (int16_t)fmaxf(-45, fminf(45, roundf(r_text)));
        int16_t p = (int16_t)fmaxf(-45, fminf(45, roundf(p_text)));

        /* ── Battery (once per ~second) ─────────────────────────── */
        if (++bat_tick >= 40) {
            bat      = bat_percent();
            bat_mv_v = bat_mv();
            bat_tick = 0;
            printf("bat: %d%% (%dmV)\n", bat, bat_mv_v);
        }

        /* ── Buttons ────────────────────────────────────────────── */
        bool a = !gpio_get(BTN_A);
        bool b = !gpio_get(BTN_B);
        bool c = !gpio_get(BTN_C);
        bool a_dn = a && !prev_a;
        bool b_dn = b && !prev_b;
        bool c_dn = c && !prev_c;

        if (menu_open) {
            /* Hold A / B / C to select; release before completion cancels */
            if (a_dn) { menu_held = 0; hold_start = now; }
            if (b_dn) { menu_held = 1; hold_start = now; }
            if (c_dn) { menu_held = 2; hold_start = now; }
            if (!a && prev_a && menu_held == 0) menu_held = -1;
            if (!b && prev_b && menu_held == 1) menu_held = -1;
            if (!c && prev_c && menu_held == 2) menu_held = -1;
        } else {
            if (a_dn) { menu_open = true; menu_held = -1; }
            if (view != VIEW_RPM && view != VIEW_STATS && b_dn) view = VIEW_BUBBLE;
            if (view != VIEW_RPM && view != VIEW_STATS && c_dn) view = VIEW_GAUGE;
        }

        /* ── Hold progress ──────────────────────────────────────── */
        float menu_prog = 0.0f;
        if (menu_open && menu_held >= 0) {
            float t = fminf((float)(now - hold_start) / (float)MENU_HOLD_MS, 1.0f);
            menu_prog = sqrtf(t);   /* ease-out: fast start, slow finish */

            if (t >= 1.0f) {
                view      = (menu_held == 0) ? VIEW_BUBBLE
                          : (menu_held == 1) ? VIEW_RPM : VIEW_STATS;
                menu_open = false;
                menu_held = -1;
            }
        }

        prev_a = a; prev_b = b; prev_c = c;

        /* ── Draw ───────────────────────────────────────────────── */
        u8g2_ClearBuffer(&u8g2);

        if (menu_open) {
            draw_menu(menu_held, menu_prog);
        } else {
            switch (view) {
            case VIEW_BUBBLE: draw_bubble(r_line, p_line, bat);         break;
            case VIEW_GAUGE:  draw_gauge(r_line, p_line, r, p, bat);    break;
            case VIEW_RPM:    draw_rpm();                                break;
            case VIEW_STATS:  draw_stats();                              break;
            }
        }

        u8g2_SendBuffer(&u8g2);
    }
}
