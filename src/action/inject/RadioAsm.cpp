// The entries through which the original's code calls the functions in ui/Radio.cpp: each takes the arguments from the
// registers the original takes them in and passes them on. Only the injected build has them.
#include "../ui/Radio.h"

// EAX the radio, EDX the item (kept); the original's stack argument (the items) is not read
// AUTOLTCG
void __declspec(naked) Asm_Radio_GetIndex(void) {
    __asm {
        push ecx
        push edx
        push edx
        push eax
        call Radio_GetIndex
        add esp, 8
        pop edx
        pop ecx
        ret
    }
}

// ECX the radio, ESI the index; the original's stack argument (the items) is not read
// AUTOLTCG
void __declspec(naked) Asm_Radio_SetIndex(void) {
    __asm {
        push ecx
        push edx
        push esi
        push ecx
        call Radio_SetIndex
        add esp, 8
        pop edx
        pop ecx
        ret
    }
}
