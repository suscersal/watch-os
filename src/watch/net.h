// net.h - сеть для ядра. Для ПК реализация на libcurl (net_curl.c), для ESP32 будет на esp_http_client.
#pragma once
#include <stdbool.h>
#include <stddef.h>

// HTTP(S) GET. Возвращает malloc-буфер с нулём в конце или NULL (тогда err содержит причину).
// Разрешены только http:// и https://; ответ больше max_size обрывается; таймаут 10 с.
char *net_get(const char *url, size_t max_size, char *err, size_t errn);
