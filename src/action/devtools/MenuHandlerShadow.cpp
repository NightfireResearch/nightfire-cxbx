// Shadow tests for parcel 16's package C: the menus' page and control handlers (the boot pages, the main menu, the
// movie pages, the codename select, the results, the dossier's rows, the options pages, the pause menu, the debug
// page's tools) and the helpers they share (the iris, option boxes, movies, rewards and medals, the controller
// styles, the statistics' rows, Timer_Seconds2String, the upgrades). Each function, the original and ours, on the same
// made-up state: controls of every kind the handlers reach, a score table, objectives, the player's settings, the
// codenames. What a function calls that is not under test goes to a logging fake: the framework's sends, the managers,
// text, sounds, movies, the game's flow, the missions, the scores, the scripts, the codename helpers. The globals this
// package owns are lent to the original as the game's copies and compared with ours.
// Run at start with MenuShadowTests=on (MenuProbe.cpp); NIGHTFIRE_SHADOW_ONLY=menuhandlers runs it alone,
// NIGHTFIRE_MENUHANDLERS_GROUP=<part of a name> one group.

#include "MenuHandlerShadow.h"
#include "ShadowHarness.h"

#include "../../common/xbeOriginal.h"
#include "../actionhelpers.h"
#include "../game.h"
#include "../input.h"
#include "../engine/Script.h"
#include "../engine/Text.h"
#include "../game/Upgrade.h"
#include "../game/mp/multiplayer.h"
#include "../engine/psiSave.h"
#include "../game/obj/Player.h"
#include "../game/sp/Mission.h"
#include "../game/sp/PlayerStats.h"
#include "../ui/Controls.h"
#include "../ui/Manager.h"
#include "../ui/Menu.h"
#include "../ui/MenuManager.h"

#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <string>
#include <vector>

// The globals the package owns (their game addresses in the pairs below)
extern uint language_flags;
extern uint intro_step;
extern uint8_t start_shown;
extern char start_legal_text[0x180];
extern uint8_t parisenum_countdown;
extern uint esthero_frame;
extern uint fmvplayer_frame;
extern char tweaks_text[18][0x20];
extern char tweaks2_text[15][0x20];
extern SCRIPTINFO *nis_script;
extern uint8_t nis_by_select;
extern uint cheat_medal;
extern uint8_t style_list_next;
extern char style_list_text[14][0x20];
extern uint8_t controls_inverted;
extern int av_music_volume;
extern int av_sfx_volume;
extern uint8_t av_leaving;
extern char pause_build_text[0x40];
extern uint8_t pause_quitting;
extern uint8_t pause_confirm_closed;
extern int iris_tick;
extern int iris_frame;
extern uint8_t iris_hold;
extern uint8_t iris_active;
extern uint optionbox_answer;
extern uint optionbox_type;
extern uint8_t optionbox_has_answer;
extern uint8_t nfselect_leaving;
extern uint8_t nfselect_from_main_menu;
extern SCORETABLE *results_score;
extern uint8_t results_last_mission;
extern char results_title_text[0x80];
extern char results_target_text[0x40];
extern char results_score_text[0x40];
extern uint8_t results_saving;
extern char bonus_text[0x84];
extern char bonus_text_before[0x80];
extern char records_best_text[0x20];
extern char records_bond_moves_text[0x20];
extern char records_upgrades_text[0x20];
extern LEVEL_BONUSES level_bonuses;
extern char timer_text[4][0x100];
extern uint8_t timer_text_next;
extern uint save_space_problem;
extern char save_space_text[0x200];
extern uint8_t mp_controller_missing[4];
extern uint8_t mp_controller_missing_joined[4];
extern char mp_controller_text[0x100];
extern uint8_t saveflow_dashboard_offered;
extern uint saveflow_hold_until;
extern char saveflow_text_buffer[0x200];
extern const char *saveflow_tip;

// The package's functions, as ours
bool P_LANGUAGE_Handler(uchar, M_CONTROL *, uint, uint, int, int);

namespace {

#define ORIGINAL(at, type) ((type)(uintptr_t)(at))
#ifndef COUNT
#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))
#endif

const int OBJECTS = 10;
const int OBJECT_SIZE = 0xa00;

union Obj {
    uint8_t raw[OBJECT_SIZE];
    M_CONTROL c;
    M_LIST list;
    M_MEMO memo;
    M_BUTTON button;
};

struct World {
    Obj objects[OBJECTS];
    SCORETABLE score;
    ScoreCategory cats[SCORE_NUM_CATEGORIES];
    M_ITEM items[12];
    REWARDINFO_tag info;
    ulong bonus[2];
    uint type;
    int queryAnswer;
    uint8_t player[0x200];
    uint8_t bloke[0x400];
    uint8_t script[0xab0];
    char labels[16][24];
    char heap[4][0x168];
    char strings[4][64];
    uint labelNext;
    uint heapNext;
};
World world;

// A case's inputs, made once a trial (the same for both runs)
struct Inputs {
    uint message;
    uchar managerNum;
    int control;
    int arg1, arg2;
    uint32_t a[8];
};
Inputs in;

int cases, differ;
uint32_t gen, caseSeed;

uint32_t R(uint32_t n) {
    gen = gen * 1664525u + 1013904223u;
    uint32_t v = gen ^ (gen >> 15);
    v *= 0x2c1b3c6du;
    v ^= v >> 12;
    v *= 0x297a2d39u;
    v ^= v >> 15;
    return n ? v % n : v;
}

void Fill(void *p, size_t n) {
    uint8_t *b = (uint8_t *)p;
    for (size_t i = 0; i < n; i++)
        b[i] = (uint8_t)R(0);
}

// ---- the globals the package owns, lent to the original as the game's copies

struct Pair {
    uint32_t game;
    void *ours;
    size_t size;
};
const Pair kPairs[] = {
    {0x0025d7cc, &language_flags, 4}, {0x0025ddd8, &intro_step, 4}, {0x0025dde1, &start_shown, 1},
    {0x0025dde8, start_legal_text, 0x180}, {0x0025df68, &parisenum_countdown, 1}, {0x0025dddc, &esthero_frame, 4},
    {0x0025df6c, &fmvplayer_frame, 4}, {0x0025d828, tweaks_text, 0x240}, {0x0025da68, tweaks2_text, 0x1e0},
    {0x002451f8, &nis_script, 4}, {0x0025d7c8, &nis_by_select, 1}, {0x002790a8, &cheat_medal, 4},
    {0x002245f0, &style_list_next, 1}, {0x00224d98, style_list_text, 0x1c0}, {0x0025d7df, &controls_inverted, 1},
    {0x0025ed70, &av_music_volume, 4}, {0x0025ed74, &av_sfx_volume, 4}, {0x0025ed78, &av_leaving, 1},
    {0x0025d7e0, pause_build_text, 0x40}, {0x0025d820, &pause_quitting, 1}, {0x0025d821, &pause_confirm_closed, 1},
    {0x0025d7ac, &iris_tick, 4}, {0x0025d7b0, &iris_frame, 4}, {0x0025d7b8, &iris_hold, 1}, {0x0025d7b9, &iris_active, 1},
    {0x00224f5c, &optionbox_answer, 4}, {0x00224f60, &optionbox_type, 4}, {0x00224f64, &optionbox_has_answer, 1},
    {0x0025df70, &nfselect_leaving, 1}, {0x0017d609, &nfselect_from_main_menu, 1}, {0x0025d7d0, &results_score, 4},
    {0x0025d7d4, &results_last_mission, 1}, {0x0025dc50, results_title_text, 0x80},
    {0x0025dcd0, results_target_text, 0x40}, {0x0025dd10, results_score_text, 0x40}, {0x0025dd50, &results_saving, 1},
    {0x0025d708, bonus_text, 0x84}, {0x0025dd58, bonus_text_before, 0x80}, {0x0025e378, records_best_text, 0x20},
    {0x0025e398, records_bond_moves_text, 0x20}, {0x0025e3b8, records_upgrades_text, 0x20},
    {0x00225080, &level_bonuses, 0x34}, {0x0029ae38, timer_text, 0x400}, {0x0029b238, &timer_text_next, 1},
    {0x0025edbc, &save_space_problem, 4}, {0x0025efd0, save_space_text, 0x200},
    {0x0022507c, mp_controller_missing, 4}, {0x00225078, mp_controller_missing_joined, 4},
    {0x00224f78, mp_controller_text, 0x100}, {0x0025edc0, &saveflow_dashboard_offered, 1},
    {0x0025edc4, &saveflow_hold_until, 4}, {0x0025edc8, saveflow_text_buffer, 0x200}, {0x0025efc8, &saveflow_tip, 4},
};

// A word as the logs show it: an offset into the world, a string's text, or the number
bool IsText(uint32_t v) {
    if (v < 0x10000)
        return false;
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery((void *)(uintptr_t)v, &mbi, sizeof(mbi)) == 0 || mbi.State != MEM_COMMIT ||
        (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0 ||
        (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY)) == 0)
        return false;
    const uint8_t *s = (const uint8_t *)(uintptr_t)v;
    size_t room = (uint8_t *)mbi.BaseAddress + mbi.RegionSize - s;
    for (size_t i = 0; i < room && i < 300; i++) {
        if (s[i] == 0)
            return true;
        if (s[i] < 0x20 && s[i] != '\n' && s[i] != '\r' && s[i] != '\t')
            return false;
    }
    return false;
}

const char *A(uint32_t v) {
    static char bufs[16][340];
    static int next;
    char *b = bufs[next++ & 15];
    uint32_t w = (uint32_t)(uintptr_t)&world;
    if (v >= w && v < w + sizeof(world)) {
        sprintf(b, "w%x", v - w);
        return b;
    }
    // an owned global, ours or the game's copy
    for (int i = 0; i < COUNT(kPairs); i++) {
        uint32_t ours = (uint32_t)(uintptr_t)kPairs[i].ours;
        if ((v >= ours && v < ours + kPairs[i].size) || (v >= kPairs[i].game && v < kPairs[i].game + kPairs[i].size)) {
            uint32_t base = v >= ours && v < ours + kPairs[i].size ? ours : kPairs[i].game;
            if (IsText(v))
                snprintf(b, sizeof(bufs[0]), "o%d+%x'%.200s'", i, v - base, (const char *)(uintptr_t)v);
            else
                sprintf(b, "o%d+%x", i, v - base);
            return b;
        }
    }
    if (false)
        ;
    else if (IsText(v))
        snprintf(b, sizeof(bufs[0]), "'%.300s'", (const char *)(uintptr_t)v);
    else
        sprintf(b, "%x", v);
    return b;
}

