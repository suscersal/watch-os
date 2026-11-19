# watch-os

Прототип «ОС» для часов Waveshare ESP32-S3 2.06" AMOLED (410×502): лаунчер в стиле Apple Watch,
приложения-модули в JSON, системные вызовы с разрешениями, экран блокировки с PIN.
Работает как симулятор на ПК (LVGL + SDL2). Порт на ESP32 написан и собирается, но на самих часах ещё не запускался (см. `esp32/README.md`).

## Сборка (Ubuntu / WSL2)

```bash
sudo apt install -y build-essential cmake git libsdl2-dev libcjson-dev libcurl4-openssl-dev
git clone --recursive <адрес-репозитория> watch-os      # LVGL подтянется как submodule (v9.5.0)
cd watch-os
cmake -B build && cmake --build build -j
./bin/main            # запускать из корня проекта: apps/ и data/ читаются отсюда
```

Автоматическая проверка ядра (без окна): `cmake --build build --target watch_smoke && ./bin/watch_smoke`.
Снимок экрана в PNG без окна (удобно смотреть вид и слать мне): `cmake --build build --target watch_shot && ./bin/watch_shot out.png launcher`; шаги `app=<имя>`, `click=<текст>`, `focus`, `tapout` описаны в начале `tests/shot.c`.

Если клонировали без `--recursive`: `git submodule update --init`.
Нужен LVGL 9.4 или новее (имена `LV_SCREEN_LOAD_ANIM_*`); на часах используется та же 9.5.0.

## Структура

```
src/main.c            запуск симулятора (окно 410x502)
src/launcher.c        сотовая сетка иконок + «рыбий глаз»
src/hal/              SDL-окно, мышь (из lv_port_pc_vscode)
esp32/                проект ESP-IDF для часов: экран, LittleFS, Wi-Fi, HTTP (см. esp32/README.md)
src/watch/            ядро (общее для ПК и ESP32):
  app_manager         сканирует apps/*/manifest.json
  app_runtime         запускает JSON-приложение
  ui_renderer         JSON -> виджеты LVGL
  syscalls, vars      системные вызовы, переменные (@name)
  tables, storage     таблицы для ответов ИИ, постоянное хранилище приложения
  installer           установка пакета по ссылке (с подтверждением разрешений)
  auth, sha256        PIN: соль + 10000 раундов SHA-256, задержки после ошибок
  lock_screen, pinpad, sys_ui, security_app   окна ядра: блокировка, PIN, диалоги, клавиатура
  fs_posix, net_curl, plat_posix   слой под ПК (libcurl); для ESP32 net/plat лежат в esp32/main/
  lvgl_inc.h          единая точка подключения LVGL
  fonts/              шрифты 14/20/28 с кириллицей (см. fonts/README.md)
apps/                 примеры приложений (manifest.json + app.json), формат: docs/app-format.md
tests/smoke.c         автоматическая проверка ядра на виртуальном экране (ASan/UBSan)
docs/                 формат приложений, спецификация BLE-моста
packages/             примеры пакетов для установщика
lv_conf.h             конфиг LVGL
lvgl/                 submodule
```

`data/` создаётся при запуске (PIN-хэш, настройки), в git не попадает.

## Что осталось сделать

Проверить:
- запуск и внешний вид на экране (размеры PIN-клавиатуры, раскладки, отступы, «рыбий глаз» пальцем с инерцией);
- прошивку на реальных часах (см. `esp32/README.md`, что проверено, а что нет).

Не сделано:
- settings: Wi-Fi, ключ API, приложение «Настройки» (сейчас в settings только PIN);
- реальные `ai.ask` / `ai.ask_table` вместо заглушки (ключ API хранится на телефоне, см. docs/ble-bridge.md);
- BLE-мост: спецификация есть (docs/ble-bridge.md), реализации на часах и Android-приложения нет;
- встроенный калькулятор;
- порт на ESP32: запустить на плате и поправить по результатам (экран, сенсор, память, Wi-Fi, HTTPS),
  потом ужимать по памяти и энергии; microSD как второе хранилище;
- часы на экране блокировки (нужен RTC/NTP);
- свои иконки вместо символов LVGL;
- виджет `image` (нужен декодер PNG и файловый драйвер LVGL);
- фоновое скачивание в установщике (сейчас экран блокируется до 10 с);
- проверка подписи пакетов (сейчас только адрес и разрешения);
- имя приложения в манифесте не длиннее 23 байт (~11 русских букв).

## Лицензии

`src/hal/` и `src/mouse_cursor_icon.c` взяты из lv_port_pc_vscode (MIT), текст в `LICENSE-lv_port_pc_vscode.txt`.
LVGL — MIT.
