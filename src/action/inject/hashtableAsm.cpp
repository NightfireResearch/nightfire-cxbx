// The entries through which the original's code calls the functions in util/hashtable.cpp: each takes the arguments
// from the registers the original takes them in and passes them on. Only the injected build has them.
#include "../util/hashtable.h"

// The original (0x0006b370) takes its hashcode in EDI; no original calls it now. It clobbers only EAX, ECX and EDX (it saves ESI
// itself), and neither caller keeps anything in ECX or EDX across the call, so a plain cdecl call is enough: our
// compiled body preserves EBX, ESI, EDI and EBP.
// AUTOLTCG
void __declspec(naked) Asm_hashtable_getentry(void) {
    _asm {
        push edi
        call hashtable_getentry
        add esp, 4
        ret
    }
}
