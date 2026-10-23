// The entries through which the original's code calls the functions in game/obj/Sub.cpp: each takes the arguments from
// the registers the original takes them in and passes them on. Only the injected build has them.
#include "../game/obj/Sub.h"

// EDI the sub, ESI its data. Not named on PS2; the name is ours.
// AUTOLTCG
void __declspec(naked) Asm_Sub_Follow(void) {
    _asm {
        push esi
        push edi
        call Sub_Follow
        add esp, 8
        ret
    }
}

// EAX the sub's data; the sub on the stack. Not named on PS2; the name is ours.
// AUTOLTCG
void __declspec(naked) Asm_Sub_Ram(void) {
    _asm {
        push eax
        push dword ptr [esp + 8]
        call Sub_Ram
        add esp, 8
        ret
    }
}

// EBX the sub, ESI its data
// AUTOLTCG
void __declspec(naked) Asm_Sub_Laser(void) {
    _asm {
        push esi
        push ebx
        call Sub_Laser
        add esp, 8
        ret
    }
}

// ESI the sub, EDI its data. Not named on PS2; the name is ours.
// AUTOLTCG
void __declspec(naked) Asm_Sub_FireMissiles(void) {
    _asm {
        push edi
        push esi
        call Sub_FireMissiles
        add esp, 8
        ret
    }
}

// EAX the sub, EDI its data
// AUTOLTCG
void __declspec(naked) Asm_Sub_DeployTorpedoes(void) {
    _asm {
        push edi
        push eax
        call Sub_DeployTorpedoes
        add esp, 8
        ret
    }
}
