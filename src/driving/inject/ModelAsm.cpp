// The entries through which the original's code calls the functions in eagl/Model.cpp: each takes the arguments from
// the registers the original takes them in and passes them on. Only the injected build has them.
#include "../eagl/Model.h"

// Not in a header
void ModelSetRenderState(uint32_t state, uint32_t value);
void ModelSetTextureState(uint32_t state, uint32_t stage, uint32_t value);
uint32_t ModelCreatePalette(uint32_t size, D3DResource **out);
void ModelRegister(D3DResource *resource, void *base);
uint32_t ModelGet2DSurfaceDesc(D3DPixelContainer *container, uint32_t level, D3DSurfaceDesc *desc);
uint32_t ModelGetSurfaceLevel(D3DPixelContainer *texture, uint32_t level, D3DPixelContainer **out);
uint32_t ModelRelease(D3DResource *resource);
uint32_t ModelSurfaceGetDesc(D3DPixelContainer *surface, D3DSurfaceDesc *desc);
uint32_t ModelLockRect(D3DPixelContainer *surface, D3DLockedRect *locked, const D3DRect *rect, uint32_t flags);

// FUNC_AT(0x000e8b20)
void __declspec(naked) Asm_ModelShapeCount(void) {
    __asm {
        mov eax, dword ptr [eax + 8]
        ret
    }
}

// FUNC_AT(0x000e8b30)
void __declspec(naked) Asm_ModelShapeImage(void) {
    __asm {
        mov eax, dword ptr [ecx + eax * 8 + 0x14]
        add eax, ecx
        ret
    }
}

// ESI = state index, EDI = value
// FUNC_AT(0x000eaca0)
void __declspec(naked) Asm_ModelSetRenderState(void) {
    __asm {
        push edi
        push esi
        call ModelSetRenderState
        add esp, 8
        ret
    }
}

// EAX = state, ECX = stage, EDX = value
// FUNC_AT(0x000eaea0)
void __declspec(naked) Asm_ModelSetTextureState(void) {
    __asm {
        push edx
        push ecx
        push eax
        call ModelSetTextureState
        add esp, 0xc
        ret
    }
}

// EAX = size, [esp+8] = out; RET 8
// FUNC_AT(0x000eaf20)
void __declspec(naked) Asm_ModelCreatePalette(void) {
    __asm {
        push dword ptr [esp + 8]
        push eax
        call ModelCreatePalette
        add esp, 8
        ret 8
    }
}

// ECX = resource, EAX = base
// FUNC_AT(0x000eaf40)
void __declspec(naked) Asm_ModelRegister(void) {
    __asm {
        push eax
        push ecx
        call ModelRegister
        add esp, 8
        ret
    }
}

// EDX, ECX, EAX
// FUNC_AT(0x000eaf50)
void __declspec(naked) Asm_ModelGet2DSurfaceDesc(void) {
    __asm {
        push eax
        push ecx
        push edx
        call ModelGet2DSurfaceDesc
        add esp, 0xc
        ret
    }
}

// ECX = texture, EAX = level, [esp+4] = out; RET 4
// FUNC_AT(0x000eaf60)
void __declspec(naked) Asm_ModelGetSurfaceLevel(void) {
    __asm {
        push dword ptr [esp + 4]
        push eax
        push ecx
        call ModelGetSurfaceLevel
        add esp, 0xc
        ret 4
    }
}

// EAX = resource
// FUNC_AT(0x000eaf90)
void __declspec(naked) Asm_ModelReleaseA(void) {
    __asm {
        push eax
        call ModelRelease
        add esp, 4
        ret
    }
}

// EAX = resource
// FUNC_AT(0x000eafb0)
void __declspec(naked) Asm_ModelReleaseB(void) {
    __asm {
        push eax
        call ModelRelease
        add esp, 4
        ret
    }
}

// ECX = surface, EAX = desc
// FUNC_AT(0x000eafc0)
void __declspec(naked) Asm_ModelSurfaceGetDesc(void) {
    __asm {
        push eax
        push ecx
        call ModelSurfaceGetDesc
        add esp, 8
        ret
    }
}

// [esp+4] = surface, EDX, ECX, EAX; RET 4
// FUNC_AT(0x000eafd0)
void __declspec(naked) Asm_ModelLockRect(void) {
    __asm {
        push eax
        push ecx
        push edx
        push dword ptr [esp + 0x10]
        call ModelLockRect
        add esp, 0x10
        ret 4
    }
}

// EAX = object
// FUNC_AT(0x000eb000)
void __declspec(naked) Asm_ModelWordAt4(void) {
    __asm {
        movsx eax, word ptr [eax + 4]
        ret
    }
}

// EAX = object
// FUNC_AT(0x000eb010)
void __declspec(naked) Asm_ModelWordAt6(void) {
    __asm {
        movsx eax, word ptr [eax + 6]
        ret
    }
}

// ECX = shapes, EAX = index
// FUNC_AT(0x000eb060)
void __declspec(naked) Asm_ModelShapeEntry(void) {
    __asm {
        mov eax, dword ptr [ecx + eax * 8 + 0x14]
        add eax, ecx
        ret
    }
}
