#include <math.h>
#include "pico/stdlib.h"
#include "hw.h"
#include "screens.h"

int main(void) {
    hw_init();
    screen_init();
    stmdev_ctx_t imu = hw_imu_init();

    float roll = 0.0f, pitch = 0.0f;
    uint8_t  bat      = bat_percent();
    uint32_t bat_tick = 0;

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

        draw_bubble(roll, pitch, bat);
        sleep_ms(16);
    }
}
