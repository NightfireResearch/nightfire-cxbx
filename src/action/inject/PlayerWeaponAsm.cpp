// The entries through which the original's code calls the functions in game/obj/PlayerWeapon.cpp: each takes the
// arguments from the registers the original takes them in and passes them on. Only the injected build has them.
#include "../game/obj/PlayerWeapon.h"

// Not in a header
void Player_TiltWeaponTowardsCrosshairs(obj_tag *player);

// The player in EAX
// AUTOLTCG
void __declspec(naked) Asm_Player_TiltWeaponTowardsCrosshairs(void) {
    _asm {
        push eax
        call Player_TiltWeaponTowardsCrosshairs
        add esp, 4
        ret
    }
}

// AUTOLTCG
void __declspec(naked) Asm_Player_SetFiringAnim(void) {
    _asm {
        push edi
        push dword ptr [esp + 0xc]
        push dword ptr [esp + 0xc]
        call Player_SetFiringAnim
        add esp, 0xc
        ret
    }
}

// AUTOLTCG
void __declspec(naked) Asm_Player_WeaponInitBullet(void) {
    _asm {
        push dword ptr [esp + 4]
        push eax
        call Player_WeaponInitBullet
        add esp, 8
        ret
    }
}
