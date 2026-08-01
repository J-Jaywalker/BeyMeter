#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "hw.h"

void screen_init(void);
void screen_splash(int step, int total, const char *label);
// have_mark/mark_* describe the previous launch: where the bubble sat when the
// lock was released, and when (ms since boot) that happened — the mark animates
// from that timestamp and greys out while the next shot is armed.
void screen_render(float roll, float pitch, uint8_t bat, bool locked,
                   bool have_mark, float mark_pitch, float mark_roll,
                   uint32_t mark_t0);
