#pragma once
#include <stdint.h>
#include "hw.h"

void screen_init(void);
void draw_bubble(float roll, float pitch, uint8_t bat, bool pressed);
void draw_launch_popup(float pitch, float roll);
void clear_launch_popup(bool pressed);
