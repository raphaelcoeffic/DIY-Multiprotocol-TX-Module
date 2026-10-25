# FrSky XJT-Lite (CC2650F128): applications at 0x8000 behind FrSky's boot
# loader, kept as it is (it starts the image at 0x8000, no checks). The last
# sector (CCFG) is never written. Flashed with OpenOCD over cJTAG on a
# J-Link (a patched OpenOCD: github.com/raphaelcoeffic/openocd, jlink-cjtag)

set(XJT_OPENOCD openocd CACHE STRING "OpenOCD with J-Link cJTAG support")

list(APPEND board_definitions MCU_CC26X0)
add_multi_ino(toolchain_gcc_m3 "-mcpu=cortex-m3;-mthumb;-mfloat-abi=soft")

# Target <name>: <name>.elf and <name>.bin in the build directory.
# flash-<name>: the application only, at 0x8000
function(add_xjt_application name)
    set(elf ${name}_elf)
    add_executable(${elf} ${ARGN})
    target_include_directories(${elf} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(${elf} PRIVATE ${board_definitions})
    target_link_libraries(${elf} driverlib_cc26x0)
    target_link_options(${elf} PRIVATE -L${CMAKE_CURRENT_SOURCE_DIR}/linker)
    set_target_properties(${elf} PROPERTIES
        OUTPUT_NAME ${name}
        LINKER_FILE ${CMAKE_CURRENT_SOURCE_DIR}/linker/xjt_lite.ld
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    )

    set(bin ${CMAKE_BINARY_DIR}/${name}.bin)
    add_custom_command(
        OUTPUT ${bin}
        COMMAND ${CMAKE_OBJCOPY} -O binary ${name}.elf ${name}.bin
        DEPENDS ${elf}
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )
    add_custom_target(${name} ALL DEPENDS ${bin})

    add_custom_target(flash-${name}
        COMMAND ${XJT_OPENOCD} -f board/frsky_xjt_lite.cfg
                -c "program ${bin} 0x8000 verify reset exit"
        DEPENDS ${name}
        USES_TERMINAL
        VERBATIM
    )
endfunction()

# Bring-up: bay serial in, echoed on S.Port
add_xjt_application(xjtl-test xjtl_test.c board.c timer.c)

# Radio bring-up: a shell on the bay link driving the RF core
add_xjt_application(xjtl-radio xjtl_radio.c board.c timer.c radio.c syscalls.c)

# EEPROM pin probe
add_xjt_application(xjtl-eeprom xjtl_eeprom.c mpm/eeprom_xjt.c board.c log.c timer.c syscalls.c)

# The MPM port: FrSky D and X, V2x2, XK (mpm/multi/mpm_config.h)
add_xjt_application(firmware
    main_xjt.c
    board.c
    frame.c
    log.c
    radio.c
    syscalls.c
    timer.c
    uart.c
    mpm/eeprom_xjt.c
    mpm/mpm.c
    mpm/mpm_frame.c
    mpm/mpm_port_xjt.c
    mpm/mpm_settings.c
    mpm/protocols.c
    mpm/proto_test.c
    mpm/multi/mpm_compat.c
    mpm/multi/xn297.c
    mpm/multi/nrf24l01.c
    mpm/multi/cc2500.c
    mpm/multi/telemetry.c
    $<TARGET_OBJECTS:multi_ino>
)
target_include_directories(firmware_elf PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/mpm/multi ${MULTI_SKETCH_DIR})
target_compile_definitions(firmware_elf PRIVATE
    MPM_PORT_VERSION_MAJOR=${PROJECT_VERSION_MAJOR} MPM_PORT_VERSION_MINOR=${PROJECT_VERSION_MINOR})
set_source_files_properties(radio.c frame.c timer.c
    PROPERTIES COMPILE_OPTIONS -O2
)
