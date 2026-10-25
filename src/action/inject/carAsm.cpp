// The entries through which the original's code calls the functions in game/obj/car.cpp: each takes the arguments from
// the registers the original takes them in and passes them on. Only the injected build has them.
#include "../game/obj/car.h"

// ESI the car's data; the car on the stack
// AUTOLTCG
void __declspec(naked) Asm_Car_SetBits(void) {
    _asm {
        push dword ptr [esp + 4]
        push esi
        call Car_SetBits
        add esp, 8
        ret
    }
}

// ECX what it touched; the car on the stack
// AUTOLTCG
void __declspec(naked) Asm_Car_InheritVelocity(void) {
    _asm {
        push ecx
        push dword ptr [esp + 8]
        call Car_InheritVelocity
        add esp, 8
        ret
    }
}
