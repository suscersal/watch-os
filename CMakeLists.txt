cmake_minimum_required(VERSION 3.12.4)
project(watch_os C)

set(CMAKE_C_STANDARD 99)
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)

find_package(SDL2 REQUIRED)
find_package(CURL REQUIRED)
find_package(Threads REQUIRED)
find_library(CJSON_LIB cjson)
find_path(CJSON_INC cJSON.h PATH_SUFFIXES cjson)   # в Ubuntu: /usr/include/cjson
set(CJSON_SRC "")
if(NOT CJSON_LIB OR NOT CJSON_INC)
    # Системного cJSON нет (например, в Termux): берём исходники из third_party/cJSON
    if(NOT EXISTS ${PROJECT_SOURCE_DIR}/third_party/cJSON/cJSON.c)
        message(FATAL_ERROR "cJSON не найден. Скачайте исходники:\n"
            "  mkdir -p third_party/cJSON && cd third_party/cJSON\n"
            "  curl -LO https://raw.githubusercontent.com/DaveGamble/cJSON/v1.7.18/cJSON.c\n"
            "  curl -LO https://raw.githubusercontent.com/DaveGamble/cJSON/v1.7.18/cJSON.h")
    endif()
    set(CJSON_SRC third_party/cJSON/cJSON.c)
    set(CJSON_INC ${PROJECT_SOURCE_DIR}/third_party/cJSON)
    set(CJSON_LIB "")
endif()

# LVGL (git submodule, закреплён на v9.4.0); lv_conf.h лежит в корне проекта
add_subdirectory(lvgl)
if(ANDROID OR DEFINED ENV{TERMUX_VERSION})
    # Termux (Android API < 28): aligned_alloc не объявлен, LVGL (драйвер SDL) его вызывает.
    # Подменяем на memalign с тем же порядком аргументов.
    # Только для C: в LVGL есть ассемблерные файлы (NEON), им заголовки подставлять нельзя.
    target_compile_options(lvgl PRIVATE
        "$<$<COMPILE_LANGUAGE:C>:SHELL:-include malloc.h>"
        "$<$<COMPILE_LANGUAGE:C>:-Daligned_alloc(a,s)=memalign(a,s)>")
endif()
target_include_directories(lvgl PUBLIC ${PROJECT_SOURCE_DIR} ${SDL2_INCLUDE_DIRS})

