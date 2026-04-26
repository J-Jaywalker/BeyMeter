# BeyMeter

Firmware for the Adafruit Feather RP2040. Written in C using the Raspberry Pi Pico SDK.

## Hardware

- **Board:** Adafruit Feather RP2040
- **IMU:** ISM330DHCX (via STEMMA QT / I2C)
- **Display:** SH1107 (via STEMMA QT / I2C)

STEMMA QT uses I2C1 on GPIO2 (SDA) and GPIO3 (SCL).

## Prerequisites

### Toolchain (WSL / Linux)

```bash
sudo apt install cmake gcc-arm-none-eabi libnewlib-arm-none-eabi build-essential
```

### Pico SDK

```bash
git clone https://github.com/raspberrypi/pico-sdk ~/pico-sdk
cd ~/pico-sdk && git submodule update --init
echo 'export PICO_SDK_PATH=~/pico-sdk' >> ~/.bashrc && source ~/.bashrc
```

## Building

Configure once (only needed on first clone or after changing CMakeLists.txt):

```bash
mkdir build && cd build
cmake ..
```

Then build:

```bash
make build        # compile only
make uf2          # compile + copy BeyMeter.uf2 to project root
make hex          # compile + copy BeyMeter.hex to project root
make clean        # remove build artifacts and copied outputs
```

## Flashing

1. Hold **BOOTSEL** on the Feather while plugging in USB — it appears as a drive called `RPI-RP2`
2. Drag `BeyMeter.uf2` onto the drive
3. The board reboots and runs the new firmware automatically

## Serial Output

The firmware prints over USB CDC serial (`stdio_init_all()` + `pico_enable_stdio_usb`).

1. Flash the board and let it reboot
2. Open a serial monitor on the COM port that appears (e.g. COM4 on Windows)
3. Any baud rate works — USB CDC is virtual

## Project Structure

```
BeyMeter/
├── CMakeLists.txt
├── Makefile
├── pico_sdk_import.cmake
├── src/
│   └── main.c
└── lib/
    ├── ism330dhcx-pid/       # ST IMU driver (drop ism330dhcx_reg.h/.c here)
    └── displaylib_1bit_PICO/ # SH1107 display driver
```
