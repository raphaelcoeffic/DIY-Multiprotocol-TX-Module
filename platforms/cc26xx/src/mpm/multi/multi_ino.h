#pragma once

// The prelude of the protocol files (Multiprotocol/*.ino), built unchanged
// as one C++ file (cmake/ino2cpp.py, the .ino files in 'extern "C"' for the
// C core): what Multiprotocol.ino gives them on the STM32. The protocols and
// options are in mpm_config.h, the .ino files of the STM32 hardware are left
// out (src/CMakeLists.txt)

// The C library first: the C++ headers can't be in 'extern "C"'
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern "C" {
#include "chips.h"

// The XN297 emulation of the port, not XN297_EMU.ino's (iface_xn297.h)
#define _IFACE_XN297_H_
#include "xn297.h"
}
