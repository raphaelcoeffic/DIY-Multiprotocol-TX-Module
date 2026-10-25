# Compiling the STM32 firmware with CMake

The STM32 firmware can be built without the Arduino IDE, from the command
line or from an IDE with CMake support (CLion, Visual Studio Code with the
CMake Tools extension). The firmware is the same as built by the Arduino
IDE: with the same compiler, the binaries are identical.

The protocols and options are still selected in
[Multiprotocol/_Config.h](../Multiprotocol/_Config.h) (or `_MyConfig.h`),
see [Compiling and programming the STM32 module](Compiling_STM32.md).

## What you need

- [CMake](https://cmake.org/download/) 3.21 or newer and [Ninja](https://ninja-build.org/)
- Python 3
- The GNU Arm toolchain (`arm-none-eabi-gcc`), in the PATH or in the
  directory set by `ARM_TOOLCHAIN_DIR` (environment or CMake variable: the
  toolchain's `bin` directory). The Arduino board package uses
  [xPack GCC 8.3.1-1.4](https://github.com/xpack-dev-tools/arm-none-eabi-gcc-xpack/releases/tag/v8.3.1-1.4):
  use it for the same binaries as the release builds. Newer versions work
  too (the code is a bit smaller with GCC 14).

Everything else (the STM32 core, [lib/stm32f1](../lib/stm32f1)) is in the
repository.

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

## How it works

The Arduino IDE builds the `.ino` files as one C++ file, with prototypes for
their functions. [cmake/ino2cpp.py](../cmake/ino2cpp.py) does the same, by
the same rules. [cmake/multi_firmware.py](../cmake/multi_firmware.py)
checks the size and appends the signature, as the board package does. The
flags of each board are in [cmake/stm32f1.cmake](../cmake/stm32f1.cmake),
from the board package's `boards.txt` and `platform.txt`.
