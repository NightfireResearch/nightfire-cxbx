// The entries through which the original's code calls the functions in game/obj/bullet.cpp: each takes the arguments
// from the registers the original takes them in and passes them on. Only the injected build has them.
#include "../game/obj/bullet.h"

// The object in ESI
// AUTOLTCG
void __declspec(naked) Asm_Bullet_trail_effect_smoke(void) {
    _asm {
        push esi
        call Bullet_trail_effect_smoke
        add esp, 4
        ret
    }
}

// The object in ESI
// AUTOLTCG
void __declspec(naked) Asm_Bullet_SpawnSmokeDust_SomeOtherProbability(void) {
    _asm {
        push esi
        call Bullet_SpawnSmokeDust_SomeOtherProbability
        add esp, 4
        ret
    }
}

// The object in ESI
// AUTOLTCG
void __declspec(naked) Asm_Bullet_SpawnSmokeDust_SomeProbability(void) {
    _asm {
        push esi
        call Bullet_SpawnSmokeDust_SomeProbability
        add esp, 4
        ret
    }
}

// The object in ECX, the bullet in EAX
// AUTOLTCG
void __declspec(naked) Asm_Bullet_DoTrails(void) {
    _asm {
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        push eax
        push ecx
        call Bullet_DoTrails
        add esp, 16
        ret
    }
}

// The object in EDI
// AUTOLTCG
void __declspec(naked) Asm_Bullet_heatseekable(void) {
    _asm {
        push ecx
        push edx
        push edi
        call Bullet_heatseekable
        add esp, 4
        pop edx
        pop ecx
        ret
    }
}

// The bullet in ESI, the object on the stack
// AUTOLTCG
void __declspec(naked) Asm_Bullet_homing(void) {
    _asm {
        push dword ptr [esp + 4]
        push esi
        call Bullet_homing
        add esp, 8
        ret
    }
}
