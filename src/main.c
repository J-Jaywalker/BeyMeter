#include <math.h>
#include "pico/stdlib.h"
#include "hw.h"
#include "screens.h"

int main(void) {
    hw_init();

    // ── Splash / boot checks ──────────────────────────────────────────
    screen_splash(1, 3, "Display init");
    sleep_ms(400);

    bool imu_ok = hw_check_imu();
    screen_splash(2, 3, imu_ok ? "IMU ready" : "IMU missing");
    sleep_ms(350);

    bool bat_ok = hw_check_battery();
    screen_splash(3, 3, bat_ok ? "Battery OK" : "No battery");
    sleep_ms(500);

    // ── IMU setup ────────────────────────────────────────────────────
    stmdev_ctx_t imu = hw_imu_init();

    float roll = 0.0f, pitch = 0.0f;
    float    last_pitch   = 0.0f, last_roll = 0.0f;
    uint8_t  bat          = bat_percent();
    uint32_t bat_tick     = 0;
    uint32_t popup_until  = 0;
    bool     was_locked   = false;

    for (;;) {
        int16_t raw[3];
        ism330dhcx_acceleration_raw_get(&imu, raw);

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
    }
}