// The game's state the package reads and writes (the game's copies of the globals above left out)
const struct {
    uint32_t at;
    size_t size;
} kGlobals[] = {
    {0x0017d540, 0xc9},     // ls, the default codenames, the language page's flags
    {0x0017d60a, 0x2},
    {0x001fe6d0, 0x560},    // PlayerInputs
    {0x001f6580, 0x100},    // GameState, the layout, subtitles, the players, ScriptCam
    {0x0025fe38, 0x23c},    // MPSettings
    {0x0025f1d0, 0x938},    // manager
    {0x0025d6e8, 0x20},     // menu_bonus
    {0x0025d78c, 0x2},
    {0x0025d7a0, 0x4},      // LanguageToLoad
    {0x0025d7bc, 0x8},      // the movies' state
    {0x0025d7d8, 0x4},
    {0x0025d7dd, 0x1},      // cn_modified
    {0x0025edb9, 0x1},      // saveflow_relist
    {0x002456b0, 0x3},
    {0x0025fb10, 0x1},
    {0x00279120, 0x24},     // Upgrades
    {0x001df138, 0x100},    // switch_channels
    {0x001df428, 0x400},    // switch_channels_time
    {0x00164068, 0x80},     // the drones' tunables
    {0x0017e8cc, 0x1c},     // the player's damage modifiers
    {0x002251b8, 0x400},    // codename_buf
    {0x00245240, 0x40},     // mp_join_slots
    {0x00245698, 0x10},     // mp_join_step
    {0x0025eda0, 0x1c},     // XBox_DoSaveFlow's state
    {0x002adf10, 0x4},      // psiInternalLoadingDataState
    {0x00262738, 0x230},    // MPGame
    {0x002245f8, 0x23c},    // mp_settings_backup
    {0x00224838, 0x560},    // mp_inputs_backup
    {0x002adf0c, 0x4},      // SavesListed
};

ShadowRegions regions;

void SaveAll(std::vector<uint8_t> &out, bool game) {
    out.clear();
    for (const Pair &p : kPairs) {
        const uint8_t *at = game ? (const uint8_t *)(uintptr_t)p.game : (const uint8_t *)p.ours;
        out.insert(out.end(), at, at + p.size);
    }
}

void LoadAll(const std::vector<uint8_t> &in, bool game) {
    size_t at = 0;
    for (const Pair &p : kPairs) {
        uint8_t *to = game ? (uint8_t *)(uintptr_t)p.game : (uint8_t *)p.ours;
        memcpy(to, in.data() + at, p.size);
        at += p.size;
    }
}

// ---- the cases

typedef uint32_t (*Runner)(bool original);
struct Case {
    const char *name;
    Runner run;
    unsigned also[6];       // the package's functions the original calls that share its owned globals: run as theirs
    uint8_t messages[8];    // a handler's messages (0 ends)
};
const Case *current;

__declspec(noinline) void ZeroStack(void) {
    volatile uint32_t below[0x1000];
    for (int i = 0; i < 0x1000; i++)
        below[i] = 0;
}

uint32_t RunCase(bool original) {
    gen = caseSeed;
    world.labelNext = 0;
    world.heapNext = 0;
    ZeroStack();
    if (!original)
        return current->run(false);
    XbeOriginalScope s0(current->also[0] ? current->also[0] : 1);
    XbeOriginalScope s1(current->also[1] ? current->also[1] : 1);
    XbeOriginalScope s2(current->also[2] ? current->also[2] : 1);
    XbeOriginalScope s3(current->also[3] ? current->also[3] : 1);
    XbeOriginalScope s4(current->also[4] ? current->also[4] : 1);
    XbeOriginalScope s5(current->also[5] ? current->also[5] : 1);
    return current->run(true);
}

#define CALL(at, ours, type, ...)                                                                                     \
    (original ? ([&]() { XbeOriginalScope scope_(at); return (uint32_t)ORIGINAL(at, type)(__VA_ARGS__); }())        \
              : (uint32_t)ours(__VA_ARGS__))
#define CALL_VOID(at, ours, type, ...)                                                                                \
    do {                                                                                                              \
        if (original) {                                                                                               \
            XbeOriginalScope scope_(at);                                                                              \
            ORIGINAL(at, type)(__VA_ARGS__);                                                                          \
        } else {                                                                                                      \
            ours(__VA_ARGS__);                                                                                        \
        }                                                                                                             \
    } while (0)

bool CompareCase(const char *what, int trial) {
    static std::vector<uint8_t> start, theirState, ourState, theirOwned, ourOwned;
    SaveAll(start, false);
    LoadAll(start, true);
    regions.Save();
    ShadowLog_Clear();
    uint32_t theirResult = RunCase(true);
    regions.Capture(theirState);
    SaveAll(theirOwned, true);
    std::string theirLog = ShadowLog_Text();
    regions.Restore();
    LoadAll(start, false);
    ShadowLog_Clear();
    uint32_t ourResult = RunCase(false);
    regions.Capture(ourState);
    SaveAll(ourOwned, false);
    const std::string &ourLog = ShadowLog_Text();

    static std::map<std::string, int> reports;
    int &reported = reports[what];
    bool same = true;
    if (theirResult != ourResult) {
        same = false;
        if (reported++ < 3)
            printf("[menuhandlers] %s case %d: returns 0x%x, the original 0x%x\n", what, trial, ourResult, theirResult);
    }
    if (theirState != ourState) {
        same = false;
        int region;
        size_t offset;
        if (reported++ < 3 && regions.Describe(theirState, ourState, &region, &offset))
            printf("[menuhandlers] %s case %d: memory differs in region %d at +0x%x\n", what, trial, region,
                   (unsigned)offset);
    }
    if (theirOwned != ourOwned) {
        same = false;
        size_t i = 0;
        while (theirOwned[i] == ourOwned[i])
            i++;
        size_t at = 0;
        const Pair *p = kPairs;
        while (at + p->size <= i) {
            at += p->size;
            p++;
        }
        if (reported++ < 3)
            printf("[menuhandlers] %s case %d: the owned global at 0x%08x differs at +0x%x: 0x%02x, the original 0x%02x\n",
                   what, trial, p->game, (unsigned)(i - at), ourOwned[i], theirOwned[i]);
    }
    if (theirLog != ourLog) {
        same = false;
        if (reported++ < 3) {
            size_t d = 0;
            while (d < theirLog.size() && d < ourLog.size() && theirLog[d] == ourLog[d])
                d++;
            size_t from = d > 160 ? d - 160 : 0;
            printf("[menuhandlers] %s case %d: calls differ at %d\n  ours:     %.480s\n  original: %.480s\n", what,
                   trial, (int)d, ourLog.c_str() + from, theirLog.c_str() + from);
        }
    }
    return same;
}

void RandomWorld(void);
void InstallFakes(void);

void Test(const char *group, const Case *list, int count, int trials) {
    const char *only = getenv("NIGHTFIRE_MENUHANDLERS_GROUP");
    if (only != NULL && strstr(group, only) == NULL)
        return;
    int groupCases = cases, groupDiffer = differ;
    for (int c = 0; c < count; c++) {
        current = &list[c];
        if (getenv("NIGHTFIRE_SHADOW_TRACE") != NULL) {
            printf("[menuhandlers] %s\n", current->name);
            fflush(stdout);
        }
        for (int trial = 0; trial < trials; trial++) {
            gen = ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
            RandomWorld();
            caseSeed = gen;
            cases++;
            if (!CompareCase(current->name, trial))
                differ++;
        }
    }
    printf("[menuhandlers] %s: %d cases, %d differ\n", group, cases - groupCases, differ - groupDiffer);
    fflush(stdout);
}

// ---- the fakes

uint32_t Small(void) {
    return R(10);
}

M_CONTROL *Object(uint32_t key) {
    return &world.objects[key % OBJECTS].c;
}

const HASHCODE kLevels[] = {
    HT_Level_Driving_Paris, HT_Level_CastleExterior, HT_Level_Driving_SnowMobile, HT_Level_Driving_Alps,
    HT_Level_HendersonA, HT_Level_TowerA, HT_Level_PowerStationA1, HT_Level_Tower2A, HT_Level_Driving_Underwater,
    HT_Level_Driving_JungleA, HT_Level_EvilBase, HT_Level_SpaceStationD, HT_Level_Menu_Pre,
};

uint __cdecl FakeMenuSend(uchar m, HASHCODE h, uint message, int a1, int a2) {
    if (message == MessageType_ListAddRow) {
        const LIST_ROW *row = (const LIST_ROW *)(uintptr_t)a1;
        ShadowLog("send(%x,%x,row '%s|%s|%s|%s')", m, h, row->cells[0].text, row->cells[1].text, row->cells[2].text,
                  row->cells[3].text);
        return 1;
    }
    ShadowLog("send(%x,%x,%x,%s,%s)", m, h, message, A(a1), A(a2));
    if (message == MessageType_GetControl)
        return (uint)(uintptr_t)Object(h);
    if (message == MessageType_GetSelectedItemValue && R(3) == 0)
        return kLevels[R(COUNT(kLevels))];
    return Small();
}

int __cdecl FakeSendMessage(M_CONTROL *c, uint message, int a1, int a2);

int __cdecl FakeMenuSendEx(uchar m, HASHCODE h, uint id, uint message, int a1, int a2) {
    ShadowLog("sendex(%x,%x,%x,%x,%s,%s)", m, h, id, message, A(a1), A(a2));
    if (message == MessageType_GetControl)
        return (int)(uintptr_t)Object(h + id);
    if (message == MessageType_GetSelectedIndex)
        return (int)R(4);
    return (int)Small();
}

int __cdecl FakeMenuSendEx2(uchar m, HASHCODE h, uint bit, uint message, int a1, int a2) {
    ShadowLog("sendex2(%x,%x,%x,%x,%s,%s)", m, h, bit, message, A(a1), A(a2));
    return (int)Small();
}

int __cdecl FakeSendMessage(M_CONTROL *c, uint message, int a1, int a2) {
    ShadowLog("sendmsg(%s,%x,%s,%s)", A((uint32_t)(uintptr_t)c), message, A(a1), A(a2));
    if (message == MessageType_GetSelectedItemValue && R(3) == 0)
        return (int)kLevels[R(COUNT(kLevels))];
    return (int)Small();
}

