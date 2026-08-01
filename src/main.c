#include <math.h>
#include "pico/stdlib.h"
#include "hw.h"
#include "screens.h"

int main(void) {
    hw_init();
    screen_init();
    stmdev_ctx_t imu = hw_imu_init();

    float roll = 0.0f, pitch = 0.0f;
    float   last_pitch    = 0.0f, last_roll = 0.0f;
    uint8_t bat           = bat_percent();
    uint32_t bat_tick     = 0;
    uint32_t popup_until  = 0;
    bool     was_locked   = false;

    for (;;) {
        int16_t raw[3];
        ism330dhcx_acceleration_raw_get(&imu, raw);

        // ay=-raw[2] is the gravity axis; ax=raw[0] is roll-sensitive; az=-raw[1] is pitch-sensitive
        float ax =  (float)raw[0];
        float ay = -(float)raw[2];
        float az = -(float)raw[1];

        float r_raw = fmaxf(-45.0f, fminf(45.0f,
            atan2f(az, ay) * (180.0f / (float)M_PI)));
        float p_raw = fmaxf(-45.0f, fminf(45.0f,
            atan2f(-ax, ay) * (180.0f / (float)M_PI)));

        roll  = ALPHA * r_raw + (1.0f - ALPHA) * roll;
        pitch = ALPHA * p_raw + (1.0f - ALPHA) * pitch;

        if (++bat_tick >= 40) {
            bat = bat_percent();
            bat_tick = 0;
        }

        bool locked = !gpio_get(BTN_LOCK);

        if (was_locked && !locked) {
            last_pitch  = pitch;
            last_roll   = roll;
            popup_until = to_ms_since_boot(get_absolute_time()) + 5000;
        }
        was_locked = locked;

        bool show_popup = (popup_until > 0)
            && !locked
            && (to_ms_since_boot(get_absolute_time()) < popup_until);

        if (!show_popup) popup_until = 0;

        screen_render(roll, pitch, bat, locked, show_popup, last_pitch, last_roll);
        // No sleep — fb_flush DMA transfer (~15ms) provides natural frame pacing
    }
}
