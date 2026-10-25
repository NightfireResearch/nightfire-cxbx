// The entries through which the original's code calls the functions in camera/CameraIniLoader.cpp: each takes the
// arguments from the registers the original takes them in and passes them on. Only the injected build has them.
#include "../camera/CameraIniLoader.h"

// FUNC_AT(0x00078590)
void __declspec(naked) Asm_CarListContains(void) {
    __asm {
        push dword ptr [esp + 4]
        push edi
        call CarListContains
        add esp, 8
        ret
    }
}
