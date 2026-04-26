cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD adafruit_feather_rp2040)

include(pico_sdk_import.cmake)

project(BeyMeter C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(BeyMeter src/main.c)

target_link_libraries(BeyMeter pico_stdlib hardware_i2c)

pico_add_extra_outputs(BeyMeter)

# USB serial output for debugging
pico_enable_stdio_usb(BeyMeter 1)
pico_enable_stdio_uart(BeyMeter 0)
