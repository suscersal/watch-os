#include "imu.h"
#include "lvgl_inc.h"
#include <math.h>
#include <string.h>

static const imu_hw_t *g_hw;

void imu_set_hw(const imu_hw_t *hw) { g_hw = hw; }
bool imu_is_simulated(void) { return g_hw == NULL; }

bool imu_read(imu_sample_t *out)
{
    if (g_hw) return g_hw->read ? g_hw->read(out) : false;
    float t = (float)lv_tick_get() / 1000.0f;           // эмуляция: часы лежат и их плавно покачивают
    memset(out, 0, sizeof *out);
    out->ax = 0.25f * sinf(t * 1.3f);
    out->ay = 0.20f * sinf(t * 0.9f + 1.0f);
    out->az = sqrtf(fmaxf(0.0f, 1.0f - out->ax * out->ax - out->ay * out->ay));
    out->gx = 90.0f * cosf(t * 1.3f);
    out->gy = 60.0f * cosf(t * 0.9f + 1.0f);
    out->gz = 40.0f * sinf(t * 0.5f);
    return true;
}

void imu_gyro_enable(bool on) { if (g_hw && g_hw->gyro_enable) g_hw->gyro_enable(on); }
