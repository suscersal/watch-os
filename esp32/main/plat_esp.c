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

#include "esp_netif.h"
#include <stdio.h>

bool plat_local_ip(char *buf, size_t n)
{
    esp_netif_t *nif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t info;
    if (!nif || esp_netif_get_ip_info(nif, &info) != ESP_OK || info.ip.addr == 0) return false;
    snprintf(buf, n, IPSTR, IP2STR(&info.ip));
    return true;
}
