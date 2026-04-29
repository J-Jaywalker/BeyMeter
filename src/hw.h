#pragma once
#include <stdint.h>
#include "pico/stdlib.h"
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

#define BTN_A  9
#define BTN_B  8
#define BTN_C  7

#define ALPHA_TEXT  0.20f
#define ALPHA_LINE  0.08f
#define ROLL_PPD    (HEIGHT / 5.0f)
#define PITCH_PPD   (63.0f / 5.0f)

#define BUBBLE_MAX_DEG  45.0f
#define BUBBLE_TRAVEL_X 60
#define BUBBLE_TRAVEL_Y 28
#define BUBBLE_DOT_R     3

#define MENU_HOLD_MS  1000
#define MENU_ITEM_H   20

typedef enum { VIEW_BUBBLE, VIEW_GAUGE, VIEW_RPM, VIEW_STATS } view_t;

extern u8g2_t u8g2;

void         hw_init(void);
stmdev_ctx_t hw_imu_init(void);
uint8_t      bat_percent(void);
uint16_t     bat_mv(void);
