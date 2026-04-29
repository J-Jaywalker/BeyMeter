#include <stdio.h>
#include <math.h>
#include "pico/stdlib.h"
#include "pico/time.h"
#include "hw.h"
#include "screens.h"
#include "splash.h"

int main(void) {
    stdio_init_all();
    hw_init();

    /* ── Splash screen — wait for A → B → C ────────────────────────── */
    {
        int seq = 0;
        bool sa = false, sb = false, sc = false;
        for (;;) {
            bool a = !gpio_get(BTN_A);
            bool b = !gpio_get(BTN_B);
            bool c = !gpio_get(BTN_C);
            bool a_dn = a && !sa;
            bool b_dn = b && !sb;
            bool c_dn = c && !sc;

            if      (seq == 0 && a_dn) { seq = 1; }
            else if (seq == 1 && b_dn) { seq = 2; }
            else if (seq == 2 && c_dn) { break;   }
            else if (a_dn || b_dn || c_dn) { seq = 0; }

            sa = a; sb = b; sc = c;

            u8g2_ClearBuffer(&u8g2);
            u8g2_DrawXBM(&u8g2, 0, 0, splash_w, splash_h, splash_bits);

            u8g2_SetFont(&u8g2, u8g2_font_5x7_tf);
            const int16_t ix = 2, iy = HEIGHT - 11;
            const char *lbls[3] = { "A", "B", "C" };
            u8g2_SetDrawColor(&u8g2, 0);
            u8g2_DrawBox(&u8g2, ix - 1, iy - 1, 35, 11);
            u8g2_SetDrawColor(&u8g2, 1);
            for (int i = 0; i < 3; i++) {
                int16_t bx = ix + i * 11;
                u8g2_DrawFrame(&u8g2, bx, iy, 9, 9);
                u8g2_DrawStr(&u8g2, bx + 2, iy + 7, lbls[i]);
                if (i < seq) {
                    u8g2_SetDrawColor(&u8g2, 2);
                    u8g2_DrawBox(&u8g2, bx + 1, iy + 1, 7, 7);
                    u8g2_SetDrawColor(&u8g2, 1);
                }
            }

            u8g2_SendBuffer(&u8g2);
            sleep_ms(16);
        }
    }

    stmdev_ctx_t imu = hw_imu_init();

    float r_text = 0.0f, p_text = 0.0f;
    float r_line = 0.0f, p_line = 0.0f;

    uint8_t  bat       = bat_percent();
    uint16_t bat_mv_v  = bat_mv();
    uint32_t bat_tick  = 0;

    view_t   view       = VIEW_BUBBLE;
    bool     menu_open  = true;
    int8_t   menu_held  = -1;
    uint32_t hold_start = 0;

    bool prev_a = false, prev_b = false, prev_c = false;

    for (;;) {
        uint32_t now = to_ms_since_boot(get_absolute_time());

        /* ── Sensor ─────────────────────────────────────────────── */
        int16_t raw[3];
        ism330dhcx_acceleration_raw_get(&imu, raw);

        float ax =  (float)raw[0];
        float ay =  (float)raw[2];
        float az = -(float)raw[1];

        float r_raw = fmaxf(-45.0f, fminf(45.0f,
            atan2f(ay, az) * (180.0f / (float)M_PI)));
        float p_raw = fmaxf(-45.0f, fminf(45.0f,
            atan2f(-ax, sqrtf(ay*ay + az*az)) * (180.0f / (float)M_PI)));

        r_text = ALPHA_TEXT * r_raw + (1.0f - ALPHA_TEXT) * r_text;
        p_text = ALPHA_TEXT * p_raw + (1.0f - ALPHA_TEXT) * p_text;
        r_line = ALPHA_LINE * r_raw + (1.0f - ALPHA_LINE) * r_line;
        p_line = ALPHA_LINE * p_raw + (1.0f - ALPHA_LINE) * p_line;

        int16_t r = (int16_t)fmaxf(-45, fminf(45, roundf(r_text)));
        int16_t p = (int16_t)fmaxf(-45, fminf(45, roundf(p_text)));

        /* ── Battery (once per ~second) ─────────────────────────── */
        if (++bat_tick >= 40) {
            bat      = bat_percent();
            bat_mv_v = bat_mv();
            bat_tick = 0;
            printf("bat: %d%% (%dmV)\n", bat, bat_mv_v);
        }

        /* ── Buttons ────────────────────────────────────────────── */
        bool a = !gpio_get(BTN_A);
        bool b = !gpio_get(BTN_B);
        bool c = !gpio_get(BTN_C);
        bool a_dn = a && !prev_a;
        bool b_dn = b && !prev_b;
        bool c_dn = c && !prev_c;

        if (menu_open) {
            if (a_dn) { menu_held = 0; hold_start = now; }
            if (b_dn) { menu_held = 1; hold_start = now; }
            if (c_dn) { menu_held = 2; hold_start = now; }
            if (!a && prev_a && menu_held == 0) menu_held = -1;
            if (!b && prev_b && menu_held == 1) menu_held = -1;
            if (!c && prev_c && menu_held == 2) menu_held = -1;
        } else {
            if (a_dn) { menu_open = true; menu_held = -1; }
            if (view != VIEW_RPM && view != VIEW_STATS && b_dn) view = VIEW_BUBBLE;
            if (view != VIEW_RPM && view != VIEW_STATS && c_dn) view = VIEW_GAUGE;
        }

        /* ── Hold progress ──────────────────────────────────────── */
        float menu_prog = 0.0f;
        if (menu_open && menu_held >= 0) {
            float t = fminf((float)(now - hold_start) / (float)MENU_HOLD_MS, 1.0f);
            menu_prog = sqrtf(t);

            if (t >= 1.0f) {
                view      = (menu_held == 0) ? VIEW_BUBBLE
                          : (menu_held == 1) ? VIEW_RPM : VIEW_STATS;
                menu_open = false;
                menu_held = -1;
            }
        }

        prev_a = a; prev_b = b; prev_c = c;

        /* ── Draw ───────────────────────────────────────────────── */
        u8g2_ClearBuffer(&u8g2);

        if (menu_open) {
            draw_menu(menu_held, menu_prog);
        } else {
            switch (view) {
            case VIEW_BUBBLE: draw_bubble(r_line, p_line, bat);      break;
            case VIEW_GAUGE:  draw_gauge(r_line, p_line, r, p, bat); break;
            case VIEW_RPM:    draw_rpm();                             break;
            case VIEW_STATS:  draw_stats();                           break;
            }
        }

        u8g2_SendBuffer(&u8g2);
    }
}
