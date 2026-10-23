// The entries through which the original's code calls the functions in game/drone/NDrone2_Cover.cpp: each takes the
// arguments from the registers the original takes them in and passes them on. Only the injected build has them.
#include "../game/drone/NDrone2_Cover.h"

// The original takes the dcv in ESI and the anim state in DI.
// AUTOLTCG
void __declspec(naked) Asm_DroneAnim_ValidateCoverAnim(void) {
    _asm {
        movzx eax, di
        push eax
        push esi
        call DroneAnim_ValidateCoverAnim
        add esp, 8
        ret
    }
}
