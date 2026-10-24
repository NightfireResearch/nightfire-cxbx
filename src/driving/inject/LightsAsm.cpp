// The entries through which the original's code calls the functions in render/Lights.cpp: each takes the arguments from
// the registers the original takes them in and passes them on. Only the injected build has them.
#include "../render/Lights.h"

// FUNC_AT(0x0007e520)
void __declspec(naked) Asm_LightEffectBrightness(void) {
    __asm {
        push esi
        call LightEffectBrightness
        add esp, 4
        ret
    }
}

// FUNC_AT(0x0007eed0)
void __declspec(naked) Asm_SetLightDirections(void) {
    __asm {
        push ebx
        push eax
        call SetLightDirections
        add esp, 8
        ret
    }
}