int __cdecl FakeManagerSend(M_MANAGER *mgr, uint message, int a1, int a2) {
    ShadowLog("manager(%s,%x,%s,%s)", A((uint32_t)(uintptr_t)mgr), message, A(a1), A(a2));
    return (int)Small();
}

void __cdecl FakeSendDelayed(int delay, uchar m, HASHCODE h, uint a1, uint a2, uint a3) {
    ShadowLog("delayed(%x,%x,%x,%x,%s,%s)", delay, m, h, a1, A(a2), A(a3));
}

uint __cdecl FakeSendDelayedMessage(uint delay, M_CONTROL *c, uint a1, int a2, int a3) {
    ShadowLog("delayedmsg(%x,%s,%x,%s,%s)", delay, A((uint32_t)(uintptr_t)c), a1, A(a2), A(a3));
    return 1;
}

// Labels without a %s in them, for a group whose formats take no strings
bool plainLabels;

const char *__cdecl FakeBindLabel(uint label, uint lock) {
    char *s = world.labels[world.labelNext++ % 16];
    if (R(6) == 0 && !plainLabels)
        sprintf(s, "L%x %%s", label);
    else
        sprintf(s, "L%x", label);
    ShadowLog("bind(%x)", label);
    return s;
}

char *__cdecl FakeStringHeap(uchar lock) {
    ShadowLog("heap(%x)", lock);
    char *s = world.heap[world.heapNext++ % 4];
    s[0] = 0;
    return s;
}

#define LOG0(name, tag)                                                                                               \
    void __cdecl name(void) { ShadowLog(tag); }
#define LOG1(name, tag)                                                                                               \
    void __cdecl name(uint a) { ShadowLog(tag "(%s)", A(a)); }
#define LOG2(name, tag)                                                                                               \
    void __cdecl name(uint a, uint b) { ShadowLog(tag "(%s,%s)", A(a), A(b)); }
#define LOG3(name, tag)                                                                                               \
    void __cdecl name(uint a, uint b, uint c) { ShadowLog(tag "(%s,%s,%s)", A(a), A(b), A(c)); }
#define ANSWER0(name, tag, answer)                                                                                    \
    uint __cdecl name(void) {                                                                                         \
        uint v = answer;                                                                                              \
        ShadowLog(tag "=%x", v);                                                                                      \
        return v;                                                                                                     \
    }
#define ANSWER1(name, tag, answer)                                                                                    \
    uint __cdecl name(uint a) {                                                                                       \
        uint v = answer;                                                                                              \
        ShadowLog(tag "(%s)=%x", A(a), v);                                                                            \
        return v;                                                                                                     \
    }

LOG1(FakeSetLanguage, "txtlanguage")
ANSWER0(FakeGetLanguage, "getlanguage", R(10))
ANSWER0(FakeSfxGetVolume, "sfxvolume", R(120))
LOG1(FakeSfxSetVolume, "sfxsetvolume")
ANSWER0(FakeMusicGetVolume, "musicvolume", R(120))
LOG1(FakeMusicSetVolume, "musicsetvolume")
LOG1(FakeSfxSetLanguage, "sfxlanguage")
LOG1(FakeFadeDown, "fadedown")
LOG0(FakeUnPause, "unpause")
LOG0(FakeUnPauseStreams, "unpausestreams")
LOG0(FakePauseStreams, "pausestreams")
LOG3(FakeStartMovie, "startmovie")
LOG0(FakeStopMovie, "stopmovie")
ANSWER0(FakeMovieFinished, "moviefinished", R(2))
LOG0(FakeRestartLoop, "restartloop")
LOG2(FakeSfxRemove, "sfxremove")
ANSWER0(FakeNoInput, "noinput", R(3) == 0 ? 0x258 : R(3) == 0 ? 0xa8c : R(0x1000))
LOG0(FakeResetNoInput, "resetnoinput")
ANSWER0(FakeLastController, "lastcontroller", R(4))
LOG1(FakeManagerDelete, "menudelete")
ANSWER0(FakeGetStatus, "status", R(6))
LOG3(FakeLevelToLoad, "leveltoload")
ANSWER0(FakeBaseMap, "basemap", 0x07000000 + R(16))
ANSWER0(FakeMissionStatus, "missionstatus", R(5))
ANSWER0(FakeNumObjectives, "numobjectives", R(7))
ANSWER0(FakeDoneBetter, "donebetter", in.a[7] & 1)   // the same all through a run
LOG3(FakeUpdateMessageBox, "messagebox")
LOG3(FakeSelectCodename, "selectcodename")
LOG1(FakePutCodenames, "putcodenames")
// The player and the slot are bytes
void __cdecl FakeMapDefault(uint player, uint slot, uint parts) {
    ShadowLog("mapdefault(%x,%x,%x)", player & 0xff, slot & 0xff, parts);
}
LOG0(FakeDashboard, "dashboard")
LOG0(FakeSaveChoice, "savechoice")
LOG2(FakeChangeStyle, "changestyle")
// The original reads the participant's byte alone
uint __cdecl FakeMPScore(uint participant) {
    uint v = R(40);
    ShadowLog("mpscore(%d)=%x", participant & 0xff, v);
    return v;
}
LOG2(FakeRamLoad, "ramload")
LOG1(FakeProcessDestroy, "processdestroy")
LOG3(FakeGetMPSkins, "getmpskins")
ANSWER0(FakeRandom, "random", R(0))
ANSWER0(FakeLoadingState, "loadingstate", R(4) == 0 ? R(0x20) : R(14))
LOG0(FakeLSFlushStates, "lsflush")
LOG0(FakeFlushEnum, "flushenum")
LOG3(FakePlayerEnable, "playerenable")
LOG2(FakePlayerDisable, "playerdisable")
LOG0(FakePushStates, "pushstates")
LOG0(FakePopStates, "popstates")
LOG1(FakeScriptUpdate, "scriptupdate")
ANSWER1(FakeScriptIsPlaying, "scriptisplaying", R(2))
LOG2(FakeScriptPlay, "scriptplay")
ANSWER1(FakeIsLoadable, "isloadable", R(3) == 0 ? 0 : R(5))
LOG2(FakeLoadableLoad, "loadableload")
ANSWER1(FakeGetItem, "getitem", R(3) == 0 ? 0 : 1)
LOG1(FakeMovementHandler, "movement")
LOG3(FakeMemoUpdate, "memoupdate")
LOG3(FakeListUpdate, "listupdate")
LOG3(FakeButtonUpdate, "buttonupdate")
ANSWER0(FakeNightfireStatus, "nightfirestatus", R(0x1000))
LOG2(FakeSfxStart, "sfxstart")
ANSWER0(FakeFreeBlocks, "freeblocks", R(3) == 0 ? 50000 : R(40))
ANSWER1(FakeBlocksFor, "blocksfor", R(40))
LOG3(FakeLaunch, "launch")

uint __cdecl FakeSaveStuff(uint mask, uint slot) {
    uint v = R(0x10000);
    ShadowLog("savestuff(%x,%x)=%x", mask & 0xffff, slot & 0xffff, v);
    return v;
}

// The XBE's sprintf: the host's (the XBE's C library cannot format a float before the game has started)
int __cdecl FakeSprintf(char *buffer, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int n = vsprintf(buffer, format, args);
    va_end(args);
    return n;
}

void __cdecl FakeCameraEnable(uint camera, uint enable, uint sprites, uint position) {
    ShadowLog("cameraenable(%x,%x,%x,%s)", camera, enable & 0xff, sprites & 0xff, A(position));
}

void __cdecl FakePushState(int state, float time, uint colour) {
    ShadowLog("pushstate(%x,%08x,%x)", state, *(uint32_t *)&time, colour);
}

void __cdecl FakeProcessCreate(M_CONTROL *c, M_MESSAGE *msg, uint frames, int unused) {
    ShadowLog("process(%s,%x,%x,%x,%x)", A((uint32_t)(uintptr_t)c), msg->type, msg->arg1, msg->arg2, frames);
}

void __cdecl FakeObjectiveState(OBJ_STATE *state, short n) {
    ShadowLog("objective(%x)", (int)n);
    state->status = R(6);
    state->statusText = R(0x400);
    state->name = R(0x400);
    state->description = R(3) == 0 ? 0xffffffffu : R(0x400);
}

uint32_t *__cdecl FakeGetScore(char player) {
    ShadowLog("getscore(%x)", (int)player);
    return R(6) == 0 ? NULL : (uint32_t *)&world.score;
}

SCORETABLE *__cdecl FakeLevelTotals(HASHCODE level) {
    ShadowLog("leveltotals(%x)", level);
    return R(5) == 0 ? NULL : &world.score;
}

uint __cdecl FakeSetLevelBonus(int level, uint medal, uchar player) {
    uint v = R(2);
    ShadowLog("setlevelbonus(%x,%x,%x)=%x", level, medal, player, v);
    return v;
}

uint __cdecl FakeCheckSaveSpace(uchar m, MENU_LS *l) {
    uint v = R(2);
    ShadowLog("checksavespace(%x,%s)=%x", m, A((uint32_t)(uintptr_t)l), v);
    return v;
}

SCRIPTINFO *__cdecl FakeScriptLoad(HASHCODE h, _VECTOR *a, _VECTOR *b, uint *c, void *d, void *e, void *f) {
    ShadowLog("scriptload(%x,%s,%s,%s,%s,%s,%s)", h, A((uint32_t)(uintptr_t)a), A((uint32_t)(uintptr_t)b),
              A((uint32_t)(uintptr_t)c), A((uint32_t)(uintptr_t)d), A((uint32_t)(uintptr_t)e), A((uint32_t)(uintptr_t)f));
    return (SCRIPTINFO *)world.script;
}

void __cdecl FakeStartLSCore(char *codename, int operation, char stale, uchar m, uchar slot, ushort mask, uint lsSlot,
                             HASHCODE page, int pageArg, M_CONTROL *control, uchar done) {
    ShadowLog("startls(%s,%x,%x,%x,%x,%x,%x,%x,%x,%s,%x)", A((uint32_t)(uintptr_t)codename), operation, stale & 0xff,
              m, slot, mask, lsSlot, page, pageArg, A((uint32_t)(uintptr_t)control), done);
}

// The original's Menu_StartLS: the codename in EAX, the operation in ECX, the stale flag in DL
__declspec(naked) void FakeStartLS(void) {
    _asm {
        push dword ptr [esp + 0x20]
        push dword ptr [esp + 0x20]
        push dword ptr [esp + 0x20]
        push dword ptr [esp + 0x20]
        push dword ptr [esp + 0x20]
        push dword ptr [esp + 0x20]
        push dword ptr [esp + 0x20]
        push dword ptr [esp + 0x20]
        push edx
        push ecx
        push eax
        call FakeStartLSCore
        add esp, 0x2c
        ret
    }
}

