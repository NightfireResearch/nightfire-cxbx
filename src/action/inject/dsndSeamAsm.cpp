// The entries through which the original's code calls the functions in sound/dsndSeam.cpp: each takes the arguments
// from the registers the original takes them in and passes them on. Only the injected build has them.
#include "../sound/dsndSeam.h"

// The original takes its channel count in EDX and its 3D flag on the stack, and still leaves the stack cleanup
// to the caller (plain RET) - confirmed via raw disassembly of both the function and its one call site in
// xboxCreateSoundBuffers (0x000e1170: "PUSH EBX / MOV EDX,EBP / CALL 0x000e0f00 / ... / ADD ESP,0x4"). Ghidra
// reports it as __fastcall, which would be a callee-cleans RET 4 if compiled as such - hence a hand-written
// entry trampoline rather than an AUTOINJECT of a __fastcall function. Same pattern as d3dRenderTargetSetup.
//
// FUNC_AT(0x000e0f00)
void __declspec(naked) Asm_dsndCreateSoundBuffer(void) {
    _asm {
        push ebp
        mov  ebp, esp
        movzx eax, byte ptr [ebp + 8] // the single stack argument: is3d
        push eax
        push edx                      // numChannels
        call dsndCreateSoundBuffer
        add  esp, 8
        pop  ebp
        ret                           // plain RET: the caller cleans up its own pushed argument
    }
}
