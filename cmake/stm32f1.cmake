# MULTI-Module STM32F103 boards, on the Maple core (lib/stm32f1)
#
# The boards, options and flags of the Arduino board package
# (DIY-Multiprotocol-TX-Module-Boards, source/stm32: boards.txt, platform.txt),
# so the firmware is the same as built by the Arduino IDE.

set(MAPLE_DIR "${CMAKE_SOURCE_DIR}/lib/stm32f1")
set(MAPLE_CORE "${MAPLE_DIR}/cores/maple")
set(MAPLE_VARIANT "${MAPLE_DIR}/variants/generic_stm32f103c")

set(MULTI_DEBUG "none" CACHE STRING "Debug option: none, native (USB) or ftdi (serial)")
set_property(CACHE MULTI_DEBUG PROPERTY STRINGS none native ftdi)
option(MULTI_USB_SUPPORT "USB support (SERIAL_USB)" OFF)

# boards.txt
set(_board MULTI_STM32_WITH_BOOT=131)
set(_ldscript ld/bootloader_20.ld)
set(_upload_flags GENERIC_BOOTLOADER)
set(_vect 0x8002000)
set(_serial_usb)
if(MULTI_USB_SUPPORT)
  set(_serial_usb SERIAL_USB)
endif()
set(MULTI_MAX_DATA 20480)

if(MULTI_BOARD STREQUAL "stm32f103cb")
  set(_cpu_flags MCU_STM32F103CB)
  set(MULTI_MAX_SIZE 120808)
  set(_max_size_ftdi 129000)
elseif(MULTI_BOARD STREQUAL "stm32f103c8")
  set(_cpu_flags MCU_STM32F103C8)
  set(MULTI_MAX_SIZE 55272)
  set(_max_size_ftdi 63464)
elseif(MULTI_BOARD STREQUAL "t18int")
  # T18 internal 5-in-1: no debug option
  set(_cpu_flags MCU_STM32F103CB)
  set(_upload_flags MULTI_5IN1_INTERNAL GENERIC_BOOTLOADER)
  set(MULTI_MAX_SIZE 120808)
  if(NOT MULTI_DEBUG STREQUAL "none" OR MULTI_USB_SUPPORT)
    message(FATAL_ERROR "MULTI_BOARD t18int has no debug option and no USB support")
  endif()
else()
  message(FATAL_ERROR "Unknown MULTI_BOARD '${MULTI_BOARD}' (stm32f103cb, stm32f103c8, t18int)")
endif()

if(MULTI_DEBUG STREQUAL "native")
  set(_serial_usb SERIAL_USB)
  set(_upload_flags GENERIC_BOOTLOADER ARDUINO_MULTI_DEBUG)
elseif(MULTI_DEBUG STREQUAL "ftdi")
  # No boot loader: flashed with an FTDI adapter
  set(_board MULTI_STM32_NO_BOOT=131)
  set(_ldscript ld/jtag.ld)
  set(_serial_usb)
  set(_upload_flags CONFIG_MAPLE_MINI_NO_DISABLE_DEBUG ARDUINO_MULTI_DEBUG)
  set(_vect 0x8000000)
  set(MULTI_MAX_SIZE ${_max_size_ftdi})
elseif(NOT MULTI_DEBUG STREQUAL "none")
  message(FATAL_ERROR "Unknown MULTI_DEBUG '${MULTI_DEBUG}' (none, native, ftdi)")
endif()
set(MULTI_BOARD_NAME ${_board})

# platform.txt: the recipes' flags
set(_cpu -mcpu=cortex-m3 -mthumb -march=armv7-m)
set(_defines
  F_CPU=72000000L
  ARDUINO=10607
  ARDUINO_${_board}
  ARDUINO_ARCH_STM32F1
  __STM32F1__
  ${_serial_usb}
  ${_upload_flags}
  ${_cpu_flags})
# Not for the assembler
set(_c_defines
  DEBUG_LEVEL=DEBUG_NONE
  BOARD_generic_stm32f103c
  VECT_TAB_ADDR=${_vect}
  ERROR_LED_PORT=GPIOB
  ERROR_LED_PIN=1)
set(_c_flags -g -Os -w -ffunction-sections -fdata-sections -nostdlib
  --param=max-inline-insns-single=500 ${_cpu})

set(MULTI_C_FLAGS ${_c_flags} -std=gnu11)
set(MULTI_CXX_FLAGS ${_c_flags} -std=gnu++11 -fno-rtti -fno-exceptions)
set(MULTI_ASM_FLAGS -g -x assembler-with-cpp ${_cpu})
set(MULTI_DEFINES ${_defines} ${_c_defines})

set(MAPLE_INCLUDES
  ${MAPLE_DIR}/system/libmaple
  ${MAPLE_DIR}/system/libmaple/include
  ${MAPLE_DIR}/system/libmaple/stm32f1/include
  ${MAPLE_DIR}/system/libmaple/usb/stm32f1
  ${MAPLE_DIR}/system/libmaple/usb/usb_lib
  ${MAPLE_CORE}
  ${MAPLE_VARIANT})
