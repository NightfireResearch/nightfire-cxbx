// The entries through which the original's code calls the functions in ui/TextBox.cpp: each takes the arguments from
// the registers the original takes them in and passes them on. Only the injected build has them.
#include "../ui/TextBox.h"

// EAX the box (kept), DL the character
// AUTOLTCG
void __declspec(naked) Asm_Text_SetupChar(void) {
    _asm {
        push ecx
        push edx
        push dword ptr [esp + 28]
        push dword ptr [esp + 28]
        push dword ptr [esp + 28]
        push dword ptr [esp + 28]
        push dword ptr [esp + 28]
        push edx
        push eax
        call Text_SetupChar
        add esp, 28
        pop edx
        pop ecx
        ret
    }
}
