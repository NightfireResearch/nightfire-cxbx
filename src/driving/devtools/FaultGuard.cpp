#include <windows.h>

// The loader (src/loader/loadermain.cpp) reports a fault that a devtools __try in this DLL will catch - the shadow
// tests catch hundreds a run on purpose - in one line instead of in full. It tells those faults by the SEH frames
// on the faulting thread: a frame whose handler is this DLL's __except handler, inside one of its __try blocks.
// This answers which handler that is: inside a __try, the newest SEH frame is this function's own.
extern "C" __declspec(dllexport) void *__cdecl NightfireHost_GuardHandler(void) {
    void *handler = NULL;
#ifdef _MSC_VER
    __try {
        handler = (void *)((NT_TIB *)NtCurrentTeb())->ExceptionList->Handler;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        handler = NULL;
    }
#endif
    return handler;
}
