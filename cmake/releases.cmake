# The release files of the STM32 boards, as the buildroot/bin/build_release_*
# scripts make them, each a firmware with options over _Config.h
# (MULTI_OPTIONS syntax):
#
#   cmake --build --preset stm32f103cb --target releases
#   cmake --build --preset stm32f103cb --target release-stm32f103-128k-4in1
#
# build/<preset>/releases/<group>/<name>-v<version>.bin; the groups are the
# main workflow's releases.

set(_releases)

# A release file of <group>
function(multi_release group name)
  set(dir "${CMAKE_BINARY_DIR}/release/${name}")
  multi_sketch(${name} "${dir}"
    "${CMAKE_BINARY_DIR}/releases/${group}/${name}-v{version}.bin" ${ARGN})
  set_target_properties(${name} ${name}_sketch PROPERTIES EXCLUDE_FROM_ALL ON)
  if(NOT TARGET release-${group})
    add_custom_target(release-${group})
    set(_releases ${_releases} release-${group} PARENT_SCOPE)
  endif()
  add_dependencies(release-${group} ${name})
endfunction()

# The same in the three channel orders: {o} in the name is aetr (_Config.h's),
# taer or reta
macro(multi_release_orders group name)
  string(REPLACE "{o}" aetr _name "${name}")
  multi_release(${group} ${_name} ${ARGN})
  string(REPLACE "{o}" taer _name "${name}")
  multi_release(${group} ${_name} ${ARGN} -AETR TAER)
  string(REPLACE "{o}" reta _name "${name}")
  multi_release(${group} ${_name} ${ARGN} -AETR RETA)
endmacro()

set(_cc2500_only -ENABLE_PPM -A7105_INSTALLED -CYRF6936_INSTALLED -NRF24L01_INSTALLED
  -INVERT_TELEMETRY)
set(_no_protocols -*_A7105_INO -*_CC2500_INO -*_CYRF6936_INO -*_NRF24L01_INO -*_CCNRF_INO
  -*_SX1276_INO)

if(MULTI_BOARD STREQUAL "stm32f103c8" AND MULTI_DEBUG STREQUAL "none")
  multi_release_orders(stm32f103-cc2500-64k mm-stm-cc2500-64-{o}
    -SCANNER_CC2500_INO ${_cc2500_only})
  multi_release_orders(stm32f103-cc2500-64k mm-stm-cc2500-64-{o}-lbt
    -SCANNER_CC2500_INO ${_cc2500_only} MULTI_EU)

elseif(MULTI_BOARD STREQUAL "stm32f103cb" AND MULTI_DEBUG STREQUAL "none")
  multi_release_orders(stm32f103-cc2500-128k mm-stm-cc2500-{o} ${_cc2500_only})
  multi_release_orders(stm32f103-cc2500-128k mm-stm-cc2500-{o}-lbt ${_cc2500_only} MULTI_EU)

  multi_release_orders(stm32f103-128k-4in1 mm-stm-serial-{o}-air -ENABLE_PPM MULTI_AIR)
  multi_release_orders(stm32f103-128k-4in1 mm-stm-serial-{o}-sfc -ENABLE_PPM MULTI_SURFACE)
  multi_release_orders(stm32f103-128k-4in1 mm-stm-serial-{o}-lbt -ENABLE_PPM MULTI_EU)
  multi_release_orders(stm32f103-128k-4in1 mm-stm-ppm-{o}
    -ENABLE_SERIAL -MULTI_STATUS -MULTI_TELEMETRY NBR_BANKS=5 MULTI_AIR)

  set(_5in1 -ENABLE_PPM SX1276_INSTALLED)
  multi_release_orders(stm32f103-128k-5in1 mm-stm-5in1-{o}-air
    ${_5in1} -CFLIE_NRF24L01_INO MULTI_AIR)
  multi_release_orders(stm32f103-128k-5in1 mm-stm-5in1-{o}-sfc ${_5in1} MULTI_SURFACE)
  multi_release_orders(stm32f103-128k-5in1 mm-stm-5in1-{o}-lbt ${_5in1} MULTI_EU)

  set(_tlite -ENABLE_PPM -INVERT_TELEMETRY MULTI_5IN1_INTERNAL=JP_TLite)
  multi_release_orders(tlite-5in1 mm-tlite5in1-{o}-air ${_tlite} MULTI_AIR)
  multi_release_orders(tlite-5in1 mm-tlite5in1-{o}-sfc ${_tlite} MULTI_SURFACE)
  multi_release_orders(tlite-5in1 mm-tlite5in1-{o}-lbt ${_tlite} MULTI_EU)

elseif(MULTI_BOARD STREQUAL "stm32f103cb" AND MULTI_DEBUG STREQUAL "native")
  multi_release(stm32f103-128k-usb-debug mm-stm-xn297dump-usbdebug
    ${_no_protocols} XN297DUMP_NRF24L01_INO)

elseif(MULTI_BOARD STREQUAL "stm32f103cb" AND MULTI_DEBUG STREQUAL "ftdi")
  multi_release(stm32f103-128k-serial-debug mm-stm-xn297dump-ftdidebug
    ${_no_protocols} XN297DUMP_NRF24L01_INO)

elseif(MULTI_BOARD STREQUAL "t18int")
  set(_t18 -ENABLE_PPM -INVERT_TELEMETRY)
  multi_release_orders(t18-5in1 mm-t18int-{o}-air ${_t18} MULTI_AIR)
  multi_release_orders(t18-5in1 mm-t18int-{o}-sfc ${_t18} MULTI_SURFACE)
  multi_release_orders(t18-5in1 mm-t18int-{o}-lbt ${_t18} MULTI_EU)
endif()

add_custom_target(releases)
if(_releases)
  add_dependencies(releases ${_releases})
endif()
