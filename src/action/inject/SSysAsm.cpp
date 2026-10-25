// The entries through which the original's code calls the functions in game/sp/SSys.cpp: each takes the arguments from
// the registers the original takes them in and passes them on. Only the injected build has them.
#include "../game/sp/SSys.h"

// The original takes the item on the stack and the message in SI.
// AUTOLTCG
void __declspec(naked) Asm_SSys_Msg(void) {
    _asm {
        movzx eax, si
        push eax
        movzx eax, word ptr [esp + 8]
        push eax
        call SSys_Msg
        add esp, 8
        ret
    }
}
