// The entries through which the original's code calls the functions in engine/UGroup.cpp: each takes the arguments from
// the registers the original takes them in and passes them on. Only the injected build has them.
#include "../engine/UGroup.h"

// Not in a header
UData *DataLocateIndexedCall(UGroup *group, uint32_t tag, int index);

// 0x0008d6e0: EAX the tag, ESI the group, the index on the stack (the caller pops it); EAX the answer. ECX and EDX
// are not kept, as the original does not keep them.
// FUNC_AT(0x0008d6e0)
void __declspec(naked) Asm_DataLocateIndexedCall(void) {
    __asm {
        push dword ptr [esp + 4]
        push eax
        push esi
        call DataLocateIndexedCall
        add esp, 12
        ret
    }
}