struct Fake {
    unsigned at;
    const void *fake;
};

const Fake kFakes[] = {
    {0x00072820, (const void *)&FakeMenuSend},
    {0x000728f0, (const void *)&FakeMenuSendEx},
    {0x000729e0, (const void *)&FakeMenuSendEx2},
    {0x00072630, (const void *)&FakeSendMessage},
    {0x00092c40, (const void *)&FakeManagerSend},
    {0x000732b0, (const void *)&FakeSendDelayed},
    {0x00073240, (const void *)&FakeSendDelayedMessage},
    {0x0006d460, (const void *)&FakeBindLabel},
    {0x0006d450, (const void *)&FakeSetLanguage},
    {0x0006d140, (const void *)&FakeStringHeap},
    {0x0006d130, (const void *)&FakeGetLanguage},
    {0x000c5090, (const void *)&FakeSfxGetVolume},
    {0x000c5060, (const void *)&FakeSfxSetVolume},
    {0x000c6360, (const void *)&FakeMusicGetVolume},
    {0x000c6350, (const void *)&FakeMusicSetVolume},
    {0x000c5400, (const void *)&FakeSfxSetLanguage},
    {0x000c5030, (const void *)&FakeFadeDown},
    {0x000c9a10, (const void *)&FakeUnPause},
    {0x000c6250, (const void *)&FakeUnPauseStreams},
    {0x000c6200, (const void *)&FakePauseStreams},
    {0x000dccd0, (const void *)&FakeStartMovie},
    {0x000dccc0, (const void *)&FakeStopMovie},
    {0x000dcd90, (const void *)&FakeMovieFinished},
    {0x0007fda0, (const void *)&FakeRestartLoop},
    {0x000c9b10, (const void *)&FakeSfxRemove},
    {0x00072f40, (const void *)&FakeNoInput},
    {0x00072f50, (const void *)&FakeResetNoInput},
    {0x00072f30, (const void *)&FakeLastController},
    {0x00095de0, (const void *)&FakeProcessCreate},
    {0x000942b0, (const void *)&FakeManagerDelete},
    {0x00092b80, (const void *)&FakeGetStatus},
    {0x000bddf0, (const void *)&FakeLevelToLoad},
    {0x0006abf0, (const void *)&FakePushState},
    {0x0009bdc0, (const void *)&FakeBaseMap},
    {0x0009be00, (const void *)&FakeMissionStatus},
    {0x0009be20, (const void *)&FakeNumObjectives},
    {0x0009be90, (const void *)&FakeObjectiveState},
    {0x000b0900, (const void *)&FakeGetScore},
    {0x000b1240, (const void *)&FakeDoneBetter},
    {0x000b1200, (const void *)&FakeLevelTotals},
    {0x0007cbf0, (const void *)&FakeSetLevelBonus},
    {0x000757d0, (const void *)&FakeUpdateMessageBox},
    {0x0007edf0, (const void *)&FakeSelectCodename},
    {0x000754c0, (const void *)&FakePutCodenames},
    {0x0007eee0, (const void *)&FakeMapDefault},
    {0x0007fef0, (const void *)&FakeStartLS},
    {0x0008fcc0, (const void *)&FakeCheckSaveSpace},
    {0x0008fd90, (const void *)&FakeDashboard},
    {0x000dbdc0, (const void *)&FakeSaveChoice},
    {0x00072f60, (const void *)&FakeChangeStyle},
    {0x0007ffa0, (const void *)&FakeMPScore},
    {0x000ae700, (const void *)&FakeRamLoad},
    {0x000abbd0, (const void *)&FakePlayerEnable},
    {0x000abd70, (const void *)&FakePlayerDisable},
    {0x00024110, (const void *)&FakePushStates},
    {0x000257b0, (const void *)&FakePopStates},
    {0x00024150, (const void *)&FakeCameraEnable},
    {0x000c36d0, (const void *)&FakeScriptUpdate},
    {0x000c26d0, (const void *)&FakeScriptIsPlaying},
    {0x000c1550, (const void *)&FakeScriptLoad},
    {0x000c2660, (const void *)&FakeScriptPlay},
    {0x000be600, (const void *)&FakeIsLoadable},
    {0x000bff90, (const void *)&FakeLoadableLoad},
    {0x0006b500, (const void *)&FakeGetItem},
    {0x0002dd00, (const void *)&FakeMovementHandler},
    {0x00094cf0, (const void *)&FakeMemoUpdate},
    {0x00090fc0, (const void *)&FakeListUpdate},
    {0x00070fe0, (const void *)&FakeButtonUpdate},
    {0x00076240, (const void *)&FakeNightfireStatus},
    {0x000ee382, (const void *)&FakeSprintf},
    {0x000cb150, (const void *)&FakeSfxStart},
    {0x000dfbc0, (const void *)&FakeFreeBlocks},
    {0x000dfbd0, (const void *)&FakeBlocksFor},
    {0x000e9350, (const void *)&FakeLaunch},
    {0x0006ec10, (const void *)&FakeSaveStuff},
    {0x00096090, (const void *)&FakeProcessDestroy},
    {0x0007f3d0, (const void *)&FakeGetMPSkins},
    {0x000d52c0, (const void *)&FakeRandom},
    {0x000dfbb0, (const void *)&FakeLoadingState},
    {0x0006eb20, (const void *)&FakeLSFlushStates},
    {0x000755d0, (const void *)&FakeFlushEnum},
};

// The fakes but those of the functions a group tests
const unsigned *fakeSkip;
int fakeSkipCount;

void InstallFakes(void) {
    for (const Fake &f : kFakes) {
        bool skip = false;
        for (int i = 0; i < fakeSkipCount; i++)
            skip |= fakeSkip[i] == f.at;
        if (!skip)
            ShadowHook_Install(f.at, f.fake);
    }
    // our _Menu_StartLS, which ours call by name
    ShadowHook_Install((unsigned)(uintptr_t)&_Menu_StartLS, (const void *)&FakeStartLSCore);
}

// ---- the made-up world

const HASHCODE kHashcodes[] = {
    (HASHCODE)0x10000137, (HASHCODE)0x10000138, (HASHCODE)0x100001a3, (HASHCODE)0x10000222, (HASHCODE)0x10000028,
    (HASHCODE)0x1000015e, (HASHCODE)0x10000025, (HASHCODE)0x100000e5, (HASHCODE)0x1000011a, (HASHCODE)0x10000120,
};
const HASHCODE kPages[] = {P_MAIN, P_CNMENU, P_NFSTATS, P_DOSSIER, P_NFSELECT, P_WINGAME, P_NFDFCTY, P_START};

void RandomWorld(void) {
    for (int i = 0; i < OBJECTS; i++) {
        Obj &o = world.objects[i];
        Fill(o.raw, sizeof(o.raw));
        o.c.hashcode = kHashcodes[R(COUNT(kHashcodes))];
        o.c.id = R(4) == 0 ? (int)R(0) : (int)R(18);
        o.c.type = R(3) == 0 ? ControlType_Scroll : R(2) ? ControlType_List : (uchar)R(16);
        o.c.state = (uchar)R(3);
        o.memo.lineHeight = (float)R(40) + (float)R(4) * 0.25f;
        o.c.parent = &world.objects[(i + 1) % OBJECTS].c;
    }
    Fill(&world.score, sizeof(world.score));
    world.score.statsTable = world.cats;
    for (int i = 0; i < 9; i++) {
        world.cats[i].target = R(4) == 0 ? R(0) : R(200);
        world.cats[i].maxPoints = R(1000);
        world.cats[i].achieved = R(4) == 0 ? R(0) : R(200);
        world.cats[i].rating = (float)R(10001) / 10000.0f;
        world.cats[i].points = (float)R(3000);
    }
    world.score.score = R(3) == 0 ? R(0) : R(60000);
    world.score.thresholdScoreBronze = R(20000);
    world.score.thresholdScoreSilver = world.score.thresholdScoreBronze + R(10000);
    world.score.thresholdScoreGold = world.score.thresholdScoreSilver + R(10000);
    world.score.thresholdScorePlatinum = world.score.thresholdScoreGold + R(10000);
    world.score.isAction = (char)R(2);
    for (int i = 0; i < COUNT(world.items); i++) {
        Fill(&world.items[i], sizeof(world.items[i]));
        world.items[i].identifier = R(3) ? R(16) : R(0);
    }
    Fill(&world.info, sizeof(world.info));
    world.bonus[0] = R(0);
    world.bonus[1] = R(0);
    Fill(world.player, sizeof(world.player));
    ((obj_tag *)world.player)->extraObjectData = world.bloke;
    world.queryAnswer = (int)R(0);
    for (int i = 0; i < 4; i++)
        sprintf(world.strings[i], "text %x", R(0x10000));

    // the game's state
    for (const auto &g : kGlobals)
        Fill((void *)(uintptr_t)g.at, g.size);
    M_MANAGER *mgrs = (M_MANAGER *)0x0025f1d0;
    for (int m = 0; m < 5; m++) {
        mgrs[m].startPlayer = (uchar)R(4);
    }
    MENU_LS &l = *(MENU_LS *)0x0017d540;
    l.slot = R(3) == 0 ? LS_SLOT_NEW : R(5);
    l.numCodenames = R(3) == 0 ? CODENAME_SLOTS_DEFAULT : R(32);
    l.operation = (uchar)R(5);
    for (int i = 0; i < 32; i++)
        sprintf(((char (*)[0x20])0x002251b8)[i], "code%x", R(0x1000));
    for (int i = 0; i < 2; i++)
        ((DEFAULT_CODENAME *)0x0017d588)[i].name = (Action_TranslatedText)R(0x400);
    U32_AT(0x0025d7a0) = R(4) == 0 ? R(0) : R(11);       // LanguageToLoad
    U32_AT(0x0025d7c0) = R(3) == 0 ? 0 : R(200);         // the movie's skip delay
    MPSettings.isMultiplayer = R(2);
    MPSettings.numPlayers = R(5);
    MPSettings.numBots = R(7);
    MPSettings.GameMode = (MultiplayerGameMode)(R(2) ? TEAMGAME : 0);
    for (int i = 0; i < 10; i++) {
        MPSettings.Player[i].TeamId = (MPTeam)R(3);
        sprintf(MPSettings.Player[i].Name, "agent%x", R(0x100));
    }
    for (int p = 0; p < 4; p++) {
        PlayerInputs[p].controlStyle = (short)(R(4) == 0 ? R(0) : R(10));
        PlayerInputs[p].inverted = (uchar)R(3);
    }
    PTR_AT(0x001f6654) = world.player;                    // glb_players[0]
    U32_AT(0x0017d604) = 0x13f;                           // the language page's flags at first: never written
    U8_AT(0x0025d78c) = (uint8_t)R(2);                    // menu_level_bonus_earned
    if (R(2))
        GameState.BaseMapHashCode = kLevels[R(COUNT(kLevels))];
    if (R(2))
        GameState.CurrentLevelHashcode = kLevels[R(COUNT(kLevels))];

    // the owned globals, ours (lent to the original before its run): the numbers and flags mostly small
    for (const Pair &p : kPairs) {
        Fill(p.ours, p.size);
        if (p.size == 1 && R(4) != 0)
            *(uint8_t *)p.ours = (uint8_t)R(3);
        else if (p.size == 4 && R(4) != 0)
            *(uint32_t *)p.ours = R(12);
    }
    results_score = R(3) ? &world.score : NULL;
    nis_script = R(3) ? (SCRIPTINFO *)world.script : NULL;
    iris_frame = (int)R(13) - 1;
    iris_tick = (int)R(5) - 1;
    style_list_next = (uint8_t)R(2);
    timer_text_next = (uint8_t)R(4);
    for (int i = 0; i < 18; i++)
        tweaks_text[i][R(0x20)] = 0;
    for (int i = 0; i < 15; i++)
        tweaks2_text[i][0x1f] = 0;
    bonus_text[R(0x84)] = 0;
    bonus_text_before[0x7f] = 0;
    results_title_text[0x7f] = 0;
    for (int i = 0; i < 14; i++)
        style_list_text[i][0x1f] = 0;

    // the case's inputs
    const Case *c = current;
    int n = 0;
    while (n < 8 && c->messages[n] != 0)
        n++;
    in.message = n == 0 ? 0 : R(12) == 0 ? R(0x80) : c->messages[R(n)];
    in.managerNum = (uchar)R(2);
    in.control = (int)R(OBJECTS);
    in.arg1 = (int)(uintptr_t)Object(R(OBJECTS));
    switch (in.message) {
        case MessageType_Select:
        case MessageType_AltSelect1:
            in.arg2 = (int)(uintptr_t)Object(R(OBJECTS));
            break;
        case MessageType_QueryBack:
            in.arg2 = (int)(uintptr_t)&world.queryAnswer;
            break;
        default:
            in.arg2 = R(3) ? kPages[R(COUNT(kPages))] : (int)R(0);
            break;
    }
    for (int i = 0; i < 8; i++)
        in.a[i] = R(0);
}

