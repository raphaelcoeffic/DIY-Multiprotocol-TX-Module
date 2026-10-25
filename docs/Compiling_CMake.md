# Compiling the firmware with CMake

The firmware can be built without the Arduino IDE, from the command line or
from an IDE with CMake support (CLion, Visual Studio Code with the CMake
Tools extension): for the STM32 modules, and for the MPM v2 boards (TI
CC26xx, [platforms/cc26xx](../platforms/cc26xx/README.md)), from the same
protocol files. The STM32 firmware is built as by the Arduino IDE, with the
same flags (with the Arduino IDE's compiler, GCC 8.3.1, the binaries are
identical).

The protocols and options are still selected in
[Multiprotocol/_Config.h](../Multiprotocol/_Config.h) (or `_MyConfig.h`),
see [Compiling and programming the STM32 module](Compiling_STM32.md).

## What you need

The compiler is downloaded by CMake (below). The STM32 core
([lib/stm32f1](../lib/stm32f1)) and the CC26xx libraries are in the
repository. Install once:

| To                | Tools                                                 |
|-------------------|-------------------------------------------------------|
| Build             | CMake 3.21 or newer, Ninja, Python 3                  |
| Flash (STM32)     | [stm32flash](https://sourceforge.net/projects/stm32flash/files/) (serial), [dfu-util](https://dfu-util.sourceforge.net/releases/) (USB) |
| Flash (MPM v2)    | nothing more through its USB-C (Python 3.10 or newer); [OpenOCD](https://openocd.org/) for a debugger (J-Link, LaunchPad's XDS110) |

CLion comes with CMake and Ninja, Visual Studio Code needs them installed
(and the CMake Tools extension).

### The compiler

CMake downloads the compiler of the builds,
[xPack GCC 15.2.1-1.1](https://github.com/xpack-dev-tools/arm-none-eabi-gcc-xpack/releases/tag/v15.2.1-1.1)
(300 MB, for Windows x64, macOS and Linux, Intel and ARM), to
`build/toolchains`, once, and checks it (SHA-256).

- `ARM_TOOLCHAIN_DIR` (environment variable or `-DARM_TOOLCHAIN_DIR=...`):
  the `bin` directory of another GNU Arm toolchain, for example the Arduino
  IDE's (GCC 8.3.1,
  `~/Library/Arduino15/packages/multi4in1/tools/xpack-arm-none-eabi-gcc/8.3.1-1.4/bin`
  on macOS) for the same binaries as the Arduino IDE.
- `-DARM_TOOLCHAIN_DOWNLOAD=OFF`: the `arm-none-eabi-gcc` in the PATH.

The compiler of a build directory can't be changed: delete the directory
(`build/<preset>`) to use another one.

### Windows

```
winget install Kitware.CMake Ninja-build.Ninja Python.Python.3.13
```

For flashing, unpack `stm32flash-0.7-binaries.zip` and
`dfu-util-0.11-binaries.tar.xz` and add the directories of `stm32flash.exe`
and `dfu-util.exe` to the PATH.

Drivers: the module's USB-serial adapter (CH340 or CP210x) and, for USB
uploads, the Maple DFU driver: run `BootLoaders\Boards\Windows\install_drivers.bat`,
see [Install device drivers](Arduino_IDE_Boards.md#install-device-drivers).
The Windows USB DFU devices always need a driver: the boot loader of the
modules (STM32duino) does not tell Windows to use its generic one.

The serial port is found when the module is the only USB serial device,
else set `MULTI_FLASH_PORT` to its COM port.

### macOS

With [Homebrew](https://brew.sh/):

```
brew install cmake ninja python stm32flash dfu-util
```

### Linux (Debian, Ubuntu)

```
sudo apt install cmake ninja-build python3 stm32flash dfu-util
```

Access to the serial and USB devices: see
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
| `mpm-v2`                   | MPM v2 (CC2652RB), see [platforms/cc26xx](../platforms/cc26xx/README.md) |
| `mpm-v2-debug`             | MPM v2, debug output on Segger RTT           |
| `launchpad`                | CC2652R1 LaunchPad                           |
| `xjt-lite`                 | FrSky XJT-Lite (CC2650), reduced MPM         |

The STM32 firmware is `build/<preset>/multi-stm-<version>.bin` (the same
as `Multiprotocol.bin`), with the signature the radios check, and the size
is checked as by the Arduino IDE. `build/<preset>/Multiprotocol.elf` is the
firmware for debuggers.

With all the protocols enabled the STM32 firmware does not fit: as with the
Arduino IDE, select the protocols you need in `_Config.h`. The CC26xx
boards have their own configuration,
[mpm_config.h](../platforms/cc26xx/src/mpm/multi/mpm_config.h).

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
`-DMULTI_FLASH_PORT=/dev/ttyUSB0` (`COM3` on Windows) or in the
`MULTI_FLASH_PORT` environment variable. For the `usb` method, a running
firmware with USB is first reset to the boot loader through its USB serial
port. The serial ports are Python's pyserial, in the repository
([lib/pyserial](../lib/pyserial/README.md)): nothing to install.

The USB uploads use the boot loader the serial upload writes (STM32duino,
"Maple"): a module flashed once through its serial port can then be
flashed through USB.

Plugged into a computer, such a module stays in its boot loader
("Maple 003") at power up, even with an external supply: the boot loader
sees the USB host and waits to be flashed. To run the firmware over USB
(USB power is enough, RF included):

```
cmake --build --preset stm32f103cb --target start
```

starts it without writing anything (`multi_flash.py --method start`, with
the libusb that dfu-util uses). A firmware with USB support then shows up
as a serial port ("Maple"); after that, unplugging and replugging USB with
an external supply connected keeps it running.

For the MPM v2, `flash` writes the firmware through its USB-C port, as the
radio does; the other methods (first install, through the radio, with a
debugger) are in [platforms/cc26xx](../platforms/cc26xx/README.md#flashing).

## How it works

The Arduino IDE builds the `.ino` files as one C++ file, with prototypes for
their functions. [cmake/ino2cpp.py](../cmake/ino2cpp.py) does the same, by
the same rules; for the CC26xx boards, without `Multiprotocol.ino` and the
STM32 hardware files, on a prelude of the platform. [cmake/multi_firmware.py](../cmake/multi_firmware.py)
checks the size and appends the signature, as the board package does. The
flags of each board are in [cmake/stm32f1.cmake](../cmake/stm32f1.cmake),
from the board package's `boards.txt` and `platform.txt`.
