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
  power, display_app  затемнение и выключение экрана при бездействии (+ меню Display: яркость, таймеры)
  gallery_app, photos   галерея: сетка миниатюр, просмотр, свайп, удаление; хранилище data/photos
  sync_app, sync_net_posix, sync_rx, clock   приём фото и времени с телефона/ПК по Wi-Fi (docs/sync-protocol.md)
  fs_posix, net_curl, plat_posix   слой под ПК (libcurl); для ESP32 net/plat лежат в esp32/main/
  lvgl_inc.h          единая точка подключения LVGL
  fonts/              шрифты 14/20/28 с кириллицей (см. fonts/README.md)
apps/                 примеры приложений (manifest.json + app.json), формат: docs/app-format.md
tests/smoke.c         автоматическая проверка ядра на виртуальном экране (ASan/UBSan)
tests/gallery_sync_test.c   протокол, TCP-приём, галерея, проверка цветов пикселей (нужны Python 3 + Pillow)
tools/sync_client.py  отправка фото и времени с компьютера (если нет Android-приложения)
android/              приложение Watch Sync (Kotlin), сборка APK в GitHub Actions: android/README.md
docs/                 формат приложений, протокол синхронизации, спецификация BLE-моста
packages/             примеры пакетов для установщика
lv_conf.h             конфиг LVGL
lvgl/                 submodule
```

`data/` создаётся при запуске (PIN-хэш, настройки), в git не попадает.

## Фото и время с телефона

1. На часах откройте **Sync**: он показывает адрес и 6-значный код, приём работает, только пока экран открыт.
2. С телефона: приложение из `android/` (инструкция в `android/README.md`). С компьютера:
   `python3 tools/sync_client.py <адрес> 8765 <код> --time фото1.jpg фото2.png`
3. Фото появляются в **Gallery**; время показывается на экране блокировки.

В симуляторе (в том числе в Termux) адрес `127.0.0.1`, порт 8765.

## Система (добавлено)

- Циферблат: большое время и дата, свайп вверх (или касание) открывает сетку приложений, свайп вниз по сетке возвращает циферблат; показывается и при включении экрана.
  Время берётся из RTC (PCF85063 на плате), при синхронизации с телефоном или в Settings пишется обратно в RTC. На ПК вместо RTC системное время.
- Строка состояния (Wi-Fi, заряд из AXP2101, время в сетке). На ПК заряд можно подставить: `WATCH_BATTERY=73 ./bin/main`.
- Settings: Wi-Fi (поиск сетей, пароль нашей клавиатурой), часовой пояс (авто с телефона или вручную), ключ API, уведомления с телефона.
  На ПК Wi-Fi имитируется (пароль короче 8 символов считается неверным). Пароль Wi-Fi и ключ API лежат в `settings.json` открытым текстом.
- Messages: последние 20 уведомлений с телефона (в памяти, после перезагрузки пусто). Android: `NotifyService`, фильтр по приложениям в MainActivity.
- QR на экранах Sync и Notifications; Android открывает ссылку `watchsync://pair?...` и подставляет адрес и код.
- Не проверено на железе: RTC, батарея и Wi-Fi-часть для ESP32, а также Android-часть (в этой среде нет ESP-IDF и Android SDK). Логика ядра проверена `watch_smoke` и `sync_test`.

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
- время сейчас приходит только с телефона и не переживает перезапуск (на ESP32 позже взять RTC/NTP);
- синхронизация по Bluetooth (сейчас только Wi-Fi), уведомления с телефона (кадр MESSAGE в протоколе зарезервирован);
- прошивка: Sync/Gallery на ESP32 не проверялись (нужны lwip, TJPGD, MEMFS - см. esp32/sdkconfig.defaults);
- свои иконки вместо символов LVGL;
- виджет `image` для JSON-приложений (декодер JPEG уже включён для галереи);
- фоновое скачивание в установщике (сейчас экран блокируется до 10 с);
- проверка подписи пакетов (сейчас только адрес и разрешения);

## Лицензии

`src/hal/` и `src/mouse_cursor_icon.c` взяты из lv_port_pc_vscode (MIT), текст в `LICENSE-lv_port_pc_vscode.txt`.
LVGL — MIT.
