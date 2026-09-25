#ifndef DRIVING_DEBUG_TELEPORT_H_
#define DRIVING_DEBUG_TELEPORT_H_

// Debug teleport for the player's car: F8 records where the car is, F9 puts it back there, and a Teleport
// setting puts it there unattended and dumps the frame. See Teleport.cpp. Called once per game loop from
// Scheduler::Run, on the game's own thread, at the point where the game processes its own events.
void Teleport_Tick(void);

#endif // DRIVING_DEBUG_TELEPORT_H_
