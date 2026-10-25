// The entries through which the original's code calls the functions in eagl/Profiler.cpp: each takes the arguments from
// the registers the original takes them in and passes them on. Only the injected build has them.
#include "../eagl/Profiler.h"
#include "../gfx/D3D8.h"

// D3DDevice_SetVertexShaderConstant with the count in EAX, the register (less 0x60) in ECX and the data in EDX,
// the three entry points' own register convention (the seam's __fastcall functions, gfx/D3D8.h): one and four
// constants go straight to their inline versions.
// FUNC_AT(0x000f4310)
void __declspec(naked) Asm_ProfilerSetVertexShaderConstant(void) {
    __asm {
        add ecx, 0x60
        cmp eax, 1
        jne NotOne
        jmp D3DDevice_SetVertexShaderConstant1
    NotOne:
        cmp eax, 4
        jne NotFour
        jmp D3DDevice_SetVertexShaderConstant4
    NotFour:
        shl eax, 2
        push eax
        call D3DDevice_SetVertexShaderConstantNotInline
        ret
    }
}
