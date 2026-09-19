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

// Patches out XAPI's allocation-notification hook, which reads the Xbox KPCR through FS:[0x20]. Called from
// Inject(), because it rewrites instructions rather than replacing whole functions.
void Inject_XboxStartup(void);

#endif // ACTION_ENGINE_XBOXSTARTUP_H_
