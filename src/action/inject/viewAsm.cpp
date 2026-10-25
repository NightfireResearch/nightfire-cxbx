// The entries through which the original's code calls the functions in game/view.cpp: each takes the arguments from the
// registers the original takes them in and passes them on. Only the injected build has them.
#include "../game/view.h"

// The cel in EAX, the view on the stack (with a second argument the original does not read)
// AUTOLTCG
void __declspec(naked) Asm_View_AddObjects(void) {
    _asm {
        push dword ptr [esp + 4]
        push eax
        call View_AddObjects
        add esp, 8
        ret
    }
}

// The view in ESI
// AUTOLTCG
void __declspec(naked) Asm_View_AddCels(void) {
    _asm {
        push esi
        call View_AddCels
        add esp, 4
        ret
    }
}

// AUTOLTCG
void __declspec(naked) Asm_View_CaptureScene(void) {
    // The original function is provided with its parameter in EAX, but we need to call our reimplementation
    // which doesn't have custom calling convention
    _asm {
        push eax
        call View_CaptureScene
        add esp, 4
        ret
    }

}

// The view in EAX, the cel in ESI
// AUTOLTCG
void __declspec(naked) Asm_View_DrawCelGlist(void) {
    _asm {
        push esi
        push eax
        call View_DrawCelGlist
        add esp, 8
        ret
    }
}
