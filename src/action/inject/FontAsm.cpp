// The entries through which the original's code calls the functions in gfx/Font.cpp: each takes the arguments from the
// registers the original takes them in and passes them on. Only the injected build has them.
#include "../gfx/Font.h"

// AL the character
// AUTOLTCG
void __declspec(naked) Asm_Font_FindGlyph(void) {
    _asm {
        push ecx
        push edx
        push eax
        call Font_FindGlyph
        add esp, 4
        pop edx
        pop ecx
        ret
    }
}

// AL the character before, the stack the character
// AUTOLTCG
void __declspec(naked) Asm_Font_GetKernAdjust(void) {
    _asm {
        push ecx
        push edx
        push dword ptr [esp + 12]
        push eax
        call Font_GetKernAdjust
        add esp, 8
        pop edx
        pop ecx
        ret
    }
}

// ECX the colour, EAX the scale
// FUNC_AT(0x00069c50)
void __declspec(naked) Asm_Font_DrawGlyphs(void) {
    _asm {
        push edx
        push ecx
        push dword ptr [esp + 48]   // fit
        push dword ptr [esp + 48]   // width
        push dword ptr [esp + 48]   // count
        push dword ptr [esp + 48]   // flags
        push dword ptr [esp + 48]   // format
        push dword ptr [esp + 48]   // text
        push dword ptr [esp + 48]
        push dword ptr [esp + 48]
        push dword ptr [esp + 48]   // y
        push dword ptr [esp + 48]   // x
        push eax
        push ecx
        call Font_DrawGlyphs
        add esp, 48
        pop ecx
        pop edx
        ret
    }
}
