#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "GameMissionsShadowC.h"
#include "FpControl.h"

#include <windows.h>
#include <float.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <initializer_list>
#include <string>
#include <vector>

#include "../data/Carp.h"                   // CARP::Instance
#include "../data/UData.h"
#include "../engine/ActionQueue.hpp"
#include "../engine/GameInterfaces.hpp"     // GHud
#include "../engine/GameLoop.h"             // LaunchPage, Launch
#include "../engine/UMemory.hpp"
#include "../game/MissionManager.h"
#include "../game/Vehicle.h"                // PVehicle
#include "../physics/PhysicsObject.h"
#include "../physics/RigidBody.h"
#include "../render/RSceneObj.hpp"
#include "../Scheduler.hpp"
#include "../../common/xbeOriginal.h"
#include "../../helpers.h"

// The port's owned globals, mirrored into the original's addresses before each original run
extern uint8_t CarOverrideLotus, CarOverrideVan, CarOverrideLimo, CarOverrideTaxi, CarOverrideSmart;
extern uint8_t CharInfoBeforeHud;

// ---------------------------------------------------------------------------------------------------------------
// NIGHTFIRE_GAMEMISSIONSSHADOWC=1, from the first simulation tick: the GM_C package (0x000b60a0-0x000b9500, the
// mission manager) against the originals, the package's range swapped back for each original run. Each case runs
// both sides on one buffer (a perturbed copy of the live manager, both sides at the same address, glbMissionManager
// pointed at it) from the same state and compares the manager's bytes (the lists' heads masked), its lists by
// content (objectives, messages, the rule lists as synthetic rule indices), the launch page, the cinematic global,
// the scheduler's two fields, the owned globals (each side's own copy), what it returns, and a log of every call
// out of the package. Every callee is a recording fake (at its address and at its port's entry): the weapon
// manager, the HUD (TheApp answers a synthetic HUD or NULL), AIElementController (synthetic AI elements whose
// vehicle's vtable reaches faked PBondCar slots), the missile, the target picker, the player camera, RColorize,
// IOModule and InputConfigManager, the rules (synthetic, their deleting destructor faked) and timers, the
// attribute lookups, Bond_SetExitToDriving, Event's operator new and ERestart, and Simulation::GetRigidBody
// (a perturbed copy of the player's body). The real ActionQueue runs on a synthetic queue of random actions.
//
// Covered: every function in the package, the constructor and destructor (and Create/Destroy) on a fresh buffer,
// Load on synthetic rule and mission-set records.
//
// Mutation this catches: IncShotsHit allowing a hit within a fifth rather than a tenth of the step count (the
// steps since the last shot are drawn across both windows, and shotsHit differs in about a fifth of the rounds).
// ---------------------------------------------------------------------------------------------------------------

namespace {

void OriginalWindow(bool original) {
    XbeOriginal_RestoreRange(0x000b60a0, 0x000b9500, original);
}

#define ShSimStepCount I32_AT(0x00234e34)
#define ShCinematic I32_AT(0x00243a64)       // the game's copy of MissionCinematicMode
#define ShScheduler (*(uint8_t **)0x001e520c)
#define ShPlayerSlot (*(PVehicle ***)0x00234e40)
#define ShOrigDefaultSet ((uint8_t *)0x00238f20)
#define ShOrigCarFlags ((uint8_t *)0x0023921a)
#define ShOrigTimes100B U8_AT(0x00239219)
#define ShOrigCharInfoFlag U8_AT(0x001c8fc4)

// ---- results

int g_cases = 0, g_checks = 0, g_differ = 0, g_details = 0, g_faults = 0;
unsigned int g_x87 = 0, g_sse = 0;

void Differ(const char *what, int index, const char *detail) {
    g_differ++;
    if (g_details++ < 10)
        printf("[gmissionsC]   %s #%d: %s\n", what, index, detail);
}

void CheckBytes(const char *what, int index, const std::vector<uint8_t> &a, const std::vector<uint8_t> &b) {
    g_checks++;
    char detail[96];
    if (a.size() != b.size()) {
        snprintf(detail, sizeof(detail), "%u bytes, port %u", unsigned(a.size()), unsigned(b.size()));
        Differ(what, index, detail);
        return;
    }
    for (size_t at = 0; at < a.size(); at++) {
        if (a[at] != b[at]) {
            snprintf(detail, sizeof(detail), "byte 0x%x of 0x%x: original %02x, port %02x", unsigned(at),
                     unsigned(a.size()), a[at], b[at]);
            Differ(what, index, detail);
            return;
        }
    }
}

void CheckWords(const char *what, int index, const std::vector<uint32_t> &a, const std::vector<uint32_t> &b) {
    g_checks++;
    char detail[96];
    if (a.size() != b.size()) {
        snprintf(detail, sizeof(detail), "%u words, port %u", unsigned(a.size()), unsigned(b.size()));
        Differ(what, index, detail);
        return;
    }
    for (size_t i = 0; i < a.size(); i++) {
        if (a[i] != b[i]) {
            snprintf(detail, sizeof(detail), "word %u: original %08x, port %08x", unsigned(i), a[i], b[i]);
            Differ(what, index, detail);
            return;
        }
    }
}

void ResetFpu() {
    _fpreset();
    FpControlSetX87(g_x87);
    FpControlSetSse(g_sse);
}

typedef void (*CaseFn)(void *context, bool original);

bool Guarded(CaseFn run, void *context, bool original) {
    if (original)
        OriginalWindow(true);
    bool ok = true;
#ifdef _MSC_VER
    __try {
        run(context, original);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ResetFpu();
        g_faults++;
        ok = false;
    }
#else
    run(context, original);
#endif
    if (original)
        OriginalWindow(false);
    return ok;
}

template <class F>
void Thunk(void *context, bool original) {
    (*static_cast<F *>(context))(original);
}

// ---- random inputs (a generator of our own)

uint32_t g_seed = 0x6d15c0deu;

uint32_t Next() {
    g_seed = g_seed * 1664525u + 1013904223u;
    return g_seed >> 8;
}

float Uniform(float lo, float hi) {
    return lo + (hi - lo) * float(Next() & 0xffff) / 65535.0f;
}

int Range(int lo, int hi) {
    return lo + int(Next() % uint32_t(hi - lo + 1));
}

bool Chance(int in) {
    return Next() % uint32_t(in) == 0;
}

// ---- jumps to the fakes

struct Hook {
    uint32_t at;
    uint8_t saved[5];
    bool on;
};

bool HookInstall(Hook *h, uint32_t at, const void *to) {
    h->at = at;
    h->on = false;
    DWORD old;
    if (!VirtualProtect((void *)uintptr_t(at), 5, PAGE_EXECUTE_READWRITE, &old))
        return false;
    memcpy(h->saved, (void *)uintptr_t(at), 5);
    uint8_t jump[5] = { 0xe9 };
    int32_t rel = int32_t(uint32_t(uintptr_t(to)) - (at + 5));
    memcpy(jump + 1, &rel, 4);
    memcpy((void *)uintptr_t(at), jump, 5);
    VirtualProtect((void *)uintptr_t(at), 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)uintptr_t(at), 5);
    h->on = true;
    return true;
}

void HookRemove(Hook *h) {
    if (!h->on)
        return;
    DWORD old;
    VirtualProtect((void *)uintptr_t(h->at), 5, PAGE_EXECUTE_READWRITE, &old);
    memcpy((void *)uintptr_t(h->at), h->saved, 5);
    VirtualProtect((void *)uintptr_t(h->at), 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)uintptr_t(h->at), 5);
    h->on = false;
}

const void *JumpTarget(uint32_t at) {
    const uint8_t *code = reinterpret_cast<const uint8_t *>(uintptr_t(at));
    if (code[0] != 0xe9)
        return NULL;
    int32_t relative;
    memcpy(&relative, code + 1, 4);
    return reinterpret_cast<const void *>(uintptr_t(at + 5 + relative));
}

struct Fake {
    uint32_t address;
    const void *fake;
    Hook entry, port;
};

void FakeInstall(Fake *f) {
    const void *port = JumpTarget(f->address);
    HookInstall(&f->entry, f->address, f->fake);
    f->port.on = false;
    if (port != NULL)
        HookInstall(&f->port, uint32_t(uintptr_t(port)), f->fake);
}

void FakeRemove(Fake *f) {
    HookRemove(&f->port);
    HookRemove(&f->entry);
}

// ---- the call log

std::vector<uint32_t> *g_log = NULL;

void Log(std::initializer_list<uint32_t> words) {
    if (g_log != NULL)
        g_log->insert(g_log->end(), words.begin(), words.end());
}

uint32_t Word(const void *p) {
    return uint32_t(uintptr_t(p));
}

