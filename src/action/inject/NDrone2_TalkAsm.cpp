// The entries through which the original's code calls the functions in game/drone/NDrone2_Talk.cpp: each takes the
// arguments from the registers the original takes them in and passes them on. Only the injected build has them.
#include "../game/drone/NDrone2.h"

// The original takes the DCVars in EDI.
// AUTOLTCG
void __declspec(naked) Asm_NDrone2_HandleTalking(void) {
    _asm {
        push edi
        call NDrone2_HandleTalking
        add esp, 4
        ret
    }
}
