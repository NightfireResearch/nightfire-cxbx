// The entries through which the original's code calls the functions in ui/HUD.cpp: each takes the arguments from the
// registers the original takes them in and passes them on. Only the injected build has them.
#include "../ui/HUD.h"

// EAX the player, ESI the crosshair
// AUTOLTCG
void __declspec(naked) Asm_HUD_UpdateCrossHair(void) {
    _asm {
        push ecx
        push edx
        push esi
        push eax
        call HUD_UpdateCrossHair
        add esp, 8
        pop edx
        pop ecx
        ret
    }
}

// EAX the player
// AUTOLTCG
void __declspec(naked) Asm_HUD_MonitorNightSight(void) {
    _asm {
        push ecx
        push edx
        push eax
        call HUD_MonitorNightSight
        add esp, 4
        pop edx
        pop ecx
        ret
    }
}

// ESI the ring; the rest on the stack
// AUTOLTCG
void __declspec(naked) Asm_HUD_PlaceLensFlare(void) {
    _asm {
        push ecx
        push edx
        push dword ptr [esp + 40]   // alpha
        push dword ptr [esp + 40]   // scale
        push dword ptr [esp + 40]   // along
        push dword ptr [esp + 40]   // cy
        push dword ptr [esp + 40]   // cx
        push dword ptr [esp + 40]   // the sun, by value
        push dword ptr [esp + 40]
        push dword ptr [esp + 40]
        push esi
        call HUD_PlaceLensFlare
        add esp, 36
        pop edx
        pop ecx
        ret
    }
}

// EAX the player, EDI the pane, ESI the layout, the stack the object
// AUTOLTCG
void __declspec(naked) Asm_HUD_CreateShift(void) {
    _asm {
        push ecx
        push edx
        push dword ptr [esp + 12]
        push esi
        push edi
        push eax
        call HUD_CreateShift
        add esp, 16
        pop edx
        pop ecx
        ret
    }
}
