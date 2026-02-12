// The lower-level C functions for sound
#include "SND.h"

#define SNDDRV_shouldContinueRunning U8_AT(0x00244c3d)
#define SNDDRV_isRunning U8_AT(0x00244c3c)

// AUTOGEN
void SNDI_mutexlock(void);
// AUTOGEN
void SNDI_mutexunlock(void);
// AUTOGEN
void SleepMilliseconds(int millis);


// AUTOINJECT
undefined4 SNDDRV_thread(void) {
  int iVar1;
  
  if (SNDDRV_shouldContinueRunning) {
    do {
      SNDI_mutexlock();
    //   if (DAT_00244fb8 != (code *)0x0) {
    //     (*DAT_00244fb8)();
    //   }
    //   FUN_0013b7b0();
    //   FUN_0013d820();
    //   FUN_0013efd0();
    //   if (DAT_00244fbc != (code *)0x0) {
    //     (*DAT_00244fbc)();
    //   }
      SNDI_mutexunlock();
    //   iVar1 = FUN_0010e1e0();
    //   iVar1 = DAT_00244c38 - iVar1;
    //   DAT_00244c38 = DAT_00244c38 + 10;
    //   if (iVar1 < 0) {
    //     iVar1 = 1;
    //   }
      SleepMilliseconds(iVar1);
    } while (SNDDRV_shouldContinueRunning != false);
  }
  SNDDRV_isRunning = false;
  return 0;
}
