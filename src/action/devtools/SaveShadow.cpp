// Shadow tests for the codename save (src/action/engine/LoadSave.cpp) and the bit functions under it
// (BIN_PushBits / BIN_PullBits*, src/action/util/bin.cpp). Each case runs the original and ours from the same state
// and compares what they leave and return:
//
// - the bit functions over random buffers, cursors, values and widths 0-32;
// - each block's make function over random settings (every slot), comparing the block and its length, and its
//   load over random blocks and the blocks of a real save, comparing everything the load writes and the calls it
//   makes;
// - LS_GetSaveStuff over every mask and slot on several crafted lists of blocks, LS_Save and LS_Load as polled
//   state machines from each starting state, with saves that succeed or fail, LS_LoadFromBuffer, and the two
//   register-argument adaptors (LS_FindBlockByLabel, LS_GetLoadStuff) called as the original callers call them.
//
// The functions the save calls are pointed at logging fakes (the missions, the scores, the bonus, the volumes, the
// language, psiSaveData / psiLoadData with a made-up file), so this runs at injection time with no game. The
// originals use the game's buffers and states (DstData, InBufData...), ours their own: each case starts both from
// the same contents and compares the two. Run at start with MenuShadowTests=on (MenuProbe.cpp); everything is put
// back afterwards.

#include "SaveShadow.h"
#include "AllOriginal.h"

#include "ShadowHarness.h"
#include "../../common/xbeOriginal.h"
#include "../engine/LoadSave.h"
#include "../engine/psiSave.h"
#include "../engine/Text.h"
#include "../game.h"
#include "../input.h"
#include "../game/mp/multiplayer.h"
#include "../game/sp/PlayerStats.h"
#include "../ui/Menu.h"

#include <stdio.h>
#include <string.h>

#include <map>
#include <string>
#include <vector>

