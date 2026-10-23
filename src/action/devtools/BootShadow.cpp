// Shadow test for the page handed between the engines (game.cpp's Boot_GetPTPData and Boot_LoadPTPData): the
// original and ours from the same made-up page, codename, player settings, cheats and multiplayer settings. What
// they call goes to logging fakes: the mission, the sound settings, the language, the menus' status and bonus,
// the medals, the driving scores, the save loaded from the page, the level to load. Run at start with
// MenuShadowTests=on (MenuProbe.cpp); NIGHTFIRE_SHADOW_ONLY=boot runs it alone.

#include "BootShadow.h"
#include "AllOriginal.h"
#include "ShadowHarness.h"

#include "../../common/xbeOriginal.h"
#include "../game.h"
#include "../ui/Menu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

#define ORIGINAL(at, type) ((type)(uintptr_t)(at))

const unsigned kGet = 0x00019a10, kLoad = 0x00019db0;

// The fakes' answers, the same sequence for both runs of a case
uint32_t answerSeed, answerState;
uint32_t Answer(uint32_t n) {
    answerState = answerState * 1664525u + 1013904223u;
    uint32_t v = answerState ^ (answerState >> 13);
    return n ? v % n : v;
}

// A mission's base map now and then Paris, where the mini mission and the title sequence come in
HASHCODE kParis = HT_Level_Driving_Paris;

void __cdecl FakeSetMap(HASHCODE level) { ShadowLog("setmap(%x)", (uint)level); }
HASHCODE __cdecl FakeBaseMap(void) {
    HASHCODE level = Answer(2) ? kParis : (HASHCODE)(0x09000000 + Answer(8));
    ShadowLog("basemap=%x", (uint)level);
    return level;
}
void __cdecl FakeSetStatus(uint status) { ShadowLog("setstatus(%x)", status); }
void *__cdecl FakeGetScore(char player) {
    ShadowLog("getscore(%x)", player & 0xff);
    return NULL;
}
uint __cdecl FakeAnswer(void) {
    uint v = Answer(0);
    ShadowLog("answer=%x", v);
    return v;
}
uint64_t __cdecl FakeGetBonus(uint player) {
    uint64_t v = (uint64_t)Answer(0) << 32 | Answer(0);
    ShadowLog("getbonus(%x)", player & 0xff);
    return v;
}
void __cdecl FakeLog1(uint a) { ShadowLog("log1(%x)", a); }
void __cdecl FakeSetBonus(uint lo, uint hi, uint player, uint orIn) {
    ShadowLog("setbonus(%x,%x,%x,%x)", lo, hi, player & 0xff, orIn & 0xff);
}
void __cdecl FakeGetDriving(uint *best, uint *moments) {
    ShadowLog("getdriving");
    for (int i = 0; i < 12; i++) {
        best[i] = Answer(0);
        moments[i] = Answer(16);
    }
}
void __cdecl FakeSetDriving(uint *best, uint *moments) {
    ShadowLog("setdriving");
    for (int i = 0; i < 12; i++)
        ShadowLog("%x,%x", best[i], moments[i]);
}
int __cdecl FakeBestMedal(HASHCODE level, uint player) {
    int v = (int)Answer(5) - 1;
    ShadowLog("bestmedal(%x,%x)=%d", (uint)level, player & 0xff, v);
    return v;
}
bool __cdecl FakeHasMedal(HASHCODE level, uint medal, uint player) {
    bool v = Answer(2) != 0;
    ShadowLog("hasmedal(%x,%x,%x)=%d", (uint)level, medal, player & 0xff, v);
    return v;
}
// The page as the driving engine hands it back: made up, the version mostly 1 (only the page's own 0xa50 bytes,
// not the 0xc00 the real one writes)
bool __cdecl FakeGetDrivingData(uint32_t *out) {
    bool v = Answer(5) != 0;
    ShadowLog("getdrivingdata=%d", v);
    uint8_t *page = (uint8_t *)out;
    for (size_t i = 0; i < sizeof(sNightFireShared_tag); i++)
        page[i] = (uint8_t)Answer(0x100);
    sNightFireShared_tag *ptp = (sNightFireShared_tag *)out;
    ptp->Version = Answer(5) ? 1 : Answer(3);
    ptp->LevelStatus = Answer(7);
    ptp->fLoadedFromMemCard = Answer(2);
    return v;
}
int __cdecl FakeLoadFromBuffer(uchar *data, uint length) {
    int v = (int)Answer(3);
    ShadowLog("loadfrombuffer(%x)=%d", length, v);
    return v;
}
void __cdecl FakeLevelToLoad(HASHCODE level, uint warm, uint skip) {
    ShadowLog("leveltoload(%x,%x,%x)", (uint)level, warm & 0xff, skip & 0xff);
}

