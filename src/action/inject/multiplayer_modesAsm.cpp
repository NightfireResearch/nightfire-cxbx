// The entries through which the original's code calls the functions in game/mp/multiplayer_modes.cpp: each takes the
// arguments from the registers the original takes them in and passes them on. Only the injected build has them.
#include "../game/mp/multiplayer.h"

// AUTOLTCG
void __declspec(naked) Asm_MP_UplinkUpdate(void) {
    _asm {
        push eax                    // gameObj
        push dword ptr [esp + 8]    // mpObj
        call MP_UplinkUpdate
        add esp, 8
        ret
    }
}
