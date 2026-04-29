#include <string.h>
#include "hw.h"

u8g2_t u8g2;

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

uint8_t bat_percent(void) {
    uint8_t pct = bat_read_reg(0x04) >> 8;
    return pct > 100 ? 100 : pct;
}

uint16_t bat_mv(void) {
    return (uint16_t)((bat_read_reg(0x02) >> 4) * 125 / 100);
}

/* ── Hardware init ───────────────────────────────────────────────── */

void hw_init(void) {
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
}

/* ── IMU init ────────────────────────────────────────────────────── */

stmdev_ctx_t hw_imu_init(void) {
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
    return imu;
}
