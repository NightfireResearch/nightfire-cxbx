// The entries through which the original's code calls the functions in ui/MenuManager.cpp: each takes the arguments
// from the registers the original takes them in and passes them on. Only the injected build has them.
#include "../ui/MenuManager.h"

// Leaves EDX alone, which MenuManager_Monitor's original keeps across the call
// AUTOLTCG
void __declspec(naked) Asm_MenuManager_GetStatus(void) {
    _asm {
        push ecx
        push edx
        call MenuManager_GetStatus
        pop edx
        pop ecx
        ret
    }
}
