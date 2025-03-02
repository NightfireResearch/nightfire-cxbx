#include "drivinghelpers.h"

#include "Scheduler.hpp"

#include <cstdio>

#define Clock U32_AT(0x001e5204)

void Scheduler::Run(int i) {

  unsigned int iVar2 = Clock;

  printf("Running scheduler, time is %i, schedule list is %08x\n", iVar2, this->listOfSchedules);


}