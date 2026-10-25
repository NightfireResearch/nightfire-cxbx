// The entries through which the original's code calls the functions in game/obj/Hurt.cpp: each takes the arguments from
// the registers the original takes them in and passes them on. Only the injected build has them.
#include "../game/obj/Hurt.h"

// The object in EAX, the hurt volume in ECX
// AUTOLTCG
void __declspec(naked) Asm_Hurt_IsTriggered(void) {
    _asm {
        push ecx
        push eax
        call Hurt_IsTriggered
        add esp, 8
        ret
    }
}
