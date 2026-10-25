// The entries through which the original's code calls the functions in game/drone/NDrone2_Damage.cpp: each takes the
// arguments from the registers the original takes them in and passes them on. Only the injected build has them.
#include "../game/drone/NDrone2_Damage.h"

// The original takes the drone in EAX.
// AUTOLTCG
void __declspec(naked) Asm_NDrone2_DealWithObjHit(void) {
    _asm {
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        push eax
        call NDrone2_DealWithObjHit
        add esp, 12
        ret
    }
}
