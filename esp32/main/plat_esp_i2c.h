// plat_esp_i2c.h - доступ к устройствам на общей шине I2C платы (шину создаёт BSP). Один дескриптор на адрес.
#pragma once
#include "driver/i2c_master.h"

// Возвращает дескриптор устройства; slot хранит его между вызовами (создаётся при первом).
i2c_master_dev_handle_t plat_esp_i2c_dev(uint8_t addr, i2c_master_dev_handle_t *slot);
i2c_master_dev_handle_t plat_esp_pmu(void);    // AXP2101 (0x34)
