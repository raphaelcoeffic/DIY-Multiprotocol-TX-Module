
set(firmware_sources
    board.c
    dma.c
    eeprom.c
    file_server.c
    flash_driver.c
    lfs_driver.c
    led_rgb.c
    main.c
    mpm/mpm.c
    mpm/mpm_frame.c
    mpm/mpm_port.c
    mpm/mpm_settings.c
    mpm/protocols.c
    mpm/proto_test.c
    mpm/multi/mpm_compat.c
    mpm/multi/xn297.c
    mpm/multi/nrf24l01.c
    mpm/multi/cc2500.c
    mpm/multi/a7105.c
    mpm/multi/cyrf6936.c
    mpm/multi/telemetry.c
    log.c
    nor_flash.c
    ppm.c
    frame.c
    radio.c
    serial.c
    settings.c
    spi.c
    syscalls.c
    timer.c
    uart.c
)

# Release builds are -Os; the code on the radio's time-critical paths gets
# -O2: RF core commands and interrupts, the frame encoders and decoders
# (CYRF6936 sync search, DSM telemetry decoded between callbacks), the
# scheduler's alarms, and the bench's measurements (bench.c). -O3 measured
# the same (timing.py within 1 us, DSM telemetry 45.5 frames/s) for 9 KB more
set_source_files_properties(radio.c frame.c timer.c bench.c
    PROPERTIES COMPILE_OPTIONS -O2
)

set(board_definitions BOARD_${BOARD})
if (BOARD STREQUAL "CC2652R1_LAUNCHXL")
    message("## Target is CC2652R1-LAUNCHXL: 48 MHz xtal")
    # XDS110 on the LaunchPad
    set(flash_tool openocd -f board/ti_cc26x2_launchpad.cfg)
elseif (BOARD STREQUAL "MPM_V2")
    message("## Target is MPM v2 (CC2652RB): HPOSC (BAW)")
    list(APPEND board_definitions MCU_CC2652RB)
    # J-Link on J10
    set(flash_tool ${CC26XX_DIR}/tools/flash_jlink.sh)
elseif (BOARD STREQUAL "XJT_LITE")
    message("## Target is FrSky XJT-Lite (CC2650F128): reduced MPM")
endif()

if (DEBUG)
    message("## Debug output enabled (Segger RTT)")
else()
    message("## Debug output disabled")
    target_compile_definitions(littlefs
        INTERFACE
        LFS_NO_DEBUG
        LFS_NO_WARN
        LFS_NO_ERROR
        LFS_NO_ASSERT
    )
endif()

