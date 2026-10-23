// The entries through which the original's code calls the functions in game/mp/multiplayer_objects.cpp: each takes the
// arguments from the registers the original takes them in and passes them on. Only the injected build has them.
#include "../game/mp/multiplayer.h"

// AUTOLTCG
void __declspec(naked) Asm_MP_DemolitionProtectionUpdate(void) {
    _asm {
        push ebx                    // gameObj
        movzx eax, ax               // defenders
        push eax
        push dword ptr [esp + 12]   // mpObj
        call MP_DemolitionProtectionUpdate
        add esp, 12
        ret
    }
}
