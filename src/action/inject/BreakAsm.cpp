// The entries through which the original's code calls the functions in game/obj/Break.cpp: each takes the arguments
// from the registers the original takes them in and passes them on. Only the injected build has them.
#include "../game/obj/Break.h"

// The object in ESI, the hit in EDI
// AUTOLTCG
void __declspec(naked) Asm_Break_Kill(void) {
    _asm {
        push edi
        push esi
        call Break_Kill
        add esp, 8
        ret
    }
}
