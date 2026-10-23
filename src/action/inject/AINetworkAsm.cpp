// The entries through which the original's code calls the functions in engine/AINetwork.cpp: each takes the arguments
// from the registers the original takes them in and passes them on. Only the injected build has them.
#include "../engine/AINetwork.h"

// The original takes the search in ESI.
// AUTOLTCG
void __declspec(naked) Asm_AINetwork_NodeSearchCel(void) {
    _asm {
        push esi
        call AINetwork_NodeSearchCel
        add esp, 4
        ret
    }
}

// The original takes the route in ESI.
// AUTOLTCG
void __declspec(naked) Asm_AINetwork_AlterDestFor_DROUTE_Nearest(void) {
    _asm {
        push esi
        call AINetwork_AlterDestFor_DROUTE_Nearest
        add esp, 4
        ret
    }
}

// The original takes the route in ESI and dropFirst in AL, then the status and dropLast on the stack.
// AUTOLTCG
void __declspec(naked) Asm_AINetwork_OptimiseRoute(void) {
    _asm {
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        push eax
        push esi
        call AINetwork_OptimiseRoute
        add esp, 16
        ret
    }
}

// The original takes the route in ESI and the destination in EDI, the rest on the stack.
// AUTOLTCG
void __declspec(naked) Asm_AINetwork_SetupRouteToPosition(void) {
    _asm {
        push dword ptr [esp + 16]
        push dword ptr [esp + 16]
        push dword ptr [esp + 16]
        push dword ptr [esp + 16]
        push edi
        push esi
        call AINetwork_SetupRouteToPosition
        add esp, 24
        ret
    }
}

// The original takes the route in ECX, the mover in EAX and the target object in EBX; the argument its callers push
// is not read.
// AUTOLTCG
void __declspec(naked) Asm_AINetwork_CalcRoute(void) {
    _asm {
        push ebx
        push eax
        push ecx
        call AINetwork_CalcRoute
        add esp, 12
        ret
    }
}

// The original takes the route in EAX and the direction on the stack (its callers push one more word, not read).
// AUTOLTCG
void __declspec(naked) Asm_AINetwork_SetupNextNode(void) {
    _asm {
        push dword ptr [esp + 4]
        push eax
        call AINetwork_SetupNextNode
        add esp, 8
        ret
    }
}

// The original takes the blocked flag in EAX and the route on the stack.
// AUTOLTCG
void __declspec(naked) Asm_LinkCreep_Increment(void) {
    _asm {
        push eax
        push dword ptr [esp + 8]
        call LinkCreep_Increment
        add esp, 8
        ret
    }
}

// The original takes the route in EAX (its caller pushes one more word, not read).
// AUTOLTCG
void __declspec(naked) Asm_LinkCreep_Decrement(void) {
    _asm {
        push eax
        call LinkCreep_Decrement
        add esp, 4
        ret
    }
}

// The original takes the route in ESI and the DCVars in EAX.
// AUTOLTCG
void __declspec(naked) Asm_LinkCreep_Handler(void) {
    _asm {
        push eax
        push esi
        call LinkCreep_Handler
        add esp, 8
        ret
    }
}
