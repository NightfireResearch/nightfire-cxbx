// The entries through which the original's code calls the functions in engine/FS.cpp: each takes the arguments from the
// registers the original takes them in and passes them on. Only the injected build has them.
#include "../engine/FS.h"

// AUTOLTCG
void __declspec(naked) Asm_FS_MatchFilenameToHeader(void) {
    // The original function is provided with its parameter in EAX, but we need to call our reimplementation
    // which doesn't have custom calling convention
    _asm {
        push eax
        call FS_MatchFilenameToHeader
        add esp, 4
        ret
    }

}
