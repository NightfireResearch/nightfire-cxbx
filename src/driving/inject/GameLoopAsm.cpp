// The entries through which the original's code calls the functions in engine/GameLoop.cpp: each takes the arguments
// from the registers the original takes them in and passes them on. Only the injected build has them.
#include "../engine/GameLoop.h"

// The original's entry: the movie on the stack, the subtitle file in EBX (RunTheGame's two calls).
// FUNC_AT(0x0005a6b0)
void __declspec(naked) Asm_GameLoop_PlayMovie(void) {
    __asm {
        push ebx
        push dword ptr [esp + 8]
        call GameLoop_PlayMovie
        add esp, 8
        ret
    }
}
