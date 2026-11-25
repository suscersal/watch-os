// b64.h - кодирование в base64 (фото для запроса к ИИ уходит в JSON строкой).
#pragma once
#include <stddef.h>
#include <stdint.h>

// Сколько байт займёт результат для n входных байт (без завершающего нуля).
#define B64_LEN(n) ((((n) + 2) / 3) * 4)

// Пишет base64 в out (нужно B64_LEN(n) + 1 байт), добавляет ноль в конце. Возвращает длину строки.
size_t b64_encode(const uint8_t *in, size_t n, char *out);
