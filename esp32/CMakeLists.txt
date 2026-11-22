# Проект ESP-IDF для часов Waveshare ESP32-S3-Touch-AMOLED-2.06.
# Сборка: cd esp32 && idf.py set-target esp32s3 && idf.py build   (см. README.md)
cmake_minimum_required(VERSION 3.16)
# LVGL берём из подмодуля ../lvgl (та же версия, что в симуляторе), а не из реестра компонентов.
# Зачем: менеджер компонентов при первом разрешении зависимостей (в CI нет dependencies.lock) проверяет условия вида
# config:LV_USE_FREETYPE из манифестов зависимостей, а Kconfig LVGL на этом этапе ещё неизвестен -> MissingKconfigError.
# Локальный компонент lvgl виден сразу, и менеджер подставляет его вместо lvgl/lvgl из реестра.
set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/../lvgl)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
add_compile_options(-Wno-format)
project(watch_os)
