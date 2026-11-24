#!/bin/sh
# Если в esp32/backtrace.txt лежит строка "Backtrace: 0x4037...:0x3fc... ..." из лога - расшифровываем её
# в build/backtrace-decoded.txt (попадёт в релиз). Нет файла - ничего не делаем.
[ -f backtrace.txt ] || exit 0
xtensa-esp32s3-elf-addr2line -pfiaC -e build/watch_os.elf $(grep -o '0x4[0-9a-f]\{7\}' backtrace.txt) > build/backtrace-decoded.txt 2>&1 || true
