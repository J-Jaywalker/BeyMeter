# BeyMeter

Firmware for the Adafruit Feather RP2040, written in C.

## First-time setup

Open WSL and run these steps:

**1. Install the toolchain**
```bash
sudo apt install cmake gcc-arm-none-eabi libnewlib-arm-none-eabi build-essential
```

**2. Clone the Pico SDK**
```bash
git clone https://github.com/raspberrypi/pico-sdk ~/pico-sdk
cd ~/pico-sdk && git submodule update --init
echo 'export PICO_SDK_PATH=~/pico-sdk' >> ~/.bashrc && source ~/.bashrc
```

**3. Clone the libraries**
```bash
cd ~/BeyMeter
mkdir lib
git clone https://github.com/olikraus/u8g2 lib/u8g2
git clone https://github.com/STMicroelectronics/ism330dhcx-pid lib/ism330dhcx-pid
```

That's it — you only need to do this once.

## Every day usage

From `~/BeyMeter`:

```bash
make uf2    # clean build → BeyMeter.uf2
make hex    # clean build → BeyMeter.hex
make build  # incremental build (faster, no clean)
make clean  # wipe build artifacts
```

## Flashing

1. Hold **BOOTSEL** on the Feather and plug it into USB
2. It appears as a drive called `RPI-RP2` in Windows Explorer
3. Drag `BeyMeter.uf2` onto the drive — it reboots automatically

## Viewing serial output

1. Flash the board and let it reboot
2. Open your serial monitor on COM4 (any baud rate)
3. You should see output printing every second

## Hardware

| Component | Connection |
|-----------|------------|
| Adafruit Feather RP2040 | — |
| ISM330DHCX IMU | STEMMA QT (I2C) |
| SH1107 Display | STEMMA QT (I2C) |