# The protocol files, Multiprotocol/*.ino, as one C++ file
# (cmake/ino2cpp.py): the object library multi_ino. Left out: the STM32 and
# AVR hardware, what the port has its own (the RF chips: mpm/multi/), and
# the emulations not ported yet
set(multi_ino_exclude
    Multiprotocol.ino Arduino.ino Binary_Signature.ino Multi_Config.ino
    Multi_Protos.ino SPI.ino Telemetry.ino
    A7105_SPI.ino CC2500_SPI.ino CYRF6936_SPI.ino NRF24l01_SPI.ino SX1276_SPI.ino
    XN297_EMU.ino HS6200_EMU.ino NRF250K_EMU.ino RF2500_EMU.ino
)
function(add_multi_ino cpu_lib cpu_flags)
    set(cpp ${CMAKE_BINARY_DIR}/sketch/multi_ino.cpp)
    set(includes ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/mpm/multi
        ${MULTI_SKETCH_DIR})
    set(defs ${board_definitions})
    if (DEBUG)
        list(APPEND defs DEBUG)
        list(APPEND includes ${CC26XX_DIR}/lib/SeggerRTT/RTT ${CC26XX_DIR}/lib/SeggerRTT/Config)
    endif()
    list(TRANSFORM includes PREPEND -I OUTPUT_VARIABLE i)
    list(TRANSFORM defs PREPEND -D OUTPUT_VARIABLE d)
    file(GLOB sketch_files CONFIGURE_DEPENDS
        ${MULTI_SKETCH_DIR}/*.ino ${MULTI_SKETCH_DIR}/*.h mpm/multi/*.h)
    add_custom_command(
        OUTPUT ${cpp}
        COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/cmake/ino2cpp.py
            --sketch ${MULTI_SKETCH_DIR} --exclude ${multi_ino_exclude}
            --prelude multi_ino.h --extern-c
            --out ${cpp} --preproc ${CMAKE_BINARY_DIR}/sketch/preproc.cpp
            -- ${CMAKE_CXX_COMPILER} -std=gnu++11 ${cpu_flags} ${d} ${i}
        DEPENDS ${sketch_files} ${CMAKE_SOURCE_DIR}/cmake/ino2cpp.py
        COMMENT "Generating multi_ino.cpp from Multiprotocol/*.ino"
        VERBATIM
    )
    add_library(multi_ino OBJECT ${cpp})
    target_include_directories(multi_ino PRIVATE ${includes})
    target_compile_definitions(multi_ino PRIVATE ${defs})
    target_link_libraries(multi_ino PRIVATE ${cpu_lib})
endfunction()

# XJT-Lite: CC26x0 driverlib, its own applications (xjt_lite.cmake)
if (BOARD STREQUAL "XJT_LITE")
    include(xjt_lite.cmake)
    return()
endif()

# Boot loader in sector 0 with CCFG, applications at 0x2000 (docs/update.md)
add_executable(bootloader bootloader/main.c board.c)
target_compile_definitions(bootloader PRIVATE ${board_definitions})
target_compile_options(bootloader PRIVATE -Os)
target_include_directories(bootloader PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bootloader driverlib_cc13x2_cc26x2)
target_link_options(bootloader PRIVATE -L${CMAKE_CURRENT_SOURCE_DIR}/linker)
set_target_properties(bootloader PROPERTIES
    LINKER_FILE ${CMAKE_CURRENT_SOURCE_DIR}/linker/bootloader.ld
    # Relinked when a script changes
    LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/linker/bootloader.ld;${CMAKE_CURRENT_SOURCE_DIR}/linker/sections.ld"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)
if (USE_CCFG)
    message("## Include CCFG")
    target_sources(bootloader PRIVATE ccfg.c)
endif()
add_custom_command(
    TARGET bootloader POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary --gap-fill 0xff bootloader.elf bootloader.bin
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

add_multi_ino(toolchain_gcc_m4f "-mcpu=cortex-m4;-mthumb;-mfloat-abi=hard;-mfpu=fpv4-sp-d16")

# An application behind the boot loader. Target <name>: in the build
# directory, <name>.elf, <name>.bin with the image header from app.ld (no
# CRC, already checked), <file>-full.bin / .hex the whole flash for a first
# install (boot loader, CCFG, application), made again when the boot loader
# changes, and with Python <file>.bin for the radio (CRC, MULTI signature).
# flash-<name>: boot loader and application through the board's debug probe.
function(add_application name file)
    set(elf ${name}_elf)
    add_executable(${elf} ${firmware_sources} $<TARGET_OBJECTS:multi_ino> ${ARGN})
    target_include_directories(${elf} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/mpm/multi ${MULTI_SKETCH_DIR})
    target_compile_definitions(${elf} PRIVATE ${board_definitions}
        MPM_PORT_VERSION_MAJOR=${PROJECT_VERSION_MAJOR} MPM_PORT_VERSION_MINOR=${PROJECT_VERSION_MINOR})
    if (DEBUG)
        target_compile_definitions(${elf} PRIVATE DEBUG)
    endif()
    target_link_libraries(${elf}
        driverlib_cc13x2_cc26x2
        segger_rtt
        littlefs
    )
    target_link_options(${elf} PRIVATE -L${CMAKE_CURRENT_SOURCE_DIR}/linker)
    set_target_properties(${elf} PROPERTIES
        OUTPUT_NAME ${name}
        LINKER_FILE ${CMAKE_CURRENT_SOURCE_DIR}/linker/app.ld
        # Relinked when a script changes
        LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/linker/app.ld;${CMAKE_CURRENT_SOURCE_DIR}/linker/sections.ld"
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    )

    # The whole flash: the application added to the boot loader at 0x2000
    set(app_section
        --add-section .app=${name}.bin
        --set-section-flags .app=alloc,load,readonly,contents
        --change-section-address .app=0x2000)
    set(images ${name}.bin ${file}-full.bin ${file}-full.hex)
    list(TRANSFORM images PREPEND ${CMAKE_BINARY_DIR}/)
    add_custom_command(
        OUTPUT ${images}
        COMMAND ${CMAKE_OBJCOPY} -O binary ${name}.elf ${name}.bin
        COMMAND ${CMAKE_OBJCOPY} -O binary --gap-fill 0xff ${app_section}
                bootloader.elf ${file}-full.bin
        COMMAND ${CMAKE_OBJCOPY} -O ihex ${app_section}
                bootloader.elf ${file}-full.hex
        DEPENDS ${elf} bootloader
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )
    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/${file}.bin
        COMMAND Python3::Interpreter ${CC26XX_DIR}/tools/fw_image.py ${name}.bin
                --version ${PROJECT_VERSION} --out ${file}.bin
        DEPENDS ${CMAKE_BINARY_DIR}/${name}.bin ${CC26XX_DIR}/tools/fw_image.py
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )
    list(APPEND images ${CMAKE_BINARY_DIR}/${file}.bin)
    add_custom_target(${name} ALL DEPENDS ${images})

    if (BOARD STREQUAL "CC2652R1_LAUNCHXL")
        set(flash_args
            -c "program ${CMAKE_BINARY_DIR}/bootloader.elf verify"
            -c "program ${CMAKE_BINARY_DIR}/${name}.bin 0x2000 verify reset exit")
    else()
        set(flash_args
            ${CMAKE_BINARY_DIR}/bootloader.elf ${CMAKE_BINARY_DIR}/${name}.bin@0x2000)
    endif()
    add_custom_target(flash-${name}
        COMMAND ${flash_tool} ${flash_args}
        DEPENDS ${name}
        USES_TERMINAL
        VERBATIM
    )
endfunction()

# The MPM port (the module EdgeTX drives)
add_application(firmware mpm-v2)

# Radio bench: test shell on UART0 instead of the MPM (docs/bench.md)
add_application(bench mpm-v2-bench bench.c sx1281.c)
target_compile_definitions(bench_elf PRIVATE BENCH)
