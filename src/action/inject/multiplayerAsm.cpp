// The entries through which the original's code calls the functions in game/mp/multiplayer.cpp: each takes the
// arguments from the registers the original takes them in and passes them on. Only the injected build has them.
#include "../game/mp/multiplayer.h"

// The original takes mpObj in EDI
// AUTOLTCG
void __declspec(naked) Asm_MP_getObjExtFromMPOBJECT(void) {
    _asm {
        push edi
        call MP_getObjExtFromMPOBJECT
        add esp, 4
        ret
    }
}

// The original takes ext in ESI and keepPos on the stack, removed by the caller (MP_getEsponageBaseObj).
// AUTOLTCG
void __declspec(naked) Asm_MP_recalcObjExtPaths(void) {
    _asm {
        push dword ptr [esp + 4]    // keepPos
        push esi                    // ext
        call MP_recalcObjExtPaths
        add esp, 8
        ret
    }
}

// The original (0x0009c880) takes obj in EAX and team in CX, exclude and teamOut on the stack; its callers clean
// the stack.
// AUTOLTCG
void __declspec(naked) Asm_MP_HitBy(void) {
    _asm {
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        movzx ecx, cx
        push ecx
        push eax
        call MP_HitBy
        add esp, 16
        ret
    }
}

// The original takes ext in EAX
// AUTOLTCG
void __declspec(naked) Asm_MP_PostLoadInitObjExt(void) {
    _asm {
        push eax
        call MP_PostLoadInitObjExt
        add esp, 4
        ret
    }
}

// The original takes ext in ECX and obj in EAX
// AUTOLTCG
void __declspec(naked) Asm_MP_initObjExt(void) {
    _asm {
        push eax
        push ecx
        call MP_initObjExt
        add esp, 8
        ret
    }
}
