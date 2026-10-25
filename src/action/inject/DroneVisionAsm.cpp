// The entries through which the original's code calls the functions in game/drone/DroneVision.cpp: each takes the
// arguments from the registers the original takes them in and passes them on. Only the injected build has them.
#include "../game/drone/DroneVision.h"

// The original takes the drone in ESI and returns in ST0.
// AUTOLTCG
void __declspec(naked) Asm_DroneVision_OpponentVisibility(void) {
    _asm {
        push esi
        call DroneVision_OpponentVisibility
        add esp, 4
        ret
    }
}

// The original takes the drone in EAX and the range on the stack.
// AUTOLTCG
void __declspec(naked) Asm_DroneVision_OpponentInView(void) {
    _asm {
        push dword ptr [esp + 4]
        push eax
        call DroneVision_OpponentInView
        add esp, 8
        ret
    }
}

// The original takes the drone in ESI.
// AUTOLTCG
void __declspec(naked) Asm_DroneVision_SeekOpponentLOS(void) {
    _asm {
        push esi
        call DroneVision_SeekOpponentLOS
        add esp, 4
        ret
    }
}

// The original takes the drone in ESI.
// AUTOLTCG
void __declspec(naked) Asm_DroneVision_HaveOpponentSight(void) {
    _asm {
        push esi
        call DroneVision_HaveOpponentSight
        add esp, 4
        ret
    }
}

// The original takes `from` in EAX and the id in EDX.
// AUTOLTCG
void __declspec(naked) Asm_DroneVision_FindDrone(void) {
    _asm {
        push edx
        push eax
        call DroneVision_FindDrone
        add esp, 8
        ret
    }
}

// The original takes the drone in ESI and the alerter in EDI.
// AUTOLTCG
void __declspec(naked) Asm_DroneVision_ConsiderAlerted(void) {
    _asm {
        push edi
        push esi
        call DroneVision_ConsiderAlerted
        add esp, 8
        ret
    }
}

// The original takes a in EDX and b in EAX.
// AUTOLTCG
void __declspec(naked) Asm_DroneVision_DetermineAlertCheck(void) {
    _asm {
        push eax
        push edx
        call DroneVision_DetermineAlertCheck
        add esp, 8
        ret
    }
}
