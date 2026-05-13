#include <string.h>
#include "hw.h"
#include "driver_st7789_interface.h"

st7789_handle_t g_st7789;

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

/* ── Hardware init ───────────────────────────────────────────────── */

void hw_init(void) {
    // NeoPixel power off
    gpio_init(17);
    gpio_set_dir(17, GPIO_OUT);
    gpio_put(17, 0);

    // I2C for IMU and battery gauge
    i2c_init(I2C_PORT, 400000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    // SPI for display
    spi_init(SPI_PORT, SPI_BAUD);
    gpio_set_function(SPI_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(SPI_MOSI, GPIO_FUNC_SPI);

    gpio_init(DISPLAY_CS);
    gpio_set_dir(DISPLAY_CS, GPIO_OUT);
    gpio_put(DISPLAY_CS, 1);

    gpio_init(DISPLAY_DC);
    gpio_set_dir(DISPLAY_DC, GPIO_OUT);
    gpio_put(DISPLAY_DC, 0);

    // Wire up st7789 handle
    g_st7789.spi_init                = st7789_interface_spi_init;
    g_st7789.spi_deinit              = st7789_interface_spi_deinit;
    g_st7789.spi_write_cmd           = st7789_interface_spi_write_cmd;
    g_st7789.cmd_data_gpio_init      = st7789_interface_cmd_data_gpio_init;
    g_st7789.cmd_data_gpio_deinit    = st7789_interface_cmd_data_gpio_deinit;
    g_st7789.cmd_data_gpio_write     = st7789_interface_cmd_data_gpio_write;
    g_st7789.reset_gpio_init         = st7789_interface_reset_gpio_init;
    g_st7789.reset_gpio_deinit       = st7789_interface_reset_gpio_deinit;
    g_st7789.reset_gpio_write        = st7789_interface_reset_gpio_write;
    g_st7789.delay_ms                = st7789_interface_delay_ms;
    g_st7789.debug_print             = st7789_interface_debug_print;

    st7789_init(&g_st7789);

    // Configure display
    st7789_sleep_out(&g_st7789);
    sleep_ms(120);
    st7789_set_interface_pixel_format(&g_st7789,
        ST7789_RGB_INTERFACE_COLOR_FORMAT_65K,
        ST7789_CONTROL_INTERFACE_COLOR_FORMAT_16_BIT);
    st7789_set_memory_data_access_control(&g_st7789, 0xC0);
    st7789_set_column(&g_st7789, WIDTH);
    st7789_set_row(&g_st7789, HEIGHT + Y_OFF);
    st7789_set_column_address(&g_st7789, 0, WIDTH - 1);
    st7789_set_row_address(&g_st7789, Y_OFF, HEIGHT - 1 + Y_OFF);
    st7789_display_inversion_on(&g_st7789);
    st7789_normal_display_mode_on(&g_st7789);
    st7789_display_on(&g_st7789);
    sleep_ms(10);
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
