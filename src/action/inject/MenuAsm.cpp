// The entries through which the original's code calls the functions in ui/Menu.cpp: each takes the arguments from the
// registers the original takes them in and passes them on. Only the injected build has them.
#include "../ui/Menu.h"

// AX the player
// AUTOLTCG
void __declspec(naked) Asm_Menu_GetAction(void) {
    __asm {
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        push eax
        call Menu_GetAction
        add esp, 12
        ret
    }
}
