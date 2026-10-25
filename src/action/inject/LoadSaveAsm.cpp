// The entries through which the original's code calls the functions in engine/LoadSave.cpp: each takes the arguments
// from the registers the original takes them in and passes them on. Only the injected build has them.
#include "../engine/LoadSave.h"

// The original takes the slot in AX and the label in EDI.
// AUTOLTCG
void __declspec(naked) Asm_LS_FindBlockByLabel(void) {
    _asm {
        push dword ptr [esp + 4]
        push edi
        movzx eax, ax
        push eax
        call LS_FindBlockByLabel
        add esp, 0xc
        ret
    }
}

// The original takes the slot in BX.
// AUTOLTCG
void __declspec(naked) Asm_LS_GetLoadStuff(void) {
    _asm {
        movzx eax, bx
        push eax
        push dword ptr [esp + 8]
        call LS_GetLoadStuff
        add esp, 8
        ret
    }
}
