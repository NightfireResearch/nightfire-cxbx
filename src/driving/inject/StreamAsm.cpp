// The entries through which the original's code calls the functions in sound/snd/Stream.cpp: each takes the arguments
// from the registers the original takes them in and passes them on. Only the injected build has them.
#include "../sound/snd/Stream.h"

// ESI = s, (bytes): bytes released; greedy again when that drops below the level
// FUNC_AT(0x0014aba0)
void __declspec(naked) Asm_SndStream_ReleaseBytes(void) {
    __asm {
        push dword ptr [esp + 4]
        push esi
        call SndStream_ReleaseBytes
        add esp, 8
        ret
    }
}

// EDI = s -> EAX: pop a free request, new generation in its id
// FUNC_AT(0x0014ac00)
void __declspec(naked) Asm_SndStream_PopFree(void) {
    __asm {
        push edi
        call SndStream_PopFree
        add esp, 4
        ret
    }
}

// EDI = s, ESI = request: append to the queue as queued
// FUNC_AT(0x0014ac70)
void __declspec(naked) Asm_SndStream_Append(void) {
    __asm {
        push esi
        push edi
        call SndStream_Append
        add esp, 8
        ret
    }
}

// EAX = request, ECX = s: unlink, back to the free list (EAX, ECX survive)
// FUNC_AT(0x0014acc0)
void __declspec(naked) Asm_SndStream_FreeRequest(void) {
    __asm {
        push eax
        push ecx
        push ecx
        push eax
        call SndStream_FreeRequest
        add esp, 8
        pop ecx
        pop eax
        ret
    }
}

// EAX = s -> EAX: deliver the chunks read; 1 = the request's end tag was delivered
// FUNC_AT(0x0014ad20)
void __declspec(naked) Asm_SndStream_Deliver(void) {
    __asm {
        push eax
        call SndStream_Deliver
        add esp, 4
        ret
    }
}

// ESI = s, (priority): start the next request: read on, open, or close and reopen
// FUNC_AT(0x0014af50)
void __declspec(naked) Asm_SndStream_NextRequest(void) {
    __asm {
        push dword ptr [esp + 4]
        push esi
        call SndStream_NextRequest
        add esp, 8
        ret
    }
}
