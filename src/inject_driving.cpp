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

char mission[] = "mis01\0";

void Inject()
{

  int a = 1;
  int missionNum = 3; // 1: paris_mis01, 2: uw_mis11, 3: junglea_mis1, 4: jungleb_mis1, 5: snow1a_mis3, 6: snow2a_mis4, 7: junglec_mis13c, 8: snow2a_race, default: s_mapari
  int c = 1;

  WriteMemory(0x002444f0, &a, 4); // Makes FUN_000596a0 actually handle the next vars
  WriteMemory(0x00244504, &missionNum, 4); // 8 has no movie
  WriteMemory(0x00244514, &c, 4); // ??? Sets DAT_002544514, range 0-2 - difficulty?

  WriteBytes(0x00244790, 1, 4); // Prevent the XGetLaunchInfo in 001306d0 from overwriting the above
 
  WriteBytes(0x0005ad78, NOP, 5); // Bypass intro cutscene

  WriteJmpRet(0x000e2e30, (size_t)&dbg_printf);
  WriteJmpRet(0x0010e75f, (size_t)&preMain);
  //WriteJmpRet(0x0010f0db, (size_t)&getLaunchInfo);
}