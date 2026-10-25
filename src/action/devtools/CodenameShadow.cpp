// Shadow tests for the codename screens and the cheat menu: the handlers and helpers in src/action/ui/ui_codenames.cpp
// (P_CNSELECT, C_SBCNSELECT, C_LBERROPTIONS, Menu_UpdateMessageBox, Menu_EnumSaves, Menu_FlushEnum,
// Menu_PutCodenamesIntoControl, Menu_MapDefaultCodename, Menu_SelectCodenameInControl, Menu_StartLS through its
// register adaptor), ui_cheats.cpp (the C_CHCH checkboxes), ui_keypad.cpp (C_KEYPAD) and Menu_IsValidCodename
// (ui_secrets.cpp).
//
// Each handler is fed every message it handles and some it does not, over made-up M_CONTROL / M_MANAGER / ls state,
// run as the original and as ours; the menu calls under it (the message sends, the iris, the option boxes, the save
// flow, the text, the saves, the player) are logging fakes whose answers each case sets, so this runs at injection
// time with no menus loaded. Compared: the return value, the calls made in order, the game memory they write (ls,
// codename_buf, the switches, the settings...) and the statics we own (lent to the original at the game's address
// and read back from ours). Run at start with MenuShadowTests=on (MenuProbe.cpp); everything is put back afterwards.

#include "CodenameShadow.h"
#include "AllOriginal.h"

#include "ShadowHarness.h"
#include "../../common/xbeOriginal.h"
#include "../engine/LoadSave.h"
#include "../engine/psiSave.h"
#include "../engine/Text.h"
#include "../game/sp/PlayerStats.h"
#include "../ui/HUD.h"
#include "../game.h"
#include "../input.h"
#include "../game/mp/multiplayer.h"
#include "../game/obj/Player.h"
#include "../ui/ui.h"
#include "../ui/Manager.h"
#include "../ui/Menu.h"

#include <stdio.h>
#include <string.h>

#include <map>
#include <string>
#include <vector>

// The statics the ports own (ui_cheats.cpp, ui_codenames.cpp, ui_keypad.cpp)
extern char hud_state, Drone_DebugSpr, zero_gravity;
extern int bDebugLockup, bTweakBrightness, bDebugControls;
extern uint ls_state, ls_delete_state, enum_state;
extern bool ls_updating;
extern uint8_t cnselect_leaving, cnselect_from_main_menu;
extern char keypad_text[8];

