#ifndef COMMON_FPUSTATE_H
#define COMMON_FPUSTATE_H

// The floating-point control state of the calling thread, for checking that nothing has changed it from the state
// the game's arithmetic assumes (docs/deterministic-maths.md): round to nearest, no flush to zero or denormals as
// zero, every exception masked, and on 32-bit x86 the x87 at 53-bit precision (0x027f, Windows' default, which
// HostPosix.cpp also sets). A library that changed it on the game's thread would change the game's results.

#include <stdint.h>
#include <stdio.h>
#if defined(_M_IX86) || defined(__i386__) || defined(_M_X64) || defined(__x86_64__)
#include <xmmintrin.h>
#endif

struct FpuState {
    uint32_t sse;    // MXCSR without its sticky exception flags (x86)
    uint32_t x87;    // the x87 control word (32-bit x86)
    uint64_t fpcr;   // FPCR (arm64)
};

inline FpuState FpuState_Read(void) {
    FpuState state = {};
#if defined(_M_IX86) || defined(__i386__) || defined(_M_X64) || defined(__x86_64__)
    state.sse = _mm_getcsr() & ~0x3fu;
#endif
#if defined(_M_IX86) && defined(_MSC_VER) && !defined(__clang__)
    uint16_t control;
    __asm fnstcw control
    state.x87 = control;
#elif defined(__i386__)
    uint16_t control;
    __asm__ __volatile__("fnstcw %0" : "=m"(control));
    state.x87 = control;
#endif
#if defined(__aarch64__) || defined(_M_ARM64)
#if defined(_MSC_VER) && !defined(__clang__)
    state.fpcr = _ReadStatusReg(ARM64_FPCR);
#else
    uint64_t fpcr;
    __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
    state.fpcr = fpcr;
#endif
#endif
    return state;
}

// The state the game assumes on this host
inline bool FpuState_IsExpected(const FpuState &state) {
#if defined(_M_IX86) || defined(__i386__)
    return state.sse == 0x1f80 && (state.x87 & 0x1f3f) == 0x023f;
#elif defined(_M_X64) || defined(__x86_64__)
    return state.sse == 0x1f80;
#elif defined(__aarch64__) || defined(_M_ARM64)
    return (state.fpcr & 0x07c09f00) == 0;   // RMode, FZ, DN, AHP, FZ16 and the trap enables clear
#else
    return true;
#endif
}

inline void FpuState_Print(const char *tag, const FpuState &state) {
    printf("%s floating-point control: MXCSR %04x, x87 %04x, FPCR %08llx\n", tag, state.sse, state.x87,
           (unsigned long long)state.fpcr);
    fflush(stdout);
}

#endif // COMMON_FPUSTATE_H
