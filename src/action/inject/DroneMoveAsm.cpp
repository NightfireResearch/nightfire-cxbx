// The entries through which the original's code calls the functions in game/drone/DroneMove.cpp: each takes the
// arguments from the registers the original takes them in and passes them on. Only the injected build has them.
#include "../game/drone/DroneMove.h"

// The original takes the obstacle drone in EAX and the test in ESI, the drone on the stack.
// AUTOLTCG
void __declspec(naked) Asm_NDrone2_EvaluateObstacle(void) {
    _asm {
        push esi
        push eax
        push dword ptr [esp + 12]
        call NDrone2_EvaluateObstacle
        add esp, 12
        ret
    }
}
