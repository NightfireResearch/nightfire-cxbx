#ifndef ACTION_ENGINE_XBOXSTARTUP_H_
#define ACTION_ENGINE_XBOXSTARTUP_H_

#include <windows.h>

// Replaces the XBE's own process startup when the game is run by the standalone loader. See XboxStartup.cpp
// for what it keeps, what it drops and why. Shaped as a thread start routine because that is how it is
// reached - the loader's PsCreateSystemThreadEx passes it straight to Win32 CreateThread.
DWORD WINAPI mainXapiStartup(LPVOID unused);

// XAPI's last-error pair, which on the Xbox live in the XBE's own TLS block rather than the Win32 one.
DWORD __stdcall Xbox_GetLastError(void);
void __stdcall Xbox_SetLastError(DWORD error);

// Process initialisation: the process heap and the XAPI initialiser table, without the console-only parts.
void __stdcall Xbox_XapiInitProcess(void);

// The C runtime's per-thread data, moved off the Xbox TLS block onto a Win32 TLS slot.
void *__cdecl Xbox_getptd(void);
void __cdecl Xbox_freeptd(void *ptd);
int __cdecl Xbox_mtinit(void);

// True when no emulator is hosting this process - that is, when the standalone loader is running the game and
// the XBE's own libraries are the only implementation there is. Everything in this file is conditional on it,
// because under CXBX the same code is already replaced by CXBX's patches and must be left exactly as it is.
bool Xbox_RunningStandalone(void);

// Installs everything above, and the instruction-level patches. Called from Inject(). None of it is done
// through AUTOINJECT or FUNC_AT, because those patch unconditionally and these must not touch a CXBX-hosted
// run at all.
void Inject_XboxStartup(void);

#endif // ACTION_ENGINE_XBOXSTARTUP_H_
