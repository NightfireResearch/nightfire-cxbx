// The entries through which the original's code calls the functions in game/WeaponCameras.cpp: each takes the arguments
// from the registers the original takes them in and passes them on. Only the injected build has them.
#include "../game/WeaponCameras.h"

// ESI position, EDI uv, then radius and turns on the stack
// FUNC_AT(0x000a68f0)
void __declspec(naked) Asm_SniperRingPoint(void) {
    __asm {
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        push edi
        push esi
        call SniperRingPoint
        add esp, 0x10
        ret
    }
}

// EAX texture, ECX rectangle
// FUNC_AT(0x000a6cd0)
void __declspec(naked) Asm_ParticleTextureRect(void) {
    __asm {
        push ecx
        push eax
        call ParticleTextureRect
        add esp, 8
        ret
    }
}
