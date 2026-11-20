cmake_minimum_required(VERSION 3.13)

# Тестовые прошивки мигания LED для проверки программатора submodule_rpico.
# 4 отдельные цели: 100 / 500 / 1000 / 2000 мс (полупериод).
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(rpico_test_fw C CXX)

pico_sdk_init()

foreach(ms 100 500 1000 2000)
    set(target blink_${ms})
    add_executable(${target} main.c)
    target_compile_definitions(${target} PRIVATE BLINK_MS=${ms})
    target_link_libraries(${target} pico_stdlib)
    pico_enable_stdio_usb(${target} 0)
    pico_enable_stdio_uart(${target} 0)
    pico_add_bin_output(${target})
endforeach()