// ---- the runners

typedef bool (*Handler)(uchar, M_CONTROL *, uint, uint, int, int);

uint32_t RunHandler(bool original, unsigned at, Handler ours) {
    M_CONTROL *c = Object(in.control);
    if (original) {
        XbeOriginalScope scope_(at);
        return ORIGINAL(at, Handler)(in.managerNum, c, c->hashcode, in.message, in.arg1, in.arg2) & 0xff;
    }
    return ours(in.managerNum, c, c->hashcode, in.message, in.arg1, in.arg2) & 0xff;
}

#define HANDLER_CASE(name, at, ...)                                                                                   \
    uint32_t Run_##name(bool original) { return RunHandler(original, at, name##_Handler); }
HANDLER_CASE(P_LANGUAGE, 0x00084130)
HANDLER_CASE(C_LANGUAGE, 0x000843e0)
HANDLER_CASE(P_INTRO, 0x00085240)
HANDLER_CASE(P_START, 0x000854c0)
HANDLER_CASE(P_PARISENUM, 0x00085630)
HANDLER_CASE(P_ESTHERO, 0x000852f0)
HANDLER_CASE(P_MAIN, 0x00085780)
HANDLER_CASE(C_GONIGHTFIRE, 0x000858a0)
HANDLER_CASE(C_GOMULTIPLAYER, 0x00085930)
HANDLER_CASE(C_GOCODENAMES, 0x000859c0)
HANDLER_CASE(P_TRAILER, 0x0008dca0)
HANDLER_CASE(P_FMVPLAYER, 0x00085cd0)
HANDLER_CASE(P_WINGAME, 0x0008ddd0)
HANDLER_CASE(P_CHEATMEDAL, 0x00083d10)
HANDLER_CASE(P_NFSELECT, 0x00085d50)
HANDLER_CASE(C_SBNFCN, 0x000861a0)
HANDLER_CASE(C_LBMSGOPTIONS, 0x000761c0)
HANDLER_CASE(P_PAUSE, 0x00080030)
HANDLER_CASE(C_GCPAUSE, 0x00080590)
HANDLER_CASE(P_ENDMISSION, 0x00083e50)
HANDLER_CASE(P_NFRESULTS, 0x00084600)
HANDLER_CASE(P_NFBONUS, 0x00084be0)
HANDLER_CASE(P_NFSTATS, 0x00084e10)
HANDLER_CASE(P_TWEAKS, 0x00082760)
HANDLER_CASE(P_TWEAKS2, 0x000833c0)
HANDLER_CASE(P_NIS, 0x00082120)
HANDLER_CASE(C_NIS, 0x00082190)
HANDLER_CASE(C_LBPMMAP, 0x00081930)
HANDLER_CASE(P_CNCONTROLS, 0x0008ce70)
HANDLER_CASE(C_RBCONTROL, 0x0008d000)
HANDLER_CASE(P_CNOPTIONS, 0x0008d0f0)
HANDLER_CASE(P_CNMPOPTIONS, 0x0008d560)
HANDLER_CASE(P_CNAVOPTIONS, 0x0008d7a0)
HANDLER_CASE(C_RBDSRECORDS, 0x00086ed0)
HANDLER_CASE(C_RBDSREWARDS, 0x00087140)
HANDLER_CASE(C_MPDBG, 0x0008bdd0)