void InstallFakes(void) {
    ShadowHook_Install(0x0009bde0, (const void *)&FakeSetMap);
    ShadowHook_Install(0x0009bdc0, (const void *)&FakeBaseMap);
    ShadowHook_Install(0x0009be10, (const void *)&FakeSetStatus);
    ShadowHook_Install(0x000b0900, (const void *)&FakeGetScore);
    ShadowHook_Install(0x000c53f0, (const void *)&FakeAnswer);    // GetSFXLanguage
    ShadowHook_Install(0x000c5400, (const void *)&FakeLog1);      // SFXSetLanguage
    ShadowHook_Install(0x00076240, (const void *)&FakeAnswer);    // Menu_GetNightfireStatus
    ShadowHook_Install(0x000762d0, (const void *)&FakeLog1);      // Menu_SetNightfireStatus
    ShadowHook_Install(0x0007c660, (const void *)&FakeGetBonus);
    ShadowHook_Install(0x0007cb10, (const void *)&FakeSetBonus);
    ShadowHook_Install(0x000b1250, (const void *)&FakeGetDriving);
    ShadowHook_Install(0x000b1310, (const void *)&FakeSetDriving);
    ShadowHook_Install(0x0006d130, (const void *)&FakeAnswer);    // GetLanguage
    ShadowHook_Install(0x0006d450, (const void *)&FakeLog1);      // Txt_SetLanguage
    ShadowHook_Install(0x000dff50, (const void *)&FakeAnswer);    // GetTvType
    ShadowHook_Install(0x000c5430, (const void *)&FakeAnswer);    // SFXGetMode
    ShadowHook_Install(0x000c5410, (const void *)&FakeLog1);      // SFXSetMode
    ShadowHook_Install(0x000c5090, (const void *)&FakeAnswer);    // SFXGetVolume
    ShadowHook_Install(0x000c5060, (const void *)&FakeLog1);      // SFXSetVolume
    ShadowHook_Install(0x000c6360, (const void *)&FakeAnswer);    // SFXMusicGetVolume
    ShadowHook_Install(0x000c6350, (const void *)&FakeLog1);      // SFXMusicSetVolume
    ShadowHook_Install(0x0007ce20, (const void *)&FakeBestMedal);
    ShadowHook_Install(0x0007ce80, (const void *)&FakeHasMedal);
    ShadowHook_Install(0x000dfb70, (const void *)&FakeGetDrivingData);
    ShadowHook_Install(0x0006ebb0, (const void *)&FakeLoadFromBuffer);
    ShadowHook_Install(0x000bddf0, (const void *)&FakeLevelToLoad);
}

// The game's state the two read and write
const struct {
    uint32_t at;
    size_t size;
} kRegions[] = {
    {0x001d7e90, 0xa50},    // PTPDATA
    {0x001d88e0, 0xa50},    // what psiGetDrivingData fills
    {0x0017d540, 0x48},     // ls
    {0x0017d588, 0x70},     // def_codename
    {0x001fe6d0, 0x158},    // PlayerInputs[0]
    {0x001f65dc, 0x10},     // CheatInfo
    {XbeOurs(0x001f6580), sizeof(GameState_t)},
    {0x0025fe38, 0x23c},    // MPSettings
    {XbeOurs(0x0025d79e), 0x1},      // menu_unlock_everything
    {XbeOurs(0x0025d7a0), 0x4},      // LanguageToLoad
    {0x001f660c, 0xc},      // the split screen's layout, the widescreen, the subtitles
};
ShadowRegions regions;

void Garble(void) {
    for (const auto &r : kRegions) {
        for (size_t i = 0; i < r.size; i++)
            *(uint8_t *)(size_t)XbeOurs(r.at + (unsigned)i) = (uint8_t)ShadowRand(0x100);
    }
    MENU_LS *l = (MENU_LS *)(size_t)XbeOurs(0x0017d540);
    l->doMiniMission = (uchar)ShadowRand(2);
    U32_AT(XbeOurs(0x001f6610)) = ShadowRand(3);   // IsWidescreen, mostly 0 or 1
    GameState_t *gs = (GameState_t *)XbeOurs(0x001f6580);
    gs->NextLevelHashcode = ShadowRand(3) ? kParis : (HASHCODE)(0x09000000 + ShadowRand(8));
}

bool getRun;
void *gotData;
uint gotSize;

uint32_t Run(bool original) {
    answerState = answerSeed;
    gotData = NULL;
    gotSize = 0;
    if (getRun) {
        if (original) {
            XbeOriginalScope o(kGet);
            ORIGINAL(kGet, void (__cdecl *)(void **, uint *))(&gotData, &gotSize);
        } else {
            Boot_GetPTPData(&gotData, &gotSize);
        }
        return (uint32_t)(uintptr_t)gotData ^ gotSize * 7;
    }
    if (original) {
        XbeOriginalScope o(kLoad);
        ORIGINAL(kLoad, void (__stdcall *)(void))();
    } else {
        Boot_LoadPTPData();
    }
    return 0;
}
uint32_t RunTheirs(void) { return Run(true); }
uint32_t RunOurs(void) { return Run(false); }

} // namespace

void BootShadow_Run(void) {
    std::vector<std::vector<uint8_t>> saved;
    for (const auto &r : kRegions) {
        saved.emplace_back(r.size);
        XbeOursRead(saved.back().data(), r.at, r.size);
    }
    regions.Clear();
    for (const auto &r : kRegions)
        regions.Add((void *)(uintptr_t)r.at, r.size);
    NfWarnMuted++;
    InstallFakes();
    int cases = 0, differ = 0;
    for (int which = 0; which < 2; which++) {
        getRun = which == 0;
        for (int trial = 0; trial < 3000; trial++) {
            Garble();
            answerSeed = ShadowRand(0x10000) | ShadowRand(0x10000) << 16;
            cases++;
            if (!ShadowCompare(regions, "boot", getRun ? "Boot_GetPTPData" : "Boot_LoadPTPData", trial, RunTheirs,
                               RunOurs))
                differ++;
            if (getenv("NIGHTFIRE_BOOT_TRACE") != NULL && trial < 5)
                printf("[boot] %s\n", ShadowLog_Text().c_str());
        }
    }
    ShadowHook_RemoveAll();
    NfWarnMuted--;
    for (size_t i = 0; i < saved.size(); i++)
        XbeOursWrite(kRegions[i].at, saved[i].data(), saved[i].size());
    printf("[boot] Boot_GetPTPData, Boot_LoadPTPData: %d cases, %d differ\n", cases, differ);
    fflush(stdout);
}
