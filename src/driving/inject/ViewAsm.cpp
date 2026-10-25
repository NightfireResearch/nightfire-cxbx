// The entries through which the original's code calls the functions in eagl/View.cpp: each takes the arguments from the
// registers the original takes them in and passes them on. Only the injected build has them.
#include "../eagl/View.h"

// Not in a header
uint32_t __cdecl RenderContextWordAt(const uint8_t *self, int index);
uint8_t __cdecl RenderContextSetFloatAt(uint8_t *self, int index, uint32_t valueBits);
float __cdecl RenderContextFloatAt(const uint8_t *self, int index);
int32_t __cdecl ShortAt(const uint8_t *p, int offset);

// EAX = [ECX + EAX * 4 + 0x20]
// FUNC_AT(0x000ee130)
void __declspec(naked) Asm_RenderContextWordAt(void) {
    __asm {
        push ecx
        push edx
        push eax
        push ecx
        call RenderContextWordAt
        add esp, 8
        pop edx
        pop ecx
        ret
    }
}

// The original leaves EAX's upper bytes (the index) under AL = 1.
// FUNC_AT(0x000ee140)
void __declspec(naked) Asm_RenderContextSetFloatAt(void) {
    __asm {
        push ecx
        push edx
        push eax
        push dword ptr [esp + 0x10]
        push eax
        push ecx
        call RenderContextSetFloatAt
        add esp, 0xc
        mov ecx, eax
        pop eax
        mov al, cl
        pop edx
        pop ecx
        ret
    }
}

// ST0 = [ECX + EAX * 4 + 0x20]
// FUNC_AT(0x000ee150)
void __declspec(naked) Asm_RenderContextFloatAt(void) {
    __asm {
        push ecx
        push edx
        push eax
        push ecx
        call RenderContextFloatAt
        add esp, 8
        pop edx
        pop ecx
        ret
    }
}

// EAX = (short) [EAX + 4]
// FUNC_AT(0x000ee170)
void __declspec(naked) Asm_ShortAt(void) {
    __asm {
        push ecx
        push edx
        push 4
        push eax
        call ShortAt
        add esp, 8
        pop edx
        pop ecx
        ret
    }
}

// EAX = (short) [EAX + 6]
// FUNC_AT(0x000ee180)
void __declspec(naked) Asm_ShortAtB(void) {
    __asm {
        push ecx
        push edx
        push 6
        push eax
        call ShortAt
        add esp, 8
        pop edx
        pop ecx
        ret
    }
}
