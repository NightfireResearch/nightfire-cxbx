#ifndef COMMON_LINUX_INTRIN_H_
#define COMMON_LINUX_INTRIN_H_

// MSVC's <intrin.h> for the game's sources on Linux (cmake/xbox-abi-linux.cmake puts this directory on their
// include path): the x86 intrinsics, and the string-instruction ones the code uses, as MinGW's header has them.

#include <stddef.h>
#include <x86intrin.h>

static inline void __movsb(unsigned char *destination, const unsigned char *source, size_t count) {
    __asm__ __volatile__("rep movsb" : "+D"(destination), "+S"(source), "+c"(count) : : "memory");
}

static inline void __movsd(unsigned long *destination, const unsigned long *source, size_t count) {
    __asm__ __volatile__("rep movsl" : "+D"(destination), "+S"(source), "+c"(count) : : "memory");
}

#endif // COMMON_LINUX_INTRIN_H_
