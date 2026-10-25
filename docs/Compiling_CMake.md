# Compiling the STM32 firmware with CMake

The STM32 firmware can be built without the Arduino IDE, from the command
line or from an IDE with CMake support (CLion, Visual Studio Code with the
CMake Tools extension). The firmware is the same as built by the Arduino
IDE: with the same compiler, the binaries are identical.

The protocols and options are still selected in
[Multiprotocol/_Config.h](../Multiprotocol/_Config.h) (or `_MyConfig.h`),
see [Compiling and programming the STM32 module](Compiling_STM32.md).

## What you need

The compiler is downloaded by CMake when needed, as the Arduino IDE does
(below). The STM32 core ([lib/stm32f1](../lib/stm32f1)) is in the
repository. Install once:

| To                | Tools                                                 |
|-------------------|-------------------------------------------------------|
| Build             | CMake 3.21 or newer, Ninja, Python 3                  |
| Flash             | [stm32flash](https://sourceforge.net/projects/stm32flash/files/) (serial), [dfu-util](https://dfu-util.sourceforge.net/releases/) (USB) |

CLion comes with CMake and Ninja, Visual Studio Code needs them installed
(and the CMake Tools extension).

### The compiler

When there is no `arm-none-eabi-gcc` in the PATH (and `ARM_TOOLCHAIN_DIR`
is not set), CMake downloads the compiler of the Arduino board package,
[xPack GCC 8.3.1-1.4](https://github.com/xpack-dev-tools/arm-none-eabi-gcc-xpack/releases/tag/v8.3.1-1.4)
(120 MB, for Windows, macOS and Linux x86-64; on Apple silicon Macs it runs
with Rosetta), to `build/toolchains`, once. With it the firmware is the same
as the Arduino IDE's and the releases'.

- `-DARM_TOOLCHAIN_DOWNLOAD=ON`: use the downloaded compiler even when
  there is one in the PATH (the same firmware as the releases).
- `ARM_TOOLCHAIN_DIR` (environment variable or `-DARM_TOOLCHAIN_DIR=...`):
  the `bin` directory of another GNU Arm toolchain. A newer one works too
  (the firmware is a bit smaller with GCC 14), installed as below.

The compiler of a build directory can't be changed: delete the directory
(`build/<preset>`) to use another one.

### Windows

```
winget install Kitware.CMake Ninja-build.Ninja Python.Python.3.13
```

and optionally a newer compiler, `winget install Arm.GnuArmEmbeddedToolchain`.
For flashing, unpack
`stm32flash-0.7-binaries.zip` and `dfu-util-0.11-binaries.tar.xz` and add
the directories of `stm32flash.exe` and `dfu-util.exe` to the PATH.

Drivers: the module's USB-serial adapter (CH340 or CP210x) and, for USB
uploads, the Maple DFU driver: run `BootLoaders\Boards\Windows\install_drivers.bat`,
see [Install device drivers](Arduino_IDE_Boards.md#install-device-drivers).
The Windows USB DFU devices always need a driver: the boot loader of the
modules (STM32duino) does not tell Windows to use its generic one.

The serial port is found when the module is the only USB serial device,
else set `MULTI_FLASH_PORT` to its COM port. A running firmware is not reset
to the USB boot loader on Windows: for the `usb` method, the module must
already be in the boot loader.

### macOS

With [Homebrew](https://brew.sh/):

```
brew install cmake ninja python stm32flash dfu-util
```

and optionally a newer compiler, `brew install --cask gcc-arm-embedded`
(not the `arm-none-eabi-gcc` formula, which has no C library).

### Linux (Debian, Ubuntu)

```
sudo apt install cmake ninja-build python3 stm32flash dfu-util
```

and optionally a newer compiler (needed on ARM64 Linux, which has no xPack
GCC 8.3.1): `sudo apt install gcc-arm-none-eabi libnewlib-arm-none-eabi
libstdc++-arm-none-eabi-newlib`. Access to the serial and USB devices: see
[Linux](Arduino_IDE_Boards.md#linux) (udev rules, `dialout` and `plugdev`
groups).

## Building

```
cmake --preset stm32f103cb
cmake --build --preset stm32f103cb
```

| Preset                     | Board                                        |
|----------------------------|----------------------------------------------|
| `stm32f103cb`              | STM32F103CB (128KB): 4-in-1, 5-in-1, T-Lite  |
| `stm32f103cb-usb-debug`    | STM32F103CB, native USB debugging            |
| `stm32f103cb-serial-debug` | STM32F103CB, serial/FTDI debugging (no boot loader) |
| `stm32f103c8`              | STM32F103C8 (64KB)                           |
| `t18int`                   | T18 internal 5-in-1                          |

The firmware is `build/<preset>/multi-stm-<version>.bin` (the same as
`Multiprotocol.bin`), with the signature the radios check, and the size is
checked as by the Arduino IDE. `build/<preset>/Multiprotocol.elf` is the
firmware for debuggers.

With all the protocols enabled the firmware does not fit: as with the
Arduino IDE, select the protocols you need in `_Config.h`.

In CLion or Visual Studio Code, open the repository's folder: the presets
are listed as configurations.

## Flashing

```
cmake --build --preset stm32f103cb --target flash
```

builds the firmware and flashes it as the Arduino IDE uploads it, with
[stm32flash](https://sourceforge.net/projects/stm32flash/) and
[dfu-util](https://dfu-util.sourceforge.net/) (Homebrew, apt:
`stm32flash`, `dfu-util`):

| Method          | How                                                              |
|-----------------|------------------------------------------------------------------|
| `auto`          | `usb` if a Maple USB device is connected, else `serial` (default)|
| `serial`        | Through the module's USB-serial port: erases the flash, writes the boot loader and the firmware |
| `usb`           | Maple USB boot loader (dfu-util), default of `stm32f103cb-usb-debug` |
| `serial-noboot` | Serial, without boot loader, default of `stm32f103cb-serial-debug` |

The method is set with `-DMULTI_FLASH_METHOD=...`. The serial port is
found when there is only one USB serial port, else set it with
`-DMULTI_FLASH_PORT=/dev/ttyUSB0` or in the `MULTI_FLASH_PORT`
environment variable. Resetting a running USB firmware to the boot loader
(the `usb` method) works on Linux and macOS.

The USB uploads use the boot loader the serial upload writes (STM32duino,
"Maple"): a module flashed once through its serial port can then be
flashed through USB.

## How it works

The Arduino IDE builds the `.ino` files as one C++ file, with prototypes for
their functions. [cmake/ino2cpp.py](../cmake/ino2cpp.py) does the same, by
the same rules. [cmake/multi_firmware.py](../cmake/multi_firmware.py)
checks the size and appends the signature, as the board package does. The
flags of each board are in [cmake/stm32f1.cmake](../cmake/stm32f1.cmake),
from the board package's `boards.txt` and `platform.txt`.
