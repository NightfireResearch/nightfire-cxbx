// The entries through which the original's code calls the functions in engine/PhysicsUtil.cpp: each takes the arguments
// from the registers the original takes them in and passes them on. Only the injected build has them.
#include "../engine/PhysicsUtil.h"

// AL = moved, ESI = position, EDI = centre, EBX = the radius out; on the stack the radius, the previous position
// and a word nothing reads. EBX, ESI, EDI and EBP kept, as the original keeps them.
// FUNC_AT(0x000bd690)
void __declspec(naked) Asm_Util_FitColliderRegion(void) {
    __asm {
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        push ebx
        push edi
        push esi
        movzx eax, al
        push eax
        call Util_FitColliderRegion
        add esp, 24
        ret
    }
}
