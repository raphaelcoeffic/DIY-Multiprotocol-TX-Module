# GNU Arm Embedded toolchain (arm-none-eabi-gcc)
#
# The compiler is the one in ARM_TOOLCHAIN_DIR (cache or environment
# variable: the toolchain's bin directory), else the one in the PATH. When
# there is none, or with ARM_TOOLCHAIN_DOWNLOAD, the compiler of the Arduino
# board package (xPack GCC 8.3.1-1.4, the release builds' compiler) is
# downloaded once to build/toolchains.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

option(ARM_TOOLCHAIN_DOWNLOAD
  "Use the compiler of the release builds (xPack GCC 8.3.1-1.4), downloaded" OFF)
set(ARM_TOOLCHAIN_CACHE "${CMAKE_CURRENT_LIST_DIR}/../build/toolchains" CACHE PATH
  "Where the compiler is downloaded")

# The CMake checks of the compiler run this file again
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES ARM_TOOLCHAIN_DIR)

function(arm_toolchain_download)
  set(version 8.3.1-1.4)
  if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    set(host win32-x64.zip)
    set(sha256 2f415819d96a50eb8f75974718e57762d1476a9f9722f4661e43b034fffe1a27)
  elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    # Intel binaries, also for Apple silicon (Rosetta)
    set(host darwin-x64.tar.gz)
    set(sha256 03ab76e2518748d12b491cb2ca8044633c56291db04933f0c3110637bfa22698)
  elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux"
         AND CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
    set(host linux-x64.tar.gz)
    set(sha256 31338c3b8bdb32d9082a857f6d65a57c033e7d6eba1662a30c7fb3e83ff46597)
  else()
    message(FATAL_ERROR "No xPack GCC ${version} for ${CMAKE_HOST_SYSTEM_NAME} "
      "${CMAKE_HOST_SYSTEM_PROCESSOR}: install a GNU Arm toolchain (arm-none-eabi-gcc)")
  endif()

  set(name xpack-arm-none-eabi-gcc-${version})
  get_filename_component(cache "${ARM_TOOLCHAIN_CACHE}" ABSOLUTE)
  set(bin "${cache}/${name}/bin")
  if(NOT EXISTS "${bin}/arm-none-eabi-gcc" AND NOT EXISTS "${bin}/arm-none-eabi-gcc.exe")
    set(archive "${cache}/${name}-${host}")
    set(url "https://github.com/xpack-dev-tools/arm-none-eabi-gcc-xpack/releases/download/v${version}/${name}-${host}")
    message(STATUS "Downloading ${url}")
    file(DOWNLOAD "${url}" "${archive}" EXPECTED_HASH SHA256=${sha256}
      SHOW_PROGRESS STATUS status)
    list(GET status 0 code)
    if(NOT code EQUAL 0)
      file(REMOVE "${archive}")
      message(FATAL_ERROR "Download of ${url} failed: ${status}")
    endif()
    file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${cache}")
    file(REMOVE "${archive}")
    if(NOT EXISTS "${bin}")
      message(FATAL_ERROR "No ${bin} in ${archive}")
    endif()
  endif()
  set(ARM_TOOLCHAIN_DIR "${bin}" CACHE PATH
    "bin directory of the arm-none-eabi toolchain (empty: in the PATH)" FORCE)
endfunction()

if(NOT ARM_TOOLCHAIN_DIR AND DEFINED ENV{ARM_TOOLCHAIN_DIR})
  set(ARM_TOOLCHAIN_DIR "$ENV{ARM_TOOLCHAIN_DIR}")
endif()
set(ARM_TOOLCHAIN_DIR "${ARM_TOOLCHAIN_DIR}" CACHE PATH
    "bin directory of the arm-none-eabi toolchain (empty: in the PATH)")

if(NOT ARM_TOOLCHAIN_DIR)
  find_program(_arm_gcc_in_path arm-none-eabi-gcc NO_CACHE)
  if(ARM_TOOLCHAIN_DOWNLOAD OR NOT _arm_gcc_in_path)
    arm_toolchain_download()
  endif()
endif()

if(ARM_TOOLCHAIN_DIR)
  find_program(_arm_gcc arm-none-eabi-gcc PATHS "${ARM_TOOLCHAIN_DIR}" NO_DEFAULT_PATH NO_CACHE)
else()
  find_program(_arm_gcc arm-none-eabi-gcc NO_CACHE)
endif()
if(NOT _arm_gcc)
  message(FATAL_ERROR "No arm-none-eabi-gcc in ARM_TOOLCHAIN_DIR '${ARM_TOOLCHAIN_DIR}'")
endif()
get_filename_component(_arm_bin "${_arm_gcc}" DIRECTORY)
get_filename_component(_exe "${_arm_gcc}" LAST_EXT)

set(CMAKE_C_COMPILER "${_arm_bin}/arm-none-eabi-gcc${_exe}")
set(CMAKE_CXX_COMPILER "${_arm_bin}/arm-none-eabi-g++${_exe}")
set(CMAKE_ASM_COMPILER "${_arm_bin}/arm-none-eabi-gcc${_exe}")
set(CMAKE_OBJCOPY "${_arm_bin}/arm-none-eabi-objcopy${_exe}" CACHE FILEPATH "" FORCE)
set(CMAKE_SIZE "${_arm_bin}/arm-none-eabi-size${_exe}" CACHE FILEPATH "" FORCE)

# No linking when CMake checks the compilers (no startup code yet)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
