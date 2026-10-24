// The entries through which the original's code calls the functions in eagl/EaglFont.cpp: each takes the arguments from
// the registers the original takes them in and passes them on. Only the injected build has them.
#include "../eagl/EaglFont.h"

// Not in a header
void EAGLFont_SetRenderState(int32_t state, uint32_t value);

// FUNC_AT(0x000eea50)
void __declspec(naked) Asm_EAGLFont_SetRenderState(void) {
    __asm {
        push edi
        push esi
        call EAGLFont_SetRenderState
        add esp, 8
        ret
    }
}
