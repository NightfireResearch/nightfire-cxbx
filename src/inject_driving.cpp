#include "inject.h"

#include "driving/logging.h"
#include "driving/UFileLoader.h"
#include "driving/main.h"
#include "common/launchInfo.h"

#include "cxbx/cxbxbinding.h"
#include <stdio.h>

#define NOP 0x90

void WriteMemory(size_t offset, void *data, size_t size)
{
  memcpy((void *) offset, data, size);
}

void WriteByte(size_t offset, unsigned char byte)
{
  WriteMemory(offset, &byte, 1);
}

void FillBytes(size_t offset, unsigned char byte, size_t count)
{
  memset((void*)offset, byte, count);
}

// Inject a new function over the top of an existing one
// This replaces the behaviour of the function, regardless of where it was called from
// This can be used on top of any function which is at least 5 bytes long
void WriteJmpTo(size_t from, size_t to)
{
  size_t relative = to - (from + 5);

  WriteByte(from, 0xE9);
  WriteMemory(from + 1, &relative, sizeof(relative));
}

// Redirect a single call to a function
// This results in a single call being rewritten as if it were to the new function
// The original function is left intact, other calls are unaffected
// This can be used on any function call which is 5 bytes long - ie any?
void WriteCall(size_t from, size_t to)
{
  size_t relative = to - (from + 5);

  WriteByte(from, 0xE8);
  WriteMemory(from + 1, &relative, sizeof(relative));
}



void* ea_malloc(int amt, char* name);
void RealClock_InterruptHandler(void);
unsigned int Scheduler_Constructor_Hook(void);
void Scheduler__Run(int i);
void EventManager__RunEvents(void);
void EventManager__Init(void);

void Inject()
{
 
  // WriteBytes(0x0005ad78, NOP, 5); // Bypass intro cutscene

  /*
   * Option 1: Launch by setting up the variables manually. This breaks for all missions with FMV!
   */

  // int a = 1;
  // int missionNum = 2; // 1: paris_mis01, 2: uw_mis11, 3: junglea_mis1, 4: jungleb_mis1, 5: snow1a_mis3, 6: snow2a_mis4, 7: junglec_mis13c, 8: snow2a_race, default: s_mapari
  // int c = 1;

  // WriteMemory(0x002444f0, &a, 4); // Makes FUN_000596a0 actually handle the next vars
  // WriteMemory(0x00244504, &missionNum, 4); // 8 has no movie
  // WriteMemory(0x00244514, &c, 4); // ??? Sets DAT_002544514, range 0-2 - difficulty?

  // WriteBytes(0x00244790, 1, 4); // Prevent the XGetLaunchInfo in 001306d0 from overwriting the above

  // int musicVol = 100;
  // int effVol = 100;
  // int audioMode = 1;

  // // Music volume
  // WriteMemory(0x002444f8, &musicVol, 4);
  // // Effect volume
  // WriteMemory(0x002444f4, &effVol, 4);
  // // Mode
  // WriteMemory(0x002445b8, &audioMode, 4);

  /*
   * Option 2: Use launch options from file
   */
  WriteJmpTo(0x0010f0db, (size_t)&XGetLaunchInfo);
  WriteJmpTo(0x0010f186, (size_t)&XLaunchNewImageA);

  // Logging goes thrugh some weird paths... 001d1bac is a table of possible outputs - console, debugger, and file
  WriteJmpTo(0x000e2e30, (size_t)&dbg_printf);
  WriteJmpTo(0x00132192, (size_t)&dbg_wprintf);
  // WriteJmpRet(0x0010e832, (size_t)&xapiDebugStringA); // UNTESTED
  WriteJmpTo(0x0010e75f, (size_t)&preMain);


  // Most of the code follows a weird indirection: At 001caf68 is a pointer to a function (at 034f50) which takes a number and a string pointer
  // This appears to be memory allocation. This indirection makes it easy to profile? but also for us to hack!
  // int addr_of_eamalloc = (int)&ea_malloc;
  // WriteMemory(0x001caf68, &addr_of_eamalloc, 4);

  // Audio debug
	//*(char*)(0x001e4760) = 1; // Mixer
	// *(char*)(0x001e4761) = 1; // Info

  // Resolution of RRenderer
  // Function 0007cfb0 sets a default 640x480

  // Some more similar stuff happens with operator_new, which goes via some pointers to another allocator, which calls another...


  //WriteJmpRet(0x005b9f0, (size_t)&RealClock_InterruptHandler);
  //WriteCall(0x0005c77b, (size_t)&Scheduler_Constructor_Hook);
  //WriteJmpRet(0x0005ba80, (size_t)&Scheduler__Run);

  // WriteByte(0x0005bb5e, 0x6); // See what that weird 12 is doing in scheduler
  //WriteBytes(0x0005ae98, 0x90, 5); // Disable whatever else runs in the hot inner loop
  // WriteBytes(0x0005aea8, 0x90, 5); // Disable scheduler run


  WriteJmpTo(0x0005a600, (size_t)&EventManager__RunEvents);
  WriteJmpTo(0x0005a550, (size_t)&EventManager__Init);

  WriteJmpTo(0x00117610, (size_t)&UFileLoader__FileLoad);

  // Binary patch - badly hack around a bug in the scheduler that causes the game to stall out.
  // this fix is terrible, vibration goes weird and animation in pause menu is bad, but it works
  // better than the stuttery lockups that happen otherwise
  WriteByte(0x05bb48, 0xb8);
  WriteByte(0x05bb49, 0x01);
  WriteByte(0x05bb4a, 0x00);
  WriteByte(0x05bb4b, 0x00);
  WriteByte(0x05bb4c, 0x00);
}
