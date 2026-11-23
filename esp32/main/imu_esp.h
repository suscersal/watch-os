// imu_esp.h - акселерометр QMI8658: пробуждение экрана при подъёме руки.
#pragma once

// После bsp_display_start_with_config() и power_init(). Запускает опрос датчика в отдельной задаче.
void imu_esp_start(void);
