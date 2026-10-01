#include "../actionhelpers.h"
#include <stdio.h>

#define MusicEventList (*(uint32_t(*)[0x40])0x0029a180)

// AUTOINJECT
void __cdecl Music_Event(uint evtId,undefined4 val) {

  if (evtId < ARRAY_SIZE(MusicEventList)) {
    MusicEventList[evtId] = val;
  } else {
    printf("FYI::INVALID MUSIC EVENT ID = %d\n", evtId);
  }

}

// AUTOINJECT
uint32_t __cdecl Get_Music_Event(int evtId) {
  return MusicEventList[evtId];
}