#include "drivinghelpers.h"

#include "Scheduler.hpp"
#include "EventManager.hpp"
#include "devtools/Teleport.h"

#include <cstdio>

#define Clock U32_AT(0x001e5204)

// Schedule::RunTasks (0x0005bce0) is declared AUTOGEN in Schedule.hpp, so calling it reaches the original.



// AUTOINJECT
void Scheduler::Run(int i) {

  unsigned int now = Clock;

  //printf("Running scheduler, time is %i, schedule list is %08x\n", now, this->listOfSchedules);

  // If we're on the same tick, early return
//   if(this->lastTickCount == now) {
//     printf("Already ran this tick\n");
//     return;
//   }

  // TODO: Handle cinematic mode
  // TODO: Frame skipping for performance

  // Schedules are virtual: Process, slot 1 of each schedule's vtable, decides which of its buckets a tick runs
  // (see Schedule::Process). The original iterates listOfSchedules, which the Scheduler's constructor fills with
  // exactly these three and nothing else adds to; the three are named here rather than walking the vector.
  //
  // The structure is:
  // 1. Run each of the simulation schedules, for each of the 8 priorities
  // 2. Run the generated events
  // 3. Run the per-frame schedule, for each of the 8 priorities
  // 4. Run the generated events

  for(int i = 0; i < 8; i++) {

    int tickNum = this->lastTickCount;

    this->s_SimRate->Process(tickNum, i);
    this->s_halfSimRate->Process(tickNum, i);
    this->s_quarterSimRate->Process(tickNum, i);
  }

  // Debug teleport (F8/F9, or Teleport= in settings.ini): here because this is where the game's own
  // EResetPlayerCarPos event would run.
  Teleport_Tick();
  
  EventManager__RunEvents();

  for(int i = 0; i < 8; i++) {
    // Run per-frame schedules
    this->s_oncePerGameLoop->Process(this->lastTickCount, i);
  }
  
  EventManager__RunEvents();

  this->lastTickCount++;

}