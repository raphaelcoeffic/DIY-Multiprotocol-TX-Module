# MPM v2: the MULTI-Module on the TI CC26xx (CC2652, CC2650) and CC27xx
# (CC2755), the radio emulating the RF chips. src/, lib/ (but stm32f1),
# tools/ and tests/ come from the mpm-v2 repo (its tools/multi_sync.sh), but
# src/mpm/sketch: the protocol files are Multiprotocol/*.ino, built unchanged
# as one C++ file. The protocols and options are in src/mpm/multi/mpm_config.h.
#
# Included from the top CMakeLists.txt for the boards (MULTI_BOARD):
#   mpm-v2     MPM v2 module (CC2652RB): its USB-C, the radio or a J-Link
#   launchpad  CC2652R1 LaunchPad (LAUNCHXL-CC26X2R1), XDS110
#   xjt-lite   FrSky XJT-Lite (CC2650F128): reduced MPM, patched OpenOCD
#   cc2755-lp  CC2755P10 LaunchPad (LP-EM-CC2755P10), XDS110, OpenOCD with
#              the cc27xx driver

if(MULTI_BOARD STREQUAL "mpm-v2")
  set(BOARD MPM_V2)
elseif(MULTI_BOARD STREQUAL "launchpad")
  set(BOARD CC2652R1_LAUNCHXL)
elseif(MULTI_BOARD STREQUAL "xjt-lite")
  set(BOARD XJT_LITE)
elseif(MULTI_BOARD STREQUAL "cc2755-lp")
  set(BOARD CC2755P10_LP)
else()
  message(FATAL_ERROR "MULTI_BOARD=${MULTI_BOARD}: not an MPM v2 board")
endif()

# MULTI_DEBUG=rtt: debug output on Segger RTT
if(MULTI_DEBUG STREQUAL "rtt")
  set(DEBUG ON)
endif()
option(USE_CCFG "Include MCU user configuration (CCFG)" ON)
option(FRSKY_CC26XX_PLAN "FrSky X/X2 on FrSky's CC26xx frequency plan (OFF: the CC2500 plan, FRSKY_FSCTRL0_OFFSET below)" ON)

# Release: the radio's time-critical code at -O2 ($<CONFIG:Release> in src/),
# the rest -Os (toolchain_gcc; the build types' flags are empty)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

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
# GNU ld 2.39 and newer warn of the flash segment's RWX permissions
# (.init_array is writable); older ones don't know the option, nor the
# READONLY section keyword that would avoid it
execute_process(COMMAND ${CMAKE_LINKER} --help OUTPUT_VARIABLE _ld_help ERROR_QUIET)
if(_ld_help MATCHES "--no-warn-rwx-segments")
  target_link_options(toolchain_gcc INTERFACE -Wl,--no-warn-rwx-segments)
endif()

add_library(toolchain_gcc_m4f INTERFACE)
target_compile_options(toolchain_gcc_m4f INTERFACE -mcpu=cortex-m4 -mfloat-abi=hard -mfpu=fpv4-sp-d16)
target_link_options(toolchain_gcc_m4f INTERFACE -mcpu=cortex-m4 -mfloat-abi=hard -mfpu=fpv4-sp-d16)
target_link_libraries(toolchain_gcc_m4f INTERFACE toolchain_gcc)

# CC26x0 (XJT-Lite's CC2650): no FPU
add_library(toolchain_gcc_m3 INTERFACE)
target_compile_options(toolchain_gcc_m3 INTERFACE -mcpu=cortex-m3 -mfloat-abi=soft)
target_link_options(toolchain_gcc_m3 INTERFACE -mcpu=cortex-m3 -mfloat-abi=soft)
target_link_libraries(toolchain_gcc_m3 INTERFACE toolchain_gcc)

# CC27xx (CC2755P10): Cortex-M33, single precision FPU
add_library(toolchain_gcc_m33f INTERFACE)
target_compile_options(toolchain_gcc_m33f INTERFACE -mcpu=cortex-m33 -mfloat-abi=hard -mfpu=fpv5-sp-d16)
target_link_options(toolchain_gcc_m33f INTERFACE -mcpu=cortex-m33 -mfloat-abi=hard -mfpu=fpv5-sp-d16)
target_link_libraries(toolchain_gcc_m33f INTERFACE toolchain_gcc)

add_subdirectory(lib)
add_subdirectory(src)
