// ai.h - запрос к ИИ через телефон (docs/ai-bridge.md). Ключ OpenRouter хранится только в приложении Watch AI на телефоне.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AI_PORT 8787

// Отправляет вопрос приложению на телефоне и кладёт ответ в out (обрезается по границе символа UTF-8, переводы строк
// сохраняются). Блокирующий вызов, ждёт до ~60 с. При ошибке возвращает false и пишет причину в err.
bool ai_ask(const char *question, char *out, size_t outn, char *err, size_t errn);

// Вопрос про фото: jpeg (JPEG из хранилища Gallery, до 256 КБ) уходит телефону base64-строкой в поле "img".
// Модель на телефоне должна уметь картинки (по умолчанию openai/gpt-4o-mini умеет). Ответ - простой текст.
bool ai_ask_image(const char *question, const uint8_t *jpeg, size_t n, char *out, size_t outn, char *err, size_t errn);

// Таблица от ИИ: телефон отвечает JSON {"rows": [[...], ...]} (первая строка - заголовок), ядро кладёт её в таблицу
// с именем name (tables.h). Показывается виджетом "table" с "src": "<name>".
bool ai_ask_table(const char *question, const char *name, char *err, size_t errn);
