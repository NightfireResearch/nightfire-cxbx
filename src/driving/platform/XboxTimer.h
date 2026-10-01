#ifndef DRIVING_PLATFORM_XBOXTIMER_H_
#define DRIVING_PLATFORM_XBOXTIMER_H_

#include <windows.h>
#include <mmsystem.h>

// Replaces XAPILIB::timeSetEvent (0x0010eed3) with Windows' own multimedia timer, so that the game's tick
// comes from winmm rather than from XAPI's kernel-timer thread. See XboxTimer.cpp, and section 2 of
// docs/driving-engine-plan.md. Patched in by Inject_XboxStartup, standalone only.
unsigned __stdcall Xbox_timeSetEvent(unsigned delayMs, unsigned resolutionMs,
                                     LPTIMECALLBACK callback, DWORD_PTR user, unsigned flags);

// Makes the cycle counter count at the console's 733 MHz where the game reads it: the frame-rate estimate in
// RRenderHigh::Render, EAGL's bare RDTSC helper, and XAPI's QueryPerformanceCounter/Frequency pair. The audit
// of every RDTSC site is in XboxTimer.cpp. Called from Inject_XboxStartup, standalone only.
void Inject_XboxCycleCounter(void);

#endif // DRIVING_PLATFORM_XBOXTIMER_H_
