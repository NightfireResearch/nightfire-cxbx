// The entries through which the original's code calls the functions in game/obj/MiniSub.cpp: each takes the arguments
// from the registers the original takes them in and passes them on. Only the injected build has them.
#include "../game/obj/MiniSub.h"

// EAX the sub's data, EBX where. Not named on PS2; the name is ours.
// AUTOLTCG
void __declspec(naked) Asm_MiniSub_SetTarget(void) {
    _asm {
        push ebx
        push eax
        call MiniSub_SetTarget
        add esp, 8
        ret
    }
}

// ESI the sub's data. Not named on PS2; the name is ours.
// AUTOLTCG
void __declspec(naked) Asm_MiniSubMove_Turn(void) {
    _asm {
        push esi
        call MiniSubMove_Turn
        add esp, 4
        ret
    }
}

// EAX the sub's data
// AUTOLTCG
void __declspec(naked) Asm_MiniSubMove_Forward(void) {
    _asm {
        push eax
        call MiniSubMove_Forward
        add esp, 4
        ret
    }
}

// ESI the sub's data; the target and radius on the stack. Not named on PS2; the name is ours.
// AUTOLTCG
void __declspec(naked) Asm_MiniSub_Route(void) {
    _asm {
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        push esi
        call MiniSub_Route
        add esp, 0xc
        ret
    }
}

// EAX the sub's data
// AUTOLTCG
void __declspec(naked) Asm_MiniSub_FireTorpedo(void) {
    _asm {
        push eax
        call MiniSub_FireTorpedo
        add esp, 4
        ret
    }
}

// EAX the sub's data
// AUTOLTCG
void __declspec(naked) Asm_MiniSubState_Attack(void) {
    _asm {
        push eax
        call MiniSubState_Attack
        add esp, 4
        ret
    }
}

// ESI the sub's data. Not named on PS2; the name is ours.
// AUTOLTCG
void __declspec(naked) Asm_MiniSubState_Init(void) {
    _asm {
        push esi
        call MiniSubState_Init
        add esp, 4
        ret
    }
}
