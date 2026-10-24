// The entries through which the original's code calls the functions in game/drone/DroneWeapon.cpp: each takes the
// arguments from the registers the original takes them in and passes them on. Only the injected build has them.
#include "../game/drone/DroneWeapon.h"

// The original takes the DCVars in EDI.
// AUTOLTCG
void __declspec(naked) Asm_DroneWeap_DoFiring(void) {
    _asm {
        push edi
        call DroneWeap_DoFiring
        add esp, 4
        ret
    }
}

// The original takes the DCVars in EDI, the two flags on the stack.
// FUNC_AT(0x00067400)
void __declspec(naked) Asm_DroneWeap_NextBulletTime(void) {
    _asm {
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        push edi
        call DroneWeap_NextBulletTime
        add esp, 12
        ret
    }
}

// The original takes the DCVars in EAX.
// AUTOLTCG
void __declspec(naked) Asm_DroneWeap_ManageBurstCount(void) {
    _asm {
        push eax
        call DroneWeap_ManageBurstCount
        add esp, 4
        ret
    }
}

// The original takes the drone in ESI.
// AUTOLTCG
void __declspec(naked) Asm_DroneWeap_BurstDelay(void) {
    _asm {
        push esi
        call DroneWeap_BurstDelay
        add esp, 4
        ret
    }
}
