# The protocol files of the MULTI-Module: Multiprotocol/*.ino, built
# unchanged as one C++ file per target (cmake/ino2cpp.py: multi_ino.h first,
# the .ino files in 'extern "C"' for the C core), with the target's
# definitions and include directories. Left out: the STM32 and AVR hardware,
# the RF chips (mpm/multi emulates them, chips.h here) and the emulations not
# ported yet. The protocols and options: mpm/multi/mpm_config.h. mpm-v2
# builds C copies of the protocol files instead (its mpm/upstream)

set(multi_ino_exclude
    Multiprotocol.ino Arduino.ino Binary_Signature.ino Multi_Config.ino
    Multi_Protos.ino SPI.ino Telemetry.ino
    A7105_SPI.ino CC2500_SPI.ino CYRF6936_SPI.ino NRF24l01_SPI.ino SX1276_SPI.ino
    XN297_EMU.ino HS6200_EMU.ino NRF250K_EMU.ino RF2500_EMU.ino
)
file(GLOB multi_ino_files CONFIGURE_DEPENDS
    ${MULTI_SKETCH_DIR}/*.ino ${MULTI_SKETCH_DIR}/*.h
    ${CMAKE_CURRENT_LIST_DIR}/*.h ${mpm_dir}/multi/*.h)

function(target_mpm_protocols target)
    set(dir ${CMAKE_BINARY_DIR}/sketch)
    set(cpp ${dir}/${target}.cpp)
    set(defs "$<TARGET_PROPERTY:${target},COMPILE_DEFINITIONS>")
    set(incs "$<TARGET_PROPERTY:${target},INCLUDE_DIRECTORIES>")
    add_custom_command(
        OUTPUT ${cpp}
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/cmake/ino2cpp.py
            --sketch ${MULTI_SKETCH_DIR} --exclude ${multi_ino_exclude}
            --prelude multi_ino.h --extern-c
            --out ${cpp} --preproc ${dir}/${target}-preproc.cpp
            -- ${CMAKE_CXX_COMPILER} -std=gnu++11 "-D$<JOIN:${defs},;-D>" "-I$<JOIN:${incs},;-I>"
        DEPENDS ${multi_ino_files} ${CMAKE_SOURCE_DIR}/cmake/ino2cpp.py
        COMMENT "Generating ${target}.cpp from Multiprotocol/*.ino"
        COMMAND_EXPAND_LISTS
        VERBATIM
    )
    target_sources(${target} PRIVATE ${cpp})
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_FUNCTION_LIST_DIR} ${MULTI_SKETCH_DIR})
endfunction()
