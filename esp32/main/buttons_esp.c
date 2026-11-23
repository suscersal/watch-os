// buttons_esp.c - опрос кнопок корпуса.
//   BOOT: GPIO0, активный уровень 0 (подтяжка внутри).
//   PWR: подключена к AXP2101 (I2C 0x34), а не к GPIO. Читаем прерывание "короткое нажатие": регистр INTSTS2 (0x49), бит 3
//   (XPowersLib: XPOWERS_AXP2101_PKEY_SHORT_IRQ = бит 11 общей маски; INTEN2 = 0x41).
#include "buttons_esp.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bsp/esp-bsp.h"
#include "bsp/esp32_s3_touch_amoled_2_06.h"
#include "plat_esp_i2c.h"
#include "watch/buttons.h"

static const char *TAG = "buttons";

#define BOOT_GPIO      GPIO_NUM_0
#define AXP_ADDR       0x34
#define AXP_INTEN2     0x41
#define AXP_INTSTS2    0x49
#define PKEY_SHORT_BIT (1u << 3)
#define POLL_MS        20

static i2c_master_dev_handle_t s_pmu;

static bool pmu_read(uint8_t reg, uint8_t *v)
{
    return s_pmu && i2c_master_transmit_receive(s_pmu, &reg, 1, v, 1, 50) == ESP_OK;
}

static bool pmu_write(uint8_t reg, uint8_t v)
{
    uint8_t b[2] = { reg, v };
    return s_pmu && i2c_master_transmit(s_pmu, b, 2, 50) == ESP_OK;
}

static void pmu_init(void)
{
    s_pmu = plat_esp_pmu();                            // тот же дескриптор, что у индикатора батареи
    if (!s_pmu) { ESP_LOGW(TAG, "AXP2101 недоступен: кнопка PWR отключена"); return; }
    uint8_t en = 0;
    if (pmu_read(AXP_INTEN2, &en)) pmu_write(AXP_INTEN2, en | PKEY_SHORT_BIT);
    pmu_write(AXP_INTSTS2, PKEY_SHORT_BIT);            // сбросить старое событие
}

// Что делает кнопка, решает общий код (watch/buttons.c): так же ведёт себя и симулятор.
static void on_boot_short(void)
{
    if (!bsp_display_lock(0)) return;
    buttons_boot_short();
    bsp_display_unlock();
}

static void on_pwr_short(void)
{
    if (!bsp_display_lock(0)) return;
    buttons_pwr_short();
    bsp_display_unlock();
}

static void buttons_task(void *arg)
{
    (void)arg;
    gpio_config_t io = { .pin_bit_mask = 1ULL << BOOT_GPIO, .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE };
    gpio_config(&io);
    pmu_init();

    int boot_low = 0;                                   // сколько опросов подряд нажата (антидребезг)
    bool boot_fired = false;
    for (;;) {
        if (gpio_get_level(BOOT_GPIO) == 0) {
            if (++boot_low == 3 && !boot_fired) { boot_fired = true; on_boot_short(); }   // срабатывает при нажатии
        } else {
            boot_low = 0;
            boot_fired = false;
        }
        uint8_t st;
        if (pmu_read(AXP_INTSTS2, &st) && (st & PKEY_SHORT_BIT)) {
            pmu_write(AXP_INTSTS2, PKEY_SHORT_BIT);
            on_pwr_short();
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
}

void buttons_esp_start(void)
{
    plat_esp_pmu();                                     // создать дескриптор AXP2101 сразу, пока нет гонки с индикатором батареи
    xTaskCreate(buttons_task, "buttons", 3072, NULL, 4, NULL);
}
