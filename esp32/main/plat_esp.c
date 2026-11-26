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

// ---------- RTC PCF85063 (адрес 0x51) и питание AXP2101 (адрес 0x34) на общей шине I2C платы ----------
// Шину создаёт BSP (дисплей и тач уже на ней). ПРОВЕРЬТЕ на плате: имена bsp_i2c_init()/bsp_i2c_get_handle() взяты из
// стандартного esp-bsp; если в вашей версии BSP они называются иначе, поправьте только plat_esp_i2c_dev() ниже.
#include "bsp/esp-bsp.h"
#include "driver/i2c_master.h"
#include "plat_esp_i2c.h"
#include <string.h>
#include <time.h>

i2c_master_dev_handle_t plat_esp_i2c_dev(uint8_t addr, i2c_master_dev_handle_t *slot)
{
    if (*slot) return *slot;
    if (bsp_i2c_init() != ESP_OK) return NULL;                 // повторный вызов безвреден
    i2c_device_config_t dc = { .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = addr, .scl_speed_hz = 400000 };
    if (i2c_master_bus_add_device(bsp_i2c_get_handle(), &dc, slot) != ESP_OK) *slot = NULL;
    return *slot;
}

static i2c_master_dev_handle_t g_rtc, g_pmu;

i2c_master_dev_handle_t plat_esp_pmu(void) { return plat_esp_i2c_dev(0x34, &g_pmu); }   // AXP2101: одно устройство на всех (батарея, кнопка PWR)

static uint8_t bcd2bin(uint8_t v) { return (uint8_t)((v >> 4) * 10 + (v & 0x0F)); }
static uint8_t bin2bcd(uint8_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

// дней от 1970-01-01 до даты (алгоритм Говарда Хиннанта), чтобы не зависеть от timegm
static int64_t days_from_civil(int y, unsigned m, unsigned d)
{
    y -= m <= 2;
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

bool plat_rtc_read(int64_t *epoch_utc)
{
    i2c_master_dev_handle_t d = plat_esp_i2c_dev(0x51, &g_rtc);
    if (!d) return false;
    uint8_t reg = 0x04, r[7];                                  // секунды, минуты, часы, день, день недели, месяц, год
    if (i2c_master_transmit_receive(d, &reg, 1, r, sizeof r, 100) != ESP_OK) return false;
    if (r[0] & 0x80) return false;                             // бит OS: генератор останавливался, время недостоверно
    int sec = bcd2bin(r[0] & 0x7F), min = bcd2bin(r[1] & 0x7F), hour = bcd2bin(r[2] & 0x3F);
    int day = bcd2bin(r[3] & 0x3F), mon = bcd2bin(r[5] & 0x1F), year = 2000 + bcd2bin(r[6]);
    if (sec > 59 || min > 59 || hour > 23 || day < 1 || day > 31 || mon < 1 || mon > 12) return false;
    *epoch_utc = days_from_civil(year, (unsigned)mon, (unsigned)day) * 86400 + hour * 3600 + min * 60 + sec;
    return true;
}

void plat_rtc_write(int64_t epoch_utc)
{
    i2c_master_dev_handle_t d = plat_esp_i2c_dev(0x51, &g_rtc);
    if (!d || epoch_utc < 946684800) return;
    time_t t = (time_t)epoch_utc;
    struct tm tm;
    gmtime_r(&t, &tm);
    if (tm.tm_year < 100 || tm.tm_year > 199) return;          // PCF85063 хранит годы 2000..2099
    uint8_t ctl;
    uint8_t reg0 = 0x00;
    if (i2c_master_transmit_receive(d, &reg0, 1, &ctl, 1, 100) == ESP_OK && (ctl & 0x22)) {   // STOP (бит 5) и 12-часовой формат (бит 1) - в рабочий режим
        uint8_t w[2] = { 0x00, (uint8_t)(ctl & ~0x22) };
        i2c_master_transmit(d, w, sizeof w, 100);
    }
    uint8_t w[8] = { 0x04, bin2bcd((uint8_t)tm.tm_sec), bin2bcd((uint8_t)tm.tm_min), bin2bcd((uint8_t)tm.tm_hour),
                     bin2bcd((uint8_t)tm.tm_mday), (uint8_t)tm.tm_wday, bin2bcd((uint8_t)(tm.tm_mon + 1)), bin2bcd((uint8_t)(tm.tm_year - 100)) };
    i2c_master_transmit(d, w, sizeof w, 100);                  // запись секунд со сброшенным битом OS снимает флаг недостоверности
}

bool plat_battery(int *pct, bool *charging)
{
    i2c_master_dev_handle_t d = plat_esp_i2c_dev(0x34, &g_pmu);
    if (!d) return false;
    uint8_t reg = 0x00, st1;                                   // STATUS1: бит 3 - батарея подключена
    if (i2c_master_transmit_receive(d, &reg, 1, &st1, 1, 50) != ESP_OK || !(st1 & 0x08)) return false;
    uint8_t rp = 0xA4, p, rs = 0x01, st2;                      // 0xA4 - процент заряда; STATUS2 бит 7..5 == 1 - идёт зарядка
    if (i2c_master_transmit_receive(d, &rp, 1, &p, 1, 50) != ESP_OK) return false;
    if (i2c_master_transmit_receive(d, &rs, 1, &st2, 1, 50) != ESP_OK) st2 = 0;
    *pct = p > 100 ? 100 : p;
    *charging = (st2 >> 5) == 1;
    return true;
}

// ---------- microSD (слот SDMMC, 1 линия данных; монтирует BSP) ----------
// Контакта обнаружения карты у платы нет, поэтому фоновая задача раз в SD_POLL_MS пробует смонтировать карту,
// а пока она смонтирована - проверяет, что она отвечает (sdmmc_get_status). Нечитаемую карту не форматируем.
// Первый вызов plat_sd_present()/plat_sd_root() запускает задачу; до первого монтирования карты "нет".
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"

#define SD_POLL_MS 4000

static volatile bool g_sd_ok;
static TaskHandle_t  g_sd_task;

static void sd_task(void *arg)
{
    (void)arg;
    for (;;) {
        if (!g_sd_ok) {
            if (bsp_sdcard_mount() == ESP_OK) g_sd_ok = true;
        } else if (!bsp_sdcard || sdmmc_get_status(bsp_sdcard) != ESP_OK) {
            bsp_sdcard_unmount();                              // карту вынули: освобождаем ресурсы, чтобы вставка подхватилась
            g_sd_ok = false;
        }
        vTaskDelay(pdMS_TO_TICKS(SD_POLL_MS));
    }
}

bool plat_sd_present(void)
{
    if (!g_sd_task) xTaskCreate(sd_task, "sd_watch", 6144, NULL, 3, &g_sd_task);   // первый вызов - из интерфейса
    return g_sd_ok;
}

const char *plat_sd_root(void) { return plat_sd_present() ? BSP_SD_MOUNT_POINT : NULL; }
