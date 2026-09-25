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

  // Schedules are virtual - would need to use vtable to determine where the true Process function is. It's the second function in the vtable.
  // Process is very basic - it just calls RunTasks, but with minor tweaks to run at half or quarter speed in the case of s_halfSimRate or s_quarterSimRate

  // It looks like the structure is:
  // 1. Run each of the simulation schedules by iterating the list. The list likely just consists of (s_SimRate, s_halfSimRate, s_quarterSimRate) though! (8 times)
  // 2. Run the generated events
  // 3. Run the per-frame schedules (8 times)
  // 4. Run the generated events

  // Instead of re-implementing the list iteration and vtable lookup, we can simplify and just call RunTasks directly. Less flexible, but easier

  for(int i = 0; i < 8; i++) {

    int tickNum = this->lastTickCount;
    
    // Run SimRate
    this->s_SimRate->RunTasks(0, i);

    // Run halfSimRate
    this->s_halfSimRate->RunTasks(tickNum & 1, i);

    // Run quarterSimRate
    this->s_quarterSimRate->RunTasks(tickNum & 3, i);
  }

  // Debug teleport (F8/F9, or Teleport= in settings.ini): here because this is where the game's own
  // EResetPlayerCarPos event would run.
  Teleport_Tick();
  
  EventManager__RunEvents();

  for(int i = 0; i < 8; i++) {
    // Run per-frame schedules
    this->s_oncePerGameLoop->RunTasks(0, i);
  }
  
  EventManager__RunEvents();

  this->lastTickCount++;

}