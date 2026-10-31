# MPM v2: the MULTI-Module on TI's CC26xx and CC27xx

The MULTI-Module firmware for the boards built on a TI CC26xx (Cortex-M4F
CC2652, Cortex-M3 CC2650) or CC27xx (Cortex-M33 CC2755). These chips have no
RF chips around them: their radio emulates the A7105, CC2500, CYRF6936,
NRF24L01 and XN297, so the protocol files are the same as on the STM32
modules, built unchanged: [Multiprotocol/*.ino](../Multiprotocol).

| Board (`MULTI_BOARD`, preset) | Hardware                                      | Flashing          |
|-------------------------------|-----------------------------------------------|-------------------|
| `mpm-v2`, `mpm-v2-debug`      | MPM v2 module (CC2652RB)                      | its USB-C, the radio, or a J-Link on J10 |
| `launchpad`                   | CC2652R1 LaunchPad (LAUNCHXL-CC26X2R1)        | XDS110 (OpenOCD)  |
| `xjt-lite`                    | FrSky XJT-Lite (CC2650F128), reduced MPM      | patched OpenOCD, cJTAG, J-Link |
| `cc2755-lp`                   | CC2755P10 LaunchPad (LP-EM-CC2755P10)         | XDS110, OpenOCD with the cc27xx driver |

```
cmake --preset mpm-v2
cmake --build --preset mpm-v2
cmake --build --preset mpm-v2 --target flash
```

In `build/mpm-v2`: `mpm-v2.bin` for the radio (flashed from EdgeTX),
`mpm-v2-full.bin` / `.hex` for a first install (boot loader and
application), `firmware.elf` for debuggers. `mpm-v2-debug` sends debug
output to Segger RTT. Each board also builds a radio bench (`bench`,
`cc27-bench`) and bring-up applications.

The CC2755 LaunchPad runs the MPM on its XDS110's UART (100000 8E2, the
radio's link): `cc27-mpm.bin` behind the boot loader at 0x2000,
`cc27-mpm-update.bin` for an update through it. OpenOCD needs the cc27xx
flash driver ([raphaelcoeffic/openocd](https://github.com/raphaelcoeffic/openocd),
branch cc27xx): `-DCC27_OPENOCD="<openocd> -s <its tcl dir>"` when not
installed. `-DCC27_MODULE_ID=0x...` gives it another module's ID (the
receivers bound to that module).

## Flashing

`--target flash`, with `-DMULTI_FLASH_METHOD=...`:

| Method     | How                                                                  |
|------------|----------------------------------------------------------------------|
| `usb`      | MPM v2 (default): its USB-C port, through the MPM v2 boot loader, as the radio flashes `mpm-v2.bin` ([tools/stk500_flash.py](../tools/stk500_flash.py)) |
| `usb-rom`  | MPM v2: its USB-C port, through the chip's ROM boot loader: boot loader and firmware (`mpm-v2-full.bin`, [tools/rom_flash.py](../tools/rom_flash.py)), for the first install or a module that no longer starts |
| `radio`    | MPM v2 in the radio's bay, through the radio's USB: EdgeTX with the external module pass-through, `MULTI_FLASH_PORT` = the radio's USB serial port |
| `debugger` | `flash-firmware` (`flash-cc27-mpm`): MPM v2 with a J-Link ([tools/flash_jlink.sh](../tools/flash_jlink.sh), OpenOCD), the LaunchPads with their XDS110, XJT-Lite with the patched OpenOCD (default of these boards) |

The USB-C port is found when one MPM v2 is connected (its CH343), else set
`MULTI_FLASH_PORT` (or `V2_PORT` in the environment). The scripts need
Python 3.10 or newer and nothing else (pyserial is in
[lib/pyserial](../lib/pyserial/README.md)). Without CMake:
`python3 tools/stk500_flash.py build/mpm-v2/mpm-v2.bin`.

## How the protocol files are built

[cmake/ino2cpp.py](../cmake/ino2cpp.py) makes one C++ file of the `.ino`
files for each application, as for the STM32, with
([src/mpm/sketch/protocols.cmake](../src/mpm/sketch/protocols.cmake)):

- the prelude [src/mpm/sketch/multi_ino.h](../src/mpm/sketch/multi_ino.h)
  in place of `Multiprotocol.ino`: the globals, flags and helpers the
  protocol files use, on the port's core
  ([src/mpm/multi/mpm_compat.h](../src/mpm/multi/mpm_compat.h)), and the RF
  chips ([src/mpm/sketch/chips.h](../src/mpm/sketch/chips.h) on
  `Multiprotocol/iface_*.h`);
- the protocols and options of
  [src/mpm/multi/mpm_config.h](../src/mpm/multi/mpm_config.h), not
  `_Config.h`;
- the `.ino` files in `extern "C"`: the port is written in C;
- left out (`multi_ino_exclude`): `Multiprotocol.ino` and the STM32/AVR
  hardware (`*_SPI.ino`, `SPI.ino`, `Telemetry.ino`, ...), which the port
  has its own of in [src/mpm/multi](../src/mpm/multi) (the chip emulations,
  the XN297 emulation, the telemetry), the protocol table
  (`Multi_Protos.ino`: [src/mpm/protocols.c](../src/mpm/protocols.c)) and
  the emulations not ported yet (HS6200, NRF250K, RF2500).

## What is where

| Directory                  | What                                                          |
|----------------------------|---------------------------------------------------------------|
| `src/core/`                | code without MCU code: framings (`frame.c`), radio bench, boot loader, log, file system, EEPROM file, settings |
| `src/hal/`                 | the interfaces a platform or a board implements: `radio.h`, `timer.h`, `uart.h`, `spi.h`, ... |
| `src/drivers/`             | external chips: SX1281, NOR flash, RGB LED                    |
| `src/platforms/<family>/`  | `src/hal` for an MCU family: `cc26xx` (CC2652, CC2650: RF core), `cc27xx` (CC2755: LRF) |
| `src/boards/<board>/`      | a board's pins, applications and flashing: `mpm_v2` (and the CC2652R1 LaunchPad), `xjt_lite`, `cc2755p10_lp` |
| `src/mpm/`                 | the MULTI-Module serial protocol, scheduler, settings, protocol table |
| `src/mpm/multi/`           | what the protocol files need: compat layer, RF chip emulations, telemetry |
| `src/mpm/sketch/`          | the build of `Multiprotocol/*.ino` (this repository only)     |
| `lib/ti/`                  | TI driverlib (CC13x2/CC26x2, CC26x0, CC27xx) and LRF layer, BSD-3-Clause |
| `lib/SeggerRTT/`           | Segger RTT (debug output)                                     |
| `lib/littlefs/`            | littlefs 2.10 (`lfs.c`, `lfs_util.c`), BSD-3-Clause          |
| `tools/`                   | `fw_image.py` (the file for the radio), flashing (`stk500_flash.py`, `rom_flash.py`, `flash_jlink.sh`, `flash_dslite.sh`), CC27xx CCFG and PHY generators, EEPROM and file system tools |
| `tests/`                   | host tests (`make -C tests/frame`, `tests/mpm`, `tests/storage`) |
| `cmake/mpm_v2.cmake`       | the compiler options and boards (this repository only)        |

`src/` (but `src/mpm/sketch`), `lib/` (but `lib/stm32f1`), `tools/` and
`tests/` are copied from the [mpmv2](https://github.com/raphaelcoeffic/mpmv2)
repository by its `tools/multi_sync.sh`, without their history; the same
files there build C copies of the protocol files (its `src/mpm/upstream`).
Its documentation (hardware, RF core notes, radio bench) stays there.
