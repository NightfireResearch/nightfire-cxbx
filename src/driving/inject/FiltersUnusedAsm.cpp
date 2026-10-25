// The entries through which the original's code calls the functions in sound/snd/FiltersUnused.cpp: each takes the
// arguments from the registers the original takes them in and passes them on. Only the injected build has them.
#include "../sound/snd/FiltersUnused.h"

// EAX = shift, (a, b, out, window): the crossfade
// FUNC_AT(0x00143be0)
void __declspec(naked) Asm_SndStretch_Crossfade(void) {
    __asm {
        push dword ptr [esp + 16]
        push dword ptr [esp + 16]
        push dword ptr [esp + 16]
        push dword ptr [esp + 16]
        push eax
        call SndStretch_Crossfade
        add esp, 20
        ret
    }
}

// EBX = &data, ESI = &error, (window, ratio): the next segment's shift
// FUNC_AT(0x00143e10)
void __declspec(naked) Asm_SndStretch_NextShift(void) {
    __asm {
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        push esi
        push ebx
        call SndStretch_NextShift
        add esp, 16
        ret
    }
}

// EBX = node, (&in, shift): one segment into the output buffer
// FUNC_AT(0x00143f20)
void __declspec(naked) Asm_SndStretch_Segment(void) {
    __asm {
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        push ebx
        call SndStretch_Segment
        add esp, 12
        ret
    }
}

// EDX = node, (&frames, &out): drain the output buffer
// FUNC_AT(0x00144050)
void __declspec(naked) Asm_SndStretch_Drain(void) {
    __asm {
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        push edx
        call SndStretch_Drain
        add esp, 12
        ret
    }
}
