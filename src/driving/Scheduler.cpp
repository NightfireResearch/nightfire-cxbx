#include "drivinghelpers.h"

#include "Scheduler.hpp"
#include "EventManager.hpp"
#include "devtools/Teleport.h"

#include <windows.h>
#include <cmath>
#include <cstdio>

// The game's clock: ticks at the video refresh rate (50 Hz PAL), from the timer callback - see
// src/driving/platform/XboxTimer.cpp and docs/driving-engine-plan.md, section 2.
#define Clock U32_AT(0x001e5204)

// The fraction of a simulation tick carried from one call to the next - the one deliberate departure from the
// original, below. Always in [0, 1).
static double g_pendingTicks = 0.0;

// Runs one simulation tick: every simulation schedule at every priority, then the events they raised; on the last
// tick of the call, the per-frame schedule (rendering among it) and its events too.
static void RunTick(Scheduler *scheduler, int tick, bool last, bool *teleportDone) {
    // The original iterates listOfSchedules, which the Scheduler's constructor fills with exactly these three and
    // nothing else adds to. Each schedule's own Process (slot 1 of its vtable) picks which of its buckets the tick
    // runs - every tick, tick & 1, tick & 3 - so this does not need to know which is which.
    Schedule *const simulation[] = { scheduler->s_SimRate, scheduler->s_halfSimRate, scheduler->s_quarterSimRate };
    for (int priority = 0; priority < 8; priority++)
        for (Schedule *schedule : simulation)
            schedule->Process(tick, (unsigned short)priority);

    // Debug teleport (F8/F9, or Teleport= in settings.ini): once a call, before the events, because this is where
    // the game's own EResetPlayerCarPos event runs.
    if (!*teleportDone) {
        Teleport_Tick();
        *teleportDone = true;
    }
    EventManager__RunEvents();

    if (last) {
        for (int priority = 0; priority < 8; priority++)
            scheduler->s_oncePerGameLoop->Process(tick, (unsigned short)priority);
        EventManager__RunEvents();
    }
}

// Scheduler::Run (0x0005ba80), called once per pass of the game loop. As the original:
//
//   - The simulation advances by the ticks the clock has moved since the last call, scaled by timeScale (1 normally;
//     2 with the speed cheat; whatever an ESetSimRate event sets, up to 4, for slow or fast motion), and runs them
//     one at a time. With oneTickPerRun set it advances at most one tick a call, clock or not.
//   - A step of 12 ticks or more - a stall, such as a load - is not simulated at all: the time is dropped, so the
//     game does not lurch forward to catch up.
//   - With cinematicModeSkipping set (the mission manager sets it while a cinematic is being skipped), it runs
//     ticks as fast as it can until the flag clears, with the per-frame schedule only every fourth tick.
//   - The per-frame schedule, which renders, runs only on a call that simulates at least one tick.
//
// The departure: the original truncates elapsed * timeScale to whole ticks and then sets lastTickCount to the
// clock, so the fraction is lost every time. The clock usually moves one tick per call, which makes that loss the
// whole of the effect - a timeScale of 0.75 or 0.9 ran one tick every two (half speed), 0.3 one in four, and 1.5
// no faster than 1. Here the fraction is carried to the next call instead, so a slow or fast motion runs at the
// rate it asks for. Slow motion still lowers the frame rate, as in the original, because rendering only happens on
// a call that simulates a tick; smoothing that would need interpolation between ticks.
//
// AUTOINJECT
void Scheduler::Run(int unused) {
    (void)unused;
    const int now = (int)Clock;
    bool teleportDone = false;

    if (!this->cinematicModeSkipping) {
        int elapsed = now - this->lastTickCount;
        if (this->oneTickPerRun && elapsed != 0)
            elapsed = 1;
        if (elapsed <= 0) {
            // Nothing to simulate until the clock moves (every 20 ms at 50 Hz). The console spun here - the game
            // loop has no sleep - which on a PC holds a core at 100% for nothing; a millisecond's yield changes
            // nothing about the game (winmm's 1 ms timer period makes it a millisecond).
            Sleep(1);
            return;
        }

        g_pendingTicks += elapsed * (double)this->timeScale;
        int ticks = (int)std::floor(g_pendingTicks);
        g_pendingTicks -= ticks;

        if (ticks >= 12) {
            g_pendingTicks = 0.0;   // the stall guard drops the time, fraction and all
        } else if (ticks == 0) {
            Sleep(1);   // slow motion, and not yet a whole tick: as above
        } else {
            for (int k = 1; k <= ticks; k++)
                RunTick(this, this->lastTickCount + k, k == ticks, &teleportDone);
        }
        this->lastTickCount = now;
        return;
    }

    // Skipping a cinematic: simulate flat out until the mission manager clears the flag, drawing every fourth tick.
    int tick = this->lastTickCount;
    int untilDraw = 4;
    do {
        tick++;
        bool draw = --untilDraw < 1;
        if (draw)
            untilDraw = 4;
        RunTick(this, tick, draw, &teleportDone);
    } while (this->cinematicModeSkipping);
    g_pendingTicks = 0.0;
    this->lastTickCount = now;
}
