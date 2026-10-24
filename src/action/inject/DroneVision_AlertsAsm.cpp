// The entries through which the original's code calls the functions in game/drone/DroneVision_Alerts.cpp: each takes
// the arguments from the registers the original takes them in and passes them on. Only the injected build has them.
#include "../game/drone/DroneVision_Alerts.h"

// The original takes the DCVars in ESI.
// AUTOLTCG
void __declspec(naked) Asm_DroneVision_AlertSound(void) {
    _asm {
        push esi
        call DroneVision_AlertSound
        add esp, 4
        ret
    }
}

// The original takes the DCVars in EDX.
// AUTOLTCG
void __declspec(naked) Asm_DroneVision_GlobalAlarm(void) {
    _asm {
        push edx
        call DroneVision_GlobalAlarm
        add esp, 4
        ret
    }
}

// AUTOLTCG
void __declspec(naked) Asm_DroneVision_ShoutMsg11(void) {
    _asm {
        push eax
        call DroneVision_ShoutMsg11
        add esp, 4
        ret
    }
}

// AUTOLTCG
void __declspec(naked) Asm_DroneVision_ShoutMsg12(void) {
    _asm {
        push eax
        call DroneVision_ShoutMsg12
        add esp, 4
        ret
    }
}

// AUTOLTCG
void __declspec(naked) Asm_DroneVision_ShoutMsg13(void) {
    _asm {
        push eax
        call DroneVision_ShoutMsg13
        add esp, 4
        ret
    }
}

// AUTOLTCG
void __declspec(naked) Asm_DroneVision_ShoutDroneAlert(void) {
    _asm {
        push eax
        call DroneVision_ShoutDroneAlert
        add esp, 4
        ret
    }
}
