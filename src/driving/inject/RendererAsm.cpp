// The entries through which the original's code calls the functions in render/Renderer.cpp: each takes the arguments
// from the registers the original takes them in and passes them on. Only the injected build has them.
#include "../render/Renderer.h"

// EAX article, EDI transform, the distance on the stack
// FUNC_AT(0x0007dcb0)
void __declspec(naked) Asm_DrawArticleEffects(void) {
    __asm {
        push dword ptr [esp + 4]
        push edi
        push eax
        call DrawArticleEffects
        add esp, 12
        ret
    }
}

// EDX instance, EDI article, the rest on the stack
// FUNC_AT(0x0007dd50)
void __declspec(naked) Asm_DrawArticleModel(void) {
    __asm {
        push dword ptr [esp + 12]
        push dword ptr [esp + 12]
        push dword ptr [esp + 12]
        push edi
        push edx
        call DrawArticleModel
        add esp, 20
        ret
    }
}
