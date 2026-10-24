// The entries through which the original's code calls the functions in game/mp/multiplayer_goldeneye.cpp: each takes
// the arguments from the registers the original takes them in and passes them on. Only the injected build has them.
#include "../game/mp/multiplayer.h"

// AUTOLTCG
void __declspec(naked) Asm_MP_GoldenEyeUpdate(void) {
    _asm {
        push eax                    // gameObj
        push dword ptr [esp + 12]   // dropped
        push dword ptr [esp + 12]   // mpObj
        call MP_GoldenEyeUpdate
        add esp, 12
        ret
    }
}
