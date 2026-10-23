// The entries through which the original's code calls the functions in engine/psiSave.cpp: each takes the arguments
// from the registers the original takes them in and passes them on. Only the injected build has them.
#include "../engine/psiSave.h"

// EAX the codename
// AUTOLTCG
void __declspec(naked) Asm_SaveGame_Delete(void) {
    _asm {
        push eax
        call SaveGame_Delete
        add esp, 4
        ret
    }
}
