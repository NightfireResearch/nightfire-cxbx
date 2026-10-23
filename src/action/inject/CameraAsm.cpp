// The entries through which the original's code calls the functions in engine/Camera.cpp: each takes the arguments from
// the registers the original takes them in and passes them on. Only the injected build has them.
#include "../engine/Camera.h"

// The original's arguments on the stack and the result in ST0, the caller cleaning up
// AUTOLTCG
void __declspec(naked) Asm_moveTowardsValue(void) {
    _asm {
        push dword ptr [esp + 0xc]
        push dword ptr [esp + 0xc]
        push dword ptr [esp + 0xc]
        call moveTowardsValue
        add esp, 0xc
        ret
    }
}
