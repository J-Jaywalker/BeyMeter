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

extern st7789_handle_t g_st7789;

void         hw_init(void);
stmdev_ctx_t hw_imu_init(void);
uint8_t      bat_percent(void);
