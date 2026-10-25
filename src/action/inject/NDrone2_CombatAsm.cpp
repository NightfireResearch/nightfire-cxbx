// The entries through which the original's code calls the functions in game/drone/NDrone2_Combat.cpp: each takes the
// arguments from the registers the original takes them in and passes them on. Only the injected build has them.
#include "../game/drone/NDrone2_Combat.h"

// AUTOLTCG
void __declspec(naked) Asm_DroneFunc_FirstSightState(void) {
    _asm {
        push esi
        call DroneFunc_FirstSightState
        add esp, 4
        ret
    }
}

// AUTOLTCG
void __declspec(naked) Asm_DroneFunc_FirstAlertState(void) {
    _asm {
        push eax
        call DroneFunc_FirstAlertState
        add esp, 4
        ret
    }
}
