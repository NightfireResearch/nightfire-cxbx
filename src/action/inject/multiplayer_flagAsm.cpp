// The entries through which the original's code calls the functions in game/mp/multiplayer_flag.cpp: each takes the
// arguments from the registers the original takes them in and passes them on. Only the injected build has them.
#include "../game/mp/multiplayer.h"

// gameObj in EAX, mpObj in EDI, dropped on the stack, removed by the caller.
// AUTOLTCG
void __declspec(naked) Asm_MP_FlagUpdate(void) {
    _asm {
        push dword ptr [esp + 4]    // dropped
        push edi                    // mpObj
        push eax                    // gameObj
        call MP_FlagUpdate
        add esp, 12
        ret
    }
}

// gameObj in EAX, the other two on the stack, removed by the caller.
// AUTOLTCG
void __declspec(naked) Asm_MP_BluePrintUpdate(void) {
    _asm {
        push eax                    // gameObj
        push dword ptr [esp + 12]   // dropped
        push dword ptr [esp + 12]   // mpObj (12 again: the push moved it along)
        call MP_BluePrintUpdate
        add esp, 12
        ret
    }
}
