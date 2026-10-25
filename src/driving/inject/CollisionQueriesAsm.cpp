// The entries through which the original's code calls the functions in world/CollisionQueries.cpp: each takes the
// arguments from the registers the original takes them in and passes them on. Only the injected build has them.
#include "../world/CollisionQueries.h"

// FindFaceInTriStrip's helper: the triangle in EAX, the normal in ESI; EAX, ECX and EDX not kept (the original's
// calls do not keep them either).
// FUNC_AT(0x000beff0)
void __declspec(naked) Asm_StripTriangleNormal(void) {
    __asm {
        push esi
        push eax
        call StripTriangleNormal
        add esp, 8
        ret
    }
}
