// The entries through which the original's code calls the functions in data/Dafi.cpp: each takes the arguments from the
// registers the original takes them in and passes them on. Only the injected build has them.
#include "../data/Dafi.h"

// EAX = line, EDX = end -> EAX; EDX kept, as the original keeps it.
// FUNC_AT(0x0011a1b0)
void __declspec(naked) Asm_DafiNextLine(void) {
    __asm {
        push edx
        push edx
        push eax
        call DafiNextLine
        add esp, 8
        pop edx
        ret
    }
}

// EAX = string -> EAX.
// FUNC_AT(0x0011a200)
void __declspec(naked) Asm_DafiTrim(void) {
    __asm {
        push eax
        call DafiTrim
        add esp, 4
        ret
    }
}
