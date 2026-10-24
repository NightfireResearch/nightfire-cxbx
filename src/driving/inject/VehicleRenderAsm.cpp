// The entries through which the original's code calls the functions in game/VehicleRender.cpp: each takes the arguments
// from the registers the original takes them in and passes them on. Only the injected build has them.
#include "../game/VehicleRender.h"

// FUNC_AT(0x00095f90)
void __declspec(naked) Asm_SwitchEffectFade(void) {
    __asm {
        push ecx
        push edx
        push ecx
        push eax
        call SwitchEffectFade
        add esp, 8
        pop edx
        pop ecx
        ret
    }
}
