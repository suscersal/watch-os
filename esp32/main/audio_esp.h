// audio_esp.h - динамик (ES8311) и микрофон (ES7210) Waveshare ESP32-S3-Touch-AMOLED-2.06 для watch/audio.
#pragma once
#include "esp_err.h"

// Один раз после bsp_display_start_with_config(). Включает аудиошину BSP (24 кГц, 16 бит) и подключает плату к audio_set_hw().
esp_err_t audio_esp_init(void);
