// The entries through which the original's code calls the functions in eagl/anim/AnimDeltaQ.cpp: each takes the
// arguments from the registers the original takes them in and passes them on. Only the injected build has them.
#include "../eagl/anim/AnimDeltaQ.h"

// Not in a header
double __cdecl Vec2Length(const float *v);

// The vector in EAX, the length in ST0; no callers
// FUNC_AT(0x001042b0)
void __declspec(naked) Asm_Vec2Length(void) {
    __asm {
        push ecx
        push edx
        push eax
        call Vec2Length
        add esp, 4
        pop edx
        pop ecx
        ret
    }
}
