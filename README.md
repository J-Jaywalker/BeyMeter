# BeyBeetle (WIP)

The BeyBeetle is a custom tool used to measure the angle and RPM of beyblade launches. This repo contains instructions and resources to create a BeyBeetle of your own!

## Design Philosophy

The BeyBeetle is built from easily available, commercial-off-the-shelf (COTS) hardware, so that it can easily be assambled by people who have limited tools, knowledge, resources and space. Even the 3D printed chasis and bill of materials (BOM) are easily aquired through online printing services and hardware vendors. 

All the code and instructions on how to compile, as well as links to recently compiled and up-to-date .uf2 files are included here, so that enthusiasts of all walks are able to build a BeyBeetle should they want to. 

Also I could have spent time and effort designing a PCB with all of the relevantly housed but I am lazy and so are other people. I dont' want to overcomplicate things for those who don't want to use PCBA servces and batch order like 25 prototypes. 

Why a Beetle? I like beetles, they are one of my favourite animals and my other projects feature them as well. 

## What it does

The BeyBeetle reads launch angle and rotational speed from an IMU strapped to your launcher. On boot it shows a splash screen — press A, B, then C to unlock it and land in the navigation menu, where you hold a button to pick your mode.

**Angle Mode** gives you two views. The bubble level is a spirit level style display with concentric rings showing how far off-axis you are, and a dot that moves in real time as you tilt the launcher — think of it like a crosshair you're trying to keep centred. The gauge view splits the screen in half and shows roll and pitch as large numbers with animated indicator lines and directional chevrons, so you can read it at a glance mid-launch.

**RPM Mode** is where launch speed will be measured once the hardware support is in place. It's on the roadmap.

**Stats** shows your blader profile and personal records — top shoot speed, total launch count, and firmware version. Nothing to brag about yet, but it'll get there.

All angle readings are smoothed with an exponential moving average so the display doesn't jitter, and a small battery icon in the corner keeps an eye on charge level so you're not caught out mid-session.

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
