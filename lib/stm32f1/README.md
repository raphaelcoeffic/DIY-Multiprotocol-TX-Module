# STM32F1 core (Maple)

The Arduino core the STM32 MULTI-Modules are built on, for the CMake build
(`CMakeLists.txt`, `cmake/stm32f1.cmake`).

It is a copy of the files used from the MULTI-Module STM32 board package
for the Arduino IDE, version 1.3.1:
[DIY-Multiprotocol-TX-Module-Boards](https://github.com/pascallanger/DIY-Multiprotocol-TX-Module-Boards),
`source/stm32` at commit d5c46ce ("STM32 board 1.3.1"), itself based on
[Arduino_STM32](https://github.com/rogerclarkmelbourne/Arduino_STM32)
and [libmaple](https://github.com/leaflabs/libmaple).

| Directory                      | Board package                                   |
|--------------------------------|-------------------------------------------------|
| `cores/maple`                  | `cores/maple`                                   |
| `system/libmaple`              | `system/libmaple` (headers)                     |
| `variants/generic_stm32f103c`  | `variants/generic_stm32f103c` (linker scripts)  |
| `libraries/SPI`, `libraries/EEPROM` | `libraries/SPI/src`, `libraries/EEPROM`     |
| `bootloaders/StmMulti4in1.bin` | `bootloaders/Multi4in1` (written by the serial upload) |

Left out: the upload tools (cmake/multi_flash.py does the same with
stm32flash and dfu-util), drivers, library examples and
makefiles. The licenses are in the files: MIT (libmaple), LGPL 2.1 (the
Arduino API).
