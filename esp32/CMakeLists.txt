# Проект ESP-IDF для часов Waveshare ESP32-S3-Touch-AMOLED-2.06.
# Сборка: cd esp32 && idf.py set-target esp32s3 && idf.py build   (см. README.md)
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
add_compile_options(-Wno-format)
project(watch_os)
