#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "hw.h"

void screen_init(void);
void screen_render(float roll, float pitch, uint8_t bat, bool locked,
                   bool popup, float popup_pitch, float popup_roll);