typedef void (__cdecl *PlayMovieFn)(HASHCODE, uchar, int, uchar, char, uint);
uint32_t Run_PlayMovie(bool original) {
    CALL_VOID(0x0007fe00, Menu_PlayMovie, PlayMovieFn, (HASHCODE)in.a[0], (uchar)in.a[1], (int)(in.a[2] & 0x101),
              (uchar)(in.a[3] & 1), (char)in.a[4], in.a[5] & 0xff);
    return 0;
}
uint32_t Run_StopMovie(bool original) {
    CALL_VOID(0x0007fea0, Menu_StopMovie, void (__cdecl *)(char), (char)(in.a[0] % 3));
    return 0;
}
uint32_t Run_MovieCounter(bool original) {
    CALL_VOID(0x0007fdf0, Menu_UpdateMovieCounter, void (__cdecl *)(void));
    return 0;
}
uint32_t Run_SoundLanguage(bool original) {
    CALL_VOID(0x00084080, Menu_SetSoundLanguage, void (__cdecl *)(int), (int)(in.a[0] % 12) - 1);
    return 0;
}
uint32_t Run_LanguageButton(bool original) {
    return CALL(0x000840d0, Menu_GetLanguageButton, int (__cdecl *)(int), in.a[0] % 4 ? (int)(in.a[2] % 20) - 2 : (int)in.a[1]);
}
uint32_t Run_WillRun(bool original) {
    return CALL(0x0007ccf0, Menu_WillRunMiniMission, uchar (__cdecl *)(void)) & 0xff;
}
uint32_t Run_RunMini(bool original) {
    return CALL(0x0007cd50, Menu_RunMiniMission, uchar (__cdecl *)(uchar), in.managerNum) & 0xff;
}
uint32_t Run_StartIris(bool original) {
    CALL_VOID(0x0007f260, Menu_StartIris, void (__cdecl *)(uint, uchar, uint), in.a[0] % 6, in.managerNum, in.a[1]);
    return 0;
}
uint32_t Run_PlayIris(bool original) {
    CALL_VOID(0x00075c80, Menu_PlayIris, void (__cdecl *)(char, uchar, uint), (char)(in.a[0] % 3), in.managerNum, in.a[1]);
    return 0;
}
uint32_t Run_CloseIris(bool original) {
    CALL_VOID(0x0007f320, Menu_ChangePageCloseIris, void (__cdecl *)(HASHCODE, uchar, uint), (HASHCODE)in.a[0],
              in.managerNum, in.a[1]);
    return 0;
}
uint32_t Run_SelectItem(bool original) {
    M_CONTROL *c = Object(in.control);
    M_ITEM *list = in.a[0] % 8 ? world.items : NULL;
    int value = in.a[1] % 4 ? (int)(in.a[1] % 18) : (int)in.a[2];
    return CALL(0x000759d0, Menu_SelectItemInControl, bool (__cdecl *)(M_CONTROL *, M_ITEM *, ushort, int), c,
                list, (ushort)(in.a[3] % 13), value) & 0xff;
}
uint32_t Run_ItemFromHash(bool original) {
    int value = in.a[1] % 4 ? (int)(in.a[1] % 18) : (int)in.a[2];
    M_ITEM *item = (M_ITEM *)(uintptr_t)CALL(0x0007cf70, Menu_GetItemFromHash, M_ITEM *(__cdecl *)(M_ITEM *, int, uint),
                                             world.items, value, in.a[3] % 13);
    return item == NULL ? 0xffffffffu : (uint32_t)(item - world.items);
}
uint32_t Run_IsBotGood(bool original) {
    return CALL(0x0007eae0, Menu_IsBotGood, bool (__cdecl *)(uint), in.a[0] % 29) & 0xff;
}
uint32_t Run_CreateBox(bool original) {
    CALL_VOID(0x00075fb0, Menu_CreateOptionBox, void (__cdecl *)(uchar, const char *, uint, char, char), in.managerNum,
              world.strings[in.a[0] % 4], in.a[1] % 16, (char)(in.a[2] % 3), (char)(in.a[3] % 3));
    return 0;
}
uint32_t Run_CreateBoxLabel(bool original) {
    CALL_VOID(0x0007f3a0, Menu_CreateOptionBoxLabel, void (__cdecl *)(uchar, Action_TranslatedText, uint, char, char),
              in.managerNum, (Action_TranslatedText)(in.a[0] % 0x400), in.a[1] % 16, (char)(in.a[2] % 3), (char)(in.a[3] % 3));
    return 0;
}
uint32_t Run_UpdateBox(bool original) {
    uint *type = in.a[0] % 3 ? &world.type : NULL;
    return CALL(0x000761a0, Menu_UpdateOptionBox, uint (__cdecl *)(uint *), type);
}
uint32_t Run_LevelBonuses(bool original) {
    HASHCODE level = in.a[0] % 5 ? kLevels[in.a[1] % COUNT(kLevels)] : (HASHCODE)in.a[2];
    uint32_t p = CALL(0x0007f500, Menu_GetLevelBonuses, LEVEL_BONUSES *(__cdecl *)(HASHCODE), level);
    return p == (original ? 0x00225080u : (uint32_t)(uintptr_t)&level_bonuses) ? 1 : p;
}
uint32_t Run_HasMedal(bool original) {
    HASHCODE level = in.a[0] % 5 ? kLevels[in.a[1] % COUNT(kLevels)] : (HASHCODE)in.a[2];
    return CALL(0x0007ce80, Menu_HasMedal, bool (__cdecl *)(HASHCODE, uint, uchar), level, in.a[3] % 7,
                (uchar)(in.a[4] % 4)) & 0xff;
}
uint32_t Run_BestMedal(bool original) {
    HASHCODE level = in.a[0] % 5 ? kLevels[in.a[1] % COUNT(kLevels)] : (HASHCODE)in.a[2];
    return CALL(0x0007ce20, Menu_GetBestMedal, int (__cdecl *)(HASHCODE, uchar), level, (uchar)(in.a[4] % 4));
}
uint32_t Run_UpdateDefault(bool original) {
    CALL_VOID(0x0007f100, Menu_UpdateDefaultCodename, void (__cdecl *)(uchar, uchar), (uchar)(in.a[0] % 4),
              (uchar)(in.a[1] % 2));
    return 0;
}
uint32_t Run_RewardCounter(bool original) {
    HASHCODE level = in.a[0] % 5 ? kLevels[in.a[1] % COUNT(kLevels)] : (HASHCODE)in.a[2];
    CALL_VOID(0x000b1740, PlrStarts_ProcessRewardCounter,
              void (__cdecl *)(ulong *, HASHCODE, uint, char, REWARDINFO_tag *), world.bonus, level,
              in.a[3] % 8 ? in.a[3] % 7 : in.a[4], (char)(in.a[5] % 3), &world.info);
    return 0;
}
uint32_t Run_TableSize(bool original) {
    return CALL(0x000b19a0, PlrStarts_GetRewardsTableSize, uint (__cdecl *)(void));
}
uint32_t Run_SetUpgrade(bool original) {
    uchar player = (uchar)(1 + in.a[0] % 3);
    short objId = (short)(in.a[1] % 4 ? (int)(in.a[1] % 40) - 2 : (int)(in.a[2] % 60));
    CALL_VOID(0x000b9e30, Set_Upgrade, void (__cdecl *)(uchar, short, uchar), player, objId, (uchar)in.a[3]);
    return 0;
}
uint32_t Run_ResetUpgrade(bool original) {
    CALL_VOID(0x000b9e60, ResetUpgrade, void (__cdecl *)(uchar), (uchar)(in.a[0] % 4));
    return 0;
}
uint32_t Run_Timer(bool original) {
    int hundredths;
    switch (in.a[0] % 6) {
        case 0: hundredths = (int)in.a[1]; break;
        case 1: hundredths = -(int)(in.a[1] % 1000); break;
        default: hundredths = (int)(in.a[1] % 2000000); break;
    }
    char *text = (char *)(uintptr_t)CALL(0x000d25d0, Timer_Seconds2String, char *(__cdecl *)(int, int), hundredths,
                                         (int)(in.a[2] % 7) - 1);
    char *base = original ? (char *)0x0029ae38 : timer_text[0];
    return (uint32_t)(text - base);
}
uint32_t Run_AddRow(bool original) {
    CALL_VOID(0x00073a80, Menu_AddRow, void (__cdecl *)(uchar, Action_TranslatedText, int, int, uint, char, char),
              in.managerNum, (Action_TranslatedText)(in.a[0] % 0x400), (int)(in.a[1] % 100000) - 20, (int)(in.a[2] % 100000),
              in.a[3] % 4 ? in.a[3] % 100000 : in.a[3], (char)(in.a[4] % 2), (char)(in.a[5] % 3));
    return 0;
}
uint32_t Run_AddRowPct(bool original) {
    CALL_VOID(0x00073bc0, Menu_AddRowPercentage, void (__cdecl *)(uchar, Action_TranslatedText, uint, int, uint, char),
              in.managerNum, (Action_TranslatedText)(in.a[0] % 0x400), in.a[1] % 120,
              in.a[2] % 3 ? (int)(in.a[2] % 101) : -1, in.a[3] % 100000, (char)(in.a[4] % 2));
    return 0;
}
uint32_t Run_CodenameWheel(bool original) {
    M_CONTROL *wheel = in.a[0] % 10 ? Object(in.control) : NULL;
    CALL_VOID(0x0007fb20, Menu_UpdateCodenameWheel,
              void (__cdecl *)(uchar, M_CONTROL *, HASHCODE, HASHCODE, HASHCODE, HASHCODE, char), in.managerNum, wheel,
              (HASHCODE)0x100000e4, (HASHCODE)(in.a[1] % 3 ? 0x100001cc : 0), (HASHCODE)(in.a[2] % 3 ? 0x100001f2 : 0),
              (HASHCODE)0x10000103, (char)(in.a[3] % 2));
    return 0;
}
uint32_t Run_DisplayStyle(bool original) {
    CALL_VOID(0x00073d10, Menu_DisplayControllerStyle, void (__cdecl *)(uchar, uint), in.managerNum,
              in.a[0] % 8 ? in.a[0] % 10 : in.a[1]);
    return 0;
}
uint32_t Run_DisplayStyleList(bool original) {
    CALL_VOID(0x000749c0, Menu_DisplayControllerStyleList, void (__cdecl *)(uchar, uint), in.managerNum,
              in.a[0] % 8 ? in.a[0] % 10 : in.a[1]);
    return 0;
}
uint32_t Run_StyleLabel(bool original) {
    style_list_next = (uint8_t)(in.a[3] % 12);
    if (original)
        U8_AT(0x002245f0) = style_list_next;
    CALL_VOID(0x00074910, Menu_UpdateControllerLabelString,
              void (__cdecl *)(uchar, const char *, Action_TranslatedText, const char *, Action_TranslatedText),
              in.managerNum, world.strings[in.a[0] % 4], (Action_TranslatedText)(in.a[1] % 0x400),
              in.a[2] % 3 ? world.strings[in.a[2] % 4] : NULL, (Action_TranslatedText)(in.a[4] % 0x400));
    return 0;
}

// ---- the front end's helpers: the music, the movie's end, the bonus, the codenames, the MP join and scores, the
// ---- room for a save

uint32_t Run_StopMusic(bool original) {
    U8_AT(0x0025d7bc) = (uint8_t)R(2);
    CALL_VOID(0x0007fd70, Menu_StopFrontEndMusic, void (__stdcall *)(void));
    return 0;
}
uint32_t Run_RestartLoop(bool original) {
    U8_AT(0x0025d7bc) = (uint8_t)R(2);
    CALL_VOID(0x0007fda0, Menu_RestartFrontEndLoop, void (__stdcall *)(void));
    return 0;
}
uint32_t Run_MovieLoop(bool original) {
    U8_AT(0x0025d7bc) = (uint8_t)R(2);
    U8_AT(0x0025d7be) = (uint8_t)R(2);
    return CALL(0x0007fe70, psiMovieLoop, bool (__cdecl *)(void)) & 0xff;
}
uint32_t Run_GetBonus(bool original) {
    for (int i = 0; i < 8; i++)
        U32_AT((0x0025d6e8 + 4 * i)) = R(0);
    byte player = (byte)R(4);
    uint64_t bonus = original ? ([&]() { XbeOriginalScope scope_(0x0007c660); return ORIGINAL(0x0007c660, uint64_t (__cdecl *)(byte))(player); }())
                              : Menu_GetBonus(player);
    return (uint32_t)bonus ^ (uint32_t)(bonus >> 32) * 3;
}
uint32_t Run_CodenameExists(bool original) {
    static const char *names[] = {"BOND", "Bond", "M", "QBRANCH", "ALURA", ""};
    ls.numCodenames = R(8);
    for (int i = 0; i < 8; i++) {
        memset(codename_buf[i], 0, 0x20);
        strcpy(codename_buf[i], names[R(COUNT(names))]);
    }
    strcpy(world.strings[0], names[R(COUNT(names))]);
    return CALL(0x00075f40, Menu_CodenameExists, bool (__cdecl *)(const char *), world.strings[0]) & 0xff;
}
uint32_t Run_FadeWhenReady(bool original) {
    CALL_VOID(0x00075480, Menu_MPFadeWhenReady, void (__cdecl *)(M_CONTROL *), Object(R(OBJECTS)));
    return 0;
}
uint32_t Run_AllReady(bool original) {
    for (int i = 0; i < NUM_PLAYERS; i++) {
        mp_join_slots[i].joined = (char)(R(2) ? 0 : R(3));
        mp_join_slots[i].ready = (char)(R(2) ? 0 : R(3));
        mp_join_slots[i].controllerPort = (uchar)R(256);
    }
    return CALL(0x000753f0, Menu_AllJoinedPlayersReady, bool (__stdcall *)(void)) & 0xff;
}
uint32_t Run_MPScore(bool original) {
    static const uint modes[] = {GM_QUICK, GM_ARENA, GM_TOPAGENT, GM_UNK3, GM_ASSASSIN, TEAMGAME, GM_TEAMARENA, GM_CTF,
                                 GM_DEMOLITION, GM_PROTECTION, GM_BLUEPRINT, GM_GOLDENEYE, GM_KOTH, GM_UPLINK,
                                 GM_TEAMKOTH, 0x20000001, 0x20000010, 0x20000400, 2};
    byte p = (byte)R(NUM_AGENTS);
    MPSettings.GameMode = (MultiplayerGameMode)modes[R(COUNT(modes))];
    MPGame.players[p].victories = R(30);
    uint32_t bits = R(4) == 0 ? R(0) : 0;
    float points = (float)(int)R(300) - 50.0f + (float)R(4) * 0.25f;
    if (bits != 0)
        memcpy(&points, &bits, 4);
    MPGame.players[p].points = points;
    return CALL(0x0007ffa0, Menu_GetMPScore, int (__cdecl *)(byte), p);
}
uint32_t Run_SaveSize(bool original) {
    return CALL(0x0006f950, LS_GetSaveSize, int (__stdcall *)(void)) & 0xffff;
}
uint32_t Run_CheckSpace(bool original) {
    SavesListed = R(3) == 0 ? 0xffc + R(3) : R(100);
    save_space_problem = U32_AT(0x0025edbc) = R(3);
    return CALL(0x0008fcc0, Menu_CheckSaveSpace, bool (__cdecl *)(uchar, MENU_LS *), (uchar)R(2), &ls) & 0xff;
}
uint32_t Run_Dashboard(bool original) {
    save_space_problem = U32_AT(0x0025edbc) = R(4);
    CALL_VOID(0x0008fd90, Menu_GoToDashboard, void (__cdecl *)(void));
    return 0;
}

