// The entries through which the original's code calls the functions in data/UData.cpp: each takes the arguments from
// the registers the original takes them in and passes them on. Only the injected build has them.
#include "../data/UData.h"

// EBX = count, [esp+4] = records **, [esp+8] = tag; EBX, ESI, EDI and EBP kept, as C++ keeps them.
// FUNC_AT(0x00117840)
void __declspec(naked) Asm_UDataFindTag(void) {
    __asm {
        mov eax, [esp + 8]
        mov edx, [esp + 4]
        push eax
        push edx
        push ebx
        call UDataFindTag
        add esp, 12
        ret
    }
}
