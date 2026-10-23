// The entries through which the original's code calls the functions in engine/Direct3D/d3dSeam.cpp: each takes the
// arguments from the registers the original takes them in and passes them on. Only the injected build has them.
#include "../engine/Direct3D/d3dSeam.h"

// The original takes its single parameter (a texture-slot index) in ESI, not on the stack - confirmed via raw
// disassembly (its very first instruction reads ESI with no prologue setting it from the stack; Ghidra's own
// decompile flags this exact gap with "unaff_ESI"/"extraout_EAX" warnings - the same class of hidden custom
// calling convention as D3DDevice_SetRenderState_Simple at the top of this file, but on a function we're
// REPLACING rather than one we call into, so it needs a callee-side entry trampoline instead of a caller-side
// one. Matches the established View_CaptureScene/View_AddCels pattern (see view.cpp) for this exact situation.
//
// AUTOLTCG
void __declspec(naked) Asm_d3dRenderTargetSetup(void) {
    _asm {
        push esi
        call d3dRenderTargetSetup
        add esp, 4
        ret
    }
}
