// The entries through which the original's code calls the functions in render/StateManager.cpp: each takes the
// arguments from the registers the original takes them in and passes them on. Only the injected build has them.
#include "../render/StateManager.h"

// FUNC_AT(0x000914f0)
void __declspec(naked) Asm_FindStateKeyword(void) {
    __asm {
        push edx
        push dword ptr [esp + 8]
        push edx
        call FindStateKeyword
        add esp, 8
        pop edx
        ret
    }
}
