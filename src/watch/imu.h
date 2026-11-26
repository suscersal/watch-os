// imu.h - инерциальный датчик (акселерометр + гироскоп) для приложений ядра. Плата подключается через imu_hw_t
// (ESP32: esp32/main/imu_esp.c, QMI8658). Без платы (симулятор на ПК) отдаёт синтетическое движение, чтобы экран можно было проверить.
#pragma once
#include <stdbool.h>

typedef struct {
    float ax, ay, az;     // ускорение, g
    float gx, gy, gz;     // угловая скорость, градусы в секунду
} imu_sample_t;

typedef struct {
    bool (*read)(imu_sample_t *out);      // false - датчик не отвечает
    void (*gyro_enable)(bool on);         // гироскоп включают только пока он нужен: он заметно потребляет
} imu_hw_t;

void imu_set_hw(const imu_hw_t *hw);
bool imu_is_simulated(void);              // платы нет: значения синтетические
bool imu_read(imu_sample_t *out);
void imu_gyro_enable(bool on);            // вызывать парно (true при открытии экрана, false при закрытии)
