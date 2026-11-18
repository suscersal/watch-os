// plat_esp.c - случайные числа на ESP32 (замена чтения /dev/urandom с ПК).
// Настоящая случайность аппаратный генератор даёт при включённом Wi-Fi/Bluetooth; соли PIN нужна
// только уникальность, поэтому этого достаточно.
#include "watch/plat.h"
#include "esp_random.h"

bool plat_random(void *buf, size_t n)
{
    esp_fill_random(buf, n);
    return true;
}
