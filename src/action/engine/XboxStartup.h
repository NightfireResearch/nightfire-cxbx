#ifndef ACTION_ENGINE_XBOXSTARTUP_H_
#define ACTION_ENGINE_XBOXSTARTUP_H_

#include "../../driving/platform/Win32Types.h"

// Replaces the XBE's own process startup, for the loader. See XboxStartup.cpp
// for what it keeps, what it drops and why. Shaped as a thread start routine because that is how it is
// reached - the loader's PsCreateSystemThreadEx passes it straight to Win32 CreateThread.
DWORD WINAPI mainXapiStartup(LPVOID unused);

// XAPI's last-error pair, which on the Xbox live in the XBE's own TLS block rather than the Win32 one.
DWORD __stdcall Xbox_GetLastError(void);
void __stdcall Xbox_SetLastError(DWORD error);

// The process heap, the host's, which the C runtime's malloc allocates from (CRuntime.cpp). XAPI's GetProcessHeap
// (0x000eb649) reads it.
extern HANDLE ProcessHeapHandle;

// Process initialisation: the process heap and the XAPI initialiser table, without the console-only parts.
void __stdcall Xbox_XapiInitProcess(void);

// The C runtime's per-thread data, moved off the Xbox TLS block onto a host TLS slot.
void *__cdecl Xbox_getptd(void);
void __cdecl Xbox_freeptd(void *ptd);
int __cdecl Xbox_mtinit(void);

// The XBE's entry point: starts the game's thread (the loader calls it, or the standalone build's main)
void __cdecl Xbox_Entry(void);

#ifndef NIGHTFIRE_STANDALONE
// Installs everything above, and the instruction-level patches. Called from Inject().
void Inject_XboxStartup(void);
#endif

#endif // ACTION_ENGINE_XBOXSTARTUP_H_
