#pragma once
#include <stdint.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/spi.h"
#include "driver_st7789.h"
#include "ism330dhcx_reg.h"

// I2C — IMU and battery gauge (STEMMA QT)
#define I2C_PORT  i2c1
#define I2C_SDA   2
#define I2C_SCL   3
#define IMU_ADDR  0x6A
#define BAT_ADDR  0x36

// SPI — display
#define SPI_PORT    spi0
#define SPI_SCK     18
#define SPI_MOSI    19
#define DISPLAY_CS  9    // D9
#define DISPLAY_DC  10   // D10
#define BTN_LOCK    11   // D11 — D2F NO, active-low
#define SPI_BAUD    62500000

#define WIDTH   240
#define HEIGHT  240

#define ALPHA  0.10f
#define Y_OFF   80      // MADCTL=0xC0: GRAM rows 80-319 map to display rows 0-239

#define BUBBLE_MAX_DEG  45.0f
#define BUBBLE_TRAVEL   105
#define BUBBLE_DOT_R      7

// ── Sleep / wake ──────────────────────────────────────────────────────────
// The display's LITE pin is not wired on this board — the breakout pulls it
// high, so the backlight is unconditionally on and is the dominant current
// draw. Run a wire from LITE to a free GPIO (27 = A1 is clear; D12/A0 are
// earmarked for the TCRT5000), define BL_PIN below, and the sleep path will
// switch the backlight off for free. Until then hw_backlight() is a no-op.
// #define BL_PIN 27

#define SLEEP_IDLE_MS  (3u * 60u * 1000u)   // no lock this long → sleep
#define WAKE_HOLD_MS   3000u                // hold lock this long to boot again

extern st7789_handle_t g_st7789;

void         hw_init(void);
stmdev_ctx_t hw_imu_init(void);
uint8_t      bat_percent(void);
bool         hw_check_imu(void);
bool         hw_check_battery(void);

void hw_backlight(bool on);
void hw_display_sleep(void);            // DISPOFF + SLPIN — panel logic off
void hw_imu_sleep(stmdev_ctx_t *imu);   // accelerometer to power-down
void hw_bat_sleep(void);
void hw_bat_wake(void);
void hw_dormant_until_button(void);     // returns once BTN_LOCK is pressed
