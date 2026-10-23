// The entries through which the original's code calls the functions in game/obj/Effect.cpp: each takes the arguments
// from the registers the original takes them in and passes them on. Only the injected build has them.
#include "../game/obj/Effect.h"

// The object in EAX, the hit in ECX
// AUTOLTCG
void __declspec(naked) Asm_Effect_Bullet(void) {
    _asm {
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        push ecx
        push eax
        call Effect_Bullet
        add esp, 16
        ret
    }
}

// The hit in EAX, the object in EBX
// AUTOLTCG
void __declspec(naked) Asm_Effect_Player(void) {
    _asm {
        push dword ptr [esp + 12]
        push dword ptr [esp + 12]
        push dword ptr [esp + 12]
        push ebx
        push eax
        call Effect_Player
        add esp, 20
        ret
    }
}

// The hit in EAX, the cel in EBX
// AUTOLTCG
void __declspec(naked) Asm_Effect_Laser(void) {
    _asm {
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        push ebx
        push eax
        call Effect_Laser
        add esp, 16
        ret
    }
}

// The hit in EAX
// AUTOLTCG
void __declspec(naked) Asm_Effect_Body(void) {
    _asm {
        push dword ptr [esp + 12]
        push dword ptr [esp + 12]
        push dword ptr [esp + 12]
        push eax
        call Effect_Body
        add esp, 16
        ret
    }
}

// The mark in EAX
// AUTOLTCG
void __declspec(naked) Asm_Effect_UnLink(void) {
    _asm {
        push ecx
        push edx
        push eax
        call Effect_UnLink
        add esp, 4
        pop edx
        pop ecx
        ret
    }
}

// The mark in EAX, the cel in EDX
// AUTOLTCG
void __declspec(naked) Asm_Effect_LinkToCel(void) {
    _asm {
        push ecx
        push edx
        push edx
        push eax
        call Effect_LinkToCel
        add esp, 8
        pop edx
        pop ecx
        ret
    }
}

// The mark in ESI, the object in EDI
// AUTOLTCG
void __declspec(naked) Asm_Effect_LinkToObject(void) {
    _asm {
        push edi
        push esi
        call Effect_LinkToObject
        add esp, 8
        ret
    }
}
