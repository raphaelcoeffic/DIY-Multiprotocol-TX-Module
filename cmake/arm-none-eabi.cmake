# GNU Arm Embedded toolchain (arm-none-eabi-gcc)
#
# The compiler of the builds is xPack GCC 15.2.1-1.1, downloaded once to
# toolchains/ (not in build/: deleting the builds keeps it). ARM_TOOLCHAIN_DIR (cache or environment variable: the
# toolchain's bin directory) selects another one, and with
# ARM_TOOLCHAIN_DOWNLOAD=OFF the one in the PATH is used.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

option(ARM_TOOLCHAIN_DOWNLOAD
  "Use the compiler of the builds (xPack GCC 15.2.1-1.1), downloaded" ON)
set(ARM_TOOLCHAIN_CACHE "${CMAKE_CURRENT_LIST_DIR}/../toolchains" CACHE PATH
  "Where the compiler is downloaded")

# The CMake checks of the compiler run this file again
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES ARM_TOOLCHAIN_DIR)

function(arm_toolchain_download)
  set(version 15.2.1-1.1)
  set(p ${CMAKE_HOST_SYSTEM_PROCESSOR})
  if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows" AND p MATCHES "^(AMD64|x86_64)$")
    set(host win32-x64.zip)
    set(sha256 bae6a3d1667697ce750c3b13d6d26d80973ecedc2cc87bf04869e83447fd93ea)
  elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin" AND p STREQUAL "arm64")
    set(host darwin-arm64.tar.gz)
    set(sha256 574082d35e49a2bcbdc355836b2a3ae5e5bb3b9456c9f5e37177db2ab4aad870)
  elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin" AND p STREQUAL "x86_64")
    set(host darwin-x64.tar.gz)
    set(sha256 5be906a5194c3173e145a58048e5607ffff947773237802b93e55e23c415f1b6)
  elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" AND p MATCHES "^(x86_64|amd64)$")
    set(host linux-x64.tar.gz)
    set(sha256 da6a49ad4003944b823c6c93702a8787c922ab34bd7e918ec0eaf6933a9b1ff6)
  elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" AND p MATCHES "^(aarch64|arm64)$")
    set(host linux-arm64.tar.gz)
    set(sha256 67980c7990eba7bb7ffdf39699102effd70889f5ac427be19a8c8a6c5fab2972)
  else()
    message(FATAL_ERROR "No xPack GCC ${version} for ${CMAKE_HOST_SYSTEM_NAME} ${p}: "
      "install a GNU Arm toolchain (arm-none-eabi-gcc) and set ARM_TOOLCHAIN_DIR")
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

if(NOT ARM_TOOLCHAIN_DIR AND ARM_TOOLCHAIN_DOWNLOAD)
  arm_toolchain_download()
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
