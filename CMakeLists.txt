cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD adafruit_feather_rp2040)

include(pico_sdk_import.cmake)

project(BeyMeter C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(BeyMeter
    src/main.c
    src/hw.c
    src/screens.c
    src/driver_st7789_interface.c
    lib/st7789/src/driver_st7789.c
    lib/ism330dhcx-pid/ism330dhcx_reg.c
)

target_include_directories(BeyMeter PRIVATE
    src
    lib/st7789/src
    lib/st7789/interface
    lib/ism330dhcx-pid
)

target_link_libraries(BeyMeter pico_stdlib hardware_i2c hardware_spi pico_float m)

pico_add_extra_outputs(BeyMeter)

pico_enable_stdio_usb(BeyMeter 0)
pico_enable_stdio_uart(BeyMeter 0)