# The libraries the sketch uses (SPI, EEPROM)
set(MAPLE_LIB_INCLUDES
  ${MAPLE_DIR}/libraries/SPI/src
  ${MAPLE_DIR}/libraries/EEPROM)

function(maple_flags target)
  target_compile_options(${target} PRIVATE
    "$<$<COMPILE_LANGUAGE:C>:${MULTI_C_FLAGS}>"
    "$<$<COMPILE_LANGUAGE:CXX>:${MULTI_CXX_FLAGS}>"
    "$<$<COMPILE_LANGUAGE:ASM>:${MULTI_ASM_FLAGS}>")
  target_compile_definitions(${target} PRIVATE
    ${_defines}
    "$<$<COMPILE_LANGUAGE:C,CXX>:${_c_defines}>")
  target_include_directories(${target} PRIVATE ${MAPLE_INCLUDES})
endfunction()

# The core, as an archive (core.a): its files in name order, as the Arduino
# builder adds them, so the link places them the same way
file(GLOB_RECURSE _core_sources CONFIGURE_DEPENDS
  ${MAPLE_CORE}/*.c ${MAPLE_CORE}/*.cpp ${MAPLE_CORE}/*.S)
list(SORT _core_sources)
add_library(maple_core STATIC ${_core_sources})
maple_flags(maple_core)
set_target_properties(maple_core PROPERTIES OUTPUT_NAME core)

# The variant and the libraries are linked as object files
file(GLOB_RECURSE _variant_sources CONFIGURE_DEPENDS
  ${MAPLE_VARIANT}/*.c ${MAPLE_VARIANT}/*.cpp ${MAPLE_VARIANT}/*.S)
list(SORT _variant_sources)
add_library(maple_variant OBJECT ${_variant_sources})
maple_flags(maple_variant)

add_library(maple_libraries OBJECT
  ${MAPLE_DIR}/libraries/SPI/src/SPI.cpp
  ${MAPLE_DIR}/libraries/EEPROM/EEPROM.cpp
  ${MAPLE_DIR}/libraries/EEPROM/flash_stm32.c)
maple_flags(maple_libraries)
target_include_directories(maple_libraries PRIVATE ${MAPLE_LIB_INCLUDES})

# The firmware: the sketch, the libraries, the variant, then the core (the
# sketch's own headers are in sketch_dir)
function(maple_firmware target sketch_cpp sketch_dir)
  add_library(${target}_sketch OBJECT ${sketch_cpp})
  maple_flags(${target}_sketch)
  target_include_directories(${target}_sketch PRIVATE ${MAPLE_LIB_INCLUDES})
  target_compile_options(${target}_sketch PRIVATE -iquote ${sketch_dir})
  add_executable(${target}
    $<TARGET_OBJECTS:${target}_sketch>
    $<TARGET_OBJECTS:maple_libraries>
    $<TARGET_OBJECTS:maple_variant>)
  set_target_properties(${target} PROPERTIES LINKER_LANGUAGE CXX)
  target_link_options(${target} PRIVATE
    -Os -Wl,--gc-sections ${_cpu}
    -T${MAPLE_VARIANT}/${_ldscript}
    -Wl,-Map,$<TARGET_FILE_DIR:${target}>/${target}.map
    -L${MAPLE_VARIANT}/ld
    -lm -lgcc
    -Wl,--cref -Wl,--check-sections -Wl,--unresolved-symbols=report-all
    -Wl,--warn-common -Wl,--warn-section-align -Wl,--warn-unresolved-symbols)
  if(_ld_rwx_warning)
    # binutils 2.39+: the linker scripts load .data from a RWX segment
    target_link_options(${target} PRIVATE -Wl,--no-warn-rwx-segments)
  endif()
  target_link_libraries(${target} PRIVATE -Wl,--start-group maple_core -Wl,--end-group)
  set_target_properties(${target} PROPERTIES SUFFIX .elf
    LINK_DEPENDS "${MAPLE_VARIANT}/${_ldscript}")
endfunction()

execute_process(COMMAND ${CMAKE_CXX_COMPILER} -Wl,--help
  OUTPUT_VARIABLE _ld_help ERROR_QUIET)
string(FIND "${_ld_help}" "--no-warn-rwx-segments" _ld_rwx_warning)
if(_ld_rwx_warning EQUAL -1)
  set(_ld_rwx_warning OFF)
endif()

# The command line to preprocess the sketch with (ino2cpp.py)
list(TRANSFORM MULTI_DEFINES PREPEND -D OUTPUT_VARIABLE _d)
list(TRANSFORM MAPLE_INCLUDES PREPEND -I OUTPUT_VARIABLE _i)
list(TRANSFORM MAPLE_LIB_INCLUDES PREPEND -I OUTPUT_VARIABLE _li)
set(MULTI_PREPROCESS ${CMAKE_CXX_COMPILER} ${MULTI_CXX_FLAGS} ${_d} ${_i} ${_li})
