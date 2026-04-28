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
#define BAT_ADDR     0x36   /* MAX17048 fuel gauge */

#define WIDTH   128
#define HEIGHT   64

/* ── Button GPIO pins ────────────────────────────────────────────────
   FeatherWing OLED: A=D9, B=D6, C=D5
   https://learn.adafruit.com/adafruit-feather-rp2040-pico/pinouts   */
#define BTN_A  9
#define BTN_B  8
#define BTN_C  7

#define DEBOUNCE_MS  150

/* ── IMU / EMA ───────────────────────────────────────────────────── */
#define ALPHA_TEXT  0.20f
#define ALPHA_LINE  0.08f
#define ROLL_PPD    (HEIGHT / 5.0f)
#define PITCH_PPD   (63.0f / 5.0f)

/* ── Bubble mode ─────────────────────────────────────────────────── */
#define BUBBLE_MAX_DEG  75.0f
#define BUBBLE_TRAVEL_X 60    /* px from centre to outer ring, horizontal */
#define BUBBLE_TRAVEL_Y 28    /* px from centre to outer ring, vertical   */
#define BUBBLE_DOT_R     3

typedef enum { MODE_BUBBLE, MODE_SPLIT, MODE_NYI } mode_t;

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

/* ── MAX17048 battery fuel gauge ─────────────────────────────────── */

static uint16_t bat_read_reg(uint8_t reg) {
    uint8_t buf[2];
    i2c_write_blocking(I2C_PORT, BAT_ADDR, &reg, 1, true);
    i2c_read_blocking(I2C_PORT, BAT_ADDR, buf, 2, false);
    return (uint16_t)(buf[0] << 8) | buf[1];
}

/* Returns state of charge 0-100 (integer %) */
static uint8_t bat_percent(void) {
    uint16_t raw = bat_read_reg(0x04);  /* SOC register */
    uint8_t pct = raw >> 8;             /* MSB = whole percent */
    return pct > 100 ? 100 : pct;
}

/* Returns cell voltage in mV */
static uint16_t bat_mv(void) {
    uint16_t raw = bat_read_reg(0x02);  /* VCELL register */
    return (uint16_t)((raw >> 4) * 125 / 100);  /* 1.25mV per LSB */
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

/* ── Screen: spirit level bubble ─────────────────────────────────── */

static void draw_bubble(float roll, float pitch, uint8_t bat) {
    u8g2_SetFont(&u8g2, u8g2_font_5x7_tf);

    u8g2_DrawFrame(&u8g2, 0, 0, WIDTH, HEIGHT);
    u8g2_DrawHLine(&u8g2, 2, HEIGHT / 2, WIDTH - 4);
    u8g2_DrawVLine(&u8g2, WIDTH / 2, 2, HEIGHT - 4);
    /* Zero-point circle */
    u8g2_DrawCircle(&u8g2, WIDTH / 2, HEIGHT / 2, 4, U8G2_DRAW_ALL);
    /* Rounded rectangles at 15° intervals */
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

    /* Battery % top-right */
    snprintf(buf, sizeof(buf), "%d%%", bat);
    u8g2_DrawStr(&u8g2, WIDTH - u8g2_GetStrWidth(&u8g2, buf) - 3, 8, buf);
}

/* ── Screen: split roll / pitch readout ─────────────────────────── */

static void draw_split(float r_line, float p_line, int16_t r, int16_t p, uint8_t bat) {
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

    /* Battery % bottom-centre, small font */
    u8g2_SetFont(&u8g2, u8g2_font_5x7_tf);
    char buf[6];
    snprintf(buf, sizeof(buf), "%d%%", bat);
    u8g2_DrawStr(&u8g2, 64 - u8g2_GetStrWidth(&u8g2, buf) / 2, HEIGHT - 1, buf);
}

/* ── Screen: not yet implemented ─────────────────────────────────── */

static void draw_nyi(void) {
    u8g2_SetFont(&u8g2, u8g2_font_profont17_tf);
    draw_centered(WIDTH / 2, 22, "NOT YET");
    draw_centered(WIDTH / 2, 45, "IMPLEMENTED");
}

/* ── Button polling ──────────────────────────────────────────────── */

static mode_t poll_buttons(mode_t current) {
    static bool prev_a = true, prev_b = true, prev_c = true;
    static uint32_t last_ms = 0;
    uint32_t now = to_ms_since_boot(get_absolute_time());

    bool a = gpio_get(BTN_A);
    bool b = gpio_get(BTN_B);
    bool c = gpio_get(BTN_C);

    mode_t next = current;
    if (now - last_ms >= DEBOUNCE_MS) {
        if (!a && prev_a) { next = MODE_BUBBLE; last_ms = now; }
        if (!b && prev_b) { next = MODE_SPLIT;  last_ms = now; }
        if (!c && prev_c) { next = MODE_NYI;    last_ms = now; }
    }

    prev_a = a; prev_b = b; prev_c = c;
    return next;
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
    mode_t mode = MODE_BUBBLE;

    /* Battery is slow-changing — read once per second (~40 frames) */
    uint8_t bat = bat_percent();
    uint16_t bat_mv_val = bat_mv();
    uint32_t bat_tick = 0;

    for (;;) {
        int16_t raw[3];
        ism330dhcx_acceleration_raw_get(&imu, raw);

        float ax =  (float)raw[0];
        float ay =  (float)raw[2];
        float az = -(float)raw[1];

        float r_raw = fmaxf(-75.0f, fminf(75.0f,
            atan2f(ay, az) * (180.0f / (float)M_PI)));
        float p_raw = fmaxf(-75.0f, fminf(75.0f,
            atan2f(-ax, sqrtf(ay*ay + az*az)) * (180.0f / (float)M_PI)));

        r_text = ALPHA_TEXT * r_raw + (1.0f - ALPHA_TEXT) * r_text;
        p_text = ALPHA_TEXT * p_raw + (1.0f - ALPHA_TEXT) * p_text;
        r_line = ALPHA_LINE * r_raw + (1.0f - ALPHA_LINE) * r_line;
        p_line = ALPHA_LINE * p_raw + (1.0f - ALPHA_LINE) * p_line;

        int16_t r = (int16_t)fmaxf(-75, fminf(75, roundf(r_text)));
        int16_t p = (int16_t)fmaxf(-75, fminf(75, roundf(p_text)));

        if (++bat_tick >= 40) {
            bat       = bat_percent();
            bat_mv_val = bat_mv();
            bat_tick  = 0;
            printf("bat: %d%% (%dmV)\n", bat, bat_mv_val);
        }

        mode = poll_buttons(mode);

        u8g2_ClearBuffer(&u8g2);
        switch (mode) {
        case MODE_BUBBLE: draw_bubble(r_line, p_line, bat);        break;
        case MODE_SPLIT:  draw_split(r_line, p_line, r, p, bat);   break;
        case MODE_NYI:    draw_nyi();                               break;
        }
        u8g2_SendBuffer(&u8g2);
    }
}
