// crc32.h - CRC-32 (IEEE 802.3, тот же, что в zlib/java.util.zip.CRC32) для проверки целостности файлов.
#pragma once
#include <stddef.h>
#include <stdint.h>

uint32_t crc32_update(uint32_t crc, const void *data, size_t n);   // начальное значение 0
