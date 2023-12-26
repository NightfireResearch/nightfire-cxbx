#include "inject.h"

#include "driving/logging.h"
#include "driving/launchInfo.h"

#include "cxbx/cxbxbinding.h"

#define NOP 0x90

void WriteMemory(size_t offset, void *data, size_t size)
{
  memcpy((void *) offset, data, size);
}

void WriteByte(size_t offset, unsigned char byte)
{
  WriteMemory(offset, &byte, 1);
}

void WriteBytes(size_t offset, unsigned char byte, size_t count)
{
  for (size_t i = 0; i < count; i++) {
  	WriteByte(offset + i, byte);
  }
}

void WriteJmpRet(size_t from, size_t to)
{
  size_t relative = to - (from + 5);

  WriteByte(from, 0xE9);
  WriteMemory(from + 1, &relative, sizeof(relative));
}

int preMain(int argc, char* argv[]);
void* ea_malloc(int amt, char* name);

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
  WriteJmpRet(0x0010f0db, (size_t)&getLaunchInfo);

  // Logging goes thrugh some weird paths... 001d1bac is a table of possible outputs - console, debugger, and file
  WriteJmpRet(0x000e2e30, (size_t)&dbg_printf);
  //WriteJmpRet(0x0010e832, (size_t)&xapiDebugStringA); // UNTESTED
  WriteJmpRet(0x0010e75f, (size_t)&preMain);


  // Most of the code follows a weird indirection: At 001caf68 is a pointer to a function (at 034f50) which takes a number and a string pointer
  // This appears to be memory allocation. This indirection makes it easy to profile? but also for us to hack!
  int addr_of_eamalloc = (int)&ea_malloc;
  WriteMemory(0x001caf68, &addr_of_eamalloc, 4);

  // Some more similar stuff happens with operator_new, which goes via some pointers to another allocator, which calls another...

}