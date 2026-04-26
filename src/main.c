#include <stdio.h>
#include <string.h>
#include <math.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "u8g2.h"
#include "ism330dhcx_reg.h"

#define I2C_PORT     i2c1
#define I2C_SDA      2
#define I2C_SCL      3
#define IMU_ADDR     0x6A
#define DISPLAY_ADDR 0x3C

#define WIDTH   128
#define HEIGHT   64

#define ALPHA_TEXT  0.20f
#define ALPHA_LINE  0.08f
#define ROLL_PPD    (HEIGHT / 5.0f)   /* px per degree, wraps every 5 deg */
#define PITCH_PPD   (63.0f / 5.0f)

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

/* ── Main loop ───────────────────────────────────────────────────── */

int main(void) {
    stdio_init_all();

    i2c_init(I2C_PORT, 400000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    /* Display — 64x128 panel rotated 90° CW → logical 128x64 */
    u8g2_Setup_sh1107_i2c_64x128_f(&u8g2, U8G2_R1,
        u8g2_byte_i2c, u8g2_gpio_delay);
    u8g2_SetI2CAddress(&u8g2, DISPLAY_ADDR << 1);
    u8g2_InitDisplay(&u8g2);
    u8g2_SetPowerSave(&u8g2, 0);
    u8g2_SetFont(&u8g2, u8g2_font_profont17_tf);

    /* IMU */
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

    /* EMA state — two speeds: fast for text readout, slow for line animation */
    float r_text = 0.0f, p_text = 0.0f;
    float r_line = 0.0f, p_line = 0.0f;

    for (;;) {
        /* Read raw accelerometer */
        int16_t raw[3];
        ism330dhcx_acceleration_raw_get(&imu, raw);

        /* Remap axes for −90° roll mounting (mirrors axis swap in poc.py) */
        float ax =  (float)raw[0];
        float ay =  (float)raw[2];   /* old az */
        float az = -(float)raw[1];   /* −old ay */

        float r_raw = fmaxf(-90.0f, fminf(90.0f,
            atan2f(ay, az) * (180.0f / (float)M_PI)));
        float p_raw = fmaxf(-90.0f, fminf(90.0f,
            atan2f(-ax, sqrtf(ay*ay + az*az)) * (180.0f / (float)M_PI)));

        r_text = ALPHA_TEXT * r_raw + (1.0f - ALPHA_TEXT) * r_text;
        p_text = ALPHA_TEXT * p_raw + (1.0f - ALPHA_TEXT) * p_text;
        r_line = ALPHA_LINE * r_raw + (1.0f - ALPHA_LINE) * r_line;
        p_line = ALPHA_LINE * p_raw + (1.0f - ALPHA_LINE) * p_line;

        int16_t r = (int16_t)fmaxf(-90, fminf(90, roundf(r_text)));
        int16_t p = (int16_t)fmaxf(-90, fminf(90, roundf(p_text)));

        /* Line positions — positive modulo matches Python's % behaviour */
        int16_t roll_y  = (((int16_t)(32.0f + r_line * ROLL_PPD) % HEIGHT) + HEIGHT) % HEIGHT;
        int16_t pitch_x = 65 + ((((int16_t)(31.0f - p_line * PITCH_PPD) % 63) + 63) % 63);

        /* ── Build frame ──────────────────────────────────────────── */
        u8g2_ClearBuffer(&u8g2);

        /* Centre divider */
        u8g2_DrawVLine(&u8g2, 63, 0, HEIGHT);

        /* Dashed indicator lines (full extent across each half) */
        dashed_hline(0, 62, roll_y);
        dashed_vline(pitch_x, 0, HEIGHT - 1);

        /* Black masks — erase line pixels behind text and chevron zones */
        u8g2_SetDrawColor(&u8g2, 0);
        u8g2_DrawBox(&u8g2,  9, 20, 44, 24);   /* LHS text area   */
        u8g2_DrawBox(&u8g2, 74, 20, 44, 24);   /* RHS text area   */
        u8g2_DrawBox(&u8g2, 20,  0, 23, 13);   /* LHS chevron top */
        u8g2_DrawBox(&u8g2, 20, 51, 23, 13);   /* LHS chevron btm */
        u8g2_DrawBox(&u8g2, 65, 24, 11, 17);   /* RHS chevron lft */
        u8g2_DrawBox(&u8g2,119, 24,  9, 17);   /* RHS chevron rgt */
        u8g2_SetDrawColor(&u8g2, 1);

        /* Chevrons — absolute coords derived from vectorio.Polygon in poc.py */
        if (r > 0) u8g2_DrawTriangle(&u8g2, 31,  2, 23, 10, 39, 10); /* roll up   */
        if (r < 0) u8g2_DrawTriangle(&u8g2, 31, 62, 23, 54, 39, 54); /* roll down */
        if (p > 0) u8g2_DrawTriangle(&u8g2, 67, 32, 73, 26, 73, 38); /* pitch fwd */
        if (p < 0) u8g2_DrawTriangle(&u8g2,127, 32,121, 26,121, 38); /* pitch bk  */

        /* Value labels */
        char r_str[4], p_str[4];
        snprintf(r_str, sizeof(r_str), "%02d", abs(r));
        snprintf(p_str, sizeof(p_str), "%02d", abs(p));
        draw_centered(31, 32, r_str);
        draw_centered(96, 32, p_str);

        u8g2_SendBuffer(&u8g2);
        /* No explicit sleep — I²C transfer (~25 ms at 400 kHz) is the rate limiter */
    }
}
