#include "../action/helpers.h"

#define eventBytesConsumed U32_AT(0x001e47dc)
#define eventHead U32_AT(0x001e47d8)
#define eventCurrent U32_AT(0x001e47e0)
#define eventBuffer U32_AT(0x001e47d4)

typedef struct {
    void (__thiscall *dtor) (void*, bool);
} vtable_Event;

typedef struct {
    vtable_Event* vtable;
} Event;

void* FUN_00114470(size_t sz, uint param_2,char* param_3) {
    return reinterpret_cast<void* (*)(size_t, uint, char*)>(0x00114470)(sz, param_2, param_3);
}

void EventManager__Init(void)
{
  eventBuffer = (unsigned int)FUN_00114470(0x8000,0,"EventBuffer");
  eventHead = eventBuffer;
  eventBytesConsumed = eventBuffer;
  return;
}

void EventManager__RunEvents(void)
{
  Event *puVar1;
  
  puVar1 = (Event*)eventBytesConsumed;
  if (eventBytesConsumed < eventHead) {
    do {
      if (puVar1 != (Event *)0x0) {
        eventCurrent = (int)puVar1;
        (*puVar1->vtable->dtor)(puVar1, true);
        puVar1 = (Event*)eventBytesConsumed;
      }
      eventCurrent = 0;
    } while ((int)puVar1 < eventHead);
  }
  eventHead = eventBuffer;
  eventBytesConsumed = eventBuffer;
  return;
}


Event* Event__operator_new(size_t param_1) {
  eventHead += (param_1 + 0xfU & 0xfffffff0);
  return (Event*)eventHead;
}

void Event__operator_delete(undefined4 param_1, size_t param_2) {
  eventBytesConsumed += (param_2 + 0xfU & 0xfffffff0);
}
