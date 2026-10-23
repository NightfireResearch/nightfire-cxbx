// The entries through which the original's code calls the functions in math/math.cpp: each takes the arguments from the
// registers the original takes them in and passes them on. Only the injected build has them.
#include "../math/math.h"

// The original (0x000d5800) only ever touches EAX and the x87 stack, and the game's own callers were compiled
// knowing that: KeyFrame_Interp and Spline_Interp keep the output matrix in ECX across the call and then write
// the translation through it (mov [ecx+0x30], ...). Our compiled body is entitled to clobber ECX and EDX, and
// clang leaves the quaternion pointer in ECX - so the translation went 0x30 bytes past a stack local instead,
// onto Spline_Interp's own return address, and loading a level with a swinging door (Door_Create ->
// Door_SetupSwing -> Spline_Interp) jumped to a float. ECX and EDX are saved here so any caller keeps what it
// had.
// AUTOLTCG
void __declspec(naked) Asm_Quat_QuatToMat(void) {
  _asm {
    push ecx
    push edx
    push dword ptr [esp + 16]   // param_2
    push dword ptr [esp + 16]   // param_1
    call Quat_QuatToMat
    add esp, 8
    pop edx
    pop ecx
    ret
  }
}

// The original writes no EDX (only ECX, with the input's address), and its callers rely on it: Mat_LookAt and
// 0x000d8590 keep EDX across the call. So the game reaches this through an entry that keeps both (tools/caller_saved.py).
// AUTOLTCG
void __declspec(naked) Asm_Vec_Normalise(void) {
    _asm {
        push ecx
        push edx
        push dword ptr [esp + 16]
        push dword ptr [esp + 16]
        call Vec_Normalise
        add esp, 8
        pop edx
        pop ecx
        ret
    }
}

// The original writes no EDX (only ECX, with the input's address), and its callers rely on it: Cel_ObjectLeftCel keeps
// EDX across the call. So the game reaches this through an entry that keeps both (tools/caller_saved.py).
// AUTOLTCG
void __declspec(naked) Asm_Vec_NormaliseLen(void) {
    _asm {
        push ecx
        push edx
        push dword ptr [esp + 16]
        push dword ptr [esp + 16]
        call Vec_NormaliseLen
        add esp, 8
        pop edx
        pop ecx
        ret
    }
}

// The original writes neither ECX nor EDX, and its callers rely on it: Vec_Cartesian_2_Spherical keeps ECX across the call. So the
// game reaches this through an entry that keeps them (tools/caller_saved.py).
// FUNC_AT(0x000d6440)
void __declspec(naked) Asm_Atan2(void) {
    _asm {
        push ecx
        push edx
        push dword ptr [esp + 16]
        push dword ptr [esp + 16]
        call Atan2
        add esp, 8
        pop edx
        pop ecx
        ret
    }
}