uint32_t Bits(float f) {
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

uint32_t Hash(const char *s) {
    if (s == NULL)
        return 0xffffffffu;
    uint32_t h = 2166136261u;
    for (; *s; s++)
        h = (h ^ uint8_t(*s)) * 16777619u;
    return h;
}

// ---- the synthetic world

SMissionManager *g_mm = NULL;           // the buffer both sides run on
SMissionManager *g_tmpl = NULL;         // its state before each side
MissionSet g_set;                       // the copy's mission set
LaunchPage g_page;               // a page for ClearScoreBuffer and TallyScores
alignas(16) uint8_t g_hudBuf[0x500];
GHud *g_hud = NULL;                     // what TheApp answers
alignas(16) uint8_t g_ioBuf[0xa4];
alignas(16) uint8_t g_icmBuf[0x30];
alignas(16) uint8_t g_missileBuf[0x710];
alignas(16) uint8_t g_eventBuf[16];
alignas(16) uint8_t g_queueBuf[sizeof(ActionQueue)];
alignas(16) uint8_t g_queueTmpl[sizeof(ActionQueue)];
ObjectiveMarker g_marker;
alignas(16) uint8_t g_kind2Buf[16];
CARP::AIElement g_carpElements[2];     // what the slots point at
CARP::AIElement g_liveElements[2];     // what Redirect answers
alignas(16) uint8_t g_vehicles[2][0x6c];    // their objects: the player's vtable, a synthetic scene object
alignas(16) uint8_t g_sceneObjs[2][sizeof(RSceneObj)];
CARP::Instance g_instances[2];
RigidBody g_body;                       // what GetRigidBody answers
alignas(16) uint8_t g_carCopy[0x6c];    // the player's car, lent for the virtual calls
WTargetable *g_selectedTarget = NULL;

float g_fadePct = 0.0f;
bool g_fadeAnimating = false;
int g_letterBox = 0;
float g_radarRadius = 100.0f;
int g_mightAsWellBeDead = 0;
int g_chainNext = 0;
const char *g_chainName = NULL;
int g_accumulate = 0;

struct FakeRule {
    void **vtable;
    int32_t id;
};
constexpr int kFakeRules = 48;
FakeRule g_rules[kFakeRules];
void *g_ruleVtable[4];

// The lists' contents for the next side
struct ObjectiveDesc {
    int32_t id, unknown04, score, unknown0c, state;
};
std::vector<ObjectiveDesc> g_objectiveDesc;
std::vector<MissionMessage> g_messageDesc;
std::vector<int> g_ruleDesc[kMissionStages + 1];    // rule indices; [kMissionStages] the global list

// ---- the fakes

void __fastcall FakeWeaponReset(void *self, int) { Log({ 0x100, Word(self) }); }
void __fastcall FakeLoadWeapon(void *self, int, int weapon, int ammo, int clip, int u34) {
    Log({ 0x101, Word(self), uint32_t(weapon), uint32_t(ammo), uint32_t(clip), uint32_t(u34) });
}
void __fastcall FakeSetSecondary(void *self, int, int weapon) { Log({ 0x102, Word(self), uint32_t(weapon) }); }
void __fastcall FakeShowHands(void *self, int) { Log({ 0x103, Word(self) }); }
void __fastcall FakeHideHands(void *self, int) { Log({ 0x104, Word(self) }); }
GHud *FakeTheApp() {
    Log({ 0x105 });
    return g_hud;
}
void __fastcall FakeHudSetAutoDrive(GHud *self, int, bool on) { Log({ 0x106, Word(self), uint32_t(on) }); }
float __fastcall FakeFadePct(GHud *self, int) {
    Log({ 0x107, Word(self) });
    return g_fadePct;
}
bool __fastcall FakeFadeAnimating(GHud *self, int) {
    Log({ 0x108, Word(self) });
    return g_fadeAnimating;
}
void __fastcall FakeFadeIn(GHud *self, int, int steps) { Log({ 0x109, Word(self), uint32_t(steps) }); }
void __fastcall FakeFadeOut(GHud *self, int, int steps) { Log({ 0x10a, Word(self), uint32_t(steps) }); }
int __fastcall FakeLetterBox(GHud *self, int) {
    Log({ 0x10b, Word(self) });
    return g_letterBox;
}
float __fastcall FakeRadar(GHud *self, int) {
    Log({ 0x10c, Word(self) });
    return g_radarRadius;
}
void __fastcall FakeStylin(GHud *self, int) { Log({ 0x10d, Word(self) }); }
CARP::AIElement *__fastcall FakeRedirect(void *self, int, CARP::AIElement *element) {
    Log({ 0x10e, Word(self), Word(element) });
    for (int i = 0; i < 2; i++)
        if (element == &g_carpElements[i])
            return &g_liveElements[i];
    return &g_liveElements[0];
}
void __fastcall FakeSync(void *self, int, CARP::AIElement *element) { Log({ 0x10f, Word(self), Word(element) }); }
void __fastcall FakeCalcDead(void *self, int) { Log({ 0x110, Word(self) }); }
int __fastcall FakeNumDead(void *self, int) {
    Log({ 0x111, Word(self) });
    return g_mightAsWellBeDead;
}
void __fastcall FakeSteerX(void *self, int, float steer) { Log({ 0x112, Word(self), Bits(steer) }); }
void __fastcall FakeSteerY(void *self, int, float steer) { Log({ 0x113, Word(self), Bits(steer) }); }
void __fastcall FakeSetTarget(void *self, int, void *target) { Log({ 0x114, Word(self), Word(target) }); }
void __fastcall FakeDoRule(void *rule, int) { Log({ 0x115, Word(rule) }); }
void *FakeInstantiate(void *record) {
    Log({ 0x116, Word(record) });
    return &g_rules[(Next() % kFakeRules)];
}
void *__fastcall FakeTimerCtor(void *timer, int) {
    Log({ 0x117, Word(timer) });
    return timer;
}
void __fastcall FakeTimerReset(void *timer, int, int value) { Log({ 0x118, Word(timer), uint32_t(value) }); }
void __fastcall FakeTimerStop(void *timer, int) { Log({ 0x119, Word(timer) }); }
void *__fastcall FakeERestart(void *self, int) {
    Log({ 0x11a, Word(self) });
    return self;
}
void *FakeEventNew(size_t size) {
    Log({ 0x11b, uint32_t(size) });
    return g_eventBuf;
}
RigidBody *__fastcall FakeGetRigidBody(void *sim, int, int slot) {
    Log({ 0x11c, Word(sim), uint32_t(slot) });
    return &g_body;
}
void __fastcall FakeActivate(void *self, int, int mode) { Log({ 0x11d, Word(self), uint32_t(mode) }); }
void __fastcall FakeDeactivate(void *self, int) { Log({ 0x11e, Word(self) }); }
WTargetable *__fastcall FakeSelected(void *self, int) {
    Log({ 0x11f, Word(self) });
    return g_selectedTarget;
}
void __fastcall FakeSetMissileCamera(void *self, int, void *missile) { Log({ 0x120, Word(self), Word(missile) }); }
void __fastcall FakeEndMissileCamera(void *self, int) { Log({ 0x121, Word(self) }); }
void __fastcall FakeColorize(void *self, int, int mode) { Log({ 0x122, Word(self), uint32_t(mode) }); }
void *FakeGetIOModule() {
    Log({ 0x123 });
    return g_ioBuf;
}
void __fastcall FakeReleaseAll(void *self, int) { Log({ 0x124, Word(self) }); }
void *FakeGetConfig() {
    Log({ 0x125 });
    return g_icmBuf;
}
void __fastcall FakeSetConfig(void *self, int, int type, int config) {
    Log({ 0x126, Word(self), uint32_t(type), uint32_t(config) });
}
int __fastcall FakeLookupInt(void *self, int, const char *key, bool *found) {
    Log({ 0x127, Word(self), Hash(key), Word(found) });
    if (strcmp(key, "CHAIN_NEXT_MISSION") == 0)
        return g_chainNext;
    if (strcmp(key, "ACCUMULATE_SCORES") == 0)
        return g_accumulate;
    return 0;
}
const char *__fastcall FakeLookupString(void *self, int, const char *key, bool *found) {
    Log({ 0x128, Word(self), Hash(key), Word(found) });
    return g_chainName;
}
void FakeExitToDriving(int mission, const char *name, bool clear) {
    Log({ 0x129, uint32_t(mission), Hash(name), uint32_t(clear) });
}
void __fastcall FakeShieldPointLoc(void *self, int, float *shield) { Log({ 0x12a, Word(self), Word(shield) }); }
void __fastcall FakeDisableTyres(void *self, int) { Log({ 0x12b, Word(self) }); }
void __fastcall FakeForceStopOn(void *self, int, uint8_t flags) { Log({ 0x12c, Word(self), flags }); }
void __fastcall FakeForceStopOff(void *self, int, uint8_t flags) { Log({ 0x12d, Word(self), flags }); }
void *__fastcall FakeQueueConstruct(void *self, int, char *name) {
    Log({ 0x12e, Hash(name) });
    return self;
}
void __fastcall FakeQueueDestruct(void *self, int) { Log({ 0x12f }); (void)self; }
void __fastcall FakeRuleDelete(FakeRule *rule, int, unsigned flags) { Log({ 0x130, uint32_t(rule->id), flags }); }

Fake g_fakes[] = {
    { 0x000bad70, (const void *)&FakeWeaponReset },
    { 0x000ba9f0, (const void *)&FakeLoadWeapon },
    { 0x000baab0, (const void *)&FakeSetSecondary },
    { 0x00022c30, (const void *)&FakeShowHands },
    { 0x00022c50, (const void *)&FakeHideHands },
    { 0x000d7c10, (const void *)&FakeTheApp },
    { 0x000d9040, (const void *)&FakeHudSetAutoDrive },
    { 0x000d7d50, (const void *)&FakeFadePct },
    { 0x000d7d40, (const void *)&FakeFadeAnimating },
    { 0x000d7c70, (const void *)&FakeFadeIn },
    { 0x000d7cd0, (const void *)&FakeFadeOut },
    { 0x000d9b90, (const void *)&FakeLetterBox },
    { 0x000d7d90, (const void *)&FakeRadar },
    { 0x000d7d60, (const void *)&FakeStylin },
    { 0x000286d0, (const void *)&FakeRedirect },
    { 0x00028a40, (const void *)&FakeSync },
    { 0x00028730, (const void *)&FakeCalcDead },
    { 0x000287c0, (const void *)&FakeNumDead },
    { 0x0005f600, (const void *)&FakeSteerX },
    { 0x0005f640, (const void *)&FakeSteerY },
    { 0x0005f680, (const void *)&FakeSetTarget },
    { 0x000b9590, (const void *)&FakeDoRule },
    { 0x000ba150, (const void *)&FakeInstantiate },
    { 0x000b95d0, (const void *)&FakeTimerCtor },
    { 0x000b95e0, (const void *)&FakeTimerReset },
    { 0x000b9ce0, (const void *)&FakeTimerStop },
    { 0x00043460, (const void *)&FakeERestart },
    { 0x0005a5a0, (const void *)&FakeEventNew },
    { 0x000b2700, (const void *)&FakeGetRigidBody },
    { 0x000ce0f0, (const void *)&FakeActivate },
    { 0x000ce150, (const void *)&FakeDeactivate },
    { 0x000cdfb0, (const void *)&FakeSelected },
    { 0x00088be0, (const void *)&FakeSetMissileCamera },
    { 0x000895e0, (const void *)&FakeEndMissileCamera },
    { 0x0009a500, (const void *)&FakeColorize },
    { 0x00051580, (const void *)&FakeGetIOModule },
    { 0x000510f0, (const void *)&FakeReleaseAll },
    { 0x00050270, (const void *)&FakeGetConfig },
    { 0x00050720, (const void *)&FakeSetConfig },
    { 0x00058180, (const void *)&FakeLookupInt },
    { 0x000583a0, (const void *)&FakeLookupString },
    { 0x00059bf0, (const void *)&FakeExitToDriving },
    { 0x00066020, (const void *)&FakeShieldPointLoc },
    { 0x00066080, (const void *)&FakeDisableTyres },
    { 0x000660f0, (const void *)&FakeForceStopOn },
    { 0x00066110, (const void *)&FakeForceStopOff },
    { 0x0004f320, (const void *)&FakeQueueConstruct },
    { 0x0004f1b0, (const void *)&FakeQueueDestruct },
};

// =============================================================================================================
// The state around a case
// =============================================================================================================

// What a case may change outside the buffer, saved once per case and put back after each side
struct Globals {
    std::vector<uint8_t> page;
    int32_t cinematic, portCinematic;
    uint8_t scheduler[8];
    int32_t steps;
    PVehicle *player;
    SMissionManager *manager;
    uint8_t carFlags[5], times100B, charInfoFlag;
    std::vector<uint8_t> defaultSet;
};

Globals g_saved;
uint8_t g_carFlags[5], g_times100B, g_charInfoFlag;    // the inputs for the owned globals
int32_t g_steps;

void SaveGlobals(Globals *g) {
    g->page.assign(reinterpret_cast<uint8_t *>(&Launch), reinterpret_cast<uint8_t *>(&Launch) + 0xa4c);
    g->cinematic = ShCinematic;
    g->portCinematic = MissionCinematicMode;
    memcpy(g->scheduler, ShScheduler + 0x1c, 8);
    g->steps = ShSimStepCount;
    g->player = *ShPlayerSlot;
    g->manager = glbMissionManager;
    g->carFlags[0] = CarOverrideLotus;
    g->carFlags[1] = CarOverrideVan;
    g->carFlags[2] = CarOverrideLimo;
    g->carFlags[3] = CarOverrideTaxi;
    g->carFlags[4] = CarOverrideSmart;
    g->times100B = MissionHealthTimes100;
    g->charInfoFlag = CharInfoBeforeHud;
    g->defaultSet.assign(reinterpret_cast<uint8_t *>(&DefaultMissionSet),
                         reinterpret_cast<uint8_t *>(&DefaultMissionSet) + sizeof(MissionSet));
}

void RestoreGlobals(const Globals *g) {
    memcpy(&Launch, g->page.data(), 0xa4c);
    ShCinematic = g->cinematic;
    MissionCinematicMode = g->portCinematic;
    memcpy(ShScheduler + 0x1c, g->scheduler, 8);
    ShSimStepCount = g->steps;
    *ShPlayerSlot = g->player;
    glbMissionManager = g->manager;
    CarOverrideLotus = g->carFlags[0];
    CarOverrideVan = g->carFlags[1];
    CarOverrideLimo = g->carFlags[2];
    CarOverrideTaxi = g->carFlags[3];
    CarOverrideSmart = g->carFlags[4];
    MissionHealthTimes100 = g->times100B;
    CharInfoBeforeHud = g->charInfoFlag;
    memcpy(&DefaultMissionSet, g->defaultSet.data(), sizeof(MissionSet));
}

// The owned globals' inputs into both copies (the original's addresses and ours)
void MirrorOwned() {
    memcpy(ShOrigCarFlags, g_carFlags, 5);
    CarOverrideLotus = g_carFlags[0];
    CarOverrideVan = g_carFlags[1];
    CarOverrideLimo = g_carFlags[2];
    CarOverrideTaxi = g_carFlags[3];
    CarOverrideSmart = g_carFlags[4];
    ShOrigTimes100B = g_times100B;
    MissionHealthTimes100 = g_times100B;
    ShOrigCharInfoFlag = g_charInfoFlag;
    CharInfoBeforeHud = g_charInfoFlag;
    ShCinematic = MissionCinematicMode = g_saved.portCinematic;
    memcpy(ShOrigDefaultSet, g_saved.defaultSet.data(), sizeof(MissionSet));
    memcpy(&DefaultMissionSet, g_saved.defaultSet.data(), sizeof(MissionSet));
}

// ---- the lists

void BuildList(MissionList *list) {
    list->head = list->BuyHead();
    list->size = 0;
}

void BuildLists(SMissionManager *m) {
    BuildList(&m->objectives);
    for (const ObjectiveDesc &d : g_objectiveDesc) {
        MissionObjective *o = static_cast<MissionObjective *>(OperatorNew(sizeof(MissionObjective)));
        o->id = d.id;
        o->unknown04 = d.unknown04;
        o->score = d.score;
        o->unknown0c = d.unknown0c;
        o->state = d.state;
        m->objectives.PushBack(o);
    }
    BuildList(&m->messages);
    for (const MissionMessage &d : g_messageDesc) {
        MissionMessage *msg = static_cast<MissionMessage *>(OperatorNew(sizeof(MissionMessage)));
        *msg = d;
        m->messages.PushBack(msg);
    }
    BuildList(&m->globalRules);
    for (int r : g_ruleDesc[kMissionStages])
        m->globalRules.PushBack(&g_rules[r]);
    for (int s = 0; s < kMissionStages; s++) {
        BuildList(&m->stageRules[s]);
        for (int r : g_ruleDesc[s])
            m->stageRules[s].PushBack(&g_rules[r]);
    }
}

uint32_t RuleWord(void *rule) {
    for (int i = 0; i < kFakeRules; i++)
        if (rule == &g_rules[i])
            return uint32_t(i);
    return 0xdead0000u | (Word(rule) & 0xffff);
}

// The lists by content, then freed (the objectives and messages with them)
void TakeLists(SMissionManager *m, std::vector<uint32_t> *out, bool freeRecords) {
    MissionList *all[3 + kMissionStages];
    all[0] = &m->objectives;
    all[1] = &m->messages;
    all[2] = &m->globalRules;
    for (int s = 0; s < kMissionStages; s++)
        all[3 + s] = &m->stageRules[s];
    for (int k = 0; k < 3 + kMissionStages; k++) {
        MissionList *list = all[k];
        out->push_back(list->size);
        if (list->head == NULL) {
            out->push_back(0xffffffffu);
            continue;
        }
        size_t guard = 0;
        for (PointerListNode *node = list->head->next; node != list->head && guard++ < 1000; node = node->next) {
            void *value = node->value;
            if (k == 0 && value != NULL) {
                const MissionObjective *o = static_cast<const MissionObjective *>(value);
                out->insert(out->end(), { uint32_t(o->id), uint32_t(o->unknown04), uint32_t(o->score),
                                          uint32_t(o->unknown0c), uint32_t(o->state) });
            } else if (k == 1 && value != NULL) {
                const MissionMessage *msg = static_cast<const MissionMessage *>(value);
                out->insert(out->end(), { uint32_t(msg->unknown00), uint32_t(msg->unknown04), uint32_t(msg->unknown08) });
            } else if (k >= 2) {
                out->push_back(RuleWord(value));
            } else {
                out->push_back(0);
            }
            if (k < 2 && freeRecords && value != NULL)
                OperatorDelete(value);
        }
        PointerListNode *erased;
        list->Erase(&erased, list->head->next, list->head);
        UMemory::FastFree(list->head, sizeof(PointerListNode));
        list->head = NULL;
        list->size = 0;
    }
}

// The manager's bytes with the pointers that differ by side normalised
std::vector<uint8_t> TakeBytes(SMissionManager *m, std::string *carType) {
    SMissionManager copy;
    memcpy(&copy, m, sizeof(copy));
    *carType = copy.carType != NULL ? copy.carType : "(null)";
    copy.carType = NULL;
    if (Word(copy.missionSet) == 0x00238f20u || copy.missionSet == &DefaultMissionSet)
        copy.missionSet = reinterpret_cast<MissionSet *>(uintptr_t(1));
    if (copy.actionQueue != NULL && copy.actionQueue != reinterpret_cast<ActionQueue *>(g_queueBuf))
        copy.actionQueue = reinterpret_cast<ActionQueue *>(uintptr_t(2));
    copy.objectives.head = copy.messages.head = copy.globalRules.head = NULL;
    for (int s = 0; s < kMissionStages; s++)
        copy.stageRules[s].head = NULL;
    const uint8_t *b = reinterpret_cast<const uint8_t *>(&copy);
    return std::vector<uint8_t>(b, b + sizeof(copy));
}

struct Side {
    std::vector<uint8_t> manager, globals, extra;
    std::vector<uint32_t> log, lists;
    std::string carType;
    bool ok;
};

void TakeGlobals(Side *side, bool original) {
    const uint8_t *page = reinterpret_cast<const uint8_t *>(&Launch);
    side->globals.assign(page, page + 0xa4c);
    const uint8_t *cin = reinterpret_cast<const uint8_t *>(original ? &ShCinematic : &MissionCinematicMode);
    side->globals.insert(side->globals.end(), cin, cin + 4);
    side->globals.insert(side->globals.end(), ShScheduler + 0x1c, ShScheduler + 0x24);
    if (original) {
        side->globals.insert(side->globals.end(), ShOrigCarFlags, ShOrigCarFlags + 5);
        side->globals.push_back(ShOrigTimes100B);
        side->globals.push_back(ShOrigCharInfoFlag);
        side->globals.insert(side->globals.end(), ShOrigDefaultSet, ShOrigDefaultSet + sizeof(MissionSet));
    } else {
        side->globals.insert(side->globals.end(), { CarOverrideLotus, CarOverrideVan, CarOverrideLimo,
                                                   CarOverrideTaxi, CarOverrideSmart });
        side->globals.push_back(MissionHealthTimes100);
        side->globals.push_back(CharInfoBeforeHud);
        const uint8_t *d = reinterpret_cast<const uint8_t *>(&DefaultMissionSet);
        side->globals.insert(side->globals.end(), d, d + sizeof(MissionSet));
    }
    const uint8_t *p = reinterpret_cast<const uint8_t *>(&g_page);
    side->globals.insert(side->globals.end(), p, p + sizeof(g_page));
    const uint8_t *s = reinterpret_cast<const uint8_t *>(&g_set);
    side->globals.insert(side->globals.end(), s, s + sizeof(g_set));
    side->globals.insert(side->globals.end(), g_ioBuf, g_ioBuf + sizeof(g_ioBuf));
}

// Runs a case: `call(original, &extra)` on each side from the template's state; `extra` takes return values
LaunchPage g_pageTmpl;
MissionSet g_setTmpl;
std::vector<uint8_t> g_pageGlobalTmpl;  // the launch page's state for the case

template <class F>
void RunCase(const char *name, int index, F call) {
    g_cases++;
    Side sides[2];
    for (int k = 0; k < 2; k++) {
        bool original = k == 0;
        Side &side = sides[k];
        RestoreGlobals(&g_saved);
        memcpy(&Launch, g_pageGlobalTmpl.data(), 0xa4c);
        ShSimStepCount = g_steps;
        MirrorOwned();
        memcpy(g_mm, g_tmpl, sizeof(SMissionManager));
        memcpy(g_queueBuf, g_queueTmpl, sizeof(g_queueBuf));
        memset(g_ioBuf, 0, sizeof(g_ioBuf));
        g_page = g_pageTmpl;
        g_set = g_setTmpl;
        BuildLists(g_mm);
        glbMissionManager = g_mm;
        memcpy(g_carCopy, g_saved.player, sizeof(g_carCopy));
        *ShPlayerSlot = reinterpret_cast<PVehicle *>(g_carCopy);
        uint32_t seed = g_seed;
        side.log.clear();
        g_log = &side.log;
        auto run = [&](bool o) { call(o, &side.extra); };
        side.ok = Guarded(&Thunk<decltype(run)>, &run, original);
        g_log = NULL;
        g_seed = seed;
        TakeGlobals(&side, original);
        side.manager = TakeBytes(g_mm, &side.carType);
        TakeLists(g_mm, &side.lists, true);
    }
    RestoreGlobals(&g_saved);
    if (!sides[0].ok || !sides[1].ok) {
        char detail[64];
        snprintf(detail, sizeof(detail), "fault: original %d, port %d", sides[0].ok, sides[1].ok);
        g_checks++;
        if (sides[0].ok != sides[1].ok)
            Differ(name, index, detail);
        return;
    }
    char what[96];
    snprintf(what, sizeof(what), "%s manager", name);
    CheckBytes(what, index, sides[0].manager, sides[1].manager);
    snprintf(what, sizeof(what), "%s globals", name);
    CheckBytes(what, index, sides[0].globals, sides[1].globals);
    snprintf(what, sizeof(what), "%s answer", name);
    CheckBytes(what, index, sides[0].extra, sides[1].extra);
    snprintf(what, sizeof(what), "%s calls", name);
    CheckWords(what, index, sides[0].log, sides[1].log);
    snprintf(what, sizeof(what), "%s lists", name);
    CheckWords(what, index, sides[0].lists, sides[1].lists);
    g_checks++;
    if (sides[0].carType != sides[1].carType) {
        snprintf(what, sizeof(what), "%s carType", name);
        Differ(what, index, (sides[0].carType + " / " + sides[1].carType).c_str());
    }
}

template <class T>
void PutAnswer(std::vector<uint8_t> *extra, const T &value) {
    const uint8_t *b = reinterpret_cast<const uint8_t *>(&value);
    extra->insert(extra->end(), b, b + sizeof(T));
}

// =============================================================================================================
// Perturbed inputs
// =============================================================================================================

float RandomFloat(float lo, float hi) {
    if (Chance(40))
        return 0.0f;
    return Uniform(lo, hi);
}

void PerturbSet(MissionSet *set) {
    if (Chance(3)) {
        const char *cars[] = { "db5", "z8", "DB5", "lotus", "vanquish", "z8x" };
        strcpy(set->name, cars[Range(0, 5)]);
    }
    for (int w = 0; w < 32; w++) {
        set->weaponEnabled[w] = uint8_t(Chance(2) ? Range(0, 2) : 0);
        set->weaponAmmo[w] = Chance(8) ? Range(9990, 10010) : Range(-2, 120);
        set->weaponClipSize[w] = Range(0, 10);
        set->weaponDamage[w] = Range(-1, 5);
    }
    int32_t *values = &set->objectivesValue;
    for (int i = 0; i < 12; i++)
        values[i] = Range(-100, 2000);
    set->maxHealth = Range(0, 5000);
    set->health = Chance(4) ? 0 : Range(1, 5000);
    set->maxShield = Range(0, 3000);
    set->shield = Range(0, 3000);
    set->fullScoreTime = Range(-10, 400);
    set->zeroScoreTime = Range(-10, 800);
    for (int i = 0; i < 3; i++) {
        set->healthScale[i] = Range(-50, 200);
        set->shieldScale[i] = Range(-50, 200);
        set->enemyAccuracyBoost[i] = Range(-5, 50);
        set->enemyGlueFactor[i] = Chance(3) ? 0.0f : Uniform(0.0f, 3.0f);
        set->missionText[i] = Chance(3) ? 0.0f : Uniform(0.0f, 3.0f);
        set->unknown2d0[i] = Range(-5, 500);
    }
    set->unknown2e0 = Range(-10, 500);
    set->unknown2e4 = Range(0, 100);
    set->unknown2e8 = Range(0, 100);
    set->healthPercentCap = Range(-10, 150);
    set->damageZeroScore = Range(-100, 20000);
    set->damageFullScore = Range(-100, 20000);
}

void PerturbPage(LaunchPage *page) {
    for (int i = 0; i < 12; i++) {
        page->accumulatedScores[i] = Range(-50, 5000);
        page->bestScores[i] = Chance(10) ? 0 : Range(-50, 5000);
    }
    page->healthRatio = Chance(3) ? 0.0f : Uniform(-0.5f, 1.5f);
    page->shieldRatio = Chance(3) ? 0.0f : Uniform(-0.5f, 1.5f);
    page->missionTime = Range(0, 600);
    page->shotsFired = Range(0, 500);
    page->shotsHit = Range(0, 600);
    page->restartStep = Chance(3) ? Range(-5, 20000) : 0;
    page->restartCount = Range(0, 5);
    page->difficulty = Range(0, 3);
    page->flags = Next() & 0xfff;
    page->cheats = Next() & 3;
    page->unknowna3c = Range(0, 5);
    if (Chance(4))
        strcpy(page->missionName, Chance(2) ? "uw_map" : "jungleb_mis13b");
}

void PerturbManager(SMissionManager *m) {
    m->missionSet = &g_set;
    m->missionState = Range(0, 5);
    m->autoDrive = uint8_t(Range(0, 2));
    m->missileHasTarget = uint8_t(Range(0, 1));
    m->driveMissile = Chance(4) ? NULL : reinterpret_cast<Missile *>(g_missileBuf);
    m->steerX = RandomFloat(-1.0f, 1.0f);
    m->steerY = RandomFloat(-1.0f, 1.0f);
    m->steerX2 = RandomFloat(-1.0f, 1.0f);
    m->steerY2 = RandomFloat(-1.0f, 1.0f);
    m->actionQueue = reinterpret_cast<ActionQueue *>(g_queueBuf);
    m->playerMaxHealth = Chance(10) ? 0.0f : Uniform(1.0f, 5000.0f);
    m->playerHealth = Uniform(-10.0f, 6000.0f);
    m->playerMaxShield = Chance(5) ? 0.0f : Uniform(-10.0f, 3000.0f);
    m->playerShield = Uniform(0.0f, 3500.0f);
    m->difficulty = Range(0, 3);
    m->shotsFired = Chance(5) ? 0 : Range(-2, 400);
    m->shotsHit = Range(0, 450);
    m->kills = Range(-3, 60);
    m->damage = RandomFloat(0.0f, 25000.0f);
    m->cinematicMode = Chance(2) ? 0 : int32_t(Next() & 0xf);
    m->skippingCinematic = uint8_t(Range(0, 1));
    m->objectiveSeenStep = g_steps - Range(-10, 400);
    m->unknown4fc = Range(0, 50);
    m->enemyCount = Chance(4) ? 0 : Range(-2, 80);
    m->lastDamageStep = g_steps - Range(-20, 200);
    for (int c = 0; c < kMagicCounters; c++) {
        m->magicCounters[c] = Range(-2, 6);
        m->magicThresholds[c] = Range(-2, 6);
    }
    m->stylinCount = Range(0, 20);
    m->missionEndTime = Chance(2) ? 0 : Range(1, 900);
    m->lastShotStep = g_steps - Range(-20, 400);
    m->loadCount = Range(0, 2);
    for (int slot = 0; slot < kObjectiveSlots; slot++) {
        int kind = Range(0, 4);
        m->objectiveType[slot] = kind;
        m->objectiveTarget[slot] = kind == kObjectiveMarker ? static_cast<const void *>(&g_marker)
                                 : kind == kObjectiveKind2 ? static_cast<const void *>(g_kind2Buf)
                                                           : static_cast<const void *>(&g_carpElements[slot]);
        m->objectiveComment[slot] = Next();
        m->objectiveVisible[slot] = uint8_t(Range(0, 1));
    }
    for (int i = 0; i < kScoreCount; i++) {
        m->startScores[i] = Range(-10, 3000);
        m->scores[i] = Range(-60, 2000);
    }
    m->objectiveCheckStep = g_steps + Range(-15, 15);
    for (int a = 0; a < kTimedActions; a++) {
        MissionTimedAction &t = m->timedActions[a];
        t.active = uint8_t(Range(0, 1));
        t.fullPointsTime = Uniform(-1.0f, 20.0f);
        t.timeLimit = Chance(5) ? t.fullPointsTime : Uniform(-1.0f, 40.0f);
        t.minPoints = Range(-50, 300);
        t.maxPoints = Range(-50, 600);
        t.startStep = g_steps - Range(-100, 2000);
    }
    m->sectionCount = Range(0, 6);
    m->currentSection = Range(-1, m->sectionCount - 1);
    for (int i = 0; i < 8; i++)
        snprintf(m->sections[i].name, sizeof(m->sections[i].name), "sect%d", Range(0, 9));
    m->unknown47a[0] = m->unknown47a[1] = 0;
}

void PerturbWorld() {
    g_hud = Chance(6) ? NULL : reinterpret_cast<GHud *>(g_hudBuf);
    g_hudBuf[0x2b0] = uint8_t(Chance(4) ? 1 : 0);
    float pcts[] = { 0.0f, 1.0f, 0.5f, -0.25f };
    g_fadePct = pcts[Range(0, 3)];
    g_fadeAnimating = Chance(2);
    g_letterBox = Chance(3) ? 1 : 0;
    g_radarRadius = Uniform(5.0f, 400.0f);
    g_mightAsWellBeDead = Range(0, 6);
    g_selectedTarget = Chance(2) ? NULL : reinterpret_cast<WTargetable *>(g_kind2Buf);
    for (int i = 0; i < 2; i++) {
        CARP::AIElement &e = g_liveElements[i];
        e.Position().x = Uniform(-500.0f, 500.0f);
        e.Position().y = Uniform(-20.0f, 50.0f);
        e.Position().z = Uniform(-500.0f, 500.0f);
        e.Position().w = 1.0f;
        e.type = Range(0, 2);
        e.state = Range(0, 2);
        e.car.helicopter = uint8_t(Chance(3));
        e.object = Chance(5) ? NULL : reinterpret_cast<PhysicsObject *>(g_vehicles[i]);
        g_instances[i].packedDimensions = Next() | (Next() << 24);
    }
    g_marker.position.x = Uniform(-500.0f, 500.0f);
    g_marker.position.y = Uniform(-20.0f, 50.0f);
    g_marker.position.z = Uniform(-500.0f, 500.0f);
    g_marker.position.w = 1.0f;
    g_marker.arrowHeight = Uniform(0.0f, 20.0f);
    g_body.velocity.x = RandomFloat(-30.0f, 30.0f);
    g_body.velocity.y = RandomFloat(-3.0f, 3.0f);
    g_body.velocity.z = RandomFloat(-30.0f, 30.0f);
    g_carFlags[0] = uint8_t(Chance(4));
    g_carFlags[1] = uint8_t(Chance(4));
    g_carFlags[2] = uint8_t(Chance(4));
    g_carFlags[3] = uint8_t(Chance(4));
    g_carFlags[4] = uint8_t(Chance(4));
    g_times100B = uint8_t(Chance(5));
    g_charInfoFlag = uint8_t(Range(0, 1));
    g_chainNext = Chance(2) ? 0 : Range(1, 8);
    g_chainName = Chance(3) ? NULL : "junglea_mis13a";
    g_accumulate = Range(0, 1);
}

void PerturbLists() {
    g_objectiveDesc.clear();
    int count = Range(0, 8);
    for (int i = 0; i < count; i++)
        g_objectiveDesc.push_back({ Range(0, 9), Range(0, 99), Range(0, 500), Range(0, 99), Range(0, 3) });
    g_messageDesc.clear();
    count = Range(0, 4);
    for (int i = 0; i < count; i++)
        g_messageDesc.push_back({ int32_t(Next()), int32_t(Next()), int32_t(Next()) });
    for (int s = 0; s <= kMissionStages; s++) {
        g_ruleDesc[s].clear();
        if (s < 6 || s == kMissionStages || Chance(16)) {
            count = Range(0, 4);
            for (int i = 0; i < count; i++)
                g_ruleDesc[s].push_back(Range(0, kFakeRules - 1));
        }
    }
}

void PerturbQueue() {
    ActionQueue *queue = reinterpret_cast<ActionQueue *>(g_queueTmpl);
    memset(g_queueTmpl, 0, sizeof(g_queueTmpl));
    queue->ring.capacity = 200;
    queue->Flush();
    int count = Range(0, 12);
    static const int kActions[] = { kActionChangeCamera, kActionCamRotateX, kActionCamRotateY, kActionCamRotateYInv,
                                    kActionCamRotateX2, kActionCamRotateY2, kActionCamRotateY2Inv,
                                    kActionAutoDriveLock, kActionAutoDriveLockRelease, kActionGas, 0x25, 0x3d };
    for (int i = 0; i < count; i++) {
        ActionData action;
        action.action = kActions[Range(0, 11)];
        action.source = 0;
        action.value = Chance(6) ? (Chance(2) ? 0.0f : -0.0f) : Uniform(-1.0f, 1.0f);
        queue->ReceiveAction(&action);
    }
}

// A fresh case: the template from the live manager, everything perturbed
void NewInputs(const SMissionManager *live) {
    g_steps = g_saved.steps + Range(-50, 3000);
    if (g_steps < 0)
        g_steps = 0;
    memcpy(g_tmpl, live, sizeof(SMissionManager));
    g_setTmpl = *live->missionSet;
    PerturbSet(&g_setTmpl);
    PerturbManager(g_tmpl);
    memcpy(&g_pageTmpl, g_saved.page.data(), sizeof(g_pageTmpl));
    PerturbPage(&g_pageTmpl);
    g_pageGlobalTmpl.assign(g_saved.page.begin(), g_saved.page.end());
    LaunchPage *globalPage = reinterpret_cast<LaunchPage *>(g_pageGlobalTmpl.data());
    LaunchPage perturbed = g_pageTmpl;
    PerturbPage(&perturbed);
    globalPage->healthRatio = perturbed.healthRatio;
    globalPage->shieldRatio = perturbed.shieldRatio;
    globalPage->missionTime = perturbed.missionTime;
    globalPage->restartStep = perturbed.restartStep;
    globalPage->restartCount = perturbed.restartCount;
    globalPage->flags = perturbed.flags;
    globalPage->cheats = perturbed.cheats;
    globalPage->difficulty = perturbed.difficulty;
    globalPage->unknowna3c = perturbed.unknowna3c;
    memcpy(globalPage->missionName, perturbed.missionName, sizeof(globalPage->missionName));
    PerturbWorld();
    PerturbLists();
    PerturbQueue();
}

// =============================================================================================================
// The cases
// =============================================================================================================

typedef void (__fastcall *VoidFn)(SMissionManager *, int);
typedef void (__fastcall *IntFn)(SMissionManager *, int, int);
typedef void (__fastcall *Int2Fn)(SMissionManager *, int, int, int);
typedef void (__fastcall *FloatFn)(SMissionManager *, int, float);
typedef int (__fastcall *RetIntFn)(SMissionManager *, int);
typedef int (__fastcall *IntRetIntFn)(SMissionManager *, int, int);
typedef float (__fastcall *RetFloatFn)(SMissionManager *, int);

SMissionManager *M() {
    return g_mm;
}

// Calls with no answer to compare: original address, the port's method
#define CASE_VOID(name, addr, call) \
    RunCase(name, i, [&](bool o, std::vector<uint8_t> *) { if (o) ((VoidFn)addr)(M(), 0); else M()->call(); })
#define CASE_INT(name, addr, call, a) \
    RunCase(name, i, [&](bool o, std::vector<uint8_t> *) { if (o) ((IntFn)addr)(M(), 0, a); else M()->call(a); })

void TestSimple(const SMissionManager *live, int rounds) {
    for (int i = 0; i < rounds; i++) {
        NewInputs(live);
        int a = Range(-2, 70), b = Range(-5, 9);
        float f = Uniform(-0.5f, 1.5f);
        CASE_VOID("SetCarType", 0x000b60a0, SetCarType);
        RunCase("GetCarType", i, [&](bool o, std::vector<uint8_t> *x) {
            const char *s = o ? ((const char *(__fastcall *)(SMissionManager *, int))0x000b6140)(M(), 0) : M()->GetCarType();
            PutAnswer(x, Hash(s));
        });
        CASE_VOID("LoadWeaponSet", 0x000b6160, LoadWeaponSet);
        RunCase("LoadWeaponSet default", i, [&](bool o, std::vector<uint8_t> *) {
            if (o) {
                M()->missionSet = reinterpret_cast<MissionSet *>(ShOrigDefaultSet);
                ((VoidFn)0x000b6160)(M(), 0);
            } else {
                M()->missionSet = &DefaultMissionSet;
                M()->LoadWeaponSet();
            }
        });
        CASE_VOID("Start", 0x000b6380, Start);
        CASE_VOID("Stop", 0x000b63a0, Stop);
        CASE_VOID("RestoreLives", 0x000b63c0, RestoreLives);
        RunCase("ClearScoreBuffer", i, [&](bool o, std::vector<uint8_t> *) {
            if (o)
                ((void (__fastcall *)(SMissionManager *, int, LaunchPage *))0x000b63f0)(M(), 0, &g_page);
            else
                M()->ClearScoreBuffer(&g_page);
        });
        CASE_INT("ChangeStage", 0x000b64b0, ChangeStage, b);
        bool on = Chance(2), controls = Chance(2);
        RunCase("SetAutoDrive", i, [&](bool o, std::vector<uint8_t> *) {
            if (o)
                ((void (__fastcall *)(SMissionManager *, int, bool, bool))0x000b64c0)(M(), 0, on, controls);
            else
                M()->SetAutoDrive(on, controls);
        });
        RunCase("GoingToSkipCinematic", i, [&](bool o, std::vector<uint8_t> *x) {
            PutAnswer(x, o ? ((RetIntFn)0x000b6590)(M(), 0) : M()->GoingToSkipCinematic());
        });
        RunCase("IsSkippingCinematic", i, [&](bool o, std::vector<uint8_t> *x) {
            PutAnswer(x, o ? ((RetIntFn)0x000b65b0)(M(), 0) : M()->IsSkippingCinematic());
        });
        RunCase("BoostPlayerHealth", i, [&](bool o, std::vector<uint8_t> *) {
            if (o) ((FloatFn)0x000b65f0)(M(), 0, f); else M()->BoostPlayerHealth(f);
        });
        RunCase("BoostPlayerShield", i, [&](bool o, std::vector<uint8_t> *) {
            if (o) ((FloatFn)0x000b6640)(M(), 0, f); else M()->BoostPlayerShield(f);
        });
        CASE_INT("SetPlayerShieldTimes", 0x000b6690, SetPlayerShieldTimes, b);
        RunCase("IncPlayerDamage", i, [&](bool o, std::vector<uint8_t> *) {
            float d = f * 300.0f;
            if (o) ((FloatFn)0x000b66c0)(M(), 0, d); else M()->IncPlayerDamage(d);
        });
        CASE_INT("IncShotsFired", 0x000b66f0, IncShotsFired, b);
        RunCase("IncShotsHit", i, [&](bool o, std::vector<uint8_t> *) {
            if (o) ((void (__fastcall *)(SMissionManager *, int, bool))0x000b6720)(M(), 0, on);
            else M()->IncShotsHit(on);
        });
        CASE_INT("IncKills", 0x000b6770, IncKills, b);
        int counter = Range(0, 63);
        RunCase("GetMagicCounter", i, [&](bool o, std::vector<uint8_t> *x) {
            PutAnswer(x, o ? ((IntRetIntFn)0x000b67a0)(M(), 0, counter) : M()->GetMagicCounter(counter));
        });
        RunCase("SetMagicCounterThreshold", i, [&](bool o, std::vector<uint8_t> *) {
            if (o) ((Int2Fn)0x000b67b0)(M(), 0, a, b); else M()->SetMagicCounterThreshold(a, b);
        });
        CASE_INT("IncMagicCounter", 0x000b7a40, IncMagicCounter, a);
        RunCase("SetMagicCounter", i, [&](bool o, std::vector<uint8_t> *) {
            if (o) ((Int2Fn)0x000b7ac0)(M(), 0, a, b); else M()->SetMagicCounter(a, b);
        });
        RunCase("GetPlayerHealth", i, [&](bool o, std::vector<uint8_t> *x) {
            double h = o ? ((double (__fastcall *)(SMissionManager *, int, bool))0x000b67d0)(M(), 0, on)
                         : M()->GetPlayerHealth(on);
            PutAnswer(x, h);
        });
        CASE_VOID("ResetCharInfo", 0x000b67f0, ResetCharInfo);
        CASE_VOID("UpdateCharInfo", 0x000b6b20, UpdateCharInfo);
        int action = Range(0, 7);
        float t1 = Uniform(-1.0f, 20.0f), t2 = Uniform(-1.0f, 40.0f);
        int p1 = Range(-50, 300), p2 = Range(-50, 600);
        RunCase("StartTimedAction", i, [&](bool o, std::vector<uint8_t> *) {
            if (o)
                ((void (__fastcall *)(SMissionManager *, int, int, float, float, int, int))0x000b6bd0)(M(), 0, action,
                                                                                                    t1, t2, p1, p2);
            else
                M()->StartTimedAction(action, t1, t2, p1, p2);
        });
        CASE_INT("CompleteTimedAction", 0x000b7de0, CompleteTimedAction, action);
        CASE_VOID("UpdateTimedActions", 0x000b7f10, UpdateTimedActions);
        int slot = Range(0, 1);
        uint32_t comment = Next();
        RunCase("SetObjectiveComment", i, [&](bool o, std::vector<uint8_t> *) {
            if (o) ((void (__fastcall *)(SMissionManager *, int, int, uint32_t))0x000b6c30)(M(), 0, slot, comment);
            else M()->SetObjectiveComment(slot, comment);
        });
        CASE_INT("ClearObjective", 0x000b6c50, ClearObjective, slot);
        RunCase("GetCurrObjective", i, [&](bool o, std::vector<uint8_t> *x) {
            PutAnswer(x, Word(o ? ((const Coord4 *(__fastcall *)(SMissionManager *, int, int))0x000b6c70)(M(), 0, slot)
                                : M()->GetCurrObjective(slot)));
        });
        RunCase("GetObjectiveComment", i, [&](bool o, std::vector<uint8_t> *x) {
            PutAnswer(x, o ? ((IntRetIntFn)0x000b6cc0)(M(), 0, slot) : int(M()->GetObjectiveComment(slot)));
        });
        RunCase("GetObjectiveArrowHeight", i, [&](bool o, std::vector<uint8_t> *x) {
            PutAnswer(x, o ? ((double (__fastcall *)(SMissionManager *, int, int))0x000b6cd0)(M(), 0, slot)
                           : M()->GetObjectiveArrowHeight(slot));
        });
        RunCase("IsObjectiveVisibleToUser", i, [&](bool o, std::vector<uint8_t> *x) {
            uint8_t v = o ? ((uint8_t (__fastcall *)(SMissionManager *, int, int))0x000b6d40)(M(), 0, slot)
                          : uint8_t(M()->IsObjectiveVisibleToUser(slot));
            PutAnswer(x, v);
        });
        RunCase("CheckObjectiveVisibleToUser", i, [&](bool o, std::vector<uint8_t> *x) {
            uint8_t v = o ? ((uint8_t (__fastcall *)(SMissionManager *, int, int))0x000b6d50)(M(), 0, slot)
                          : uint8_t(M()->CheckObjectiveVisibleToUser(slot));
            PutAnswer(x, v);
        });
        int score = Range(0, 11), points = Range(-20, 200);
        RunCase("AddScore", i, [&](bool o, std::vector<uint8_t> *) {
            if (o) ((Int2Fn)0x000b6e30)(M(), 0, score, points); else M()->AddScore(score, points);
        });
        CASE_VOID("CalcEndMissionScores", 0x000b6e80, CalcEndMissionScores);
        RunCase("GetEnemyAccuracyBoost", i, [&](bool o, std::vector<uint8_t> *x) {
            PutAnswer(x, o ? ((RetIntFn)0x000b7260)(M(), 0) : M()->GetEnemyAccuracyBoost());
        });
        RunCase("GetEnemyGlueFactor", i, [&](bool o, std::vector<uint8_t> *x) {
            PutAnswer(x, Bits(o ? ((RetFloatFn)0x000b7290)(M(), 0) : M()->GetEnemyGlueFactor()));
        });
        RunCase("ShowMissionText", i, [&](bool o, std::vector<uint8_t> *x) {
            PutAnswer(x, Bits(o ? ((RetFloatFn)0x000b72c0)(M(), 0) : M()->ShowMissionText()));
        });
        int event = Range(0, 63);
        RunCase("ProgrammerDefinedEvent", i, [&](bool o, std::vector<uint8_t> *) {
            if (o) ((void (__fastcall *)(SMissionManager *, int, int, const char *))0x000b72f0)(M(), 0, event, "");
            else M()->ProgrammerDefinedEvent(event, "");
        });
        RunCase("CheckProgrammerEvent", i, [&](bool o, std::vector<uint8_t> *x) {
            uint8_t v = o ? ((uint8_t (__fastcall *)(SMissionManager *, int, int))0x000b7300)(M(), 0, event)
                          : uint8_t(M()->CheckProgrammerEvent(event));
            PutAnswer(x, v);
        });
        CASE_INT("ClearProgrammerEvent", 0x000b7310, ClearProgrammerEvent, event);
        char section[16];
        snprintf(section, sizeof(section), "sect%d", Range(0, 9));
        const char *sectionName = Chance(8) ? NULL : Chance(8) ? "" : section;
        if (Chance(4))
            g_tmpl->sectionCount = kSectionProfiles - Range(0, 1);
        RunCase("StartSectionProfile", i, [&](bool o, std::vector<uint8_t> *) {
            if (o) ((void (__fastcall *)(SMissionManager *, int, const char *))0x000b7320)(M(), 0, sectionName);
            else M()->StartSectionProfile(sectionName);
        });
        g_tmpl->sectionCount = Range(0, 6);
        g_tmpl->currentSection = Range(-1, g_tmpl->sectionCount - 1);
        RunCase("TallyScores", i, [&](bool o, std::vector<uint8_t> *) {
            if (o)
                ((void (__fastcall *)(SMissionManager *, int, LaunchPage *, bool))0x000b7470)(M(), 0, &g_page, on);
            else
                M()->TallyScores(&g_page, on);
        });
        int mask = Chance(4) ? -1 : int(Next() & 0xf);
        CASE_INT("ClearCinematicMode", 0x000b7950, ClearCinematicMode, mask);
        CASE_VOID("SkipCinematic", 0x000b79d0, SkipCinematic);
        CASE_INT("SetCinematicMode", 0x000b8160, SetCinematicMode, Chance(3) ? 0 : mask);
        CASE_VOID("UpdateAutoDriveMissile", 0x000b7ae0, UpdateAutoDriveMissile);
        RunCase("SetObjective marker", i, [&](bool o, std::vector<uint8_t> *) {
            const ObjectiveMarker *t = Chance(5) ? NULL : &g_marker;
            if (o) ((void (__fastcall *)(SMissionManager *, int, int, const void *))0x000b7f60)(M(), 0, slot, t);
            else M()->SetObjective(slot, t);
        });
        RunCase("SetObjective kind 2", i, [&](bool o, std::vector<uint8_t> *) {
            const ObjectiveKind2Target *t = Chance(5) ? NULL : reinterpret_cast<const ObjectiveKind2Target *>(g_kind2Buf);
            if (o) ((void (__fastcall *)(SMissionManager *, int, int, const void *))0x000b7fb0)(M(), 0, slot, t);
            else M()->SetObjective(slot, t);
        });
        RunCase("SetObjective element", i, [&](bool o, std::vector<uint8_t> *) {
            CARP::AIElement *t = Chance(5) ? NULL : &g_carpElements[slot];
            if (o) ((void (__fastcall *)(SMissionManager *, int, int, void *))0x000b8000)(M(), 0, slot, t);
            else M()->SetObjective(slot, t);
        });
        CASE_VOID("InitPlayerCar", 0x000b8090, InitPlayerCar);
        RunCase("SetDriveMissile", i, [&](bool o, std::vector<uint8_t> *) {
            Missile *m = Chance(2) ? NULL : reinterpret_cast<Missile *>(g_missileBuf);
            if (o) ((void (__fastcall *)(SMissionManager *, int, Missile *))0x000b80b0)(M(), 0, m);
            else M()->SetDriveMissile(m);
        });
        CASE_VOID("UpdateObjectiveSpecial", 0x000b81a0, UpdateObjectiveSpecial);
        CASE_VOID("Win", 0x000b84b0, Win);
        CASE_VOID("Lose", 0x000b8580, Lose);
        CASE_INT("CallStage", 0x000b85d0, CallStage, Range(0, 7));
        CASE_VOID("ProcessRules", 0x000b8610, ProcessRules);
        CASE_VOID("Update", 0x000b8680, Update);
    }
}

void TestLists(const SMissionManager *live, int rounds) {
    for (int i = 0; i < rounds; i++) {
        NewInputs(live);
        int id = Range(0, 10), index = Range(-1, 8);
        int a = Range(0, 99), b = Range(0, 500), c = Range(0, 99);
        RunCase("GetObjectiveByIndex", i, [&](bool o, std::vector<uint8_t> *x) {
            MissionObjective *r = o ? ((MissionObjective *(__fastcall *)(SMissionManager *, int, int))0x000b8460)(M(), 0, index)
                                    : M()->GetObjectiveByIndex(index);
            PutAnswer(x, r != NULL ? r->id : -999);
            PutAnswer(x, r != NULL ? r->score : -999);
        });
        CASE_INT("PassObjective", 0x000b87f0, PassObjective, id);
        CASE_INT("FailObjective", 0x000b8840, FailObjective, id);
        CASE_INT("SetCurrentObjective", 0x000b8880, SetCurrentObjective, id);
        CASE_INT("SetIncompleteObjective", 0x000b88c0, SetIncompleteObjective, id);
        RunCase("AddObjective", i, [&](bool o, std::vector<uint8_t> *) {
            if (o) ((void (__fastcall *)(SMissionManager *, int, int, int, int, int))0x000b9320)(M(), 0, id, a, b, c);
            else M()->AddObjective(id, a, b, c);
        });
        int before = Range(0, 10);
        RunCase("InsertObjective", i, [&](bool o, std::vector<uint8_t> *) {
            if (o)
                ((void (__fastcall *)(SMissionManager *, int, int, int, int, int, int))0x000b93c0)(M(), 0, before, id,
                                                                                                 a, b, c);
            else
                M()->InsertObjective(before, id, a, b, c);
        });
        RunCase("AddMessage", i, [&](bool o, std::vector<uint8_t> *) {
            if (o) ((void (__fastcall *)(SMissionManager *, int, int, int, int))0x000b94a0)(M(), 0, a, b, c);
            else M()->AddMessage(a, b, c);
        });
        int stage = Range(0, 6);
        RunCase("DestroyRuleList", i, [&](bool o, std::vector<uint8_t> *) {
            MissionList *list = stage == 6 ? &M()->globalRules : &M()->stageRules[stage];
            if (o) ((void (__fastcall *)(SMissionManager *, int, MissionList *))0x000b8900)(M(), 0, list);
            else M()->DestroyRuleList(list);
        });
        CASE_VOID("ClearObjectiveList", 0x000b8960, ClearObjectiveList);
        CASE_VOID("ClearMessageList", 0x000b89c0, ClearMessageList);
        CASE_VOID("ClearState", 0x000b8a30, ClearState);
        CASE_VOID("Reset", 0x000b8bf0, Reset);
        RunCase("BuyHead+IncreaseSize", i, [&](bool o, std::vector<uint8_t> *x) {
            MissionList list;
            list.size = uint32_t(Chance(4) ? 0x3ffffff0 + Range(0, 15) : Range(0, 100));
            uint32_t count = uint32_t(Range(0, 20));
            PointerListNode *head;
            if (o) {
                head = ((PointerListNode *(__fastcall *)(MissionList *, int))0x000b8490)(&list, 0);
                if (0x3fffffff - list.size >= count)
                    ((void (__fastcall *)(MissionList *, int, uint32_t))0x000b9090)(&list, 0, count);
            } else {
                head = list.BuyHead();
                if (0x3fffffff - list.size >= count)
                    list.IncreaseSize(count);
            }
            PutAnswer(x, uint32_t(head->next == head && head->prev == head));
            PutAnswer(x, list.size);
            UMemory::FastFree(head, sizeof(PointerListNode));
        });
    }
}

// The rules' and mission sets' records for Load: a UData of 0x20-byte records, relative or a plain pointer
alignas(16) uint8_t g_ruleData[16 + 0x20 * 40];
alignas(16) uint8_t g_setData[16 + sizeof(MissionSet)];
alignas(16) uint8_t g_setDataTmpl[16 + sizeof(MissionSet)];

void TestLoad(const SMissionManager *live, int rounds) {
    for (int i = 0; i < rounds; i++) {
        NewInputs(live);
        UData *rules = reinterpret_cast<UData *>(g_ruleData);
        int count = Range(0, 40);
        rules->tag = 0;
        rules->count = uint32_t(count);
        if (Chance(2)) {
            rules->flags = kDataRelative;
            rules->data = 16;
        } else {
            rules->flags = 0;
            rules->data = Word(g_ruleData + 16);
        }
        for (int r = 0; r < count; r++) {
            uint8_t *record = g_ruleData + 16 + 0x20 * r;
            memset(record, 0, 0x20);
            int32_t stage = Chance(3) ? -1 : Range(0, 9);
            memcpy(record + 8, &stage, 4);
        }
        UData *sets = reinterpret_cast<UData *>(g_setDataTmpl);
        sets->tag = 0;
        sets->count = 1;
        sets->flags = kDataRelative;
        sets->data = 16;
        MissionSet set;
        memcpy(&set, live->missionSet, sizeof(set));
        PerturbSet(&set);
        memcpy(g_setDataTmpl + 16, &set, sizeof(set));
        bool withSets = !Chance(4);
        RunCase("Load", i, [&](bool o, std::vector<uint8_t> *x) {
            memcpy(g_setData, g_setDataTmpl, sizeof(g_setData));
            UData *s = withSets ? reinterpret_cast<UData *>(g_setData) : NULL;
            if (!withSets) {
                // the default set by side
                M()->missionSet = o ? reinterpret_cast<MissionSet *>(ShOrigDefaultSet) : &DefaultMissionSet;
            }
            if (o) ((void (__fastcall *)(SMissionManager *, int, UData *, UData *))0x000b9140)(M(), 0, rules, s);
            else M()->Load(rules, s);
            x->insert(x->end(), g_setData, g_setData + sizeof(g_setData));
            if (Word(M()->missionSet) == Word(g_setData + 16))
                M()->missionSet = &g_set;
            if (M()->missionSets == s)
                M()->missionSets = NULL;
        });
    }
}

// The constructor and destructor, and Create/Destroy, on fresh memory
alignas(16) uint8_t g_fresh[sizeof(SMissionManager)];

void TestLifetime(const SMissionManager *live, int rounds) {
    for (int i = 0; i < rounds; i++) {
        NewInputs(live);
        RunCase("Construct+Destruct", i, [&](bool o, std::vector<uint8_t> *x) {
            SMissionManager *m = reinterpret_cast<SMissionManager *>(g_fresh);
            memset(g_fresh, 0xcd, sizeof(g_fresh));
            SMissionManager *r = o ? ((SMissionManager *(__fastcall *)(SMissionManager *, int))0x000b8c30)(m, 0)
                                   : m->Construct();
            PutAnswer(x, uint32_t(r == m));
            std::string carType;
            std::vector<uint8_t> bytes = TakeBytes(m, &carType);
            PutAnswer(x, Hash(carType.c_str()));
            x->insert(x->end(), bytes.begin(), bytes.end());
            if (o) ((VoidFn)0x000b8e70)(m, 0); else m->Destruct();
        });
        RunCase("Create+Destroy", i, [&](bool o, std::vector<uint8_t> *x) {
            if (o) ((void (*)())0x000b8ff0)(); else SMissionManager::Create();
            SMissionManager *m = glbMissionManager;
            PutAnswer(x, uint32_t(m != NULL));
            if (m != NULL) {
                std::string carType;
                std::vector<uint8_t> bytes = TakeBytes(m, &carType);
                x->insert(x->end(), bytes.begin(), bytes.end());
            }
            if (o) ((void (*)())0x000b9060)(); else SMissionManager::Destroy();
            PutAnswer(x, Word(glbMissionManager));
            glbMissionManager = g_mm;
            // the calls name the new manager (operator new's, a different block on each side) by offset
            for (uint32_t &word : *g_log)
                if (m != NULL && word - Word(m) < sizeof(SMissionManager))
                    word = 0xa0000000u + (word - Word(m));
        });
    }
}

}  // namespace

