// The entries through which the original's code calls the functions in game/obj/SS.cpp: each takes the arguments from
// the registers the original takes them in and passes them on. Only the injected build has them.
#include "../game/obj/SS.h"

// Not in a header
struct SIMPLESCRIPT;
ushort SS_Operate(obj_tag *obj, SIMPLESCRIPT *ss);

// The original takes the object in EAX and the simple script in ESI.
// AUTOLTCG
void __declspec(naked) Asm_SS_Operate(void) {
    _asm {
        push esi
        push eax
        call SS_Operate
        add esp, 8
        ret
    }
}
