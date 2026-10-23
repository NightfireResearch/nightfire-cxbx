// The entries through which the original's code calls the functions in game/obj/PlayerTraverse.cpp: each takes the
// arguments from the registers the original takes them in and passes them on. Only the injected build has them.
#include "../game/obj/Player.h"

// AUTOLTCG
void __declspec(naked) Asm_Player_InitGrapple(void) {
    _asm {
        push dword ptr [esp + 4]
        push esi
        call Player_InitGrapple
        add esp, 8
        ret
    }
}

// AUTOLTCG
void __declspec(naked) Asm_Player_SSGrapple(void) {
    _asm {
        push edi
        push eax
        call Player_SSGrapple
        add esp, 8
        ret
    }
}

// AUTOLTCG
void __declspec(naked) Asm_Player_GrappleSetRope(void) {
    _asm {
        push edi
        call Player_GrappleSetRope
        add esp, 4
        ret
    }
}
