// The entries through which the original's code calls the functions in sound/Sound.cpp: each takes the arguments from
// the registers the original takes them in and passes them on. Only the injected build has them.
#include "../sound/Sound.h"

// The original takes the sound in EAX, the rest on the stack
// AUTOLTCG
void __declspec(naked) Asm_Sound_Play(void) {
    _asm {
        push ecx
        push edx
        push dword ptr [esp + 36]   // ref
        push dword ptr [esp + 36]   // is3d
        push dword ptr [esp + 36]   // pitch
        push dword ptr [esp + 36]   // radiusInner
        push dword ptr [esp + 36]   // radiusOuter
        push dword ptr [esp + 36]   // volume
        push dword ptr [esp + 36]   // position
        push eax
        call Sound_Play
        add esp, 32
        pop edx
        pop ecx
        ret
    }
}

// The original takes the sound in EDI
// AUTOLTCG
void __declspec(naked) Asm_Sound_ReqestPlaySfx(void) {
    _asm {
        push ecx
        push edx
        push edi
        call Sound_ReqestPlaySfx
        add esp, 4
        pop edx
        pop ecx
        ret
    }
}
