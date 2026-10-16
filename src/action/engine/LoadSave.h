#ifndef LOADSAVE_H_
#define LOADSAVE_H_

#include "../actionhelpers.h"

// A codename's save (docs/savestate/): six labelled blocks of bit-packed fields, built from the game's settings
// and progress by LS_Save and applied back by LS_Load. Both are polled each frame from Menu_UpdateMessageBox and
// return the state they found: 0 idle, 1 loading or 2 saving, 7 done, 8 failed; LS_FlushStates resets them.

// The blocks, by their bit in a mask
#define SAVE_PLRS 0x01   // one player's control options
#define SAVE_MSSN 0x02   // missions open, best scores and Bond moments
#define SAVE_MPSG 0x04   // one player's multiplayer settings
#define SAVE_GSET 0x08   // volumes, subtitles, split-screen layout
#define SAVE_CHET 0x10   // cheats
#define SAVE_BNUS 0x20   // the bonus word

#define LS_STATE_IDLE    0
#define LS_STATE_LOADING 1
#define LS_STATE_SAVING  2
#define LS_STATE_DONE    7
#define LS_STATE_FAILED  8

#define LS_SLOTS 4
#define LS_SLOT_SIZE 0x1000

// The save being written and the last one loaded or saved, per slot, and the two states (for the shadow tests)
extern uchar DstData[LS_SLOTS][LS_SLOT_SIZE];
extern uchar InBufData[LS_SLOTS][LS_SLOT_SIZE];
extern ushort Load_State;
extern ushort Save_State;
extern ushort LoadSlot;   // the slot a load in progress is for

void LS_FlushStates(void);
// The originals take register arguments; these adaptors call the C++ cores (LoadSave.cpp)
uchar* _LS_FindBlockByLabel(ushort slot, uint label, ushort *length);
void LS_FindBlockByLabel(void);
void _LS_GetLoadStuff(ushort mask, ushort slot);
void LS_GetLoadStuff(void);
int LS_LoadFromBuffer(uchar *data, uint length);
ushort LS_GetSaveStuff(ushort mask, ushort slot);
uint LS_Save(char *codename, uint slot, ushort mask);
uint LS_Load(char *codename, ushort slot, ushort mask);

uchar *LS_MakePlrSettings(uint label, ushort slot, ushort *length);
void LS_LoadPlrSettings(uchar *block, ushort slot);
uchar *LS_MakeMission(uint label, ushort slot, ushort *length);
void LS_LoadMission(uchar *block, ushort slot);
uchar *LS_MakeMPSettings(uint label, ushort slot, ushort *length);
void LS_LoadMPSettings(uchar *block, ushort slot);
uchar *LS_MakeGlobalSettings(uint label, ushort slot, ushort *length);
void LS_LoadGlobalSettings(uchar *block, ushort slot);
uchar *LS_MakeCheats(uint label, ushort slot, ushort *length);
void LS_LoadCheats(uchar *block, ushort slot);
uchar *LS_MakeBonus(uint label, ushort slot, ushort *length);
void LS_LoadBonus(uchar *block, ushort slot);

#endif // LOADSAVE_H_
