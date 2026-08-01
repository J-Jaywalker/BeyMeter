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
    float    mark_pitch   = 0.0f, mark_roll = 0.0f;
    uint32_t mark_t0      = 0;
    bool     have_mark    = false;
    uint8_t  bat          = bat_percent();
    uint32_t bat_tick     = 0;
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

        // Lock released = launch: drop a mark where the bubble was sitting.
        // Only the most recent launch is kept.
        if (was_locked && !locked) {
            mark_pitch = pitch;
            mark_roll  = roll;
            mark_t0    = to_ms_since_boot(get_absolute_time());
            have_mark  = true;
        }
        was_locked = locked;

        screen_render(roll, pitch, bat, locked,
                      have_mark, mark_pitch, mark_roll, mark_t0);
    }
}
