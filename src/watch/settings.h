// settings.h - постоянные настройки системы: часовой пояс, ключ API, Wi-Fi, уведомления с телефона.
// Файл <root>/settings.json. Пароль Wi-Fi и ключ API лежат там открытым текстом (шифрования флеша нет),
// приложения из apps/ к этому файлу доступа не имеют.
#pragma once
#include <stdbool.h>
#include <stddef.h>

void settings_init(const char *data_root);

// Часовой пояс: auto - берём пояс, который прислал телефон; иначе фиксированное смещение в минутах.
// tz_min - последнее известное смещение (пояс телефона или выбранный вручную).
bool settings_tz_auto(void);
int  settings_tz_min(void);
void settings_set_tz(bool is_auto, int tz_min);

#define SETTINGS_API_KEY_MAX 160
const char *settings_api_key(void);                 // "" если не задан
bool        settings_set_api_key(const char *key);  // "" - удалить

#define SETTINGS_SSID_MAX 33
#define SETTINGS_PASS_MAX 65
bool settings_wifi_get(char *ssid, size_t ssid_n, char *pass, size_t pass_n);   // false, если сеть не сохранена
void settings_wifi_set(const char *ssid, const char *pass);
void settings_wifi_clear(void);

// Уведомления с телефона: часы слушают порт в фоне, телефон входит постоянным кодом (только MESSAGE и TIME).
bool        settings_notify_on(void);
void        settings_set_notify(bool on);           // при первом включении создаёт код
const char *settings_notify_code(void);             // 6 цифр или ""
void        settings_notify_regen(void);            // новый код (старые сопряжения перестают работать)
