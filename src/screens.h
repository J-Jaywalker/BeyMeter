#pragma once
#include <stdint.h>
#include "hw.h"

void draw_bubble(float roll, float pitch, uint8_t bat);
void draw_gauge(float r_line, float p_line, int16_t r, int16_t p, uint8_t bat);
void draw_rpm(void);
void draw_stats(void);
void draw_menu(int8_t held_item, float progress);
