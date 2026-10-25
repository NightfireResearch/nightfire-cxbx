// The entries through which the original's code calls the functions in game/obj/Copter.cpp: each takes the arguments
// from the registers the original takes them in and passes them on. Only the injected build has them.
#include "../game/obj/Copter.h"

// The placement in EBX
// AUTOLTCG
void __declspec(naked) Asm_Copter_CreatePath(void) {
    _asm {
        push ecx
        push edx
        push ebx
        call Copter_CreatePath
        add esp, 4
        pop edx
        pop ecx
        ret
    }
}

// The helicopter in ESI
// AUTOLTCG
void __declspec(naked) Asm_Copter_NextPath(void) {
    _asm {
        push ecx
        push edx
        push esi
        call Copter_NextPath
        add esp, 4
        pop edx
        pop ecx
        ret
    }
}

// The helicopter in EAX; the object and the flag on the stack
// AUTOLTCG
void __declspec(naked) Asm_Copter_Move(void) {
    _asm {
        push ecx
        push edx
        push dword ptr [esp + 16]
        push dword ptr [esp + 16]
        push eax
        call Copter_Move
        add esp, 12
        pop edx
        pop ecx
        ret
    }
}

// The helicopter in EAX; the object on the stack
// AUTOLTCG
void __declspec(naked) Asm_Copter_FireGun(void) {
    _asm {
        push ecx
        push edx
        push dword ptr [esp + 12]
        push eax
        call Copter_FireGun
        add esp, 8
        pop edx
        pop ecx
        ret
    }
}

// The helicopter in EAX; the object on the stack
// AUTOLTCG
void __declspec(naked) Asm_Copter_FireMissile(void) {
    _asm {
        push ecx
        push edx
        push dword ptr [esp + 12]
        push eax
        call Copter_FireMissile
        add esp, 8
        pop edx
        pop ecx
        ret
    }
}

// The player in EDI; the helicopter's object on the stack
// AUTOLTCG
void __declspec(naked) Asm_Copter_SeesPlayer(void) {
    _asm {
        push ecx
        push edx
        push dword ptr [esp + 12]
        push edi
        call Copter_SeesPlayer
        add esp, 8
        pop edx
        pop ecx
        ret
    }
}
