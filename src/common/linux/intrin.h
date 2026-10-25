#ifndef COMMON_LINUX_INTRIN_H_
#define COMMON_LINUX_INTRIN_H_

// MSVC's <intrin.h> for the game's sources on Linux (cmake/xbox-abi-linux.cmake puts this directory on their
// include path): the x86 intrinsics. The string-instruction ones the driving engine uses are
// src/driving/platform/Sse.h's.

#include <x86intrin.h>

#endif // COMMON_LINUX_INTRIN_H_
