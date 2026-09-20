#include "XboxTimer.h"
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>

// ---------------------------------------------------------------------------------------------------------------
// The tick source, which standalone becomes ours - docs/driving-engine-plan.md section 2.
//
// Timer_Init (0x0010ae50) asks for a periodic callback at the video refresh rate:
//
//     SysTimerHandle = timeSetEvent(1000 / freqHz, 0, TIMER_ontick, 0, TIME_PERIODIC);
//
// and TIMER_ontick (0x0010ae10) runs the task list that includes RealClock_InterruptHandler, which is what
// advances the game's Clock. So this one call is where the whole simulation's sense of time comes from.
//
// On the console that timeSetEvent is XAPI's own, statically linked into the XBE, and it is a small operating
// system in itself: it starts a thread (FUN_0010ece5) that initialises sixty-four kernel timer objects, waits
// on all of them with KeWaitForMultipleObjects, and dispatches callbacks as each expires - through
// KeInitializeTimerEx, KeSetTimerEx, KeQueryInterruptTime, KeSetEvent, NtSetEvent and NtPulseEvent. Under the
// standalone loader every one of those is an unimplemented kernel import, and implementing them would mean
// reproducing the Xbox's dispatcher objects and DPC semantics so that a thread we control can wait on them -
// to deliver a callback that Windows will deliver by itself.
//
// So the layer is replaced rather than the kernel underneath it. Windows has this exact API: XAPI's
// multimedia timer is Windows' multimedia timer, argument for argument and flag for flag, because that is
// where it was ported from. TIMER_ontick even ends in "ret 0x14" - five stack arguments, the shape of an
// LPTIMECALLBACK.
//
// What this changes about the plan's section 2: the jitter it blames on CXBX's timeSetEvent emulation is now
// winmm's, with nothing in between, and timeBeginPeriod(1) is what makes that a millisecond rather than the
// scheduler's 15.6 ms quantum. Whether the game's original Scheduler::Run semantics behave with a tick source
// this even is step 4 of the plan's order of work, and is a measurement to make once the engine runs - not a
// reason to change anything here.
//
// The one caller is Timer_Init, and nothing kills the timer afterwards (Timer_Active is set once and never
// cleared), so there is no timeKillEvent to match.
// ---------------------------------------------------------------------------------------------------------------

// Set once, so that the process-wide period is not raised repeatedly if the game ever re-arms the timer.
static bool g_periodRaised = false;

unsigned __stdcall Xbox_timeSetEvent(unsigned delayMs, unsigned resolutionMs,
                                     LPTIMECALLBACK callback, DWORD_PTR user, unsigned flags) {
    // The game asks for resolution 0, meaning "whatever you have". What Windows has by default is the
    // scheduler's 15.6 ms quantum, which at a 20 ms period would deliver ticks in visible pairs; asking for
    // 1 ms costs a little power and is what every game of this era did.
    if (!g_periodRaised) {
        if (timeBeginPeriod(1) == TIMERR_NOERROR)
            g_periodRaised = true;
        else
            printf("[timer] timeBeginPeriod(1) refused - ticks will be as coarse as the scheduler's quantum\n");
    }

    unsigned resolution = resolutionMs != 0 ? resolutionMs : 1;
    unsigned id = timeSetEvent(delayMs, resolution, callback, user, flags);
    if (id == 0) {
        printf("[timer] timeSetEvent(%u ms) failed - the game's clock will never advance\n", delayMs);
        fflush(stdout);
        return 0;
    }

    printf("[timer] tick every %u ms (timer %u), callback at 0x%08x\n",
           delayMs, id, (unsigned)(uintptr_t)callback);
    fflush(stdout);
    return id;
}
