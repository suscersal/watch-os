cmake_minimum_required(VERSION 3.12.4)
project(watch_os C)

set(CMAKE_C_STANDARD 99)
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)

find_package(SDL2 REQUIRED)
find_package(CURL REQUIRED)
find_library(CJSON_LIB cjson REQUIRED)
find_path(CJSON_INC cJSON.h PATH_SUFFIXES cjson REQUIRED)   # в Ubuntu: /usr/include/cjson

# LVGL (git submodule, закреплён на v9.4.0); lv_conf.h лежит в корне проекта
add_subdirectory(lvgl)
target_include_directories(lvgl PUBLIC ${PROJECT_SOURCE_DIR} ${SDL2_INCLUDE_DIRS})

file(GLOB WATCH_SOURCES src/watch/*.c src/watch/fonts/*.c)
add_executable(main
    src/main.c
    src/launcher.c
    src/hal/hal.c
    src/mouse_cursor_icon.c
    ${WATCH_SOURCES})
target_compile_definitions(main PRIVATE LV_CONF_INCLUDE_SIMPLE)
target_include_directories(main PRIVATE ${CJSON_INC} ${SDL2_INCLUDE_DIRS})
target_link_libraries(main lvgl ${SDL2_LIBRARIES} ${CJSON_LIB} CURL::libcurl m pthread)

# make run: запуск из корня проекта (apps/ и data/ читаются отсюда)
add_custom_target(run
    COMMAND $<TARGET_FILE:main>
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    DEPENDS main
    VERBATIM)
