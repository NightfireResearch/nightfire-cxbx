// The entries through which the original's code calls the functions in anim/AnimEngine.cpp: each takes the arguments
// from the registers the original takes them in and passes them on. Only the injected build has them.
#include "../anim/AnimEngine.h"

// FUNC_AT(0x000776d0)
void __declspec(naked) Asm_FindAnimInfo(void) {
    __asm {
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        push eax
        push edx
        call FindAnimInfo
        add esp, 16
        ret
    }
}

// FUNC_AT(0x00077b60)
void __declspec(naked) Asm_DeactivateActiveSystem(void) {
    __asm {
        movzx eax, byte ptr [esp + 4]
        push eax
        push ecx
        push edi
        call DeactivateActiveSystem
        add esp, 12
        ret
    }
}