namespace {

const unsigned kBinFirst = 0x0006b1d0, kBinEnd = 0x0006b370;
const unsigned kLsFirst = 0x0006eb20, kLsEnd = 0x0006fa30;

const unsigned kPushBits = 0x0006b1d0, kPullBits = 0x0006b250, kPullU32 = 0x0006b2e0, kPullU8 = 0x0006b310,
               kPullU16 = 0x0006b340;
const unsigned kFindBlock = 0x0006eb30, kLoadFromBuffer = 0x0006ebb0, kGetSaveStuff = 0x0006ec10,
               kGetLoadStuff = 0x0006ecd0, kSave = 0x0006f880, kLoad = 0x0006f960, kFlushStates = 0x0006eb20;

struct BlockFunctions {
    const char *label;
    unsigned make, load;
    uchar *(*ourMake)(uint, ushort, ushort *);
    void (*ourLoad)(uchar *, ushort);
};
const BlockFunctions kBlocks[6] = {
    {"PLRS", 0x0006ed30, 0x0006eee0, LS_MakePlrSettings, LS_LoadPlrSettings},
    {"MSSN", 0x0006f110, 0x0006f230, LS_MakeMission, LS_LoadMission},
    {"MPSG", 0x0006f450, 0x0006f500, LS_MakeMPSettings, LS_LoadMPSettings},
    {"GSET", 0x0006f550, 0x0006f720, LS_MakeGlobalSettings, LS_LoadGlobalSettings},
    {"CHET", 0x0006f000, 0x0006f0c0, LS_MakeCheats, LS_LoadCheats},
    {"BNUS", 0x0006f340, 0x0006f3f0, LS_MakeBonus, LS_LoadBonus},
};

typedef void(__cdecl *PushFn)(uchar *, uint *, uint, uchar);
typedef void(__cdecl *PullFn)(uchar *, uint *, void *, uchar);
typedef uchar *(__cdecl *MakeFn)(uint, ushort, ushort *);
typedef void(__cdecl *LoadFn)(uchar *, ushort);
typedef ushort(__cdecl *GetSaveStuffFn)(ushort, ushort);
typedef uint(__cdecl *SaveFn)(char *, uint, ushort);
typedef uint(__cdecl *LoadSaveFn)(char *, ushort, ushort);
typedef int(__cdecl *LoadFromBufferFn)(uchar *, uint);

// The game's copies of what LoadSave.cpp owns
uchar (*const GameDstData)[LS_SLOT_SIZE] = (uchar(*)[LS_SLOT_SIZE])XbeOurs(0x00215790);
uchar (*const GameInBufData)[LS_SLOT_SIZE] = (uchar(*)[LS_SLOT_SIZE])XbeOurs(0x00219790);
#define GameLoadState U16_AT(XbeOurs(0x0021d790))
#define GameSaveState U16_AT(XbeOurs(0x0021d794))
#define GameLoadSlot U16_AT(XbeOurs(0x002237b4))

// A save as the game wrote one (docs/savestate/blocks.md)
const uchar kBondSave[166] = {
    0x50, 0x4c, 0x52, 0x53, 0x0b, 0x00, 0x00, 0x00, 0x00, 0xf2, 0x01, 0x4d, 0x53, 0x53, 0x4e, 0x44, 0x00, 0x00, 0x00,
    0xff, 0x0f, 0x00, 0x00, 0x0c, 0x52, 0x53, 0x03, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x6e, 0x51, 0x03, 0x00, 0x02,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x70, 0x48,
    0x31, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x58, 0x3c, 0x00, 0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x4d, 0x50, 0x53, 0x47, 0x0d, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x47, 0x53, 0x45,
    0x54, 0x31, 0x00, 0x00, 0x00, 0x4b, 0x32, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x43, 0x48, 0x45, 0x54, 0x09, 0x00, 0x00, 0x00, 0x06, 0x42, 0x4e,
    0x55, 0x53, 0x10, 0x00, 0x00, 0x00, 0x01, 0x08, 0x00, 0x01, 0x08, 0x00, 0x00, 0x49,
};
const uint kBondBlockAt[6] = {0, 11, 79, 92, 141, 150};   // where each block of kBondSave starts
const uint kBondBlockLength[6] = {11, 68, 13, 49, 9, 16};

// Every original of the save and the bit functions in place, for one run of theirs
struct Originals {
    Originals() {
        XbeOriginal_RestoreRange(kBinFirst, kBinEnd, true);
        XbeOriginal_RestoreRange(kLsFirst, kLsEnd, true);
    }
    ~Originals() {
        XbeOriginal_RestoreRange(kLsFirst, kLsEnd, false);
        XbeOriginal_RestoreRange(kBinFirst, kBinEnd, false);
    }
};

int g_cases, g_differ;

constexpr uint Label(char a, char b, char c, char d) {
    return (uint)(uchar)a | (uint)(uchar)b << 8 | (uint)(uchar)c << 16 | (uint)(uchar)d << 24;
}

void Report(const char *what, int trial, const char *detail) {
    g_differ++;
    static std::map<std::string, int> reported;
    if (reported[what]++ < 3)
        printf("[saveshadow] %s case %d: %s\n", what, trial, detail);
}

// ---- the fakes

struct Fake {
    uint status;            // Menu_GetNightfireStatus
    uint scoreCount;        // PlrStats_GetScoreTable
    SCORETABLE scores[12];
    ScoreCategory categories[12][SCORE_NUM_CATEGORIES];
    uint64_t bonus;         // Menu_GetBonus
    uint language, music, sfx, mode;
    bool saveWorks, loadWorks;
    uchar file[LS_SLOT_SIZE];   // what psiLoadData reads
    uint fileSize;
} fake;

uint __cdecl FakeGetNightfireStatus(void) { return fake.status; }
void __cdecl FakeSetNightfireStatus(uint status) { ShadowLog("setstatus(%x)", status); }

SCORETABLE *__cdecl FakeGetScoreTable(uint *count) {
    *count = fake.scoreCount;
    return fake.scores;
}

void __cdecl FakeSetupScoreTable(SCORETABLE *tables, uint count) {
    ShadowLog("setupscores(%u)", count);
    for (uint i = 0; i < count; i++) {
        const SCORETABLE *t = &tables[i];
        ShadowLog("[%x %x %x %x %x %x %x %x %x %x %x %x %x]", t->levelHashcode, t->thresholdScoreBronze,
                  t->thresholdScoreSilver, t->thresholdScoreGold, t->thresholdScorePlatinum, *(uint *)&t->detectionPenalty,
                  t->timeTaken, t->parTime, t->difficultyMultiplier, t->difficultyBonus, t->baseScore, t->bestScore,
                  t->score);
        if (t->statsTable == NULL) {
            ShadowLog("nostats");
            continue;
        }
        for (int c = 0; c < SCORE_NUM_CATEGORIES; c++) {
            const ScoreCategory *k = &t->statsTable[c];
            ShadowLog("%x,%x,%x,%x,%x", k->target, k->maxPoints, k->achieved, *(uint *)&k->rating, *(uint *)&k->points);
        }
    }
}

uint64_t __cdecl FakeGetBonus(byte player) {
    ShadowLog("getbonus(%u)", player);
    return fake.bonus;
}

void __cdecl FakeSetBonus(uint lo, uint hi, byte player, char orIn) {
    ShadowLog("setbonus(%x,%x,%u,%d)", lo, hi, player, orIn);
}

uint __cdecl FakeGetLanguage(void) { return fake.language; }
int __cdecl FakeMusicGetVolume(void) { return fake.music; }
uint __cdecl FakeSfxGetVolume(void) { return fake.sfx; }
uint __cdecl FakeSfxGetMode(void) { return fake.mode; }
void __cdecl FakeMusicSetVolume(int volume) { ShadowLog("music(%d)", volume); }
void __cdecl FakeSfxSetVolume(int volume) { ShadowLog("sfx(%d)", volume); }

uint32_t Hash(const uchar *data, uint size) {
    uint32_t h = 2166136261u;
    for (uint i = 0; i < size; i++)
        h = (h ^ data[i]) * 16777619u;
    return h;
}

undefined4 __cdecl FakeSaveData(void *data, uint32_t size, const char *name) {
    ShadowLog("save(%u,%08x,%s)", size, Hash((const uchar *)data, size), name);
    psiInternalLoadingDataState = fake.saveWorks ? 4 : 5;
    return 1;
}

undefined4 __cdecl FakeLoadData(void *buffer, uint32_t *size, const char *name) {
    ShadowLog("load(%u,%s)", *size, name);
    memset(buffer, 0, *size);
    memcpy(buffer, fake.file, fake.fileSize < *size ? fake.fileSize : *size);
    psiInternalLoadingDataState = fake.loadWorks ? 1 : 3;
    return 1;
}

// A port of ours is hooked where our code calls it too, not only behind the original's entry
void HookOurs(unsigned original, const void *ours, const void *fake) {
    ShadowHook_Install(original, fake);
    if (XbeOriginal_Target(original) != (unsigned)(uintptr_t)ours)
        ShadowHook_Install((unsigned)(uintptr_t)ours, fake);
}

void InstallFakes(void) {
    HookOurs(0x00076240, (const void *)&Menu_GetNightfireStatus, (const void *)&FakeGetNightfireStatus);
    HookOurs(0x000762d0, (const void *)&Menu_SetNightfireStatus, (const void *)&FakeSetNightfireStatus);
    HookOurs(0x000b1230, (const void *)&PlrStats_GetScoreTable, (const void *)&FakeGetScoreTable);
    HookOurs(0x000b1530, (const void *)&PlrStats_SetupScoreTable, (const void *)&FakeSetupScoreTable);
    ShadowHook_Install(0x0007c660, (const void *)&FakeGetBonus);
    HookOurs(0x0007cb10, (const void *)&Menu_SetBonus, (const void *)&FakeSetBonus);
    HookOurs(0x0006d130, (const void *)&GetLanguage, (const void *)&FakeGetLanguage);
    ShadowHook_Install(0x000c6360, (const void *)&FakeMusicGetVolume);
    ShadowHook_Install(0x000c6350, (const void *)&FakeMusicSetVolume);
    ShadowHook_Install(0x000c5090, (const void *)&FakeSfxGetVolume);
    ShadowHook_Install(0x000c5060, (const void *)&FakeSfxSetVolume);
    ShadowHook_Install(0x000c5430, (const void *)&FakeSfxGetMode);
    HookOurs(0x000dfe10, (const void *)&psiSaveData, (const void *)&FakeSaveData);
    HookOurs(0x000dfbe0, (const void *)&psiLoadData, (const void *)&FakeLoadData);
}

void RandomBytes(void *at, size_t size) {
    for (size_t i = 0; i < size; i++)
        *(uchar *)(size_t)XbeOurs((unsigned)(size_t)at + (unsigned)i) = (uchar)ShadowRand(256);
}

uint RandomWord(void) {
    return ShadowRand(0x10000) | ShadowRand(0x10000) << 16;
}

// The settings the blocks are made from and loaded into
void AddSettings(ShadowRegions &regions) {
    regions.Add(PlayerInputs, 4 * sizeof(PlayerInput));
    regions.Add(MPSettings.Player, sizeof(MPSettings.Player));
    regions.Add((void *)0x001f65dc, 0xc);   // CheatInfo: immortal, all weapons, unlimited ammo
    regions.Add((void *)0x001f660c, 0xc);   // the split-screen layout, widescreen, subtitles
    regions.Add(&GameState.WeaponUpgradeRelated, 1);
    regions.Add((void *)XbeOurs(0x002adf10), 4);     // psiInternalLoadingDataState
}

void RandomSettings(void) {
    RandomBytes(PlayerInputs, 4 * sizeof(PlayerInput));
    RandomBytes(MPSettings.Player, sizeof(MPSettings.Player));
    RandomBytes((void *)0x001f65dc, 0xc);
    RandomBytes((void *)0x001f660c, 0xc);
    GameState.WeaponUpgradeRelated = (uchar)ShadowRand(256);
    fake.status = RandomWord();
    fake.scoreCount = ShadowRand(13);
    for (int i = 0; i < 12; i++) {
        fake.scores[i].bestScore = RandomWord();
        fake.scores[i].statsTable = fake.categories[i];
        fake.categories[i][SCORE_BOND_MOMENTS].achieved = RandomWord();
    }
    fake.bonus = (uint64_t)RandomWord() << 32 | RandomWord();
    fake.language = RandomWord();
    fake.music = RandomWord();
    fake.sfx = RandomWord();
    fake.mode = RandomWord();
}

// ---- the bit functions

void BinCases(void) {
    for (int trial = 0; trial < 4000; trial++) {
        uchar start[24];
        RandomBytes(start, sizeof(start));
        uint cursor = ShadowRand(64);
        uchar width = (uchar)ShadowRand(33);
        uint value = RandomWord();
        int kind = trial % 5;

        uchar theirs[24], ours[24];
        memcpy(theirs, start, sizeof(start));
        memcpy(ours, start, sizeof(start));
        uchar theirOut[16], ourOut[16];
        RandomBytes(theirOut, sizeof(theirOut));
        memcpy(ourOut, theirOut, sizeof(ourOut));
        uint theirCursor = cursor, ourCursor = cursor;
        {
            Originals originals;
            switch (kind) {
            case 0: ((PushFn)kPushBits)(theirs, &theirCursor, value, width); break;
            case 1: ((PullFn)kPullBits)(theirs, &theirCursor, theirOut, width); break;
            case 2: ((PullFn)kPullU32)(theirs, &theirCursor, theirOut, width); break;
            case 3: ((PullFn)kPullU8)(theirs, &theirCursor, theirOut, width); break;
            case 4: ((PullFn)kPullU16)(theirs, &theirCursor, theirOut, width); break;
            }
        }
        switch (kind) {
        case 0: BIN_PushBits(ours, &ourCursor, value, width); break;
        case 1: BIN_PullBits(ours, &ourCursor, (int64_t *)ourOut, width); break;
        case 2: BIN_PullBits_U32(ours, &ourCursor, (uint *)ourOut, width); break;
        case 3: BIN_PullBits_U8(ours, &ourCursor, ourOut, width); break;
        case 4: BIN_PullBits_U16(ours, &ourCursor, (ushort *)ourOut, width); break;
        }
        static const char *const names[5] = {"BIN_PushBits", "BIN_PullBits", "BIN_PullBits_U32", "BIN_PullBits_U8",
                                              "BIN_PullBits_U16"};
        g_cases++;
        char detail[96];
        if (memcmp(theirs, ours, sizeof(ours)) != 0 || memcmp(theirOut, ourOut, sizeof(ourOut)) != 0 ||
            theirCursor != ourCursor) {
            snprintf(detail, sizeof(detail), "cursor %u width %u value %08x: result differs", cursor, width, value);
            Report(names[kind], trial, detail);
        }
    }
}

// ---- the blocks

void MakeCases(void) {
    for (int trial = 0; trial < 300; trial++) {
        RandomSettings();
        for (int b = 0; b < 6; b++)
            for (ushort slot = 0; slot < 4; slot++) {
                uint label = RandomWord();
                ushort theirLength = 0x5a5a, ourLength = 0x5a5a;
                uchar *theirBlock, *ourBlock;
                ShadowLog_Clear();
                {
                    Originals originals;
                    theirBlock = ((MakeFn)kBlocks[b].make)(label, slot, &theirLength);
                }
                std::string theirLog = ShadowLog_Text();
                ShadowLog_Clear();
                ourBlock = kBlocks[b].ourMake(label, slot, &ourLength);
                g_cases++;
                // the whole of each buffer that a block can reach, so the bits after the last field count too
                if (theirLength != ourLength || memcmp(theirBlock, ourBlock, 0x60) != 0 || theirLog != ShadowLog_Text()) {
                    char detail[96];
                    snprintf(detail, sizeof(detail), "%s make, slot %u: length %u, the original %u, or the block differs",
                             kBlocks[b].label, slot, ourLength, theirLength);
                    Report("make", trial, detail);
                }
            }
    }
}

uchar g_block[0x100];
ushort g_slot;
int g_loadBlock;
uint32_t TheirLoad(void) {
    Originals originals;
    ((LoadFn)kBlocks[g_loadBlock].load)(g_block, g_slot);
    return 0;
}
uint32_t OurLoad(void) {
    kBlocks[g_loadBlock].ourLoad(g_block, g_slot);
    return 0;
}

void LoadCases(void) {
    ShadowRegions regions;
    AddSettings(regions);
    for (int trial = 0; trial < 400; trial++) {
        RandomSettings();
        for (int b = 0; b < 6; b++)
            for (g_slot = 0; g_slot < 4; g_slot++) {
                g_loadBlock = b;
                if (trial == 0) {
                    memset(g_block, 0, sizeof(g_block));
                    memcpy(g_block, kBondSave + kBondBlockAt[b], kBondBlockLength[b]);
                } else {
                    RandomBytes(g_block, sizeof(g_block));
                    if (b == 1)
                        g_block[12] = (uchar)ShadowRand(13);   // the level count: no more than the score tables hold
                }
                g_cases++;
                if (!ShadowCompare(regions, "saveshadow", kBlocks[b].label, trial, TheirLoad, OurLoad))
                    g_differ++;
            }
    }
}

// ---- the drivers

// The state owned twice: the originals' copy in the game and ours
struct Owned {
    uchar dst[LS_SLOTS][LS_SLOT_SIZE], in[LS_SLOTS][LS_SLOT_SIZE];
    ushort loadState, saveState, loadSlot;

