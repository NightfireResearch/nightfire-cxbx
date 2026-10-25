// The entries through which the original's code calls the functions in game/obj/Door.cpp: each takes the arguments from
// the registers the original takes them in and passes them on. Only the injected build has them.
#include "../game/obj/Door.h"

// The original takes the door in EAX, and the placement on the stack, unread.
// AUTOLTCG
void __declspec(naked) Asm_Door_SetupSwing(void) {
    _asm {
        push eax
        call Door_SetupSwing
        add esp, 4
        ret
    }
}

// FUN_0002eb80 in the original, which takes the door in EAX, and the placement on the stack, unread. The name is ours.
// AUTOLTCG
void __declspec(naked) Asm_Door_SetupSlide(void) {
    _asm {
        push eax
        call Door_SetupSlide
        add esp, 4
        ret
    }
}

// The original takes the door in ESI and the state in BX.
// AUTOLTCG
void __declspec(naked) Asm_Door_SetState(void) {
    _asm {
        movzx eax, bx
        push eax
        push esi
        call Door_SetState
        add esp, 8
        ret
    }
}

// The original takes the door in EAX and its data in EDI, then advance on the stack.
// AUTOLTCG
void __declspec(naked) Asm_Door_Interp(void) {
    _asm {
        push dword ptr [esp + 4]
        push edi
        push eax
        call Door_Interp
        add esp, 12
        ret
    }
}

// FUN_0002f7e0 in the original, which takes the lead door's data in ESI and active in BL, then the lead door on the
// stack. The name is ours.
// AUTOLTCG
void __declspec(naked) Asm_Door_MoveGroup(void) {
    _asm {
        movzx eax, bl
        push eax
        push esi
        push dword ptr [esp + 12]
        call Door_MoveGroup
        add esp, 12
        ret
    }
}

// FUN_0002f850 in the original, which takes the door in EDI. The name is ours.
// AUTOLTCG
void __declspec(naked) Asm_Door_CloseIfClear(void) {
    _asm {
        push edi
        call Door_CloseIfClear
        add esp, 4
        ret
    }
}