namespace {

typedef bool(__cdecl *HandlerFn)(uchar, M_CONTROL *, uint, uint, int, int);

struct Lent {
    unsigned game;
    void *ours;
    unsigned size;
    unsigned owner, owner2;   // the functions that use it: their originals use the game's copy
};
const Lent kLent[] = {
    {XbeOurs(0x0017d5f8), &hud_state, 1, 0x00081c60},        {XbeOurs(0x001dfa28), &Drone_DebugSpr, 1, 0x00081d30},
    {XbeOurs(0x0025d822), &zero_gravity, 1, 0x00081f80},     {XbeOurs(0x0017d5fc), &bDebugLockup, 4, 0x00081fe0},
    {XbeOurs(0x0025d7a4), &bTweakBrightness, 4, 0x00082030}, {XbeOurs(0x0017d600), &bDebugControls, 4, 0x000820d0},
    {XbeOurs(0x00224f70), &ls_state, 4, 0x000757d0},         {XbeOurs(0x00224f6c), &ls_delete_state, 4, 0x000757d0},
    {XbeOurs(0x00224f74), &ls_updating, 1, 0x000757d0},      {XbeOurs(0x00224f58), &enum_state, 4, 0x000755e0, 0x000755d0},
    {XbeOurs(0x0025ed21), &cnselect_leaving, 1, 0x0008be40}, {XbeOurs(0x0017d60a), &cnselect_from_main_menu, 1, 0x0008be40},
    {XbeOurs(0x0025dc48), keypad_text, 8, 0x00083fa0},
};
const int kLentCount = sizeof(kLent) / sizeof(kLent[0]);

int g_cases, g_differ;

void Report(const char *what, int trial, const char *detail) {
    g_differ++;
    static std::map<std::string, int> reported;
    if (reported[what]++ < 3)
        printf("[cnshadow] %s case %d: %s\n", what, trial, detail);
}

void RandomBytes(void *at, size_t size) {
    for (size_t i = 0; i < size; i++)
        *(uchar *)(size_t)XbeOurs((unsigned)(size_t)at + (unsigned)i) = (uchar)ShadowRand(256);
}

uint RandomWord(void) {
    return ShadowRand(0x10000) | ShadowRand(0x10000) << 16;
}

// ---- made-up controls, managers and players

struct FakeControl {
    M_CONTROL control;
    char rest[0x200];
};
FakeControl g_controls[16];
int g_controlCount;

// The control with that hashcode, made on first use
M_CONTROL *ControlFor(HASHCODE hashcode) {
    for (int i = 0; i < g_controlCount; i++)
        if (g_controls[i].control.hashcode == hashcode)
            return &g_controls[i].control;
    if (g_controlCount == 16)
        return &g_controls[15].control;
    FakeControl *c = &g_controls[g_controlCount++];
    memset(c, 0, sizeof(*c));
    c->control.hashcode = hashcode;
    c->control.type = ControlType_Scroll;
    return &c->control;
}

const char *Name(const void *p) {
    static char text[4][24];
    static int next;
    char *t = text[next++ & 3];
    if (p == NULL)
        return "null";
    for (int i = 0; i < g_controlCount; i++)
        if (p == &g_controls[i].control) {
            snprintf(t, 24, "c%x", g_controls[i].control.hashcode);
            return t;
        }
    if (p >= (const void *)manager && p < (const void *)(manager + 4)) {
        snprintf(t, 24, "mgr%d", (int)((const M_MANAGER *)p - manager));
        return t;
    }
    snprintf(t, 24, "%p", p);
    return t;
}

M_MANAGER g_fakeManager;   // XBox_DoSaveFlow's, for C_LBERROPTIONS
M_PAGE g_fakePage;
uchar g_bloke[4][0x1000];
uchar g_player[0x200];

// ---- what the fakes answer

struct Answers {
    int value;              // GetValue
    int selected;           // GetSelectedIndex
    uint box, answer;       // Menu_UpdateOptionBox
    bool deleted;           // psiDeleteSave
    bool space;             // Menu_CheckSaveSpace
    uint loadResult, saveResult;
    const char *saves[12];  // psiGetNextSave, in turn; (char *)-1 for "not yet"
    int saveCount, saveNext;
} g_answers;

bool IsText(uint message) {
    return message == MessageType_SetText || message == MessageType_AddItem;
}

const char *TextOf(int arg) {
    static char text[64];
    if (arg == 0)
        return "(0)";
    snprintf(text, sizeof(text), "'%s'", (const char *)arg);
    return text;
}

int Answer(uint message, M_CONTROL *control, HASHCODE hashcode) {
    switch (message) {
    case MessageType_GetValue: return g_answers.value;
    case MessageType_GetSelectedIndex: return g_answers.selected;
    case MessageType_GetControl: return (int)ControlFor(hashcode);
    default: return 1;
    }
}

int __cdecl FakeSendMessage(M_CONTROL *control, uint message, int a1, int a2) {
    if (IsText(message))
        ShadowLog("msg(%s,%x,%s,%x)", Name(control), message, TextOf(a1), a2);
    else
        ShadowLog("msg(%s,%x,%x,%x)", Name(control), message, a1, a2);
    return Answer(message, control, control != NULL ? control->hashcode : (HASHCODE)0);
}

uint __cdecl FakeSend(uchar managerNum, HASHCODE hashcode, uint message, int a1, int a2) {
    if (IsText(message))
        ShadowLog("send(%u,%x,%x,%s,%x)", managerNum, hashcode, message, TextOf(a1), a2);
    else
        ShadowLog("send(%u,%x,%x,%x,%x)", managerNum, hashcode, message, a1, a2);
    return Answer(message, NULL, hashcode);
}

int __cdecl FakeManagerSend(M_MANAGER *mgr, MessageType message, int a1, int a2) {
    ShadowLog("mgr(%s,%x,%s,%x)", Name(mgr), message, message == MessageType_SaveFlowDone ? Name((void *)a1) : "", a2);
    if (message != MessageType_SaveFlowDone)
        ShadowLog("%x", a1);
    return 1;
}

void __cdecl FakeStartIris(uint op, uchar managerNum, uint iris) { ShadowLog("startiris(%u,%u,%x)", op, managerNum, iris); }
void __cdecl FakePlayIris(char op, uchar managerNum, uint iris) { ShadowLog("playiris(%d,%u,%x)", op, managerNum, iris); }
void __cdecl FakeChangePage(HASHCODE page, uchar managerNum, uint iris) { ShadowLog("changepage(%x,%u,%x)", page, managerNum, iris); }

undefined4 __cdecl FakeUpdateOptionBox(undefined4 *type) {
    ShadowLog("optionbox(%s)", type ? "type" : "null");
    if (type != NULL)
        *type = g_answers.box;
    return g_answers.answer;
}

void __cdecl FakeCreateOptionBoxLabel(byte managerNum, uint text, undefined4 type, char a, char b) {
    ShadowLog("newbox(%u,%x,%x,%d,%d)", managerNum, text, type, a, b);
}

void __cdecl FakeUpdateWheel(byte managerNum, M_CONTROL *wheel, HASHCODE a, HASHCODE b, HASHCODE c, HASHCODE d, char e) {
    ShadowLog("wheel(%u,%s,%x,%x,%x,%x,%d)", managerNum, Name(wheel), a, b, c, d, e);
}

undefined4 __cdecl FakeSendDelayed(uint duration, M_CONTROL *control, uint a1, int a2, int a3) {
    ShadowLog("delayed(%u,%s,%x,%x,%x)", duration, Name(control), a1, a2, a3);
    return 1;
}

void __cdecl FakeProcessCreate(M_CONTROL *control, M_MESSAGE *msg, undefined4 delay) {
    ShadowLog("process(%s,%x,%x,%x,%x)", Name(control), msg->type, msg->arg1, msg->arg2, delay);
}

void __cdecl FakeSaveFlow(M_MANAGER *mgr, uint state, MENU_LS *operation) {
    ShadowLog("saveflow(%s,%u,%s)", Name(mgr), state, operation == &ls ? "ls" : "?");
}

const char *__cdecl FakeTxtBindLabel(uint label, uint p2) {
    static char text[64];
    ShadowLog("label(%x)", label);
    if (label == CODENAME_NEW)
        return "New Codename";
    if (label == CODENAME_DEFAULT)
        return "Default";
    snprintf(text, sizeof(text), "label %x", label);
    return text;
}

char *__cdecl FakeGetNextSave(void) {
    ShadowLog("nextsave");
    if (g_answers.saveNext >= g_answers.saveCount)
        return NULL;
    return (char *)g_answers.saves[g_answers.saveNext++];
}

void __cdecl FakeStartSaveEnum(void) { ShadowLog("startenum"); }
void __cdecl FakeEndSaveEnum(void) { ShadowLog("endenum"); }

bool __cdecl FakeDeleteSave(const char *name) {
    ShadowLog("delete('%s')", name);
    return g_answers.deleted;
}

bool __cdecl FakeCheckSaveSpace(uchar managerNum, MENU_LS *operation) {
    ShadowLog("space(%u)", managerNum);
    return g_answers.space;
}

void __cdecl FakeDashboard(void) { ShadowLog("dashboard"); }
void __cdecl FakeLaunch(int blocks, void *data, int length) { ShadowLog("launch(%d,%p,%d)", blocks, data, length); }
void __cdecl FakeClearDelayed(void) { ShadowLog("cleardelayed"); }

ushort __cdecl FakeChangeSubState(obj_tag *obj, ushort state) {
    ShadowLog("substate(%s,%u)", obj == (obj_tag *)g_player ? "player" : Name(obj), state);
    return 0;
}

undefined4 __cdecl FakeEquipWeapon(BLData *bloke, short weapon, short ammo) {
    ShadowLog("equip(%p,%d,%d)", bloke, weapon, ammo);
    return 1;
}

void __cdecl FakeCheckWeapons(BLData *bloke) { ShadowLog("checkweapons(%p)", bloke); }
void __cdecl FakeHUDEnable(void *hud, int pane, char enable, ushort state) { ShadowLog("hud(%p,%d,%d,%u)", hud, pane, enable, state); }
void __cdecl FakeSetBonus(uint lo, uint hi, byte player, char orIn) { ShadowLog("setbonus(%x,%x,%u,%d)", lo, hi, player, orIn); }
void __cdecl FakeSetNightfireStatus(uint status) { ShadowLog("setstatus(%x)", status); }
void __cdecl FakeResetForMission(void) { ShadowLog("resetmission"); }
void __cdecl FakeMusicSetVolume(int volume) { ShadowLog("music(%d)", volume); }
void __cdecl FakeSfxSetVolume(int volume) { ShadowLog("sfx(%d)", volume); }
uint __cdecl FakeLoad(char *name, ushort slot, ushort mask) { ShadowLog("lsload('%s',%u,%x)", name, slot, mask); return g_answers.loadResult; }
uint __cdecl FakeSave(char *name, uint slot, ushort mask) { ShadowLog("lssave('%s',%u,%x)", name, slot, mask); return g_answers.saveResult; }
void __cdecl FakeFlushStates(void) { ShadowLog("flush"); }

// A port of ours is hooked where our code calls it too, not only behind the original's entry
void HookOurs(unsigned original, const void *ours, const void *fake) {
    ShadowHook_Install(original, fake);
    if (XbeOriginal_Target(original) != (unsigned)(uintptr_t)ours)
        ShadowHook_Install((unsigned)(uintptr_t)ours, fake);
}

void InstallFakes(void) {
    ShadowHook_Install(0x00072630, (const void *)&FakeSendMessage);
    ShadowHook_Install(0x00072820, (const void *)&FakeSend);
    ShadowHook_Install(0x00092c40, (const void *)&FakeManagerSend);
    ShadowHook_Install(0x0007f260, (const void *)&FakeStartIris);
    ShadowHook_Install(0x00075c80, (const void *)&FakePlayIris);
    ShadowHook_Install(0x0007f320, (const void *)&FakeChangePage);
    ShadowHook_Install(0x000761a0, (const void *)&FakeUpdateOptionBox);
    ShadowHook_Install(0x0007f3a0, (const void *)&FakeCreateOptionBoxLabel);
    ShadowHook_Install(0x0007fb20, (const void *)&FakeUpdateWheel);
    HookOurs(0x00073240, (const void *)&__Menu_SendDelayedMessage, (const void *)&FakeSendDelayed);
    ShadowHook_Install(0x00095de0, (const void *)&FakeProcessCreate);
    ShadowHook_Install(0x0008f3f0, (const void *)&FakeSaveFlow);
    HookOurs(0x0006d460, (const void *)&Txt_BindLabel, (const void *)&FakeTxtBindLabel);
    ShadowHook_Install(0x000dfe60, (const void *)&FakeGetNextSave);
    HookOurs(0x000dfe30, (const void *)&psiStartSaveEnum, (const void *)&FakeStartSaveEnum);
    HookOurs(0x000dfe90, (const void *)&psiEndSaveEnum, (const void *)&FakeEndSaveEnum);
    ShadowHook_Install(0x000dfec0, (const void *)&FakeDeleteSave);
    ShadowHook_Install(0x0008fcc0, (const void *)&FakeCheckSaveSpace);
    ShadowHook_Install(0x0008fd90, (const void *)&FakeDashboard);
    ShadowHook_Install(0x000e9350, (const void *)&FakeLaunch);
    HookOurs(0x00073340, (const void *)&Menu_ClearDelayedMessages, (const void *)&FakeClearDelayed);
    ShadowHook_Install(0x000a9720, (const void *)&FakeChangeSubState);
    ShadowHook_Install(0x000ba5e0, (const void *)&FakeEquipWeapon);
    HookOurs(0x000b9b40, (const void *)&Player_CheckWeaponsLoaded, (const void *)&FakeCheckWeapons);
    HookOurs(0x000b1a60, (const void *)&HUD_Enable, (const void *)&FakeHUDEnable);
    HookOurs(0x0007cb10, (const void *)&Menu_SetBonus, (const void *)&FakeSetBonus);
    HookOurs(0x000762d0, (const void *)&Menu_SetNightfireStatus, (const void *)&FakeSetNightfireStatus);
    HookOurs(0x000b04a0, (const void *)&PlrStat_ResetForMission, (const void *)&FakeResetForMission);
    ShadowHook_Install(0x000c6350, (const void *)&FakeMusicSetVolume);
    ShadowHook_Install(0x000c5060, (const void *)&FakeSfxSetVolume);
    HookOurs(0x0006f960, (const void *)&LS_Load, (const void *)&FakeLoad);
    HookOurs(0x0006f880, (const void *)&LS_Save, (const void *)&FakeSave);
    HookOurs(0x0006eb20, (const void *)&LS_FlushStates, (const void *)&FakeFlushStates);
}

// ---- the state compared

ShadowRegions g_regions;

void AddRegions(void) {
    g_regions.Clear();
    g_regions.Add(&ls, sizeof(MENU_LS));
    g_regions.Add(codename_buf, 0x20 * 24);
    g_regions.Add((void *)0x0025eda0, 0x20);    // XBox_DoSaveFlow's state
    g_regions.Add(PlayerInputs, 4 * sizeof(PlayerInput));
    g_regions.Add(&MPSettings, sizeof(MPSettings));
    g_regions.Add((void *)0x001f65dc, 0xc);     // CheatInfo
    g_regions.Add((void *)0x001f660c, 0xc);     // layout, widescreen, subtitles
    g_regions.Add(&GameState.WeaponUpgradeRelated, 1);
    g_regions.Add((void *)XbeOurs(0x0025d78d), 1);       // menu_bonus_replace
    g_regions.Add((void *)0x001df99c, 8);       // the drones' switches
    g_regions.Add((void *)XbeOurs(0x001df9dc), 4);       // switch_SHOW_COORDS
    g_regions.Add((void *)XbeOurs(0x001dfa29), 1);       // Drone_DummyPlayer
    g_regions.Add((void *)0x0025d79c, 4);       // switch_allowFreeze, menu_unlock_everything
    g_regions.Add((void *)0x001fec49, 2);       // debug_scanmode, debug_clipmode
    g_regions.Add((void *)0x001f6568, 0x18);    // GlobalVars
    g_regions.Add((void *)XbeOurs(0x00194804), 4);       // SaveEnum_EntriesPerFrame
    g_regions.Add(g_bloke, sizeof(g_bloke));
    g_regions.Add(g_controls, sizeof(g_controls));
    g_regions.Add(&g_controlCount, sizeof(g_controlCount));
    g_regions.Add(&g_answers, sizeof(g_answers));
    g_regions.Add(&manager[0].currentPage, 4);
}

std::vector<uint8_t> g_lentStart;

void SetLent(const void *ours, uint value) {
    size_t at = 0;
    for (int i = 0; i < kLentCount; i++) {
        if (kLent[i].ours == ours)
            memcpy(&g_lentStart[at], &value, kLent[i].size);
        at += kLent[i].size;
    }
}

void RandomLent(void) {
    g_lentStart.clear();
    for (int i = 0; i < kLentCount; i++)
        for (unsigned b = 0; b < kLent[i].size; b++)
            g_lentStart.push_back((uint8_t)ShadowRand(256));
    // the values the code sets, mostly
    static const uint enumStates[] = {0, 1, 4, 7, 2};
    SetLent(&enum_state, enumStates[ShadowRand(5)]);
    static const uint deleteStates[] = {0, 4, 5, 6};
    SetLent(&ls_delete_state, deleteStates[ShadowRand(4)]);
    SetLent(&ls_updating, ShadowRand(4) == 0);
    SetLent(&cnselect_leaving, ShadowRand(2));
    SetLent(&cnselect_from_main_menu, ShadowRand(2));
}

// Both copies start the same
void Lend(void) {
    size_t at = 0;
    for (int i = 0; i < kLentCount; i++) {
        memcpy((void *)kLent[i].game, &g_lentStart[at], kLent[i].size);
        memcpy(kLent[i].ours, &g_lentStart[at], kLent[i].size);
        at += kLent[i].size;
    }
}

// The copy each static's user ran with: the game's where an original under test uses it, else ours
void CaptureLent(std::initializer_list<unsigned> originals, std::vector<uint8_t> &out) {
    out.clear();
    for (int i = 0; i < kLentCount; i++) {
        bool game = false;
        for (unsigned at : originals)
            game |= at == kLent[i].owner || at == kLent[i].owner2;
        const uint8_t *p = game ? (const uint8_t *)kLent[i].game : (const uint8_t *)kLent[i].ours;
        out.insert(out.end(), p, p + kLent[i].size);
    }
}

void CaptureCopies(bool game, std::vector<uint8_t> &out) {
    out.clear();
    for (int i = 0; i < kLentCount; i++) {
        const uint8_t *p = game ? (const uint8_t *)kLent[i].game : (const uint8_t *)kLent[i].ours;
        out.insert(out.end(), p, p + kLent[i].size);
    }
}

void RestoreCopies(bool game, const std::vector<uint8_t> &from) {
    size_t at = 0;
    for (int i = 0; i < kLentCount; i++) {
        memcpy(game ? (void *)kLent[i].game : kLent[i].ours, &from[at], kLent[i].size);
        at += kLent[i].size;
    }
}

// Runs a case as the original (with those addresses swapped back) and as ours, and compares
template <class Call>
void Case(const char *what, int trial, std::initializer_list<unsigned> originals, Call call) {
    g_regions.Save();
    Lend();
    ShadowLog_Clear();
    std::vector<uint32_t> theirResults;
    for (unsigned at : originals)
        XbeOriginal_Restore(at, true);
    call(true, theirResults);
    for (unsigned at : originals)
        XbeOriginal_Restore(at, false);
    std::string theirLog = ShadowLog_Text();
    std::vector<uint8_t> theirState, theirLent, ourState, ourLent;
    g_regions.Capture(theirState);
    CaptureLent(originals, theirLent);

    g_regions.Restore();
    Lend();
    ShadowLog_Clear();
    std::vector<uint32_t> ourResults;
    call(false, ourResults);
    g_regions.Capture(ourState);
    CaptureLent({}, ourLent);

    g_cases++;
    char detail[800];
    int region;
    size_t offset;
    if (theirResults != ourResults) {
        snprintf(detail, sizeof(detail), "returns differ (ours %x, the original %x)", ourResults.empty() ? 0 : ourResults[0],
                 theirResults.empty() ? 0 : theirResults[0]);
        Report(what, trial, detail);
    } else if (g_regions.Describe(theirState, ourState, &region, &offset)) {
        snprintf(detail, sizeof(detail), "memory differs in region %d at +0x%x", region, (unsigned)offset);
        Report(what, trial, detail);
    } else if (theirLent != ourLent) {
        Report(what, trial, "a static differs");
    } else if (theirLog != ShadowLog_Text()) {
        snprintf(detail, sizeof(detail), "calls differ:\n  ours:     %.300s\n  original: %.300s", ShadowLog_Text().c_str(),
                 theirLog.c_str());
        Report(what, trial, detail);
    }
}

const char *const kNames[] = {"BOND", "Q", "M", "ALPHA", "ZED", "MONEYPENNY", "bond", "R", "XENIA", "A B"};

// A codenames list of the two defaults and some saved names, and ls about it
void RandomCodenames(void) {
    memset(codename_buf, 0, 0x20 * 24);
    strcpy(codename_buf[0], "New Codename");
    strcpy(codename_buf[1], "Default");
    int saved = ShadowRand(9);
    for (int i = 0; i < saved; i++)
        strcpy(codename_buf[2 + i], kNames[ShadowRand(10)]);
    int fewer = ShadowRand(3);   // sometimes fewer than are there, but never fewer than the two defaults
    ls.numCodenames = 2 + saved - (fewer < saved ? fewer : saved);
}

void RandomLs(void) {
    RandomBytes(&ls, sizeof(MENU_LS));
    ls.busy = ShadowRand(3) == 0 ? 0 : ShadowRand(3) == 0 ? RandomWord() : 1;
    static const uint slots[] = {0, 1, 2, 3, 5, LS_SLOT_NEW};
    ls.slot = slots[ShadowRand(6)];
    ls.operation = (uchar)ShadowRand(6);
    ls.done = (uchar)ShadowRand(2);
    ls.listStale = (uchar)(ShadowRand(3) == 0 ? ShadowRand(256) : ShadowRand(2));
    ls.canSave = (uchar)ShadowRand(2);
    ls.control = ControlFor(C_SBCNSELECT);
    ls.returnPage = ShadowRand(2) ? (HASHCODE)0 : P_NFSELECT;
    strcpy(ls.codename, ShadowRand(4) == 0 ? "" : kNames[ShadowRand(10)]);
    ls.numCodenames = 2 + ShadowRand(6);
}

void RandomAnswers(void) {
    static const int values[] = {0, 1, 2, 3, 4, 0x80, -1};
    g_answers.value = values[ShadowRand(7)];
    g_answers.selected = ShadowRand(4);
    static const uint boxes[] = {0, 2, 3, 5, 9};
    g_answers.box = boxes[ShadowRand(5)];
    g_answers.answer = ShadowRand(4);
    g_answers.deleted = ShadowRand(2) != 0;
    g_answers.space = ShadowRand(2) != 0;
    static const uint results[] = {0, 1, 2, 7, 8};
    g_answers.loadResult = results[ShadowRand(5)];
    g_answers.saveResult = results[ShadowRand(5)];
    static const char *const saves[] = {"BOND", "MONEYPENNYS", "Q", "AVERYLONGCODENAMEOFMORETHAN32CHARS", "ALPHA", "Z",
                                        "THIRTYTWOCHARACTERSEXACTLYHERE!!", (const char *)-1};
    g_answers.saveCount = ShadowRand(9);
    for (int i = 0; i < g_answers.saveCount; i++)
        g_answers.saves[i] = saves[ShadowRand(8)];
    g_answers.saveNext = 0;
}

void RandomGame(void) {
    for (int i = 0; i < 4; i++) {
        memset(g_bloke[i], 0, sizeof(g_bloke[i]));
        glb_blokes[i] = i > 0 && ShadowRand(3) == 0 ? NULL : (BLData *)g_bloke[i];   // player 0 is always there
        ((BLData *)g_bloke[i])->hudInfo = (HUDINFO_tag *)(0x1000 + i);
    }
    glb_players[0] = (obj_tag *)g_player;
    RandomBytes((void *)(size_t)XbeOurs(0x001df99c), 8);
    RandomBytes((void *)XbeOurs(0x001df9dc), 4);
    RandomBytes((void *)XbeOurs(0x001dfa29), 1);
    RandomBytes((void *)(size_t)XbeOurs(0x0025d79c), 4);
    RandomBytes((void *)(size_t)XbeOurs(0x001fec49), 2);
    RandomBytes((void *)(size_t)XbeOurs(0x001f65dc), 0xc);
    U32_AT(XbeOurs(0x00194804)) = ShadowRand(4);
}

const uint kMessages[] = {MessageType_Select, MessageType_ControlCreated, MessageType_PageEnter, MessageType_PageUpdate,
                          MessageType_PageResume, MessageType_SaveFlowDone, MessageType_QueryBack, MessageType_Scroll,
                          MessageType_ValueSet, MessageType_AltSelect1, MessageType_AltSelect2,
                          MessageType_BackOnControl, MessageType_PageLeave, 0x4a, 0x6c, 0x6f};
const int kMessageCount = sizeof(kMessages) / sizeof(kMessages[0]);

struct HandlerCase {
    const char *name;
    unsigned address;
    HandlerFn ours;
    HASHCODE hashcode;
};

#define HANDLER(name, address) {#name, address, name##_Handler, name}
const HandlerCase kHandlers[] = {
    HANDLER(C_CHCHFLY, 0x00081a50),         HANDLER(C_CHCHDRONES, 0x00081ab0),   HANDLER(C_CHCHBLIND, 0x00081b00),
    HANDLER(C_CHCHWEAP, 0x00081b50),        HANDLER(C_CHCHHEALTH, 0x00081c10),   HANDLER(C_CHCHHUD, 0x00081c60),
    HANDLER(C_CHCHDEBUG, 0x00081d30),       HANDLER(C_CHCHCOORDS, 0x00081d80),   HANDLER(C_CHCHALLOWFREEZE, 0x00081e90),
    HANDLER(C_CHCHDUMMY, 0x00081ee0),       HANDLER(C_CHCHZEROG, 0x00081f80),    HANDLER(C_CHCHLOCKUP, 0x00081fe0),
    HANDLER(C_CHCHBRIGHT, 0x00082030),      HANDLER(C_CHCHUNLOCK, 0x00082080),   HANDLER(C_CHCHCONTROLS, 0x000820d0),
    HANDLER(C_KEYPAD, 0x00083fa0),          HANDLER(P_CNSELECT, 0x0008be40),     HANDLER(C_SBCNSELECT, 0x0008c360),
    HANDLER(C_LBERROPTIONS, 0x0008fbd0),
};

void HandlerCases(void) {
    for (const HandlerCase &h : kHandlers)
        for (int trial = 0; trial < 320; trial++) {
            g_controlCount = 0;
            RandomLs();
            RandomCodenames();
            RandomAnswers();
            RandomGame();
            RandomLent();
            M_CONTROL *control = ControlFor(h.hashcode);
            control->id = ShadowRand(10);
            static const uchar types[] = {ControlType_Scroll, ControlType_List, ControlType_Radio, ControlType_Button};
            control->type = types[ShadowRand(4)];
            // GlobalVars: the decoder's digit count and position
            RandomBytes((void *)(size_t)XbeOurs(0x001f6568), 0x18);
            U8_AT(XbeOurs(0x001f657a)) = (uchar)ShadowRand(5);
            U8_AT(XbeOurs(0x001f657b)) = (uchar)ShadowRand(5);
            // XBox_DoSaveFlow's state, for C_LBERROPTIONS
            memset(&g_fakeManager, 0, sizeof(g_fakeManager));
            g_fakeManager.underPage = ShadowRand(2) ? &g_fakePage : NULL;
            RandomBytes((void *)(size_t)XbeOurs(0x0025eda0), 0x20);
            *(MENU_LS **)XbeOurs(0x0025eda0) = &ls;
            *(M_MANAGER **)XbeOurs(0x0025eda4) = ShadowRand(2) ? &g_fakeManager : &manager[1];
            manager[0].currentPage = NULL;
            uchar managerNum = (uchar)ShadowRand(2);
            uint message = kMessages[trial % kMessageCount];
            static const HASHCODE previous[] = {P_MAIN, P_CNMENU, P_NFSELECT};
            int arg1 = (int)control;
            int arg2 = (int)previous[ShadowRand(3)];
            int keep = 0;
            if (message == MessageType_QueryBack)
                arg2 = (int)&keep;
            Case(h.name, trial, {h.address}, [&](bool original, std::vector<uint32_t> &r) {
                keep = 0;
                bool result = original ? ((HandlerFn)h.address)(managerNum, control, h.hashcode, message, arg1, arg2)
                                       : h.ours(managerNum, control, h.hashcode, message, arg1, arg2);
                r.push_back((uchar)result);
                r.push_back(keep);
            });
        }
}

// ---- the helpers

typedef void(__cdecl *UpdateMessageBoxFn)(uchar, ushort, byte);
typedef uint(__cdecl *EnumSavesFn)(uchar, char);
typedef void(__cdecl *VoidFn)(void);
typedef void(__cdecl *PutCodenamesFn)(M_CONTROL *);
typedef void(__cdecl *MapDefaultFn)(byte, byte, uint);
typedef void(__cdecl *SelectCodenameFn)(byte, char *, HASHCODE);
typedef uint(__cdecl *IsValidFn)(char *);

// Menu_StartLS: the codename in EAX, the operation in ECX, the stale flag in DL, eight arguments on the stack
__declspec(naked) void __cdecl CallStartLS(unsigned at, char *codename, int operation, int stale, const uint *args) {
    _asm {
        push esi
        mov esi, [esp + 0x18]
        push dword ptr [esi + 0x1c]
        push dword ptr [esi + 0x18]
        push dword ptr [esi + 0x14]
        push dword ptr [esi + 0x10]
        push dword ptr [esi + 0x0c]
        push dword ptr [esi + 0x08]
        push dword ptr [esi + 0x04]
        push dword ptr [esi]
        mov eax, [esp + 0x2c]
        mov ecx, [esp + 0x30]
        mov edx, [esp + 0x34]
        mov esi, [esp + 0x28]
        call esi
        add esp, 0x20
        pop esi
        ret
    }
}

void HelperCases(void) {
    for (int trial = 0; trial < 2400; trial++) {
        g_controlCount = 0;
        RandomLs();
        RandomCodenames();
        RandomAnswers();
        RandomGame();
        RandomLent();
        int which = trial % 8;
        uchar managerNum = (uchar)ShadowRand(2);
        switch (which) {
        case 0: {   // Menu_UpdateMessageBox, a few frames
            ushort mask = (ushort)ShadowRand(0x100);
            byte slot = (byte)ShadowRand(4);
            int frames = 1 + ShadowRand(3);
            Case("Menu_UpdateMessageBox", trial, {0x000757d0}, [&](bool original, std::vector<uint32_t> &r) {
                for (int f = 0; f < frames; f++) {
                    if (original)
                        ((UpdateMessageBoxFn)0x000757d0)(managerNum, mask, slot);
                    else
                        Menu_UpdateMessageBox(managerNum, mask, slot);
                }
            });
            break;
        }
        case 1: {   // Menu_EnumSaves (with Menu_FlushEnum first, sometimes), a few frames
            char firstOnly = (char)(ShadowRand(3) == 0 ? ShadowRand(256) : ShadowRand(2));
            bool flush = ShadowRand(2) != 0;
            int frames = 1 + ShadowRand(5);
            Case("Menu_EnumSaves", trial, {0x000755e0, 0x000755d0}, [&](bool original, std::vector<uint32_t> &r) {
                if (flush)
                    original ? ((VoidFn)0x000755d0)() : Menu_FlushEnum();
                for (int f = 0; f < frames; f++)
                    r.push_back(original ? ((EnumSavesFn)0x000755e0)(managerNum, firstOnly)
                                         : Menu_EnumSaves(managerNum, firstOnly));
            });
            break;
        }
        case 2: {   // Menu_PutCodenamesIntoControl
            M_CONTROL *target = ControlFor(C_SBCNSELECT);
            static const uchar types[] = {ControlType_Scroll, ControlType_List, ControlType_Radio, ControlType_Button,
                                          ControlType_Spin};
            target->type = types[ShadowRand(5)];
            g_fakePage.control.hashcode = ShadowRand(2) ? P_NFSELECT : P_CNSELECT;
            manager[0].currentPage = ShadowRand(3) ? &g_fakePage : NULL;
            if (ShadowRand(8) == 0)
                target = NULL;
            Case("Menu_PutCodenamesIntoControl", trial, {0x000754c0}, [&](bool original, std::vector<uint32_t> &r) {
                original ? ((PutCodenamesFn)0x000754c0)(target) : Menu_PutCodenamesIntoControl(target);
            });
            break;
        }
        case 3: {   // Menu_MapDefaultCodename
            RandomBytes(PlayerInputs, 4 * sizeof(PlayerInput));
            RandomBytes(&MPSettings, sizeof(MPSettings));
            byte player = (byte)ShadowRand(4), slot = (byte)ShadowRand(2);
            uint parts = ShadowRand(3) == 0 ? 0xff : ShadowRand(3) == 0 ? 0 : RandomWord();
            Case("Menu_MapDefaultCodename", trial, {0x0007eee0}, [&](bool original, std::vector<uint32_t> &r) {
                original ? ((MapDefaultFn)0x0007eee0)(player, slot, parts) : Menu_MapDefaultCodename(player, slot, parts);
            });
            break;
        }
        case 4: {   // Menu_SelectCodenameInControl
            char name[32];
            int pick = ShadowRand(4);
            strcpy(name, pick == 0 ? "" : pick == 1 ? "NOBODY" : codename_buf[ShadowRand(6)]);
            char *codename = ShadowRand(8) == 0 ? NULL : name;
            Case("Menu_SelectCodenameInControl", trial, {0x0007edf0}, [&](bool original, std::vector<uint32_t> &r) {
                original ? ((SelectCodenameFn)0x0007edf0)(managerNum, codename, C_SBCNSELECT)
                         : Menu_SelectCodenameInControl(managerNum, codename, C_SBCNSELECT);
            });
            break;
        }
        case 5: {   // Menu_IsValidCodename
            static const char *const typed[] = {"", " ", "   ", "BOND", " BOND ", "bond", "  q  ", "NEW CODENAME",
                                                "new codenamex", "MYDEFAULT", "Default", "A B", "\xc4\xc2X", "a  ",
                                                "  z", "ABCDEFGH", "abcdefgh", "DEFAUL", "x\xe0y"};
            char name[32];
            strcpy(name, typed[ShadowRand(sizeof(typed) / sizeof(typed[0]))]);
            Case("Menu_IsValidCodename", trial, {0x00076330}, [&](bool original, std::vector<uint32_t> &r) {
                char buffer[32];
                memcpy(buffer, name, sizeof(buffer));
                r.push_back(original ? ((IsValidFn)0x00076330)(buffer) : Menu_IsValidCodename(buffer));
                for (int i = 0; i < 32; i += 4)
                    r.push_back(*(uint *)&buffer[i]);
            });
            break;
        }
        case 6: {   // Menu_StartLS, as the original callers call it
            uint args[8];
            args[0] = managerNum | 0x5a5a5a00;
            args[1] = ShadowRand(4) | 0xa5a5a500;     // slot (a byte)
            args[2] = ShadowRand(0x100) | 0x77770000; // mask (a word)
            args[3] = ShadowRand(3) ? ShadowRand(4) : LS_SLOT_NEW;
            args[4] = ShadowRand(2) ? 0 : P_CNMENU;
            args[5] = ShadowRand(4);
            args[6] = (uint)ControlFor(C_SBCNSELECT);
            args[7] = ShadowRand(2) | 0x12345600;     // done (a byte)
            int operation = ShadowRand(6);
            int stale = ShadowRand(3) == 0 ? (int)(0xffffff00 | ShadowRand(256)) : ShadowRand(2);
            char name[32];
            strcpy(name, kNames[ShadowRand(10)]);
            char *codename = ShadowRand(4) == 0 ? NULL : name;
            Case("Menu_StartLS", trial, {0x0007fef0}, [&](bool original, std::vector<uint32_t> &r) {
                CallStartLS(0x0007fef0, codename, operation, stale, args);
            });
            break;
        }
        case 7: {   // Menu_FlushEnum, then a listing
            Case("Menu_FlushEnum", trial, {0x000755d0}, [&](bool original, std::vector<uint32_t> &r) {
                original ? ((VoidFn)0x000755d0)() : Menu_FlushEnum();
            });
            break;
        }
        }
    }
}

} // namespace

void CodenameShadow_Run(void) {
    AddRegions();
    // everything the cases write, put back at the end
    ShadowRegions everything = g_regions;
    everything.Add(glb_blokes, sizeof(glb_blokes));
    everything.Add(glb_players, sizeof(glb_players));
    everything.Add((void *)0x0025d6e8, 0x20);
    everything.Save();
    std::vector<uint8_t> lent, gameLent;
    CaptureCopies(false, lent);
    CaptureCopies(true, gameLent);

    InstallFakes();
    HandlerCases();
    HelperCases();
    ShadowHook_RemoveAll();

    everything.Restore();
    RestoreCopies(false, lent);
    RestoreCopies(true, gameLent);
    printf("[cnshadow] the codename pages, the cheat checkboxes, the keypad and their helpers: %d cases, %d differ\n",
           g_cases, g_differ);
}
