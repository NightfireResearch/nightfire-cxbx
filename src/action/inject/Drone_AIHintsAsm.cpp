// The entries through which the original's code calls the functions in game/obj/Drone_AIHints.cpp: each takes the
// arguments from the registers the original takes them in and passes them on. Only the injected build has them.
#include "../game/obj/Drone_AIHints.h"

// AUTOLTCG
void __declspec(naked) Asm_Drone_IsCoverNodeUsable(void) {
    _asm {
        push esi
        push edi
        call Drone_IsCoverNodeUsable
        add esp, 8
        ret
    }
}
