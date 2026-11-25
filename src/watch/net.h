// net.h - сеть для ядра. Для ПК реализация на libcurl (net_curl.c), для ESP32 будет на esp_http_client.
#pragma once
#include <stdbool.h>
#include <stddef.h>

// HTTP(S) GET. Возвращает malloc-буфер с нулём в конце или NULL (тогда err содержит причину).
// Разрешены только http:// и https://; ответ больше max_size обрывается; таймаут 10 с.
char *net_get(const char *url, size_t max_size, char *err, size_t errn);

// HTTP POST с телом JSON (для моста ИИ через телефон). Возвращает malloc-буфер с ответом при ЛЮБОМ статусе HTTP
// (статус кладёт в *status), или NULL, если связи нет (тогда err содержит причину). Ответ больше max_size обрывается.
char *net_post_json(const char *url, const char *json, size_t max_size, int timeout_s, int *status, char *err, size_t errn);

// Адрес телефона в сети раздачи: на ESP32 это шлюз Wi-Fi, на ПК 127.0.0.1 (или переменная WATCH_AI_HOST).
bool net_phone_host(char *host, size_t n);
