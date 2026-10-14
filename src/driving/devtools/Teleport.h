#ifndef DRIVING_DEBUG_TELEPORT_H_
#define DRIVING_DEBUG_TELEPORT_H_

// Debug teleport for the player's car: F8 records where the car is, F9 puts it back there, and a Teleport
// setting puts it there unattended and dumps the frame. See Teleport.cpp. Called once per game loop from
// Scheduler::Run, on the game's own thread, at the point where the game processes its own events.
void Teleport_Tick(void);

// NIGHTFIRE_LOCKSTEP=1 only: sets the scheduler's oneTickPerRun. From Scheduler::Run, first thing, because the flag
// must hold on every call: the scheduler's constructor and Scheduler::Reset (ERestart) clear it, and a call made
// without it simulates as many ticks as the clock has moved. Unpaced, it also moves the clock one tick.
class Scheduler;
void Teleport_BeforeRun(Scheduler *scheduler);

// NIGHTFIRE_LOCKSTEP=1 only: answers GetLocalTime (the dashboard clock) with a fixed time and returns true.
struct _SYSTEMTIME;
bool Teleport_LockstepLocalTime(struct _SYSTEMTIME *time);

// The D3D9 backend's GfxHost_PresentEvery: 0 outside lockstep. In lockstep a frame need not be shown, nor wait for
// its time: every 50th is presented (NIGHTFIRE_PRESENT_EVERY=N for every Nth; 0 presents and paces every frame).
int Teleport_LockstepPresentEvery(void);

#endif // DRIVING_DEBUG_TELEPORT_H_
