// ai.h - запрос к ИИ через телефон (docs/ai-bridge.md). Ключ OpenRouter хранится только в приложении Watch AI на телефоне.
#pragma once
#include <stdbool.h>
#include <stddef.h>

#define AI_PORT 8787

// Отправляет вопрос приложению на телефоне и кладёт ответ в out (обрезается по границе символа UTF-8, переводы строк
// заменяются пробелами). Блокирующий вызов, ждёт до ~60 с. При ошибке возвращает false и пишет причину в err.
bool ai_ask(const char *question, char *out, size_t outn, char *err, size_t errn);
