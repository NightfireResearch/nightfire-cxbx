#include "drivinghelpers.h"
#include "EventManager.hpp"

#define eventBytesConsumed U32_AT(0x001e47dc)
#define eventHead U32_AT(0x001e47d8)
#define eventCurrent U32_AT(0x001e47e0)
#define eventBuffer U32_AT(0x001e47d4)

void* FUN_00114470(size_t sz, uint param_2,const char* param_3) {
    return reinterpret_cast<void* (*)(size_t, uint,const char*)>(0x00114470)(sz, param_2, param_3);
}

// FUNC_AT(0005a550)
void EventManager__Init(void)
{
  eventBuffer = (unsigned int)FUN_00114470(0x8000,0,"EventBuffer");
  eventHead = eventBuffer;
  eventBytesConsumed = eventBuffer;
  return;
}

// FUNC_AT(0005a600)
void EventManager__RunEvents(void)
{
  Event *event;

  event = (Event*)eventBytesConsumed;
  if (eventBytesConsumed < eventHead) {
    do {
      if (event != (Event *)0x0) {
        eventCurrent = (int)event;
        // Runs the event and advances eventBytesConsumed past it - see Event::DeletingDestructor.
        event->DeletingDestructor(1);
        event = (Event*)eventBytesConsumed;
      }
      eventCurrent = 0;
    } while ((uint32_t)event < eventHead);
  }
  eventHead = eventBuffer;
  eventBytesConsumed = eventBuffer;
  return;
}


Event* Event__operator_new(size_t param_1) {
  eventHead += (param_1 + 0xfU & 0xfffffff0); // Align to 0x10 bytes
  return (Event*)eventHead;
}

void Event__operator_delete(undefined4 param_1, size_t param_2) {
  eventBytesConsumed += (param_2 + 0xfU & 0xfffffff0); // Align to 0x10 bytes
}
