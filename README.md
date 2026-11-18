# watch-os

Прототип «ОС» для часов Waveshare ESP32-S3 2.06" AMOLED (410×502): лаунчер в стиле Apple Watch,
приложения-модули в JSON, системные вызовы с разрешениями, экран блокировки с PIN.
Сейчас работает только как симулятор на ПК (LVGL + SDL2); порт на ESP32 ещё не написан.

## Сборка (Ubuntu / WSL2)

```bash
sudo apt install -y build-essential cmake git libsdl2-dev libcjson-dev libcurl4-openssl-dev
git clone --recursive <адрес-репозитория> watch-os      # LVGL подтянется как submodule (v9.4.0)
cd watch-os
cmake -B build && cmake --build build -j
./bin/main            # запускать из корня проекта: apps/ и data/ читаются отсюда
```

Если клонировали без `--recursive`: `git submodule update --init`.
Нужен именно LVGL 9.4+ (имена `LV_SCREEN_LOAD_ANIM_*`).

## Структура

```
src/main.c            запуск симулятора (окно 410x502)
src/launcher.c        сотовая сетка иконок + «рыбий глаз»
src/hal/              SDL-окно, мышь (из lv_port_pc_vscode)
src/watch/            ядро:
  app_manager         сканирует apps/*/manifest.json
  app_runtime         запускает JSON-приложение
  ui_renderer         JSON -> виджеты LVGL
  syscalls, vars      системные вызовы, переменные (@name)
  installer           установка пакета по ссылке (с подтверждением разрешений)
  auth, sha256        PIN: соль + 10000 раундов SHA-256, задержки после ошибок
  lock_screen, pinpad, sys_ui, security_app   окна ядра: блокировка, PIN, диалоги, клавиатура
  fs_*, net_*, plat_* слой под платформу (на ПК: POSIX и libcurl)
  fonts/              шрифты 14/20/28 с кириллицей (см. fonts/README.md)
apps/                 примеры приложений (manifest.json + app.json)
packages/             примеры пакетов для установщика
lv_conf.h             конфиг LVGL
lvgl/                 submodule
```

`data/` создаётся при запуске (PIN-хэш, настройки), в git не попадает.

## Что осталось сделать

Проверить:
- запуск и внешний вид на экране (размеры PIN-клавиатуры, раскладки, отступы, «рыбий глаз» пальцем с инерцией).

Не сделано:
- settings: Wi-Fi, ключ API, приложение «Настройки» (сейчас в settings только PIN);
- реальные `ai.ask` / `ai.ask_table` вместо заглушки (ключ API хранить в ядре);
- встроенный калькулятор;
- порт на ESP32: драйверы экрана и сенсора, LittleFS/microSD, Wi-Fi, HTTP-клиент вместо libcurl,
  `esp_fill_random` вместо `/dev/urandom`, ужать по памяти;
- часы на экране блокировки (нужен RTC/NTP);
- свои иконки вместо символов LVGL;
- больше виджетов (список, таблица, картинка, ползунок, индикатор);
- вызовы для разрешения `storage` (оно объявлено, вызовов нет);
- фоновое скачивание в установщике (сейчас экран блокируется до 10 с);
- проверка подписи пакетов (сейчас только адрес и разрешения);
- имя приложения в манифесте не длиннее 23 байт (~11 русских букв).

## Лицензии

`src/hal/` и `src/mouse_cursor_icon.c` взяты из lv_port_pc_vscode (MIT), текст в `LICENSE-lv_port_pc_vscode.txt`.
LVGL — MIT.
