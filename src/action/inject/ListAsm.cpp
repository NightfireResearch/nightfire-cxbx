// The entries through which the original's code calls the functions in ui/List.cpp: each takes the arguments from the
// registers the original takes them in and passes them on. Only the injected build has them.
#include "../ui/List.h"

// EAX the list, EDX the row (kept); the original's stack argument (the rows) is not read
// AUTOLTCG
void __declspec(naked) Asm_List_GetIndex(void) {
    __asm {
        push ecx
        push edx
        push edx
        push eax
        call List_GetIndex
        add esp, 8
        pop edx
        pop ecx
        ret
    }
}

// ECX the list, EDX the index (kept); the original's stack argument (the rows) is not read
// AUTOLTCG
void __declspec(naked) Asm_List_SetIndex(void) {
    __asm {
        push ecx
        push edx
        push edx
        push ecx
        call List_SetIndex
        add esp, 8
        pop edx
        pop ecx
        ret
    }
}
