// The entries through which the original's code calls the functions in game/obj/Pickup.cpp: each takes the arguments
// from the registers the original takes them in and passes them on. Only the injected build has them.
#include "../game/obj/Pickup.h"

// The pickup in EAX, what it landed on in ECX
// AUTOLTCG
void __declspec(naked) Asm_Pickup_CanInheritPosition(void) {
    _asm {
        push ecx
        push edx
        push ecx
        push eax
        call Pickup_CanInheritPosition
        add esp, 8
        pop edx
        pop ecx
        ret
    }
}

// The pickup's information in EDI; the pickup and who takes it on the stack
// AUTOLTCG
void __declspec(naked) Asm_Pickup_Handler(void) {
    _asm {
        push ecx
        push edx
        push dword ptr [esp + 16]
        push edi
        push dword ptr [esp + 20]
        call Pickup_Handler
        add esp, 12
        pop edx
        pop ecx
        ret
    }
}
