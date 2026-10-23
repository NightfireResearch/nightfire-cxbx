// The entries through which the original's code calls the functions in game/drone/DroneSM.cpp: each takes the arguments
// from the registers the original takes them in and passes them on. Only the injected build has them.
#include "../game/drone/Drone.h"

// The original takes the DCVars in ESI and the message on the stack, and its callers clean up.
// AUTOLTCG
void __declspec(naked) Asm_Drone_SM_RouteMsgDCV(void) {
    _asm {
        push dword ptr [esp + 4]
        push esi
        call Drone_SM_RouteMsgDCV
        add esp, 8
        ret
    }
}
