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
