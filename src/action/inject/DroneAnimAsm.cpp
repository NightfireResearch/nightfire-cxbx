// The entries through which the original's code calls the functions in game/drone/DroneAnim.cpp: each takes the
// arguments from the registers the original takes them in and passes them on. Only the injected build has them.
#include "../game/drone/DroneAnim.h"

// AUTOLTCG
void __declspec(naked) Asm_DroneAnim_BaseCallState(void) {
    _asm {
        movzx eax, ax
        push eax
        call DroneAnim_BaseCallState
        add esp, 4
        ret
    }
}

// The original takes the slot, &drone->animMode.
// AUTOLTCG
void __declspec(naked) Asm_DroneAnim_SetDAnimVeryInternal(void) {
    _asm {
        movzx ecx, word ptr [esp + 4]
        push ecx
        movzx edx, dx
        push edx
        sub eax, 0x464
        push eax
        call DroneAnim_SetDAnimVeryInternal
        add esp, 0xc
        ret
    }
}

// AUTOLTCG
void __declspec(naked) Asm_DroneAnim_SkipAnim(void) {
    _asm {
        push eax
        call DroneAnim_SkipAnim
        add esp, 4
        ret
    }
}

// AUTOLTCG
void __declspec(naked) Asm_DroneAnim_TakeCall(void) {
    _asm {
        push esi
        call DroneAnim_TakeCall
        add esp, 4
        ret
    }
}

// AUTOLTCG
void __declspec(naked) Asm_DroneAnim_GetDAnimForAnimState(void) {
    _asm {
        push edi
        push esi
        movzx ecx, word ptr [esp + 0xc]
        push ecx
        movzx edx, dx
        push edx
        movzx eax, ax
        push eax
        call DroneAnim_GetDAnimForAnimState
        add esp, 0x14
        ret
    }
}

// AUTOLTCG
void __declspec(naked) Asm_DroneAnim_GetDAnimForCall(void) {
    _asm {
        push ecx
        push dword ptr [esp + 0xc]
        push dword ptr [esp + 0xc]
        movzx eax, ax
        push eax
        movzx edx, dx
        push edx
        push ebx
        call DroneAnim_GetDAnimForCall
        add esp, 0x18
        ret
    }
}

// AUTOLTCG
void __declspec(naked) Asm_DroneAnim_CanSetAnimCall(void) {
    _asm {
        push eax
        call DroneAnim_CanSetAnimCall
        add esp, 4
        ret
    }
}

// AUTOLTCG
void __declspec(naked) Asm_DroneAnim_CallFullyComplete(void) {
    _asm {
        push esi
        call DroneAnim_CallFullyComplete
        add esp, 4
        ret
    }
}
