// The entries through which the original's code calls the functions in engine/Text.cpp: each takes the arguments from
// the registers the original takes them in and passes them on. Only the injected build has them.
#include "../engine/Text.h"

// EAX the line, BL the player, EDI the type
// AUTOLTCG
void __declspec(naked) Asm_Text_NextMsg(void) {
    _asm {
        push ecx
        push edx
        push edi
        push ebx
        push eax
        call Text_NextMsg
        add esp, 12
        pop edx
        pop ecx
        ret
    }
}
