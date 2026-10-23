// The entries through which the original's code calls the functions in input.cpp: each takes the arguments from the
// registers the original takes them in and passes them on. Only the injected build has them.
#include "../input.h"

// The original takes x in EDI and y in ESI
// AUTOLTCG
void __declspec(naked) Asm_StickCompensation2(void) {
    _asm {
        push ecx
        push edx
        push esi
        push edi
        call StickCompensation2
        add esp, 8
        pop edx
        pop ecx
        ret
    }
}
