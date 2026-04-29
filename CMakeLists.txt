cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD adafruit_feather_rp2040)

include(pico_sdk_import.cmake)

project(BeyMeter C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

file(GLOB U8G2_SOURCES lib/u8g2/csrc/*.c)

add_executable(BeyMeter
    src/main.c
    src/hw.c
    src/screens.c
    ${U8G2_SOURCES}
    lib/ism330dhcx-pid/ism330dhcx_reg.c
)

target_include_directories(BeyMeter PRIVATE
    lib/u8g2/csrc
    lib/ism330dhcx-pid
)

target_link_libraries(BeyMeter pico_stdlib hardware_i2c pico_float m)

pico_add_extra_outputs(BeyMeter)

# USB serial output for debugging
pico_enable_stdio_usb(BeyMeter 1)
pico_enable_stdio_uart(BeyMeter 0)