// Messages: 0x49 Scroll, 0x4b Select, 0x4c Enter, 0x4d Leave, 0x4e GainFocus, 0x50 Update, 0x51 Created, 0x54 ValueSet,
// 0x5d AltSelect1, 0x5e AltSelect2, 0x6a SaveFlowDone, 0x6b QueryBack, 0x6d BackOnControl, 0x6e Resume
#define IRIS 0x0007f260, 0x00075c80, 0x0007f320
#define BOX 0x000761a0, 0x00075fb0, 0x0007f3a0

const Case kFrontEnd[] = {
    {"P_LANGUAGE", Run_P_LANGUAGE, {0}, {0x4c, 0x50}},
    {"C_LANGUAGE", Run_C_LANGUAGE, {0}, {0x4e, 0x4b}},
    {"P_INTRO", Run_P_INTRO, {0}, {0x4c, 0x50}},
    {"P_START", Run_P_START, {0}, {0x4c, 0x50}},
    {"P_PARISENUM", Run_P_PARISENUM, {0x000761a0, 0x0007ccf0, 0x0007cd50}, {0x4c, 0x50}},
    {"P_ESTHERO", Run_P_ESTHERO, {0}, {0x4c, 0x50}},
    {"P_MAIN", Run_P_MAIN, {0}, {0x4c, 0x50}},
    {"C_GONIGHTFIRE", Run_C_GONIGHTFIRE, {0}, {0x4b}},
    {"C_GOMULTIPLAYER", Run_C_GOMULTIPLAYER, {0}, {0x4b}},
    {"C_GOCODENAMES", Run_C_GOCODENAMES, {0}, {0x4b}},
    {"Menu_SetSoundLanguage", Run_SoundLanguage, {0}, {0}},
    {"Menu_GetLanguageButton", Run_LanguageButton, {0}, {0}},
    {"Menu_WillRunMiniMission", Run_WillRun, {0}, {0}},
    {"Menu_RunMiniMission", Run_RunMini, {0}, {0}},
};
const Case kMovies[] = {
    {"Menu_PlayMovie", Run_PlayMovie, {0}, {0}},
    {"Menu_StopMovie", Run_StopMovie, {0}, {0}},
    {"Menu_UpdateMovieCounter", Run_MovieCounter, {0}, {0}},
    {"P_TRAILER", Run_P_TRAILER, {0}, {0x4b, 0x4c, 0x50}},
    {"P_FMVPLAYER", Run_P_FMVPLAYER, {0}, {0x4c, 0x50}},
    {"P_WINGAME", Run_P_WINGAME, {0}, {0x4c, 0x50}},
};
const Case kIris[] = {
    {"Menu_StartIris", Run_StartIris, {0x00075c80}, {0}},
    {"Menu_PlayIris", Run_PlayIris, {0}, {0}},
    {"Menu_ChangePageCloseIris", Run_CloseIris, {0x00075c80, 0x0007f260}, {0}},
    {"Menu_SelectItemInControl", Run_SelectItem, {0}, {0}},
    {"Menu_GetItemFromHash", Run_ItemFromHash, {0}, {0}},
    {"Menu_IsBotGood", Run_IsBotGood, {0}, {0}},
    {"Menu_CreateOptionBox", Run_CreateBox, {0}, {0}},
    {"Menu_CreateOptionBoxLabel", Run_CreateBoxLabel, {0x00075fb0}, {0}},
    {"Menu_UpdateOptionBox", Run_UpdateBox, {0}, {0}},
    {"C_LBMSGOPTIONS", Run_C_LBMSGOPTIONS, {0}, {0x4b, 0x6d}},
};
const Case kCodenames[] = {
    {"Menu_UpdateCodenameWheel", Run_CodenameWheel, {IRIS}, {0}},
    {"Menu_UpdateDefaultCodename", Run_UpdateDefault, {0}, {0}},
    {"P_NFSELECT", Run_P_NFSELECT, {IRIS, 0x000761a0, 0x0007fb20}, {0x4c, 0x6e, 0x50, 0x6a, 0x6b}},
    {"C_SBNFCN", Run_C_SBNFCN, {IRIS, 0x0007fb20, 0x00075fb0, 0x0007f3a0}, {0x49, 0x54, 0x4b, 0x5d}},
};
const Case kRewards[] = {
    {"PlrStarts_ProcessRewardCounter", Run_RewardCounter, {0}, {0}},
    {"PlrStarts_GetRewardsTableSize", Run_TableSize, {0}, {0}},
    {"Menu_GetLevelBonuses", Run_LevelBonuses, {0}, {0}},
    {"Menu_HasMedal", Run_HasMedal, {0}, {0}},
    {"Menu_GetBestMedal", Run_BestMedal, {0}, {0}},
    {"Set_Upgrade", Run_SetUpgrade, {0}, {0}},
    {"ResetUpgrade", Run_ResetUpgrade, {0}, {0}},
    {"C_RBDSRECORDS", Run_C_RBDSRECORDS, {0x0007f500}, {0x49}},
    {"C_RBDSREWARDS", Run_C_RBDSREWARDS, {0x0007f500}, {0x49, 0x54}},
};
const Case kResults[] = {
    {"Timer_Seconds2String", Run_Timer, {0}, {0}},
    {"Menu_AddRow", Run_AddRow, {0x000d25d0}, {0}},
    {"Menu_AddRowPercentage", Run_AddRowPct, {0}, {0}},
    {"P_NFRESULTS", Run_P_NFRESULTS, {0x000761a0}, {0x4c, 0x4b, 0x50, 0x5d, 0x5e}},
    {"P_NFBONUS", Run_P_NFBONUS, {0x0007f500}, {0x4c, 0x4b, 0x5d, 0x5e}},
    {"P_NFSTATS", Run_P_NFSTATS, {0x00073a80, 0x00073bc0, 0x000d25d0}, {0x4c}},
};
const Case kOptions[] = {
    {"Menu_DisplayControllerStyle", Run_DisplayStyle, {0}, {0}},
    {"Menu_DisplayControllerStyleList", Run_DisplayStyleList, {0x00074910}, {0}},
    {"Menu_UpdateControllerLabelString", Run_StyleLabel, {0}, {0}},
    {"P_CNCONTROLS", Run_P_CNCONTROLS, {0}, {0x4c}},
    {"C_RBCONTROL", Run_C_RBCONTROL, {0}, {0x49, 0x4b, 0x5e}},
    {"P_CNOPTIONS", Run_P_CNOPTIONS, {0}, {0x4c, 0x4b}},
    {"P_CNMPOPTIONS", Run_P_CNMPOPTIONS, {0}, {0x4c, 0x4b}},
    {"P_CNAVOPTIONS", Run_P_CNAVOPTIONS, {0}, {0x4c, 0x4d, 0x50, 0x4b}},
};
const Case kPause[] = {
    {"P_PAUSE", Run_P_PAUSE, {0x000761a0}, {0x6b, 0x50, 0x4c}},
    {"C_GCPAUSE", Run_C_GCPAUSE, {0x000d25d0, 0x000749c0, 0x00074910, 0x00075fb0, 0x0007f3a0}, {0x4e, 0x4b, 0x49, 0x5e}},
    {"P_ENDMISSION", Run_P_ENDMISSION, {0}, {0x4c, 0x4b}},
};
const Case kFrontEndHelpers[] = {
    {"Menu_StopFrontEndMusic", Run_StopMusic, {0}, {0}},
    {"Menu_RestartFrontEndLoop", Run_RestartLoop, {0}, {0}},
    {"psiMovieLoop", Run_MovieLoop, {0}, {0}},
    {"Menu_GetBonus", Run_GetBonus, {0}, {0}},
    {"Menu_CodenameExists", Run_CodenameExists, {0}, {0}},
    {"Menu_MPFadeWhenReady", Run_FadeWhenReady, {0}, {0}},
    {"Menu_AllJoinedPlayersReady", Run_AllReady, {0}, {0}},
    {"Menu_GetMPScore", Run_MPScore, {0}, {0}},
    {"C_MPDBG", Run_C_MPDBG, {0}, {0x4b, 0x5d, 0x4c}},
    {"LS_GetSaveSize", Run_SaveSize, {0}, {0}},
    {"Menu_CheckSaveSpace", Run_CheckSpace, {0x00075fb0}, {0}},
    {"Menu_GoToDashboard", Run_Dashboard, {0}, {0}},
};
const unsigned kFrontEndHelperFakes[] = {0x0007fda0, 0x0007ffa0, 0x0008fcc0, 0x0008fd90};

// ---- the movie list, the multiplayer join and setup pages and the rules

#define mp_join_step (*(int(*)[4])0x00245698)
#define mp_good_bot_taken U8_AT(0x002456b0)

HANDLER_CASE(P_FMVTEST, 0x00085a50)

// The join pages' state made small: players 0-3 (now and then a byte with more above it), steps 0-5, the codename
// loading a player's, a codename's end, the mode now and then Top Agent
void SmallJoinState(void) {
    M_CONTROL *c = Object(in.control);
    c->id = (int)(in.a[0] % 4) | (in.a[0] % 7 == 0 ? 0x300 : 0);
    ls.mpPlayer = (uchar)(1 + in.a[1] % 4);
    ls.codename[in.a[1] % 32] = 0;
    for (int i = 0; i < 4; i++) {
        mp_join_step[i] = (int)((in.a[2] >> (4 * i)) % 6);
        mp_join_slots[i].joined = (char)((in.a[3] >> i) & 1);
        mp_join_slots[i].ready = (char)((in.a[3] >> (i + 4)) & 1);
        PlayerInputs[i].controllerIsActive = ((in.a[3] >> (i + 8)) & 3) != 0;
    }
    if (in.a[4] % 3 == 0)
        MPSettings.GameMode = GM_TOPAGENT;
    if (in.a[4] % 5 == 0)
        mp_good_bot_taken = (uint8_t)(in.a[5] % 14);
}

