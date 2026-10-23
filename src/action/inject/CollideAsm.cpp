// The entries through which the original's code calls the functions in engine/Collide.cpp: each takes the arguments
// from the registers the original takes them in and passes them on. Only the injected build has them.
#include "../engine/Collide.h"

// The list in EBX
// AUTOLTCG
void __declspec(naked) Asm_Collide_Sort(void) {
    _asm {
        push ebx
        call Collide_Sort
        add esp, 4
        ret
    }
}

// The vertices in EDX, ECX and ESI, the box in EDI and EBX
// AUTOLTCG
void __declspec(naked) Asm_Intersect_AABB_SweptVolume(void) {
    _asm {
        push ebx
        push edi
        push esi
        push ecx
        push edx
        call Intersect_AABB_SweptVolume
        add esp, 20
        ret
    }
}

// The spheres (a centre and its radius) in EBX and EDI, the difference to ESI
// AUTOLTCG
void __declspec(naked) Asm_Intersect_SphereSphere(void) {
    _asm {
        push dword ptr [esp + 4]
        push esi
        push dword ptr [edi + 12]
        push edi
        push dword ptr [ebx + 12]
        push ebx
        call Intersect_SphereSphere
        add esp, 24
        ret
    }
}

// The test in ESI
// AUTOLTCG
void __declspec(naked) Asm_Intersect_RaySphere(void) {
    _asm {
        push dword ptr [esp + 4]
        push esi
        call Intersect_RaySphere
        add esp, 8
        ret
    }
}

// The test in ESI
// AUTOLTCG
void __declspec(naked) Asm_Intersect_CylSphere(void) {
    _asm {
        push dword ptr [esp + 4]
        push esi
        call Intersect_CylSphere
        add esp, 8
        ret
    }
}

// The test in EAX
// AUTOLTCG
void __declspec(naked) Asm_Collide_PickObj(void) {
    _asm {
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        push eax
        call Collide_PickObj
        add esp, 12
        ret
    }
}

// The hit in EAX
// AUTOLTCG
void __declspec(naked) Asm_Collide_Jointy(void) {
    _asm {
        push eax
        push dword ptr [esp + 8]
        call Collide_Jointy
        add esp, 8
        ret
    }
}

// The hit in EAX
// AUTOLTCG
void __declspec(naked) Asm_Collide_Intersect(void) {
    _asm {
        push dword ptr [esp + 8]
        push eax
        push dword ptr [esp + 12]
        call Collide_Intersect
        add esp, 12
        ret
    }
}

// The flags in EAX, the test in EDI. Our name (Ghidra had Collide_CylinderIntersect, which is 0x2c6f0).
// AUTOLTCG
void __declspec(naked) Asm_Collide_TestHits(void) {
    _asm {
        push dword ptr [esp + 4]
        push eax
        push edi
        call Collide_TestHits
        add esp, 12
        ret
    }
}
