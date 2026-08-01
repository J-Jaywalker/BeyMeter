#include <string.h>
#include <stdbool.h>
#include "hw.h"
#include "driver_st7789_interface.h"
#include "hardware/clocks.h"
#include "hardware/pll.h"
#include "hardware/xosc.h"
#include "hardware/structs/clocks.h"
#include "hardware/regs/io_bank0.h"
#include "pico/runtime_init.h"   // runtime_init_clocks() — restores the PLLs

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

static void bat_write_reg(uint8_t reg, uint16_t val) {
    uint8_t buf[3] = { reg, (uint8_t)(val >> 8), (uint8_t)val };
    i2c_write_blocking(I2C_PORT, BAT_ADDR, buf, 3, false);
}

// MAX17048 CONFIG (0x0C): POR default 0x971C, bit 7 of the low byte = SLEEP.
// SLEEP only takes effect once EnSleep (bit 13 of MODE, 0x06) has been set.
// Writing the default back on wake avoids a read-modify-write on a gauge that
// might not be fitted.
#define BAT_CONFIG_DEFAULT 0x971CU
#define BAT_CONFIG_SLEEP   0x0080U

void hw_bat_sleep(void) {
    bat_write_reg(0x06, 0x2000);                                  // MODE: EnSleep
    bat_write_reg(0x0C, BAT_CONFIG_DEFAULT | BAT_CONFIG_SLEEP);
}

void hw_bat_wake(void) {
    bat_write_reg(0x0C, BAT_CONFIG_DEFAULT);
}

uint8_t bat_percent(void) {
    uint8_t pct = bat_read_reg(0x04) >> 8;
    return pct > 100 ? 100 : pct;
}

/* ── Hardware init ───────────────────────────────────────────────── */

void hw_init(void) {
    // NeoPixel power off
    gpio_init(16);
    gpio_set_dir(16, GPIO_OUT);
    gpio_put(16, 0);
    gpio_init(17);
    gpio_set_dir(17, GPIO_OUT);
    gpio_put(17, 1);

    // D2F lock button — active-low, internal pull-up
    gpio_init(BTN_LOCK);
    gpio_set_dir(BTN_LOCK, GPIO_IN);
    gpio_pull_up(BTN_LOCK);

    // Backlight — no-op until LITE is wired and BL_PIN defined (see hw.h)
#ifdef BL_PIN
    gpio_init(BL_PIN);
    gpio_set_dir(BL_PIN, GPIO_OUT);
#endif
    hw_backlight(true);

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

    // Clear the gauge's sleep bit in case we got here from a wake
    hw_bat_wake();
}

/* ── Sleep / wake ────────────────────────────────────────────────── */

void hw_backlight(bool on) {
#ifdef BL_PIN
    gpio_put(BL_PIN, on ? 1 : 0);
#else
    (void)on;   // LITE unwired — the backlight cannot be switched from here
#endif
}

void hw_display_sleep(void) {
    st7789_display_off(&g_st7789);   // blank every pixel
    st7789_sleep_in(&g_st7789);      // stop the panel booster and oscillator
    sleep_ms(5);
}

void hw_imu_sleep(stmdev_ctx_t *imu) {
    ism330dhcx_xl_data_rate_set(imu, ISM330DHCX_XL_ODR_OFF);
}

// Parks the chip in DORMANT — every clock stopped, including the crystal — and
// returns once BTN_LOCK is pulled low. Nothing runs in between.
void hw_dormant_until_button(void) {
    const uint32_t src_hz = XOSC_KHZ * 1000;
    const uint32_t wake_ev = IO_BANK0_DORMANT_WAKE_INTE0_GPIO0_EDGE_LOW_BITS;

    // We wake on a falling edge, so never go under with the button already
    // down — that edge would never arrive and the meter would hang
    while (!gpio_get(BTN_LOCK))
        tight_loop_contents();

    // DORMANT halts the crystal, so nothing may still be running off a PLL
    clock_configure(clk_ref, CLOCKS_CLK_REF_CTRL_SRC_VALUE_XOSC_CLKSRC, 0,
                    src_hz, src_hz);
    clock_configure(clk_sys, CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLK_REF, 0,
                    src_hz, src_hz);
    clock_configure(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS,
                    src_hz, src_hz);
    clock_stop(clk_usb);
    clock_stop(clk_adc);
    clock_stop(clk_rtc);
    pll_deinit(pll_sys);
    pll_deinit(pll_usb);

    gpio_set_dormant_irq_enabled(BTN_LOCK, wake_ev, true);
    xosc_dormant();                  // ← stops here until the button is pressed
    gpio_acknowledge_irq(BTN_LOCK, wake_ev);
    gpio_set_dormant_irq_enabled(BTN_LOCK, wake_ev, false);

    runtime_init_clocks();           // PLLs back up, full speed
}

/* ── Hardware presence checks ────────────────────────────────────── */

bool hw_check_imu(void) {
    // ISM330DHCX WHO_AM_I = 0x0F, expected response = 0x6B
    uint8_t reg = 0x0F, val = 0;
    if (i2c_write_blocking(I2C_PORT, IMU_ADDR, &reg, 1, true) < 0) return false;
    if (i2c_read_blocking(I2C_PORT, IMU_ADDR, &val, 1, false) < 0) return false;
    return val == 0x6B;
}

bool hw_check_battery(void) {
    // MAX17048 — just verify the device ACKs on the I2C bus
    uint8_t reg = 0x04;  // SOC register
    return i2c_write_blocking(I2C_PORT, BAT_ADDR, &reg, 1, false) >= 0;
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