uint32_t RunJoinHandler(bool original, unsigned at, Handler ours) {
    SmallJoinState();
    M_CONTROL *c = Object(in.control);
    int arg2 = in.message == MessageType_SaveFlowDone && in.a[6] % 2 ? 8 : in.arg2;
    if (original) {
        XbeOriginalScope scope_(at);
        return ORIGINAL(at, Handler)(in.managerNum, c, c->hashcode, in.message, in.arg1, arg2) & 0xff;
    }
    return ours(in.managerNum, c, c->hashcode, in.message, in.arg1, arg2) & 0xff;
}
#define JOIN_CASE(name, at)                                                                                               uint32_t Run_##name(bool original) { return RunJoinHandler(original, at, name##_Handler); }
JOIN_CASE(P_MPJOIN, 0x00087880)
JOIN_CASE(C_RBMPSTART, 0x00087dd0)
JOIN_CASE(C_RBMPCNAME, 0x000880a0)
JOIN_CASE(P_MPSETUP, 0x000882c0)
JOIN_CASE(C_RBMPFINISH, 0x00088530)
JOIN_CASE(P_MPRULES, 0x00089b20)

uint32_t Run_UpdateControllers(bool original) {
    SmallJoinState();
    CALL_VOID(0x00075a60, Menu_UpdateMPControllers, void (__cdecl *)(uchar, uchar, char), in.managerNum,
              (uchar)(in.a[7] % 4), (char)(in.a[7] % 3 == 0 ? 0 : 1 + in.a[6] % 2));
    return 0;
}
uint32_t Run_BotShortName(bool original) {
    BotNum skin = in.a[0] % 4 == 0 ? in.a[1] : in.a[1] % 32;
    return CALL(0x0007eb90, Menu_GetBotShortName, char *(__cdecl *)(BotNum), skin) == (uint32_t)0x0015d3c0 ? 1 : 2;
}

// A frame of the save flow: a state, a frame count near the tips' turn, the box open or not, a text or none
uint32_t Run_SaveFlow(bool original) {
    uchar m = (uchar)(in.a[0] % 2);
    M_MANAGER *mgr = &((M_MANAGER *)0x0025f1d0)[m];
    mgr->index = m;
    uint state = in.a[1] % 12;
    U32_AT(0x0025edb0) = in.a[2] % 4 ? (in.a[2] >> 8) % 4 == 0 ? 299 + in.a[2] % 3 : in.a[2] % 700 : in.a[2];
    U8_AT(0x0025eda8) = (uint8_t)(in.a[3] & 1);
    U8_AT(0x0025edba) = (uint8_t)((in.a[3] >> 1) & 1);
    U32_AT(0x0025edac) = in.a[4] % 3 ? 0 : (uint32_t)(uintptr_t)world.strings[1];
    saveflow_tip = in.a[5] % 3 ? NULL : world.strings[2];
    U32_AT(0x0025efc8) = (uint32_t)(uintptr_t)saveflow_tip;
    saveflow_hold_until = in.a[6] % 3 ? 0 : in.a[6] % 900;
    U32_AT(0x0025edc4) = saveflow_hold_until;
    ls.operation = (uchar)(in.a[7] % 6);
    CALL_VOID(0x0008f3f0, XBox_DoSaveFlow, void (__cdecl *)(M_MANAGER *, uint, MENU_LS *), mgr, state, &ls);
    // The text pointing at the buffer, the game's or ours
    if (U32_AT(0x0025edac) == (original ? 0x0025edc8u : (uint32_t)(uintptr_t)saveflow_text_buffer))
        U32_AT(0x0025edac) = 1;
    return 0;
}

// Messages: 0x4b Select, 0x4c Enter, 0x50 Update, 0x5c PlayerFocusGained, 0x60 PlayerFocusLost, 0x6a SaveFlowDone,
// 0x6b QueryBack
const Case kJoin[] = {
    {"P_FMVTEST", Run_P_FMVTEST, {0}, {0x4c, 0x4b}},
    {"Menu_GetBotShortName", Run_BotShortName, {0}, {0}},
    {"Menu_UpdateMPControllers", Run_UpdateControllers, {0}, {0}},
    {"P_MPJOIN", Run_P_MPJOIN, {0x00075a60}, {0x4c, 0x50, 0x6a, 0x6b}},
    {"C_RBMPSTART", Run_C_RBMPSTART, {0}, {0x5c, 0x60, 0x4b, 0x6b}},
    {"C_RBMPCNAME", Run_C_RBMPCNAME, {0}, {0x6b, 0x5c, 0x4b}},
    {"P_MPSETUP", Run_P_MPSETUP, {0x00075a60}, {0x4c, 0x50}},
    {"C_RBMPFINISH", Run_C_RBMPFINISH, {0}, {0x5c, 0x60, 0x6b}},
    {"P_MPRULES", Run_P_MPRULES, {0}, {0x4c, 0x4b}},
    {"XBox_DoSaveFlow", Run_SaveFlow, {0}, {0}},
};

const Case kDebug[] = {
    {"P_CHEATMEDAL", Run_P_CHEATMEDAL, {0}, {0x4b, 0x4c}},
    {"P_TWEAKS", Run_P_TWEAKS, {0}, {0x4c, 0x50, 0x4b, 0x5d}},
    {"P_TWEAKS2", Run_P_TWEAKS2, {0}, {0x4c, 0x50, 0x4b}},
    {"P_NIS", Run_P_NIS, {0}, {0x50}},
    {"C_NIS", Run_C_NIS, {0}, {0x51, 0x4b, 0x5d}},
    {"C_LBPMMAP", Run_C_LBPMMAP, {0}, {0x49, 0x4b, 0x51}},
};

} // namespace

// The package's originals: each keeps its jump to ours through the suite (a scope opened twice on one address
// leaves the original in place for good)
const unsigned kPackage[] = {
    0x00073a80, 0x00073bc0, 0x00073d10, 0x00074910, 0x000749c0, 0x000759d0, 0x00075c80, 0x00075fb0, 0x000761a0,
    0x000761c0, 0x0007ccf0, 0x0007cd50, 0x0007ce20, 0x0007ce80, 0x0007cf70, 0x0007eae0, 0x0007f100, 0x0007f260,
    0x0007f320, 0x0007f3a0, 0x0007f500, 0x0007fb20, 0x0007fdf0, 0x0007fe00, 0x0007fea0, 0x00080030, 0x00080590,
    0x00081930, 0x00082120, 0x00082190, 0x00082760, 0x000833c0, 0x00083d10, 0x00083e50, 0x00084080, 0x000840d0,
    0x00084130, 0x000843e0, 0x00084600, 0x00084be0, 0x00084e10, 0x00085240, 0x000852f0, 0x000854c0, 0x00085630,
    0x00085780, 0x000858a0, 0x00085930, 0x000859c0, 0x00085cd0, 0x00085d50, 0x000861a0, 0x00086ed0, 0x00087140,
    0x0008ce70, 0x0008d000, 0x0008d0f0, 0x0008d560, 0x0008d7a0, 0x0008dca0, 0x0008ddd0, 0x000b1740, 0x000b19a0,
    0x000b9e30, 0x000b9e60, 0x000d25d0, 0x00075a60, 0x0007eb90, 0x00085a50, 0x00087880, 0x00087dd0, 0x000880a0,
    0x000882c0, 0x00088530, 0x00089b20, 0x0008f3f0
};

void MenuHandlerShadow_Run(void) {
    unsigned jumps[COUNT(kPackage)];
    for (int i = 0; i < COUNT(kPackage); i++)
        jumps[i] = XbeOriginal_Target(kPackage[i]);
    std::vector<std::vector<uint8_t>> saved;
    for (const auto &g : kGlobals)
        saved.emplace_back((uint8_t *)(uintptr_t)g.at, (uint8_t *)(uintptr_t)g.at + g.size);
    std::vector<uint8_t> savedOwned, savedGame;
    SaveAll(savedOwned, false);
    SaveAll(savedGame, true);
    uint32_t savedPlayer = U32_AT(0x001f6654);

    regions.Clear();
    regions.Add(&world, sizeof(world));
    for (const auto &g : kGlobals)
        regions.Add((void *)(uintptr_t)g.at, g.size);
    NfWarnMuted++;
    cases = differ = 0;
    ShadowHook_RemoveAll();
    InstallFakes();

    Test("the boot pages and the main menu", kFrontEnd, COUNT(kFrontEnd), 3000);
    Test("the movies", kMovies, COUNT(kMovies), 3000);
    Test("the iris, items and option boxes", kIris, COUNT(kIris), 3000);
    Test("the codename select", kCodenames, COUNT(kCodenames), 4000);
    Test("the rewards and the dossier's rows", kRewards, COUNT(kRewards), 4000);
    Test("the results and times", kResults, COUNT(kResults), 3000);
    Test("the options pages and controller styles", kOptions, COUNT(kOptions), 3000);
    Test("the pause menu", kPause, COUNT(kPause), 4000);
    Test("the debug page's tools", kDebug, COUNT(kDebug), 3000);
    ShadowHook_RemoveAll();
    fakeSkip = kFrontEndHelperFakes;
    fakeSkipCount = COUNT(kFrontEndHelperFakes);
    InstallFakes();
    plainLabels = true;
    Test("the front end's helpers", kFrontEndHelpers, COUNT(kFrontEndHelpers), 3000);
    plainLabels = false;
    fakeSkipCount = 0;
    ShadowHook_RemoveAll();
    InstallFakes();
    plainLabels = true;
    Test("the movie list, the multiplayer join and the rules", kJoin, COUNT(kJoin), 4000);
    plainLabels = false;
    ShadowHook_RemoveAll();
    for (int i = 0; i < COUNT(kPackage); i++)
        if (XbeOriginal_Target(kPackage[i]) != jumps[i])
            printf("[menuhandlers] 0x%08x lost its jump to ours\n", kPackage[i]);

    NfWarnMuted--;
    for (size_t i = 0; i < saved.size(); i++)
        memcpy((void *)(uintptr_t)kGlobals[i].at, saved[i].data(), saved[i].size());
    LoadAll(savedOwned, false);
    LoadAll(savedGame, true);
    U32_AT(0x001f6654) = savedPlayer;
}
