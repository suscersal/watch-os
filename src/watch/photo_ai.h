// photo_ai.h - экран "спросить ИИ про фото": вопрос + JPEG из Gallery уходят на телефон (docs/ai-bridge.md).
#pragma once
#include <stdint.h>
#include "lvgl_inc.h"

// Открывает экран поверх текущего (из просмотра в Gallery); кнопка "назад" возвращает на него.
void photo_ai_open(uint32_t photo_id);

// Для тестов: подставить ответ без сети (NULL - обычная отправка).
void photo_ai_test_send(const char *question);
