#ifndef DRIVING_DEVTOOLS_FPCONTROL_H_
#define DRIVING_DEVTOOLS_FPCONTROL_H_

// The x87 control word and the SSE control/status register, read and written raw, for shadow tests that save the
// floating-point state before running code that might fault and put it back afterwards. (MSVC's __control87_2 is
// not in MinGW's runtime, which the Linux cross build links against.)

#include <xmmintrin.h>

static inline void FpControlGet(unsigned int *x87, unsigned int *sse) {
    unsigned short cw;
    __asm { fnstcw cw }
    if (x87 != 0)
        *x87 = cw;
    if (sse != 0)
        *sse = _mm_getcsr();
}

static inline void FpControlSetX87(unsigned int x87) {
    unsigned short cw = (unsigned short)x87;
    __asm { fldcw cw }
}

static inline void FpControlSetSse(unsigned int sse) {
    _mm_setcsr(sse);
}

#endif // DRIVING_DEVTOOLS_FPCONTROL_H_