file(GLOB WATCH_SOURCES src/watch/*.c src/watch/fonts/*.c)
add_executable(main
    src/main.c
    src/launcher.c
    src/hal/hal.c
    src/hal/emu_slow.c
    src/mouse_cursor_icon.c
    ${WATCH_SOURCES}
    ${CJSON_SRC})
target_compile_definitions(main PRIVATE LV_CONF_INCLUDE_SIMPLE)
target_include_directories(main PRIVATE ${CJSON_INC} ${SDL2_INCLUDE_DIRS})
target_link_libraries(main lvgl ${SDL2_LIBRARIES} ${CJSON_LIB} CURL::libcurl m Threads::Threads)

# make run: запуск из корня проекта (apps/ и data/ читаются отсюда)
add_custom_target(run
    COMMAND $<TARGET_FILE:main>
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    DEPENDS main
    VERBATIM)

# Автоматическая проверка ядра на виртуальном экране (без окна и SDL-ввода), с AddressSanitizer/UBSan:
#   cmake --build build --target watch_smoke && ./bin/watch_smoke
add_executable(watch_smoke EXCLUDE_FROM_ALL tests/smoke.c src/launcher.c ${WATCH_SOURCES} ${CJSON_SRC})
target_compile_definitions(watch_smoke PRIVATE LV_CONF_INCLUDE_SIMPLE)
target_compile_options(watch_smoke PRIVATE -g -fno-omit-frame-pointer -fsanitize=address,undefined)
target_link_options(watch_smoke PRIVATE -fsanitize=address,undefined)
target_include_directories(watch_smoke PRIVATE ${PROJECT_SOURCE_DIR}/src ${CJSON_INC} ${SDL2_INCLUDE_DIRS})
target_link_libraries(watch_smoke lvgl ${SDL2_LIBRARIES} ${CJSON_LIB} CURL::libcurl m Threads::Threads)

# Рисует экраны в PNG без окна, чтобы посмотреть вид интерфейса (см. комментарий в tests/shot.c):
#   cmake --build build --target watch_shot && ./bin/watch_shot out.png launcher
add_executable(watch_shot EXCLUDE_FROM_ALL tests/shot.c src/launcher.c ${WATCH_SOURCES} ${CJSON_SRC})
target_compile_definitions(watch_shot PRIVATE LV_CONF_INCLUDE_SIMPLE)
target_include_directories(watch_shot PRIVATE ${PROJECT_SOURCE_DIR}/src ${CJSON_INC} ${SDL2_INCLUDE_DIRS})
target_link_libraries(watch_shot lvgl ${SDL2_LIBRARIES} ${CJSON_LIB} CURL::libcurl m Threads::Threads)

# Проверка протокола синхронизации, TCP-приёма и галереи (нужны Python 3 и Pillow):
#   cmake --build build --target sync_test
# Тест стирает data/ и apps/ в рабочей папке, поэтому запускается в build/sync_test_run.
add_executable(watch_sync_test EXCLUDE_FROM_ALL tests/gallery_sync_test.c src/launcher.c ${WATCH_SOURCES} ${CJSON_SRC})
target_compile_definitions(watch_sync_test PRIVATE LV_CONF_INCLUDE_SIMPLE)
target_compile_options(watch_sync_test PRIVATE -g -fno-omit-frame-pointer -fsanitize=address)
target_link_options(watch_sync_test PRIVATE -fsanitize=address)
target_include_directories(watch_sync_test PRIVATE ${PROJECT_SOURCE_DIR}/src ${CJSON_INC} ${SDL2_INCLUDE_DIRS})
target_link_libraries(watch_sync_test lvgl ${SDL2_LIBRARIES} ${CJSON_LIB} CURL::libcurl m Threads::Threads)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/sync_test_run)
add_custom_target(sync_test
    COMMAND ${CMAKE_COMMAND} -E make_directory fx
    COMMAND python3 ${PROJECT_SOURCE_DIR}/tests/make_fixtures.py fx
    COMMAND ${CMAKE_COMMAND} -E env ASAN_OPTIONS=detect_leaks=0 $<TARGET_FILE:watch_sync_test> fx ${PROJECT_SOURCE_DIR}/tools/sync_client.py
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/sync_test_run
    DEPENDS watch_sync_test
    VERBATIM)

# Ядро как сервер синхронизации без окна (для проверки клиентов, например tests/run_kotlin_selftest.py)
add_executable(sync_server EXCLUDE_FROM_ALL tests/sync_server.c src/launcher.c ${WATCH_SOURCES} ${CJSON_SRC})
target_compile_definitions(sync_server PRIVATE LV_CONF_INCLUDE_SIMPLE)
target_include_directories(sync_server PRIVATE ${PROJECT_SOURCE_DIR}/src ${CJSON_INC} ${SDL2_INCLUDE_DIRS})
target_link_libraries(sync_server lvgl ${SDL2_LIBRARIES} ${CJSON_LIB} CURL::libcurl m Threads::Threads)

# Цветной код 16x16 (ввод пароля Wi-Fi без клавиатуры): кодек без LVGL, с AddressSanitizer/UBSan:
#   cmake --build build --target colorcode_test
add_executable(watch_colorcode_test EXCLUDE_FROM_ALL tests/colorcode_test.c src/watch/colorcode.c)
target_compile_options(watch_colorcode_test PRIVATE -g -Wall -Wextra -fsanitize=address,undefined)
target_link_options(watch_colorcode_test PRIVATE -fsanitize=address,undefined)
add_custom_target(colorcode_test COMMAND watch_colorcode_test DEPENDS watch_colorcode_test VERBATIM)

# Постоянные токены сопряжения (paired.c): без LVGL, с AddressSanitizer/UBSan. Тест пишет paired_dir/ в текущей папке:
#   cmake --build build --target paired_test
add_executable(watch_paired_test EXCLUDE_FROM_ALL tests/paired_test.c src/watch/paired.c src/watch/fs_posix.c src/watch/sha256.c ${CJSON_SRC})
target_compile_options(watch_paired_test PRIVATE -g -Wall -Wextra -fsanitize=address,undefined)
target_link_options(watch_paired_test PRIVATE -fsanitize=address,undefined)
target_include_directories(watch_paired_test PRIVATE ${PROJECT_SOURCE_DIR}/src ${CJSON_INC})
target_link_libraries(watch_paired_test ${CJSON_LIB} m)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/paired_test_run)
add_custom_target(paired_test COMMAND $<TARGET_FILE:watch_paired_test> DEPENDS watch_paired_test WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/paired_test_run VERBATIM)

# Рукопожатие с постоянным токеном (sync_rx.c + paired.c, остальное заглушки), с AddressSanitizer/UBSan:
#   cmake --build build --target pairing_rx_test
add_executable(watch_pairing_rx_test EXCLUDE_FROM_ALL tests/pairing_rx_test.c src/watch/ota.c src/watch/sync_rx.c src/watch/paired.c src/watch/fs_posix.c src/watch/sha256.c src/watch/crc32.c ${CJSON_SRC})
target_compile_definitions(watch_pairing_rx_test PRIVATE LV_CONF_INCLUDE_SIMPLE)
target_compile_options(watch_pairing_rx_test PRIVATE -g -fsanitize=address,undefined)
target_link_options(watch_pairing_rx_test PRIVATE -fsanitize=address,undefined)
target_include_directories(watch_pairing_rx_test PRIVATE ${PROJECT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/src ${CJSON_INC} $<TARGET_PROPERTY:lvgl,INTERFACE_INCLUDE_DIRECTORIES>)
target_link_libraries(watch_pairing_rx_test ${CJSON_LIB} m)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/pairing_rx_test_run)
add_custom_target(pairing_rx_test COMMAND $<TARGET_FILE:watch_pairing_rx_test> DEPENDS watch_pairing_rx_test WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/pairing_rx_test_run VERBATIM)
