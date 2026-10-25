# MPM v2: the MULTI-Module on the TI CC26xx

The MULTI-Module firmware for the boards built on a TI CC26xx (Cortex-M4F
CC2652, Cortex-M3 CC2650). These chips have no RF chips around them: their
radio emulates the A7105, CC2500, CYRF6936, NRF24L01 and XN297, so the
protocol files are the same as on the STM32 modules, built unchanged:
[Multiprotocol/*.ino](../../Multiprotocol).

| Board (`MULTI_BOARD`, preset) | Hardware                                      | Flashing          |
|-------------------------------|-----------------------------------------------|-------------------|
| `mpm-v2`, `mpm-v2-debug`      | MPM v2 module (CC2652RB)                      | its USB-C, the radio, or a J-Link on J10 |
| `launchpad`                   | CC2652R1 LaunchPad (LAUNCHXL-CC26X2R1)        | XDS110 (OpenOCD)  |
| `xjt-lite`                    | FrSky XJT-Lite (CC2650F128), reduced MPM      | patched OpenOCD, cJTAG, J-Link |

```
cmake --preset mpm-v2
cmake --build --preset mpm-v2
cmake --build --preset mpm-v2 --target flash
```

In `build/mpm-v2`: `mpm-v2.bin` for the radio (flashed from EdgeTX),
`mpm-v2-full.bin` / `.hex` for a first install (boot loader and
application), `firmware.elf` for debuggers. `mpm-v2-debug` sends debug
output to Segger RTT.

## Flashing

`--target flash`, with `-DMULTI_FLASH_METHOD=...`:

| Method     | How                                                                  |
|------------|----------------------------------------------------------------------|
| `usb`      | MPM v2 (default): its USB-C port, through the MPM v2 boot loader, as the radio flashes `mpm-v2.bin` ([tools/stk500_flash.py](tools/stk500_flash.py)) |
| `usb-rom`  | MPM v2: its USB-C port, through the chip's ROM boot loader: boot loader and firmware (`mpm-v2-full.bin`, [tools/rom_flash.py](tools/rom_flash.py)), for the first install or a module that no longer starts |
| `radio`    | MPM v2 in the radio's bay, through the radio's USB: EdgeTX with the external module pass-through, `MULTI_FLASH_PORT` = the radio's USB serial port |
| `debugger` | `flash-firmware`: MPM v2 with a J-Link ([tools/flash_jlink.sh](tools/flash_jlink.sh), OpenOCD), LaunchPad with its XDS110, XJT-Lite with the patched OpenOCD (default of these boards) |

The USB-C port is found when one MPM v2 is connected (its CH343), else set
`MULTI_FLASH_PORT` (or `V2_PORT` in the environment). The `usb` and `radio`
methods need Python's pyserial (`pip install pyserial`, Debian/Ubuntu:
`apt install python3-serial`), or [uv](https://docs.astral.sh/uv/), which
CMake then runs the scripts with. Without CMake:
`tools/stk500_flash.py build/mpm-v2/mpm-v2.bin`.

## How the protocol files are built

[cmake/ino2cpp.py](../../cmake/ino2cpp.py) makes one C++ file of the
`.ino` files, as for the STM32, with:

- the prelude [src/mpm/multi/multi_ino.h](src/mpm/multi/multi_ino.h) in
  place of `Multiprotocol.ino`: the globals, flags and helpers the protocol
  files use, on the port's core
  ([src/mpm/multi/mpm_compat.h](src/mpm/multi/mpm_compat.h));
- the protocols and options of
  [src/mpm/multi/mpm_config.h](src/mpm/multi/mpm_config.h), not `_Config.h`;
- the `.ino` files in `extern "C"`: the port is written in C;
- left out ([src/CMakeLists.txt](src/CMakeLists.txt), `multi_ino_exclude`):
  `Multiprotocol.ino` and the STM32/AVR hardware (`*_SPI.ino`, `SPI.ino`,
  `Telemetry.ino`, ...), which the port has its own of in
  [src/mpm/multi](src/mpm/multi) (the chip emulations, the XN297 emulation,
  the telemetry), the protocol table (`Multi_Protos.ino`:
  [src/mpm/protocols.c](src/mpm/protocols.c)) and the emulations not
  ported yet (HS6200, NRF250K, RF2500).

## What is where

| Directory          | What                                                          |
|--------------------|---------------------------------------------------------------|
| `src/`             | core: boards, RF core driver, framing, scheduler, UART, PPM, storage, boot loader |
| `src/mpm/`         | the MULTI-Module serial protocol, settings, protocol table    |
| `src/mpm/multi/`   | what the protocol files need: compat layer, RF chip emulations, telemetry |
| `lib/ti/devices/`  | TI driverlib (CC13x2/CC26x2, CC26x0), BSD-3-Clause            |
| `lib/SeggerRTT/`   | Segger RTT (debug output)                                     |
| `lib/littlefs/`    | littlefs 2.10 (`lfs.c`, `lfs_util.c`), BSD-3-Clause          |
| `tools/`           | `fw_image.py` (the file for the radio), flashing (`stk500_flash.py`, `rom_flash.py`, `flash_jlink.sh`), EEPROM and file system tools |
| `tests/`           | host tests (`make -C tests/frame`, `tests/mpm`, `tests/storage`) |

This is a copy of the [mpmv2](https://github.com/raphaelcoeffic/mpmv2)
port (v0.3.0, 70babb8) without its history; its documentation (hardware,
RF core notes, radio bench) stays there.
