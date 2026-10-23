// The entries through which the original's code calls the functions in game/obj/GT.cpp: each takes the arguments from
// the registers the original takes them in and passes them on. Only the injected build has them.
#include "../game/obj/GT.h"

// The matrix to turn towards in EAX, the one turned from in ECX
// AUTOLTCG
void __declspec(naked) Asm_GT_maybeInterpolateAimDir(void) {
    _asm {
        push ecx
        push edx
        push ecx
        push eax
        call GT_maybeInterpolateAimDir
        add esp, 8
        pop edx
        pop ecx
        ret
    }
}

// The turret in EAX, the point on the stack
// AUTOLTCG
void __declspec(naked) Asm_GT_AimAt(void) {
    _asm {
        push ecx
        push edx
        push dword ptr [esp + 12]
        push eax
        call GT_AimAt
        add esp, 8
        pop edx
        pop ecx
        ret
    }
}

// The turret in ESI; its object on the stack, not read
// AUTOLTCG
void __declspec(naked) Asm_GT_RandMove(void) {
    _asm {
        push ecx
        push edx
        push esi
        call GT_RandMove
        add esp, 4
        pop edx
        pop ecx
        ret
    }
}
