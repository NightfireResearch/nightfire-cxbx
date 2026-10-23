// The entries through which the original's code calls the functions in engine/AIPath.cpp: each takes the arguments from
// the registers the original takes them in and passes them on. Only the injected build has them.
#include "../engine/AIPath.h"

// AUTOLTCG
void __declspec(naked) Asm_AIPath_Prepare(void) {
    _asm {
        push esi
        call AIPath_Prepare
        add esp, 4
        ret
    }
}

// AUTOLTCG
void __declspec(naked) Asm_AIBounds_Link2Cel(void) {
    _asm {
        push dword ptr [esp + 4]
        push eax
        call AIBounds_Link2Cel
        add esp, 8
        ret
    }
}

// AUTOLTCG
void __declspec(naked) Asm_AIBounds_Prepare(void) {
    _asm {
        push edi
        call AIBounds_Prepare
        add esp, 4
        ret
    }
}