    void FromGame() {
        memcpy(dst, GameDstData, sizeof(dst));
        memcpy(in, GameInBufData, sizeof(in));
        loadState = GameLoadState;
        saveState = GameSaveState;
        loadSlot = GameLoadSlot;
    }
    void FromOurs() {
        memcpy(dst, DstData, sizeof(dst));
        memcpy(in, InBufData, sizeof(in));
        loadState = Load_State;
        saveState = Save_State;
        loadSlot = LoadSlot;
    }
    void ToBoth() const {
        memcpy(GameDstData, dst, sizeof(dst));
        memcpy(DstData, dst, sizeof(dst));
        memcpy(GameInBufData, in, sizeof(in));
        memcpy(InBufData, in, sizeof(in));
        GameLoadState = Load_State = loadState;
        GameSaveState = Save_State = saveState;
        GameLoadSlot = LoadSlot = loadSlot;
    }
    bool Same(const Owned &o, char *detail, size_t size) const {
        for (int s = 0; s < LS_SLOTS; s++) {
            if (memcmp(dst[s], o.dst[s], LS_SLOT_SIZE) != 0) {
                snprintf(detail, size, "DstData[%d] differs", s);
                return false;
            }
            if (memcmp(in[s], o.in[s], LS_SLOT_SIZE) != 0) {
                snprintf(detail, size, "InBufData[%d] differs", s);
                return false;
            }
        }
        if (loadState != o.loadState || saveState != o.saveState || loadSlot != o.loadSlot) {
            snprintf(detail, size, "states differ: load %u/%u save %u/%u slot %u/%u", loadState, o.loadState, saveState,
                     o.saveState, loadSlot, o.loadSlot);
            return false;
        }
        return true;
    }
};

Owned g_start, g_theirs, g_ours;
ShadowRegions g_settings;

// Runs one case both ways from g_start and the current settings, and compares
template <class Theirs, class Ours>
void Driver(const char *what, int trial, Theirs theirs, Ours ours) {
    g_settings.Save();
    g_start.ToBoth();
    ShadowLog_Clear();
    std::vector<uint32_t> theirResults;
    {
        Originals originals;
        theirs(theirResults);
    }
    std::string theirLog = ShadowLog_Text();
    g_theirs.FromGame();
    std::vector<uint8_t> theirSettings, ourSettings;
    g_settings.Capture(theirSettings);

    g_settings.Restore();
    g_start.ToBoth();
    ShadowLog_Clear();
    std::vector<uint32_t> ourResults;
    ours(ourResults);
    g_ours.FromOurs();
    g_settings.Capture(ourSettings);

    g_cases++;
    char detail[160];
    if (theirResults != ourResults) {
        snprintf(detail, sizeof(detail), "returns differ (first: ours %x, the original %x)",
                 ourResults.empty() ? 0 : ourResults[0], theirResults.empty() ? 0 : theirResults[0]);
        Report(what, trial, detail);
    } else if (!g_theirs.Same(g_ours, detail, sizeof(detail))) {
        Report(what, trial, detail);
    } else if (theirSettings != ourSettings) {
        Report(what, trial, "the settings differ");
    } else if (theirLog != ShadowLog_Text()) {
        snprintf(detail, sizeof(detail), "calls differ: ours %.60s / the original %.60s", ShadowLog_Text().c_str(),
                 theirLog.c_str());
        Report(what, trial, detail);
    }
}

// A slot's last save, made up: the real one, nothing, blocks reordered with junk after an end, some missing, a list
// that runs to the end of the slot, random bytes after a valid start
void MakeList(uchar *slot, int kind) {
    memset(slot, 0, LS_SLOT_SIZE);
    switch (kind) {
    case 0:
        memcpy(slot, kBondSave, sizeof(kBondSave));
        break;
    case 1:
        break;
    case 2: {
        uint at = 0;
        static const int order[6] = {5, 3, 1, 4, 0, 2};
        for (int i = 0; i < 6; i++) {
            memcpy(slot + at, kBondSave + kBondBlockAt[order[i]], kBondBlockLength[order[i]]);
            at += kBondBlockLength[order[i]];
        }
        memset(slot + at + 4, 0xa5, 64);   // after the zero label that ends the list
        break;
    }
    case 3:
        memcpy(slot, kBondSave + kBondBlockAt[1], kBondBlockLength[1]);
        memcpy(slot + kBondBlockLength[1], kBondSave + kBondBlockAt[4], kBondBlockLength[4]);
        break;
    case 4: {
        // an unknown block filling the slot to just under its end, then the end
        memcpy(slot, kBondSave, kBondBlockLength[0]);
        uint at = kBondBlockLength[0];
        memcpy(slot + at, "JUNK", 4);
        uint length = LS_SLOT_SIZE - 4 - at;
        memcpy(slot + at + 4, &length, 4);
        break;
    }
    case 5:
        memcpy(slot, kBondSave, sizeof(kBondSave));
        RandomBytes(slot + 0x30, 0x10);   // inside MSSN: its fields, not its header
        break;
    }
}

uchar g_bufferImage[0x1400];

void DriverCases(void) {
    g_settings.Clear();
    AddSettings(g_settings);
    int trial = 0;

    // LS_GetSaveStuff: every mask, every slot, every kind of list
    for (int kind = 0; kind < 6; kind++)
        for (ushort mask = 0; mask < 0x40; mask++)
            for (ushort slot = 0; slot < LS_SLOTS; slot++, trial++) {
                if (kind == 4 && slot == LS_SLOTS - 1)
                    continue;   // its last header would be read past InBufData, which is not the same memory in both
                RandomSettings();
                memset(&g_start, 0, sizeof(g_start));
                for (int s = 0; s < LS_SLOTS; s++)
                    MakeList(g_start.in[s], (kind + s) % 6 == 4 && s == LS_SLOTS - 1 ? 0 : (kind + s) % 6);
                Driver("LS_GetSaveStuff", trial,
                       [&](std::vector<uint32_t> &r) { r.push_back(((GetSaveStuffFn)kGetSaveStuff)(mask, slot)); },
                       [&](std::vector<uint32_t> &r) { r.push_back(LS_GetSaveStuff(mask, slot)); });
            }

    // LS_Save and LS_Load, three polls each from every starting state
    static const ushort states[] = {0, 1, 2, 7, 8};
    for (int round = 0; round < 400; round++, trial++) {
        RandomSettings();
        memset(&g_start, 0, sizeof(g_start));
        for (int s = 0; s < LS_SLOTS; s++)
            MakeList(g_start.in[s], ShadowRand(6) == 4 ? 0 : ShadowRand(6));
        g_start.loadState = states[ShadowRand(5)];
        g_start.saveState = states[ShadowRand(5)];
        g_start.loadSlot = (ushort)ShadowRand(LS_SLOTS);
        ushort slot = (ushort)ShadowRand(LS_SLOTS);
        ushort mask = (ushort)(round < 100 ? 0xff : ShadowRand(0x40));
        // LS_Load reads as much as a full save of the current state would take: with 12 levels, as the game has,
        // that is the made-up file's length; a shorter one could cut a header in two (an endless walk again)
        fake.scoreCount = 12;
        fake.saveWorks = ShadowRand(4) != 0;
        fake.loadWorks = ShadowRand(4) != 0;
        psiInternalLoadingDataState = ShadowRand(6);
        MakeList(fake.file, ShadowRand(6) == 4 ? 0 : ShadowRand(6));
        // a file cut short ends at a block or inside its fields: a header cut short (a label and no length) would
        // hold LS_FindBlockByLabel in place for ever, in the original as in ours
        static const uint cuts[] = {0, 11, 40, 79, 92, 100, 141, 150, 160, 166};
        fake.fileSize = ShadowRand(3) == 0 ? cuts[ShadowRand(10)] : LS_SLOT_SIZE;
        char codename[] = "SHADOWT";
        bool flushFirst = ShadowRand(3) == 0;
        Driver("LS_Save", trial,
               [&](std::vector<uint32_t> &r) {
                   if (flushFirst)
                       ((void(__cdecl *)(void))kFlushStates)();
                   for (int poll = 0; poll < 3; poll++)
                       r.push_back(((SaveFn)kSave)(codename, slot, mask));
               },
               [&](std::vector<uint32_t> &r) {
                   if (flushFirst)
                       LS_FlushStates();
                   for (int poll = 0; poll < 3; poll++)
                       r.push_back(LS_Save(codename, slot, mask));
               });
        Driver("LS_Load", trial,
               [&](std::vector<uint32_t> &r) {
                   if (flushFirst)
                       ((void(__cdecl *)(void))kFlushStates)();
                   for (int poll = 0; poll < 3; poll++)
                       r.push_back(((LoadSaveFn)kLoad)(codename, slot, mask));
               },
               [&](std::vector<uint32_t> &r) {
                   if (flushFirst)
                       LS_FlushStates();
                   for (int poll = 0; poll < 3; poll++)
                       r.push_back(LS_Load(codename, slot, mask));
               });
    }

    // LS_LoadFromBuffer, with Boot_LoadPTPData's length and others
    for (int round = 0; round < 200; round++, trial++) {
        memset(&g_start, 0, sizeof(g_start));
        RandomBytes(g_start.in, sizeof(g_start.in));
        RandomBytes(g_bufferImage, sizeof(g_bufferImage));
        if (round % 4 == 0)
            memset(g_bufferImage, 0, 4);
        else if (round % 4 == 1)
            memset(g_bufferImage, 0, 3);
        uint length = round % 3 == 0 ? 0x12c0 : ShadowRand(0x1001);
        Driver("LS_LoadFromBuffer", trial,
               [&](std::vector<uint32_t> &r) { r.push_back(((LoadFromBufferFn)kLoadFromBuffer)(g_bufferImage, length)); },
               [&](std::vector<uint32_t> &r) { r.push_back(LS_LoadFromBuffer(g_bufferImage, length)); });
    }
}

// ---- the adaptors, called with the original callers' registers

// LS_FindBlockByLabel(slot in AX, label in EDI, ushort *length on the stack)
__declspec(naked) uint __cdecl CallFindBlock(unsigned at, uint slot, uint label, ushort *length) {
    _asm {
        push edi
        mov edi, [esp + 0x10]
        push dword ptr [esp + 0x14]
        mov eax, 0x5a5a0000
        mov ax, [esp + 0x10]
        mov ecx, [esp + 0x0c]
        call ecx
        add esp, 4
        pop edi
        ret
    }
}

// LS_GetLoadStuff(ushort mask on the stack, slot in BX)
__declspec(naked) void __cdecl CallGetLoadStuff(unsigned at, uint mask, uint slot) {
    _asm {
        push ebx
        mov ebx, [esp + 0x10]
        push dword ptr [esp + 0x0c]
        mov ecx, [esp + 0x0c]
        call ecx
        add esp, 4
        pop ebx
        ret
    }
}

void AdaptorCases(void) {
    int trial = 0;
    static const uint labels[] = {Label('P', 'L', 'R', 'S'), Label('M', 'S', 'S', 'N'),
                                  Label('M', 'P', 'S', 'G'), Label('G', 'S', 'E', 'T'),
                                  Label('C', 'H', 'E', 'T'), Label('B', 'N', 'U', 'S'),
                                  Label('J', 'U', 'N', 'K'), Label('N', 'O', 'P', 'E')};
    for (int kind = 0; kind < 6; kind++)
        for (uint slot = 0; slot < LS_SLOTS; slot++)
            for (int l = 0; l < 8; l++, trial++) {
                if (kind == 4 && slot == LS_SLOTS - 1)
                    continue;
                memset(&g_start, 0, sizeof(g_start));
                for (int s = 0; s < LS_SLOTS; s++)
                    MakeList(g_start.in[s], kind == 4 && s == LS_SLOTS - 1 ? 0 : kind);
                uint label = labels[l];
                Driver("LS_FindBlockByLabel", trial,
                       [&](std::vector<uint32_t> &r) {
                           ushort length = 0x1234;
                           uint found = CallFindBlock(kFindBlock, slot, label, &length);
                           r.push_back(found == 0 ? 0 : found - (uint)(uintptr_t)GameInBufData);
                           r.push_back(length);
                       },
                       [&](std::vector<uint32_t> &r) {
                           ushort length = 0x1234;
                           uint found = CallFindBlock(kFindBlock, slot, label, &length);   // reaches the adaptor
                           r.push_back(found == 0 ? 0 : found - (uint)(uintptr_t)InBufData);
                           r.push_back(length);
                       });
                RandomSettings();
                uint mask = ShadowRand(0x40) | 0x5a5a0000;   // the high half is not the mask's
                Driver("LS_GetLoadStuff", trial,
                       [&](std::vector<uint32_t> &r) { CallGetLoadStuff(kGetLoadStuff, mask, slot | 0xa5a50000); },
                       [&](std::vector<uint32_t> &r) { CallGetLoadStuff(kGetLoadStuff, mask, slot | 0xa5a50000); });
            }
}

} // namespace

void SaveShadow_Run(void) {
    static Owned saved;   // big: not on the stack
    ShadowRegions everything;
    AddSettings(everything);
    everything.Save();
    saved.FromOurs();
    Owned *game = new Owned;
    game->FromGame();

    InstallFakes();
    BinCases();
    MakeCases();
    LoadCases();
    DriverCases();
    AdaptorCases();
    ShadowHook_RemoveAll();

    everything.Restore();
    saved.ToBoth();
    memcpy(GameDstData, game->dst, sizeof(game->dst));
    memcpy(GameInBufData, game->in, sizeof(game->in));
    GameLoadState = game->loadState;
    GameSaveState = game->saveState;
    GameLoadSlot = game->loadSlot;
    delete game;
    printf("[saveshadow] bit functions, blocks, drivers and adaptors: %d cases, %d differ\n", g_cases, g_differ);
}
