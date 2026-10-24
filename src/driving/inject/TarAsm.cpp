// The entries through which the original's code calls the functions in eagl/Tar.cpp: each takes the arguments from the
// registers the original takes them in and passes them on. Only the injected build has them.
#include "../eagl/Tar.h"

// The image count of a SHPX file in EAX.
// FUNC_AT(0x000ede20)
void __declspec(naked) Asm_EAGL_ShapeCount(void) {
    __asm {
        mov eax, dword ptr [eax + 8]
        ret
    }
}

// Image EAX of the SHPX file in ECX.
// FUNC_AT(0x000ede30)
void __declspec(naked) Asm_EAGL_ShapeImage(void) {
    __asm {
        mov eax, dword ptr [ecx + eax * 8 + 0x14]
        add eax, ecx
        ret
    }
}
