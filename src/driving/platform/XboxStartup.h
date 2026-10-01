#ifndef DRIVING_PLATFORM_XBOXSTARTUP_H_
#define DRIVING_PLATFORM_XBOXSTARTUP_H_

#include <windows.h>

#include "../../common/standalone.h"

// The driving engine's half of the startup work described in docs/driving-engine-plan.md section 0 and step 2
// of section 7. The action engine met all of this first (src/action/engine/XboxStartup.cpp); the XAPI in both
// XBEs is the same library, so this is the same set of replacements at the driving build's addresses, and the
// reasoning is only summarised here - the action file carries it in full.

// Replaces the XBE's own process startup when the game is run by the standalone loader. Shaped as a thread
// start routine because that is how it is reached: the loader's PsCreateSystemThreadEx passes it to Win32
// CreateThread.
DWORD WINAPI mainXapiStartup(LPVOID unused);

// XAPI's last-error pair, which on the Xbox lives in the XBE's own TLS block rather than the Win32 one.
DWORD __stdcall Xbox_GetLastError(void);
void __stdcall Xbox_SetLastError(DWORD error);

// Process initialisation: the process heap and the XAPI initialiser table, without the console-only parts.
void __stdcall Xbox_XapiInitProcess(void);

// The C runtime's per-thread data, moved off the Xbox TLS block onto a Win32 TLS slot.
void *__cdecl Xbox_getptd(void);
void __cdecl Xbox_freeptd(void *ptd);
int __cdecl Xbox_mtinit(void);

// Installs everything above, and the instruction-level patches. Called from Inject(), and deliberately not
// through AUTOINJECT: that patches unconditionally, and none of this may touch a CXBX-hosted run.
void Inject_XboxStartup(void);

#endif // DRIVING_PLATFORM_XBOXSTARTUP_H_
