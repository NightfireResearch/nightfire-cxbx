// The entries through which the original's code calls the functions in sound/snd/DecodeUnused.cpp: each takes the
// arguments from the registers the original takes them in and passes them on. Only the injected build has them.
#include "../sound/snd/DecodeUnused.h"

// 0x00146e00: EBX = out, [esp+4] = the coefficients; keeps EBX, ESI, EDI and EBP, as the original does.
// FUNC_AT(0x00146e00)
void __declspec(naked) Asm_MutLpcCoefficients(void) {
    __asm {
        push dword ptr [esp + 4]
        push ebx
        call MutLpcCoefficients
        add esp, 8
        ret
    }
}

// 0x00146f00: EAX = first output index, ESI = the state, [esp+4] = groups of 12; keeps EBX, ESI, EDI and EBP.
// FUNC_AT(0x00146f00)
void __declspec(naked) Asm_MutSynthesise(void) {
    __asm {
        push dword ptr [esp + 4]
        push esi
        push eax
        call MutSynthesise
        add esp, 12
        ret
    }
}
