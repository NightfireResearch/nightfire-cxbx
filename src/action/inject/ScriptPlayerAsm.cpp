// The entries through which the original's code calls the functions in game/obj/ScriptPlayer.cpp: each takes the
// arguments from the registers the original takes them in and passes them on. Only the injected build has them.
#include "../game/obj/ScriptPlayer.h"

// Not in a header
void SP_LoadScript(obj_tag *obj, SCRIPTPLAYER *scriptPlayer);

// The original takes the script player in EBX. SP_Update is its only caller.
// AUTOLTCG
void __declspec(naked) Asm_SP_Hit(void) {
    _asm {
        push ebx
        call SP_Hit
        add esp, 4
        ret
    }
}

// The original takes the object on the stack and the script player in ESI.
// AUTOLTCG
void __declspec(naked) Asm_SP_LoadScript(void) {
    _asm {
        push esi
        push dword ptr [esp + 8]
        call SP_LoadScript
        add esp, 8
        ret
    }
}
