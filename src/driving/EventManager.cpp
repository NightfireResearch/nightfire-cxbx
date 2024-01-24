#include "../action/helpers.h"

#define eventBytesConsumed U32_AT(0x001e47dc)
#define eventHead U32_AT(0x001e47d8)
#define DAT_001e47e0 U32_AT(0x001e47e0)
#define DAT_001e47d4 U32_AT(0x001e47d4)

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
  DAT_001e47d4 = (unsigned int)FUN_00114470(0x8000,0,"EventBuffer");
  eventHead = DAT_001e47d4;
  eventBytesConsumed = DAT_001e47d4;
  return;
}

void EventManager__RunEvents(void)
{
  Event *puVar1;
  
  puVar1 = (Event*)eventBytesConsumed;
  if (eventBytesConsumed < eventHead) {
    do {
      if (puVar1 != (Event *)0x0) {
        DAT_001e47e0 = (int)puVar1;
        (*puVar1->vtable->dtor)(puVar1, true);
        puVar1 = (Event*)eventBytesConsumed;
      }
      DAT_001e47e0 = 0;
    } while ((int)puVar1 < eventHead);
  }
  eventHead = DAT_001e47d4;
  eventBytesConsumed = DAT_001e47d4;
  return;
}
