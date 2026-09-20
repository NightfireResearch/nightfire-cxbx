#ifndef DRIVING_PLATFORM_XBOXTIMER_H_
#define DRIVING_PLATFORM_XBOXTIMER_H_

#include <windows.h>
#include <mmsystem.h>

// Replaces XAPILIB::timeSetEvent (0x0010eed3) with Windows' own multimedia timer, so that the game's tick
// comes from winmm rather than from XAPI's kernel-timer thread. See XboxTimer.cpp, and section 2 of
// docs/driving-engine-plan.md. Patched in by Inject_XboxStartup, standalone only.
unsigned __stdcall Xbox_timeSetEvent(unsigned delayMs, unsigned resolutionMs,
                                     LPTIMECALLBACK callback, DWORD_PTR user, unsigned flags);

#endif // DRIVING_PLATFORM_XBOXTIMER_H_
