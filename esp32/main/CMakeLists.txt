set(WATCH_ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)

# Ядро берём из общей папки src/ (то же самое, что собирается на ПК), без ПК-реализаций
file(GLOB WATCH_CORE ${WATCH_ROOT}/src/watch/*.c ${WATCH_ROOT}/src/watch/fonts/*.c)
list(FILTER WATCH_CORE EXCLUDE REGEX "plat_posix\\.c$|net_curl\\.c$|wifi_ctl_posix\\.c$")

idf_component_register(
    SRCS
        main.c wifi.c net_esp.c plat_esp.c audio_esp.c buttons_esp.c
        ${WATCH_ROOT}/src/launcher.c
        ${WATCH_CORE}
    INCLUDE_DIRS . ${WATCH_ROOT}/src
    REQUIRES
        esp_wifi esp_event esp_netif nvs_flash esp_http_client esp-tls mbedtls json
        esp_timer esp_system lwip driver
        waveshare__esp32_s3_touch_amoled_2_06 lvgl joltwallet__littlefs)

# lvgl.h подключается как "lvgl.h" (см. src/watch/lvgl_inc.h); данные лежат на LittleFS в /flash
target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE WATCH_ROOT="/flash/")
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error=cpp)

# Образ файловой системы: apps/ и packages/ из репозитория.
# ВАЖНО: `idf.py flash` прошивает и этот образ, то есть стирает PIN и установленные приложения.
# Чтобы обновить только программу, используйте `idf.py app-flash`.
set(FS_STAGE ${CMAKE_BINARY_DIR}/fs_image)
file(REMOVE_RECURSE ${FS_STAGE})
file(COPY ${WATCH_ROOT}/apps DESTINATION ${FS_STAGE})
file(COPY ${WATCH_ROOT}/packages DESTINATION ${FS_STAGE})
littlefs_create_partition_image(storage ${FS_STAGE} FLASH_IN_PROJECT)
