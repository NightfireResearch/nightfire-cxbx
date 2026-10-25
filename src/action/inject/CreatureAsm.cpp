// The entries through which the original's code calls the functions in game/obj/Creature.cpp: each takes the arguments
// from the registers the original takes them in and passes them on. Only the injected build has them.
#include "../game/obj/Creature.h"

// The creature in EDI. Not named on PS2; the name is ours.
// AUTOLTCG
void __declspec(naked) Asm_Creature_HurtPlayers(void) {
    _asm {
        push edi
        call Creature_HurtPlayers
        add esp, 4
        ret
    }
}

// ECX the angles, ESI dir, EBX the last waypoint, EDI the creature; the waypoint, distance and size on the stack
// AUTOLTCG
void __declspec(naked) Asm_Creature_CalcPoint(void) {
    _asm {
        push edi
        push ebx
        push esi
        push ecx
        push dword ptr [esp + 0x1c]
        push dword ptr [esp + 0x1c]
        push dword ptr [esp + 0x1c]
        call Creature_CalcPoint
        add esp, 0x1c
        ret
    }
}

// EAX the creature's data, EDX the creature, EDI dir; the answer in AL. Not named on PS2; the name is ours.
// AUTOLTCG
void __declspec(naked) Asm_Creature_SeekPlayer(void) {
    _asm {
        push edi
        push edx
        push eax
        call Creature_SeekPlayer
        add esp, 0xc
        ret
    }
}

// EAX the creature, ESI its data. Not named on PS2; the name is ours.
// AUTOLTCG
void __declspec(naked) Asm_Creature_Move(void) {
    _asm {
        push esi
        push eax
        call Creature_Move
        add esp, 8
        ret
    }
}
