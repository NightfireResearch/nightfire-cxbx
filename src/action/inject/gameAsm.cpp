// The entries through which the original's code calls the functions in game.cpp: each takes the arguments from the
// registers the original takes them in and passes them on. Only the injected build has them.
#include "../game.h"

// The original (0x000bedf0) takes the level in EAX, the hint pointer in ESI and the objective pointer in ECX, and
// preserves ESI and EDI, which its caller relies on; the C++ core preserves them too.
// AUTOLTCG
void __declspec(naked) Asm_ResetMap_LevelCode2Img(void) {
    _asm {
        push ecx
        push esi
        push eax
        call ResetMap_LevelCode2Img
        add esp, 12
        ret
    }
}