void GameMissionsShadowC_Run(void) {
    const char *flag = getenv("NIGHTFIRE_GAMEMISSIONSSHADOWC");
    if (flag == NULL || atoi(flag) == 0)
        return;
    SMissionManager *live = glbMissionManager;
    if (live == NULL || live->missionSet == NULL || *ShPlayerSlot == NULL || ShScheduler == NULL) {
        printf("[gmissionsC] no mission manager or player yet: skipped\n");
        fflush(stdout);
        return;
    }
    FpControlGet(&g_x87, &g_sse);
    g_mm = static_cast<SMissionManager *>(_aligned_malloc(sizeof(SMissionManager), 16));
    g_tmpl = static_cast<SMissionManager *>(_aligned_malloc(sizeof(SMissionManager), 16));
    SaveGlobals(&g_saved);

    // The synthetic objects
    g_ruleVtable[0] = (void *)&FakeRuleDelete;
    for (int r = 0; r < kFakeRules; r++) {
        g_rules[r].vtable = g_ruleVtable;
        g_rules[r].id = r;
    }
    if (g_saved.player->flags & PhysicsObject::kSimpleBody) {
        puts("[gmissionsC] the player has a simple body: skipped");
        fflush(stdout);
        return;
    }
    memcpy(&g_body, g_saved.player->GetRigidBody(), sizeof(g_body));
    for (int k = 0; k < 2; k++) {
        memset(g_vehicles[k], 0, sizeof(g_vehicles[k]));
        PhysicsObject *vehicle = reinterpret_cast<PhysicsObject *>(g_vehicles[k]);
        vehicle->vtable = g_saved.player->vtable;
        vehicle->renderObject = reinterpret_cast<RSceneObj *>(g_sceneObjs[k]);
        memset(g_sceneObjs[k], 0, sizeof(g_sceneObjs[k]));
        reinterpret_cast<RSceneObj *>(g_sceneObjs[k])->sourceInstance = &g_instances[k];
        memset(&g_carpElements[k], 0, sizeof(g_carpElements[k]));
        memset(&g_liveElements[k], 0, sizeof(g_liveElements[k]));
    }
    memset(g_hudBuf, 0, sizeof(g_hudBuf));
    memset(g_ioBuf, 0, sizeof(g_ioBuf));
    memset(g_icmBuf, 0, sizeof(g_icmBuf));

    for (Fake &f : g_fakes)
        FakeInstall(&f);

    TestSimple(live, 40);
    TestLists(live, 40);
    TestLoad(live, 30);
    TestLifetime(live, 6);

    for (Fake &f : g_fakes)
        FakeRemove(&f);
    RestoreGlobals(&g_saved);
    _aligned_free(g_mm);
    _aligned_free(g_tmpl);
    g_mm = g_tmpl = NULL;
    printf("[gmissionsC] the mission manager (0x000b60a0-0x000b9500): %d cases, %d checks, %d differ, %d faults\n",
           g_cases, g_checks, g_differ, g_faults);
    fflush(stdout);
}
