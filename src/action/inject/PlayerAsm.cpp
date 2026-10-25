// The entries through which the original's code calls the functions in game/obj/Player.cpp: each takes the arguments
// from the registers the original takes them in and passes them on. Only the injected build has them.
#include "../game/obj/Player.h"

// Not in a header
void Player_Swim(obj_tag *player, BLData *blData);
void Player_ClampSomeAngles(BLData *blData, obj_tag *player);
void Player_UpdateBlurAndFade(BLData *blData);

// AUTOLTCG
void __declspec(naked) Asm_Player_InWater(void) {
    _asm {
        push edi
        push esi
        call Player_InWater
        add esp, 8
        ret
    }
}

// AUTOLTCG
void __declspec(naked) Asm_Player_HandlePain(void) {
    _asm {
        movzx eax, bx
        push eax
        movsx eax, di
        push eax
        push esi
        push dword ptr [esp + 0x14]
        push dword ptr [esp + 0x14]
        call Player_HandlePain
        add esp, 0x14
        ret
    }
}

// AUTOLTCG
void __declspec(naked) Asm_Player_HandleDeath(void) {
    _asm {
        push eax
        call Player_HandleDeath
        add esp, 4
        ret
    }
}

// AUTOLTCG
void __declspec(naked) Asm_Player_SSWalk(void) {
    _asm {
        push ecx
        push eax
        call Player_SSWalk
        add esp, 8
        ret
    }
}

// AUTOLTCG
void __declspec(naked) Asm_Player_SSCrouch(void) {
    _asm {
        push ecx
        push eax
        call Player_SSCrouch
        add esp, 8
        ret
    }
}

// AUTOLTCG
void __declspec(naked) Asm_Player_MonitorAir(void) {
    _asm {
        push ecx
        push eax
        call Player_MonitorAir
        add esp, 8
        ret
    }
}

// AUTOLTCG
void __declspec(naked) Asm_Player_HandleJump(void) {
    _asm {
        push dword ptr [esp + 4]
        push ecx
        push eax
        call Player_HandleJump
        add esp, 0xc
        ret
    }
}

// The original's caller keeps EDX across the call (Player_Aiming), and Ghidra has its return in ST0 as custom
// storage; the adaptor keeps ECX and EDX.
// AUTOLTCG
void __declspec(naked) Asm_AccelFunc0(void) {
    _asm {
        push ecx
        push edx
        mov eax, esp
        push dword ptr [eax + 0x24]
        push dword ptr [eax + 0x20]
        push dword ptr [eax + 0x1c]
        push dword ptr [eax + 0x18]
        push dword ptr [eax + 0x14]
        push dword ptr [eax + 0x10]
        push dword ptr [eax + 0x0c]
        call AccelFunc0
        add esp, 0x1c
        pop edx
        pop ecx
        ret
    }
}

// The object in EAX, the hit in ECX
// AUTOLTCG
void __declspec(naked) Asm_PlayerOrDrone_FeetOnPoint_DoCollision(void) {
    _asm {
        push ecx
        push eax
        call PlayerOrDrone_FeetOnPoint_DoCollision
        add esp, 8
        ret
    }
}

// AUTOLTCG
void __declspec(naked) Asm_Player_ZeroG(void) {
    _asm {
        push dword ptr [esp + 4]
        push esi
        call Player_ZeroG
        add esp, 8
        ret
    }
}

// AUTOLTCG
void __declspec(naked) Asm_Player_SSScanMode(void) {
    _asm {
        push ebx
        push eax
        call Player_SSScanMode
        add esp, 8
        ret
    }
}

// AUTOLTCG
void __declspec(naked) Asm_Player_Activate(void) {
    _asm {
        push edi
        push eax
        call Player_Activate
        add esp, 8
        ret
    }
}

// The player's object in ESI, his data in EAX
// AUTOLTCG
void __declspec(naked) Asm_Player_Swim(void) {
    _asm {
        push eax
        push esi
        call Player_Swim
        add esp, 8
        ret
    }
}

// The player's data in ECX, his object on the stack
// AUTOLTCG
void __declspec(naked) Asm_Player_ClampSomeAngles(void) {
    _asm {
        push dword ptr [esp + 4]
        push ecx
        call Player_ClampSomeAngles
        add esp, 8
        ret
    }
}

// The player's data in ESI
// AUTOLTCG
void __declspec(naked) Asm_Player_UpdateBlurAndFade(void) {
    _asm {
        push esi
        call Player_UpdateBlurAndFade
        add esp, 4
        ret
    }
}
