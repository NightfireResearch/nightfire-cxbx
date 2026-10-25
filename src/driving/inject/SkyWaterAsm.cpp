// The entries through which the original's code calls the functions in render/SkyWater.cpp: each takes the arguments
// from the registers the original takes them in and passes them on. Only the injected build has them.
#include "../render/SkyWater.h"

// EAX a, the rest on the stack
// FUNC_AT(0x000a87e0)
void __declspec(naked) Asm_QueueBrokenWindowQuad(void) {
    __asm {
        push dword ptr [esp + 0x18]
        push dword ptr [esp + 0x18]
        push dword ptr [esp + 0x18]
        push dword ptr [esp + 0x18]
        push dword ptr [esp + 0x18]
        push dword ptr [esp + 0x18]
        push eax
        call QueueBrokenWindowQuad
        add esp, 0x1c
        ret
    }
}
