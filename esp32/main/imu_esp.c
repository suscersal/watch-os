// imu_esp.c - подъём руки по акселерометру QMI8658 (I2C 0x6B, в примерах Waveshare для этой платы так же).
//
// Алгоритм: пока экран затемнён или выключен по бездействию, читаем ускорение ~25 раз в секунду. Ось Z - перпендикуляр к
// экрану. Рука опущена или лежит вдоль тела - экран смотрит вбок (Z около 0), при взгляде на часы экран повёрнут к лицу
// (Z заметно положительна). Подъёмом считаем: Z выросла на RAISE_DZ за последние ~0.8 с, сейчас Z > RAISE_Z_MIN и датчик почти
// неподвижен (рука остановилась, а не машет). Лежащие лицом вверх часы не будят (Z и раньше большая).
//
// ПРОВЕРЬТЕ на плате: знак оси Z (RAISE_Z_SIGN) и пороги. Для подбора включите IMU_DEBUG: раз в 0.5 с в лог пойдут ax/ay/az в g.
#include "imu_esp.h"
#include <math.h>
#include <stdlib.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bsp/esp-bsp.h"
#include "plat_esp_i2c.h"
#include "watch/power.h"

static const char *TAG = "imu";

#define IMU_ADDR       0x6B
#define REG_WHOAMI     0x00
#define REG_CTRL1      0x02
#define REG_CTRL2      0x03
#define REG_CTRL7      0x08
#define REG_AX_L       0x35
#define REG_RESET      0x60
#define LSB_PER_G      8192.0f        // диапазон ±4 g: 32768 / 4

#define RAISE_Z_SIGN   1.0f           // -1, если на вашей плате экран смотрит в сторону -Z
#define RAISE_Z_MIN    0.45f          // экран повёрнут к лицу (g)
#define RAISE_DZ       0.35f          // рост Z за окно (g)
#define STILL_G        0.10f          // допустимое дрожание в конце жеста (g)
#define WINDOW         20             // ~0.8 с при 25 Гц
#define POLL_ACTIVE_MS 40
#define POLL_IDLE_MS   250
#define COOLDOWN_MS    2000
#define IMU_DEBUG      0

static i2c_master_dev_handle_t s_imu;

static bool rd(uint8_t reg, uint8_t *buf, size_t n)
{
    return i2c_master_transmit_receive(s_imu, &reg, 1, buf, n, 50) == ESP_OK;
}

static bool wr(uint8_t reg, uint8_t v)
{
    uint8_t b[2] = { reg, v };
    return i2c_master_transmit(s_imu, b, 2, 50) == ESP_OK;
}

static bool imu_init(void)
{
    static i2c_master_dev_handle_t slot;
    s_imu = plat_esp_i2c_dev(IMU_ADDR, &slot);
    if (!s_imu) return false;
    wr(REG_RESET, 0xB0);                                   // программный сброс
    vTaskDelay(pdMS_TO_TICKS(30));
    uint8_t id = 0;
    if (!rd(REG_WHOAMI, &id, 1) || id != 0x05) { ESP_LOGW(TAG, "QMI8658 не найден (WHO_AM_I=0x%02X)", id); return false; }
    return wr(REG_CTRL1, 0x40)                             // автоинкремент адреса, little-endian
        && wr(REG_CTRL2, 0x17)                             // акселерометр: ±4 g, 62.5 Гц
        && wr(REG_CTRL7, 0x01);                            // включить только акселерометр
}

static bool read_g(float *x, float *y, float *z)
{
    uint8_t b[6];
    if (!rd(REG_AX_L, b, sizeof b)) return false;
    *x = (int16_t)(b[0] | (b[1] << 8)) / LSB_PER_G;
    *y = (int16_t)(b[2] | (b[3] << 8)) / LSB_PER_G;
    *z = (int16_t)(b[4] | (b[5] << 8)) / LSB_PER_G * RAISE_Z_SIGN;
    return true;
}

static void wake_locked(void)
{
    if (!bsp_display_lock(0)) return;
    power_raise_wake();
    bsp_display_unlock();
}

static void imu_task(void *arg)
{
    (void)arg;
    if (!imu_init()) { ESP_LOGW(TAG, "подъём руки отключён"); vTaskDelete(NULL); return; }

    float hz[WINDOW] = {0}, hm[WINDOW] = {0};              // история Z и модуля вектора
    int n = 0;
    TickType_t last_wake = 0;
    int dbg = 0;
    for (;;) {
        // Опрашиваем часто только когда есть что будить: экран затемнён или выключен по бездействию.
        bool armed = power_raise_enabled() && (power_dimmed() || (power_screen_off() && !power_screen_forced_off()));
        if (!armed) { n = 0; vTaskDelay(pdMS_TO_TICKS(POLL_IDLE_MS)); continue; }

        float x, y, z;
        if (read_g(&x, &y, &z)) {
            float m = sqrtf(x * x + y * y + z * z);
            for (int i = 0; i < WINDOW - 1; i++) { hz[i] = hz[i + 1]; hm[i] = hm[i + 1]; }
            hz[WINDOW - 1] = z; hm[WINDOW - 1] = m;
            if (n < WINDOW) n++;
#if IMU_DEBUG
            if (++dbg % 12 == 0) ESP_LOGI(TAG, "x=%.2f y=%.2f z=%.2f |a|=%.2f", x, y, z, m);
#else
            (void)dbg;
#endif
            if (n == WINDOW && xTaskGetTickCount() - last_wake > pdMS_TO_TICKS(COOLDOWN_MS)) {
                float zmin = hz[0];
                for (int i = 1; i < WINDOW; i++) if (hz[i] < zmin) zmin = hz[i];
                float mmin = hm[WINDOW - 5], mmax = hm[WINDOW - 5];          // последние ~0.2 с: рука остановилась?
                for (int i = WINDOW - 4; i < WINDOW; i++) { if (hm[i] < mmin) mmin = hm[i]; if (hm[i] > mmax) mmax = hm[i]; }
                bool still = (mmax - mmin) < STILL_G && fabsf(m - 1.0f) < 0.2f;
                if (z > RAISE_Z_MIN && z - zmin > RAISE_DZ && still) {
                    last_wake = xTaskGetTickCount();
                    n = 0;
                    wake_locked();
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_ACTIVE_MS));
    }
}

void imu_esp_start(void)
{
    xTaskCreate(imu_task, "imu", 3072, NULL, 3, NULL);
}
