// The entries through which the original's code calls the functions in ui/ui_codenames.cpp: each takes the arguments
// from the registers the original takes them in and passes them on. Only the injected build has them.
#include "../ui/Menu.h"

// The original takes the codename in EAX, the operation in ECX and the stale flag in DL; the rest on the stack.
// AUTOLTCG
void __declspec(naked) Asm_Menu_StartLS(void) {
    _asm {
        push dword ptr [esp + 0x20]
        push dword ptr [esp + 0x20]
        push dword ptr [esp + 0x20]
        push dword ptr [esp + 0x20]
        push dword ptr [esp + 0x20]
        push dword ptr [esp + 0x20]
        push dword ptr [esp + 0x20]
        push dword ptr [esp + 0x20]
        push edx
        push ecx
        push eax
        call Menu_StartLS
        add esp, 0x2c
        ret
    }
}
