#include <math.h>
#include "pico/stdlib.h"
#include "hw.h"
#include "screens.h"

// Splash + presence checks. Runs at power-on and again on every wake.
static void boot_sequence(void) {
    screen_splash(1, 3, "Display init");
    sleep_ms(400);

    bool imu_ok = hw_check_imu();
    screen_splash(2, 3, imu_ok ? "IMU ready" : "IMU missing");
    sleep_ms(350);

    bool bat_ok = hw_check_battery();
    screen_splash(3, 3, bat_ok ? "Battery OK" : "No battery");
    sleep_ms(500);
}

// Shuts everything down and parks in DORMANT. Returns only once the lock
// button has been held for WAKE_HOLD_MS — a shorter press goes straight back
// to sleep, so a knock in a bag can't switch the meter on.
static void sleep_until_wake(stmdev_ctx_t *imu) {
    hw_display_sleep();
    hw_backlight(false);
    hw_imu_sleep(imu);
    hw_bat_sleep();

    for (;;) {
        hw_dormant_until_button();

        uint32_t t0 = to_ms_since_boot(get_absolute_time());
        while (!gpio_get(BTN_LOCK)) {                  // still held?
            if (to_ms_since_boot(get_absolute_time()) - t0 >= WAKE_HOLD_MS)
                return;
            sleep_ms(20);
        }
    }
}

int main(void) {
    hw_init();
    boot_sequence();

    // ── IMU setup ────────────────────────────────────────────────────
    stmdev_ctx_t imu = hw_imu_init();

    float roll = 0.0f, pitch = 0.0f;
    float    mark_pitch   = 0.0f, mark_roll = 0.0f;
    uint32_t mark_t0      = 0;
    bool     have_mark    = false;
    uint8_t  bat          = bat_percent();
    uint32_t bat_tick     = 0;
    bool     was_locked   = false;
    uint32_t last_lock    = to_ms_since_boot(get_absolute_time());

    for (;;) {
        uint32_t now = to_ms_since_boot(get_absolute_time());

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
        if (locked) last_lock = now;

        // Lock released = launch: drop a mark where the bubble was sitting.
        // Only the most recent launch is kept.
        if (was_locked && !locked) {
            mark_pitch = pitch;
            mark_roll  = roll;
            mark_t0    = now;
            have_mark  = true;
        }
        was_locked = locked;

        screen_render(roll, pitch, bat, locked,
                      have_mark, mark_pitch, mark_roll, mark_t0);

        // Nothing locked in for SLEEP_IDLE_MS — shut down until woken, then
        // come back up exactly as if the meter had just been switched on
        if (now - last_lock >= SLEEP_IDLE_MS) {
            sleep_until_wake(&imu);

            hw_init();
            boot_sequence();
            imu = hw_imu_init();

            while (!gpio_get(BTN_LOCK))   // don't count the wake hold as a lock
                tight_loop_contents();

            roll = pitch = 0.0f;
            have_mark  = false;
            was_locked = false;
            bat        = bat_percent();
            bat_tick   = 0;
            last_lock  = to_ms_since_boot(get_absolute_time());
        }
    }
}
