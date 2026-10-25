// The entries through which the original's code calls the functions in eagl/RenderMethod.cpp: each takes the arguments
// from the registers the original takes them in and passes them on. Only the injected build has them.
#include "../eagl/RenderMethod.h"
#include "../platform/RealMath.h"

// 0x000f13b0, Transform::Invert's and Determinant's determinant (EAX = matrix, answered in ST0, other registers
// kept): the same instructions as the maths library's Determinant4x4, which is ours (Transform.cpp says so too).
// A register argument, so an adaptor tagged AUTOLTCG under Ghidra's name (FUNC_AT refuses EAX inputs).
// FUNC_AT(0x000f13b0)
void __declspec(naked) Asm_Determinant4x4(void) {
    __asm {
        push ecx
        push edx
        push eax
        call Determinant4x4
        add esp, 4
        pop edx
        pop ecx
        ret
    }
}

// Unreferenced code taking EAX, ECX and EDX: [EDX + ECX * 4 + 0x20] = EAX, answers true. In its own instructions:
// no C++ convention passes EAX.
// FUNC_AT(0x000f4540)
void __declspec(naked) Asm_EAGL_StoreRegisterArgs(void) {
    __asm {
        mov dword ptr [edx + ecx * 4 + 0x20], eax
        mov al, 1
        ret
    }
}

// Unreferenced code taking EAX (an object whose +4 is a data pointer) and EDX (the output): [EDX] = [EAX + 4] +
// the first stack argument, RET 0xc. In its own instructions.
// FUNC_AT(0x000f6b90)
void __declspec(naked) Asm_EAGL_LockRegisterArgs(void) {
    __asm {
        mov ecx, dword ptr [eax + 4]
        add ecx, dword ptr [esp + 4]
        mov dword ptr [edx], ecx
        ret 0xc
    }
}
