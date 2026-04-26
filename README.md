# BeyMeter

Firmware for the Adafruit Feather RP2040, written in C.

## First-time setup

Open WSL and run these three steps:

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

**3. Configure the build**
```bash
cd ~/BeyMeter
mkdir build && cd build && cmake ..
```

That's it — you only need to do this once.

## Every day usage

From `~/BeyMeter`:

```bash
make uf2    # build and output BeyMeter.uf2
make hex    # build and output BeyMeter.hex
make clean  # clean up build artifacts
```

## Flashing

1. Hold **BOOTSEL** on the Feather and plug it into USB
2. It appears as a drive called `RPI-RP2` in Windows Explorer
3. Drag `BeyMeter.uf2` onto the drive — done, it reboots automatically

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
