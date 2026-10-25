# MPM v2: the MULTI-Module on the TI CC26xx (CC2652, CC2650), its radio
# emulating the RF chips. The protocol files are Multiprotocol/*.ino, built
# unchanged as one C++ file (src/mpm/multi/multi_ino.h); the protocols and
# options are in src/mpm/multi/mpm_config.h.
#
# Included from the top CMakeLists.txt for the boards (MULTI_BOARD):
#   mpm-v2     MPM v2 module (CC2652RB), J-Link
#   launchpad  CC2652R1 LaunchPad (LAUNCHXL-CC26X2R1), XDS110
#   xjt-lite   FrSky XJT-Lite (CC2650F128): reduced MPM, patched OpenOCD

project(MPMv2 VERSION 0.3.0.0 LANGUAGES C CXX ASM)

set(CC26XX_DIR ${CMAKE_CURRENT_SOURCE_DIR})

if(MULTI_BOARD STREQUAL "mpm-v2")
  set(BOARD MPM_V2)
elseif(MULTI_BOARD STREQUAL "launchpad")
  set(BOARD CC2652R1_LAUNCHXL)
elseif(MULTI_BOARD STREQUAL "xjt-lite")
  set(BOARD XJT_LITE)
else()
  message(FATAL_ERROR "MULTI_BOARD=${MULTI_BOARD}: not a CC26xx board")
endif()

# MULTI_DEBUG=rtt: debug output on Segger RTT
if(MULTI_DEBUG STREQUAL "rtt")
  set(DEBUG ON)
endif()
option(USE_CCFG "Include MCU user configuration (CCFG)" ON)

set(CMAKE_EXECUTABLE_SUFFIX ".elf")
set(CMAKE_EXECUTABLE_SUFFIX_C ".elf")
set(CMAKE_EXECUTABLE_SUFFIX_CXX ".elf")

add_library(toolchain_gcc INTERFACE)
target_compile_options(toolchain_gcc INTERFACE
  $<$<COMPILE_LANGUAGE:C>:-std=c11>
  $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++11 -fno-rtti -fno-exceptions>
  -Os -Wall
  -mthumb -g
  -mabi=aapcs
  -fomit-frame-pointer
  -ffunction-sections
  -fdata-sections
  -fmacro-prefix-map=${CMAKE_SOURCE_DIR}=.
)
target_link_options(toolchain_gcc INTERFACE
  # If linker-file property exists, add linker file
  $<$<NOT:$<STREQUAL:$<TARGET_PROPERTY:LINKER_FILE>,>>:-Wl,-T,$<TARGET_PROPERTY:LINKER_FILE>>
  # If map-file property exists, set map file
  $<$<NOT:$<STREQUAL:$<TARGET_PROPERTY:LINKER_MAP>,>>:-Wl,-Map,$<TARGET_PROPERTY:LINKER_MAP>>
  -specs=nano.specs
  # Disables 0x10000 sector allocation boundaries, which interfere
  # with the SPE layouts and prevent proper secure operation
  -Wl,--nmagic
  # Link only what is used
  -Wl,--gc-sections
)
add_library(toolchain_gcc_m4f INTERFACE)
target_compile_options(toolchain_gcc_m4f INTERFACE -mcpu=cortex-m4 -mfloat-abi=hard -mfpu=fpv4-sp-d16)
target_link_options(toolchain_gcc_m4f INTERFACE -mcpu=cortex-m4 -mfloat-abi=hard -mfpu=fpv4-sp-d16)
target_link_libraries(toolchain_gcc_m4f INTERFACE toolchain_gcc)

# CC26x0 (XJT-Lite's CC2650): no FPU
add_library(toolchain_gcc_m3 INTERFACE)
target_compile_options(toolchain_gcc_m3 INTERFACE -mcpu=cortex-m3 -mfloat-abi=soft)
target_link_options(toolchain_gcc_m3 INTERFACE -mcpu=cortex-m3 -mfloat-abi=soft)
target_link_libraries(toolchain_gcc_m3 INTERFACE toolchain_gcc)

add_subdirectory(lib)
add_subdirectory(src)

# cmake --build --preset <preset> --target flash: the MPM firmware.
#   usb       MPM v2: through its USB-C port and the MPM v2 boot loader, as
#             the radio flashes it (tools/stk500_flash.py, mpm-v2.bin)
#   usb-rom   MPM v2: through its USB-C port and the chip's ROM boot loader,
#             boot loader and firmware (tools/rom_flash.py, mpm-v2-full.bin):
#             the first install, or a module that no longer starts
#   radio     MPM v2 in the radio's bay, through the radio's USB (EdgeTX
#             with the external module pass-through, its CLI on
#             MULTI_FLASH_PORT)
#   debugger  flash-firmware: MPM v2 with a J-Link, LaunchPad with its
#             XDS110, XJT-Lite with the patched OpenOCD
# The usb and radio methods need pyserial (pip install pyserial), or uv.
if(BOARD STREQUAL "MPM_V2")
  set(_flash_methods usb usb-rom radio debugger)
else()
  set(_flash_methods debugger)
endif()
set(MULTI_FLASH_METHOD "" CACHE STRING
  "Flash method: usb, usb-rom, radio, debugger (empty: the board's first)")
set(MULTI_FLASH_PORT "" CACHE STRING
  "Serial port: the MPM v2's USB-C (empty: the first CH343, or V2_PORT in the environment), the radio's for radio")
if(MULTI_FLASH_METHOD)
  set(_flash_method ${MULTI_FLASH_METHOD})
else()
  list(GET _flash_methods 0 _flash_method)
endif()
if(NOT _flash_method IN_LIST _flash_methods)
  message(FATAL_ERROR "MULTI_FLASH_METHOD=${_flash_method}: one of ${_flash_methods} for ${MULTI_BOARD}")
endif()

if(_flash_method STREQUAL "debugger")
  add_custom_target(flash)
  add_dependencies(flash flash-firmware)
else()
  # Python with pyserial, else uv (the scripts name their dependencies)
  execute_process(COMMAND ${Python3_EXECUTABLE} -c "import serial"
    RESULT_VARIABLE _no_pyserial OUTPUT_QUIET ERROR_QUIET)
  find_program(UV uv)
  if(NOT _no_pyserial)
    set(_python ${Python3_EXECUTABLE})
  elseif(UV)
    set(_python ${UV} run --script)
  else()
    set(_python ${Python3_EXECUTABLE})
    message(WARNING "No pyserial for ${Python3_EXECUTABLE}: the flash target needs it "
      "(pip install pyserial)")
  endif()
  if(MULTI_FLASH_PORT)
    set(_port --port ${MULTI_FLASH_PORT})
  endif()
  if(_flash_method STREQUAL "usb-rom")
    set(_flash_cmd ${CC26XX_DIR}/tools/rom_flash.py ${CMAKE_BINARY_DIR}/mpm-v2-full.bin ${_port})
  elseif(_flash_method STREQUAL "radio")
    set(_flash_cmd ${CC26XX_DIR}/tools/stk500_flash.py ${CMAKE_BINARY_DIR}/mpm-v2.bin ${_port} --radio)
  else()
    set(_flash_cmd ${CC26XX_DIR}/tools/stk500_flash.py ${CMAKE_BINARY_DIR}/mpm-v2.bin ${_port})
  endif()
  add_custom_target(flash
    COMMAND ${_python} ${_flash_cmd}
    DEPENDS firmware
    USES_TERMINAL
    VERBATIM)
endif()
