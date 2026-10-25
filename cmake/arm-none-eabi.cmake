# GNU Arm Embedded toolchain (arm-none-eabi-gcc)
#
# The compiler is searched in ARM_TOOLCHAIN_DIR (cache or environment
# variable: the toolchain's bin directory), then in the PATH.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

if(NOT ARM_TOOLCHAIN_DIR AND DEFINED ENV{ARM_TOOLCHAIN_DIR})
  set(ARM_TOOLCHAIN_DIR "$ENV{ARM_TOOLCHAIN_DIR}")
endif()
set(ARM_TOOLCHAIN_DIR "${ARM_TOOLCHAIN_DIR}" CACHE PATH
    "bin directory of the arm-none-eabi toolchain (empty: in the PATH)")

find_program(ARM_GCC arm-none-eabi-gcc HINTS "${ARM_TOOLCHAIN_DIR}" REQUIRED)
get_filename_component(_arm_bin "${ARM_GCC}" DIRECTORY)

set(CMAKE_C_COMPILER "${_arm_bin}/arm-none-eabi-gcc")
set(CMAKE_CXX_COMPILER "${_arm_bin}/arm-none-eabi-g++")
set(CMAKE_ASM_COMPILER "${_arm_bin}/arm-none-eabi-gcc")
set(CMAKE_OBJCOPY "${_arm_bin}/arm-none-eabi-objcopy" CACHE FILEPATH "")
set(CMAKE_SIZE "${_arm_bin}/arm-none-eabi-size" CACHE FILEPATH "")

# No linking when CMake checks the compilers (no startup code yet)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
