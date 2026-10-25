cmake_minimum_required(VERSION 3.21)

# MULTI-Module firmware, without the Arduino IDE
#
#   cmake --preset stm32f103cb
#   cmake --build --preset stm32f103cb
#
# The protocols and options are set in Multiprotocol/_Config.h (or
# _MyConfig.h), as for the Arduino IDE. The firmware is
# build/<preset>/multi-stm-<version>.bin.

set(MULTI_BOARD "stm32f103cb" CACHE STRING "Board: stm32f103cb, stm32f103c8, t18int")
set_property(CACHE MULTI_BOARD PROPERTY STRINGS stm32f103cb stm32f103c8 t18int)

if(NOT CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/arm-none-eabi.cmake")
endif()

project(Multiprotocol LANGUAGES C CXX ASM)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

# The flags come from the board, whatever the build type
foreach(lang C CXX ASM)
  foreach(cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set(CMAKE_${lang}_FLAGS_${cfg} "")
  endforeach()
endforeach()

include(cmake/stm32f1.cmake)

# The sketch as one C++ file, as the Arduino builder makes it
set(MULTI_SKETCH_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Multiprotocol" CACHE PATH "The sketch (Multiprotocol.ino)")
set(_sketch_cpp "${CMAKE_CURRENT_BINARY_DIR}/sketch/Multiprotocol.ino.cpp")
set(_preproc "${CMAKE_CURRENT_BINARY_DIR}/sketch/preproc.cpp")
file(GLOB _sketch_files CONFIGURE_DEPENDS
  "${MULTI_SKETCH_DIR}/*.ino" "${MULTI_SKETCH_DIR}/*.h")
add_custom_command(
  OUTPUT ${_sketch_cpp} ${_preproc}
  COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/cmake/ino2cpp.py
    --sketch ${MULTI_SKETCH_DIR} --main Multiprotocol.ino
    --out ${_sketch_cpp} --preproc ${_preproc}
    -- ${MULTI_PREPROCESS} -iquote ${MULTI_SKETCH_DIR}
  DEPENDS ${_sketch_files} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/ino2cpp.py
  COMMENT "Generating the sketch's Multiprotocol.ino.cpp"
  VERBATIM)

maple_firmware(Multiprotocol ${_sketch_cpp} ${MULTI_SKETCH_DIR})

add_custom_command(TARGET Multiprotocol POST_BUILD
  COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:Multiprotocol>
    $<TARGET_FILE_DIR:Multiprotocol>/Multiprotocol.bin
  COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/cmake/multi_firmware.py
    --elf $<TARGET_FILE:Multiprotocol> --size-tool ${CMAKE_SIZE}
    --max-size ${MULTI_MAX_SIZE} --max-data ${MULTI_MAX_DATA}
    --bin $<TARGET_FILE_DIR:Multiprotocol>/Multiprotocol.bin
    --preproc ${_preproc}
    --header ${MULTI_SKETCH_DIR}/Multiprotocol.h
    --board ${MULTI_BOARD_NAME}
  VERBATIM)

# cmake --build --preset <preset> --target flash
set(MULTI_FLASH_METHOD "" CACHE STRING
  "Upload method: auto, usb, serial (with the boot loader), serial-noboot (empty: the board's)")
set_property(CACHE MULTI_FLASH_METHOD PROPERTY STRINGS "" auto usb serial serial-noboot)
set(MULTI_FLASH_PORT "" CACHE STRING
  "Serial port of the module (empty: the only USB serial port; or MULTI_FLASH_PORT in the environment)")
find_program(STM32FLASH stm32flash)
find_program(DFU_UTIL dfu-util)
if(MULTI_FLASH_METHOD)
  set(_flash_method ${MULTI_FLASH_METHOD})
else()
  set(_flash_method ${MULTI_FLASH_DEFAULT})
endif()
add_custom_target(flash
  COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/cmake/multi_flash.py
    --bin $<TARGET_FILE_DIR:Multiprotocol>/Multiprotocol.bin
    --method ${_flash_method}
    --bootloader ${MULTI_BOOTLOADER} --erase-len ${MULTI_ERASE_LEN}
    --port "${MULTI_FLASH_PORT}"
    --stm32flash "$<$<BOOL:${STM32FLASH}>:${STM32FLASH}>"
    --dfu-util "$<$<BOOL:${DFU_UTIL}>:${DFU_UTIL}>"
  DEPENDS Multiprotocol
  USES_TERMINAL
  VERBATIM)
