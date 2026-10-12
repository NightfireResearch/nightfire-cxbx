#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "GameEventsShadowB.h"
#include "FpControl.h"

#include <windows.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <bit>
#include <map>
#include <string>

#include "../EventManager.hpp"
#include "../Scheduler.hpp"                  // Scheduler, Schedule
#include "../anim/Weapon.h"                 // ActWeapon
#include "../audio/Bank.h"                  // fgBanks, ABank
#include "../data/Carp.h"
#include "../engine/SimRandom.h"
#include "../game/EventsB.h"
#include "../game/AIViews.h"
#include "../game/FireWeaponEvent.h"
#include "../engine/GameInterfaces.hpp"
#include "../game/MissionManager.h"
#include "../game/Simulation.h"
#include "../game/WeaponManager.h"
#include "../game/Vehicle.h"                // PVehicle's slots
#include "../physics/PhysicsObject.h"
#include "../physics/RigidBody.h"
#include "../physics/SimpleRigidBody.h"
#include "../render/PathEngine.h"           // RPathHandle
#include "../render/RSceneObj.hpp"
#include "../world/CollisionInstance.h"     // WTrigger
#include "../world/Targeting.h"             // WTargetable
#include "../world/Trigger.h"               // gEventDynamicData, TriggerEvents
#include "../world/World.h"                 // ArticleEffect
#include "../../common/xbeOriginal.h"
#include "../../helpers.h"

// ---------------------------------------------------------------------------------------------------------------
// A shadow test of package GEV_B, run once from the first simulation tick when NIGHTFIRE_GAMEEVENTSSHADOWB is set.
// Each case builds an event the way the game does - its MakeEvent at its RegisterEvent address, on a synthetic
// record of the CARP data and a synthetic gEventDynamicData - and runs it - its vtable's scalar deleting
// destructor, flags 1 (or 0) - first as the ORIGINAL (the package's entries swapped back, so the originals reach
// each other) and then as the PORT, on the same state put back between the two, and compares:
//
//   - the log of every call beyond the package: the HUD, the mission manager, the Simulation (bodies, spawns,
//     deletes), the AI (elements, vehicles, road spawner, zones), the weapons (nuclear sequence, ray shells,
//     sentries), the GFX effects, the sounds (operator new, AIndex::Lookup, the ASound and AOneShotSound
//     constructors, AMenuSound::Trigger), the allocators, the scheduler, triggers' FireEvents, the event callbacks,
//     RColorize, the scene objects' and physics objects' methods and virtual slots, the targets and target picker,
//     the player camera, force feedback, attributes and printf - all recording fakes answering per-case values.
//     Pointers are logged as offsets into the synthetic world, or by name (live objects), vectors by value;
//   - the event's bytes after MakeEvent and after the destructor, and every byte of the synthetic world after
//     (the objects the events write: instances' flags, triggers' flags, AI elements, path handles, rigid and simple
//     bodies, scoreable flags, the weapon's streak counter, the events' own fields);
//   - the game globals the events write (gFeedbackAllowed, Sim.simState, the road spawner's flag,
//     gEventDynamicData, the Simulation's random state, the renderer's random seed) and the package's owned
//     counters (the glass and machine gun sound counters: both copies written before each side, each side's own
//     copy compared after).
//
// The cases: every class on random records (objective ids, counts, flags, weights, speeds, throttles), the dynamic
// data's flag and index, the scene object or AI element found or not, AI elements of both types with and without
// unknown77 and scoring, bodies near and far from the player's car, simple and rigid bodies, the trigger present
// or not; ResolveEvent and SimRandom::GenerateBits directly; and EFireWeapon over every weapon type 0..0x1f with
// every kind of shooter (the player's weapon, the player's car with and without auto-drive, an AI car, flying or not, a
// helicopter, a sentry, a holder's weapon with and without accuracy and command target, none), the muzzle a gfx
// effect, a glare or none, targets found or not, accuracy boosts, difficulties 1-3, the camera aiming or not, the
// selected AI helicopter slow and alive or not, spawns that fail, "snow1a_mis3" or another mission name.
//
// A mutation the test sees: EFireWeapon's AI noise phase taken from (ownerSig >> 24) & 0xff instead of & 0x7f -
// the synthetic shooters' signatures have bit 31 set on half the cases, so Util_PerturbVector's turns and the
// spawned shots' directions differ. Or EKickObject's reach test without its 0.25: the player's car is put just
// within and just beyond the sum of the radii.
//
// One summary line: [gameeventsshadowb] ...: N cases, M checks, D differ.
// ---------------------------------------------------------------------------------------------------------------

namespace {

#define GameGlassShatterCount U32_AT(0x001e239c)
#define GameStoreAlarmCount U32_AT(0x001e2398)
#define GameCarGunShots I32_AT(0x001e23a8)
#define GameHenchGunPick U32_AT(0x001e23ac)
#define GameHenchGunShots U32_AT(0x001e23b0)

#define LiveFeedbackAllowed BOOL8_AT(0x001b6f50)
#define LiveSoundOption BOOL8_AT(0x001b7054)
#define LiveSimState I32_AT(0x00234e24)
#define LiveCarsFirst (*(PhysicsObject ***)0x00234e40)
#define LiveMissionManager PTR_AT(0x00239220)
#define LiveWeaponManager PTR_AT(0x0023923c)
#define LiveMissionName ((char *)0x00244084)
#define LiveRenderSeed U32_AT(0x001c45c4)
#define LivePhysicsObjects ((PhysicsObject **)0x002342a0)
#define LiveScheduler PTR_AT(0x001e520c)
#define LiveRenderHigh PTR_AT(0x001ec488)

// The package's entries (swapped back for the original's side)
const unsigned kEntries[] = {
    0x0003fb70, 0x0003fb80, 0x0003fbf0, 0x0003fc10, 0x0003fc60, 0x0003fc80, 0x0003fca0, 0x0003fd20, 0x0003fd40,
    0x0003fe40, 0x0003feb0, 0x0003ff60, 0x00040020, 0x00040070, 0x00040090, 0x000400e0, 0x00040100, 0x000401a0,
    0x00040200, 0x00040250, 0x00040270, 0x000402d0, 0x00040300, 0x00040320, 0x00040340, 0x000403c0, 0x000403f0,
    0x00040450, 0x000404c0, 0x00040510, 0x00040530, 0x000405b0, 0x000405f0, 0x00040640, 0x00040660, 0x000406b0,
    0x000406d0, 0x00040710, 0x000408c0, 0x000408f0, 0x00040930, 0x00040960, 0x00040a00, 0x00040a30, 0x00040aa0,
    0x00040ac0, 0x00040b20, 0x00040b40, 0x00040bb0, 0x00040bd0, 0x00040c40, 0x00040c60, 0x00040cb0, 0x00040e10,
    0x00040e50, 0x00040ee0, 0x00040f10, 0x00040f70, 0x00040fb0, 0x00041010, 0x00041040, 0x000410b0, 0x000410e0,
    0x00041140, 0x00041180, 0x000411f0, 0x00041220, 0x00041290, 0x000412c0, 0x00041310, 0x00041330, 0x00041350,
    0x00041380, 0x00041410, 0x00041450, 0x000414e0, 0x00041520, 0x000415c0, 0x00041600, 0x000416a0, 0x000416e0,
    0x00041810, 0x00047a70, 0x00047a90, 0x00047ab0, 0x00047ad0, 0x00047af0, 0x00047b10, 0x00047b30, 0x00047b50,
    0x00047b70, 0x00047b90, 0x00047e50, 0x00047e70, 0x00047e90, 0x00047eb0, 0x00047ed0, 0x00047ef0, 0x00047f10,
    0x00047f30, 0x00047f50, 0x00047f70, 0x00047f90, 0x00047fb0, 0x00047fd0, 0x00047ff0, 0x00048010, 0x00048030,
    0x00048050, 0x00048070, 0x00048090, 0x000480b0, 0x000480d0, 0x000480f0, 0x00048110, 0x00048130, 0x00048150,
    0x00048700, 0x0004ac40, 0x0004c770, 0x0004c7d0, 0x0004c7f0, 0x0004ca20, 0x0004cbc0, 0x0004ec40, 0x0004ec60,
    0x0004ec80, 0x0004eca0, 0x0004ecc0,
};

long g_cases, g_checks, g_differ, g_faults;
int g_reported;
std::map<std::string, long> g_differBy;     // differences by case, listed after the summary

// The first two differences of each case are printed, up to 60 in all
bool ShouldReport(const char *what) {
    return ++g_differBy[what] <= 2 && g_reported++ < 60;
}
unsigned g_x87, g_sse;

void Check(bool same, const char *what, long index, const std::string &original, const std::string &port) {
    g_checks++;
    if (same)
        return;
    g_differ++;
    if (ShouldReport(what)) {
        size_t at = 0;
        while (at < original.size() && at < port.size() && original[at] == port[at])
            at++;
        size_t from = at > 80 ? at - 80 : 0;
        printf("[gameeventsshadowb] DIFF %s, case %ld at %u:\n  orig ...%s\n  port ...%s\n", what, index,
               unsigned(at), original.substr(from, 200).c_str(), port.substr(from, 200).c_str());
        fflush(stdout);
    }
}

uint32_t g_seed = 0x2545f491;
uint32_t Random() {
    g_seed = g_seed * 1664525u + 1013904223u;
    return g_seed >> 8;
}
uint32_t Random(uint32_t n) {
    return n == 0 ? 0 : Random() % n;
}
float Uniform(float lo, float hi) {
    return lo + (hi - lo) * float(Random() & 0xffff) * (1.0f / 65536.0f);
}
bool Chance(int percent) {
    return int(Random(100)) < percent;
}

uint32_t Bits(float f) {
    return std::bit_cast<uint32_t>(f);
}

template <class F>
bool Guarded(const F &call) {
#ifdef _MSC_VER
    __try {
        call();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_faults++;
        FpControlSetX87(g_x87);
        FpControlSetSse(g_sse);
        return false;
    }
#else
    call();
#endif
    return true;
}

// ---- hooks: a jump over an entry (and over the port the entry's own jump leads to)

struct Hook {
    uint32_t at;
    uint8_t saved[5];
    bool on;
};
Hook g_hooks[256];          // two for each fake whose entry jumps to a port
int g_hookCount;

void HookOne(uint32_t at, const void *to) {
    if (g_hookCount == int(sizeof(g_hooks) / sizeof(g_hooks[0]))) {
        printf("[gameeventsshadowb] the hook table is full: 0x%08x not faked\n", at);
        return;
    }
    Hook &h = g_hooks[g_hookCount++];
    h.at = at;
    h.on = false;
    DWORD old;
    if (!VirtualProtect((void *)(uintptr_t)at, 5, PAGE_EXECUTE_READWRITE, &old))
        return;
    memcpy(h.saved, (void *)(uintptr_t)at, 5);
    uint8_t jump[5];
    jump[0] = 0xe9;
    int32_t rel = (int32_t)((uint32_t)(uintptr_t)to - (at + 5));
    memcpy(jump + 1, &rel, 4);
    memcpy((void *)(uintptr_t)at, jump, 5);
    VirtualProtect((void *)(uintptr_t)at, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)(uintptr_t)at, 5);
    h.on = true;
}

void HookInstall(uint32_t at, const void *to) {
    const uint8_t *entry = (const uint8_t *)(uintptr_t)at;
    uint32_t port = 0;
    if (entry[0] == 0xe9) {
        int32_t rel;
        memcpy(&rel, entry + 1, 4);
        port = at + 5 + uint32_t(rel);
    }
    HookOne(at, to);
    if (port != 0 && port != (uint32_t)(uintptr_t)to)
        HookOne(port, to);
}

void HooksRemove() {
    for (int i = g_hookCount; i-- > 0;) {
        Hook &h = g_hooks[i];
        if (!h.on)
            continue;
        DWORD old;
        VirtualProtect((void *)(uintptr_t)h.at, 5, PAGE_EXECUTE_READWRITE, &old);
        memcpy((void *)(uintptr_t)h.at, h.saved, 5);
        VirtualProtect((void *)(uintptr_t)h.at, 5, old, &old);
        FlushInstructionCache(GetCurrentProcess(), (void *)(uintptr_t)h.at, 5);
        h.on = false;
    }
    g_hookCount = 0;
}

struct Originals {
    Originals() {
        for (unsigned entry : kEntries)
            XbeOriginal_Restore(entry, true);
    }
    ~Originals() {
        for (unsigned entry : kEntries)
            XbeOriginal_Restore(entry, false);
    }
};

// ---- the synthetic world: everything the events reach, put back before each side

const int kRigidSlot0 = 60;      // rigid body slots 60..63 are bodies[0..3]
const int kSimpleSlot0 = 90;     // simple slots 90..94 are simple[0..4]
const int kObjects = 5;          // physics objects: 0 the player's car, 1 an AI car, 2 a helicopter, 3 a sentry or
                                 // human, 4 a spawned shot

struct Answers {
    int weaponType;
    int accuracyBoost;
    uint32_t system;
    bool okToSpawnHench;
    bool cameraAiming;
    bool sceneFound;
    bool pathFound;
    bool agentFound;
    bool spawnFails;
    bool attitude;
    int attitudeValue;
    int selected;               // -1 none, else targets[n]
    int flying[kObjects];
    int streak[kObjects];
    int goodAim[kObjects];
    int hasTurret;
    float radius[kObjects];
    float hitPoints[kObjects];
    uint32_t sig[kObjects];
    float scalarSpeed[5];
    uint32_t fakeSeed;          // the fakes' own numbers, the same on both sides
};

struct World {
    alignas(16) uint8_t eventBuf[0x200];
    alignas(16) uint8_t arena[0x1800];
    alignas(16) uint8_t scenes[3][0x40];
    alignas(16) uint8_t physics[kObjects][0x440];
    alignas(16) RigidBody bodies[4];
    alignas(16) RigidBodyInfo info;
    alignas(16) SimpleRigidBody simple[5];
    alignas(16) CARP::AIElement elements[3];
    alignas(16) AIElementTracking tracking[3];
    alignas(16) uint8_t aiVehicle[0x240];
    alignas(16) uint8_t character[0x100];
    alignas(16) uint8_t attitude[0x10];
    alignas(16) uint8_t heliAI[0x140];
    alignas(16) WTrigger triggers[4];
    alignas(16) CARP::Instance instances[3];
    alignas(16) ArticleEffect effect;
    alignas(16) RPathHandle path;
    alignas(16) WTargetable targets[3];
    alignas(16) uint8_t weapon[0x170];
    alignas(16) WeaponAccuracy accuracy;
    alignas(16) WeaponRecord defs[32];
    alignas(16) uint8_t weaponManager[0x80];
    alignas(16) uint8_t missionManager[0x500];
    alignas(16) ABank banks[kBankSlotCount];
    alignas(16) uint8_t record[0x80];
    alignas(16) uint8_t list[0x80];             // a TriggerEvents with its events and their data
    alignas(16) Coord4 aim;                     // the camera's GetForwardAimVec4
    alignas(16) Coord4 positions[kObjects];     // the objects' GetPosition
    alignas(16) Coord4 velocities[kObjects];    // ... GetLinearVelocity
    alignas(16) Coord4 targetPos;               // AIGroundVehicle::GetTargetPos
    alignas(16) uint8_t feedback[kObjects][0x20];
    uint8_t keys[4];                            // the CARP AI elements' addresses (AIElementController's keys)
    char names[32][8];                          // the weapons' names
    PhysicsObject *playerList[2];
    uint32_t eventUsed;
    uint32_t arenaUsed;
    Answers answers;
};

World g_world;
World g_init;
std::string g_log;

void *g_physVtbl[83];
void *g_sceneVtbl[19];
void *g_characterVtbl[4];
void *g_attitudeVtbl[39];

PhysicsObject *Object(int i) {
    return reinterpret_cast<PhysicsObject *>(g_world.physics[i]);
}
RSceneObj *Scene(int i) {
    return reinterpret_cast<RSceneObj *>(g_world.scenes[i]);
}
template <class T> T *At(uint8_t *bytes) {
    return reinterpret_cast<T *>(bytes);
}

void Log(const char *format, ...) {
    char line[512];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    g_log += line;
    g_log += '\n';
}

// A pointer by what it is: an offset into the synthetic world, a live object by name, or "?" (a stack address,
// which differs between the sides)
std::string Name(const void *p) {
    char text[32];
    uintptr_t at = uintptr_t(p), base = uintptr_t(&g_world);
    if (p == NULL)
        return "null";
    if (at >= base && at < base + sizeof(World)) {
        snprintf(text, sizeof(text), "w+%x", unsigned(at - base));
        return text;
    }
    if (p == &fgSimulation)
        return "sim";
    if (p == fgAIElementController)
        return "aiec";
    if (LiveScheduler != NULL && p == static_cast<Scheduler *>(LiveScheduler)->s_SimRate)
        return "simrate";
    if (LiveRenderHigh != NULL && p == *reinterpret_cast<void **>(uintptr_t(LiveRenderHigh) + 4))
        return "camera";
    if (p == reinterpret_cast<const void *>(0x00243b90))
        return "launch";
    return "?";
}

std::string Floats(const void *p, int n) {
    std::string text;
    if (p == NULL)
        return "null";
    const float *f = static_cast<const float *>(p);
    char word[16];
    for (int i = 0; i < n; i++) {
        snprintf(word, sizeof(word), "%08x ", Bits(f[i]));
        text += word;
    }
    return text;
}

std::string Matrix(const MATRIX4 *m) {
    return m == NULL ? "null" : Floats(m, 16);
}

std::string Text(const char *s) {
    return s == NULL ? "null" : std::string("'") + s + "'";
}

float FakeFloat() {
    g_world.answers.fakeSeed = g_world.answers.fakeSeed * 1103515245u + 12345u;
    return float((g_world.answers.fakeSeed >> 8) & 0xffff) / 65536.0f * 4.0f - 2.0f;
}

int ObjectIndex(const void *p) {
    for (int i = 0; i < kObjects; i++)
        if (p == g_world.physics[i])
            return i;
    return -1;
}

// ---- the fakes (thiscall as __fastcall with the EDX word)

GHud *FakeTheApp() {
    Log("GHud::TheApp");
    return reinterpret_cast<GHud *>(g_world.record + 0x78);
}
void __fastcall FakePauseOff(void *self, int) { Log("PauseOff %s", Name(self).c_str()); }
void __fastcall FakeGadgetOff(void *self, int) { Log("GadgetOff %s", Name(self).c_str()); }
void __fastcall FakeGadgetOn(void *self, int, int gadget, float value) {
    Log("GadgetOn %s %d %08x", Name(self).c_str(), gadget, Bits(value));
}
void __fastcall FakeLava(void *self, int, bool on) { Log("EnableLavaDamageEffect %s %d", Name(self).c_str(), on); }
void __fastcall FakeLetterBoxOff(void *self, int, bool flag) { Log("LetterBoxOff %s %d", Name(self).c_str(), flag); }
void __fastcall FakeLetterBoxOn(void *self, int, bool flag) { Log("LetterBoxOn %s %d", Name(self).c_str(), flag); }
void __fastcall FakeObjectiveMsg(void *self, int, int kind, int id, bool flag) {
    Log("TriggerObjectiveMsg %s %d %d %d", Name(self).c_str(), kind, id, flag);
}

void __fastcall FakeTallyScores(void *self, int, void *page, bool end) {
    Log("TallyScores %s %s %d", Name(self).c_str(), Name(page).c_str(), end);
}
void __fastcall FakeIncMagicCounter(void *self, int, int counter) { Log("IncMagicCounter %s %d", Name(self).c_str(), counter); }
void __fastcall FakeClearCinematic(void *self, int, int mask) { Log("ClearCinematicMode %s %d", Name(self).c_str(), mask); }
void __fastcall FakeSetCinematic(void *self, int, int mask) { Log("SetCinematicMode %s %d", Name(self).c_str(), mask); }
void __fastcall FakeAddObjective(void *self, int, int a, int b, int c, int d) {
    Log("AddObjective %s %d %d %d %d", Name(self).c_str(), a, b, c, d);
}
void __fastcall FakeInsertObjective(void *self, int, int a, int b, int c, int d, int e) {
    Log("InsertObjective %s %d %d %d %d %d", Name(self).c_str(), a, b, c, d, e);
}
void __fastcall FakePassObjective(void *self, int, int id) { Log("PassObjective %s %d", Name(self).c_str(), id); }
void __fastcall FakeFailObjective(void *self, int, int id) { Log("FailObjective %s %d", Name(self).c_str(), id); }
void __fastcall FakeSetCurrent(void *self, int, int id) { Log("SetCurrentObjective %s %d", Name(self).c_str(), id); }
void __fastcall FakeSetIncomplete(void *self, int, int id) { Log("SetIncompleteObjective %s %d", Name(self).c_str(), id); }
void __fastcall FakeIncKills(void *self, int, int kills) { Log("IncKills %s %d", Name(self).c_str(), kills); }
int __fastcall FakeAccuracyBoost(void *self, int) {
    Log("GetEnemyAccuracyBoost %s", Name(self).c_str());
    return g_world.answers.accuracyBoost;
}
void __fastcall FakeSetDriveMissile(void *self, int, void *missile) {
    Log("SetDriveMissile %s %s", Name(self).c_str(), Name(missile).c_str());
}

void __fastcall FakeAdvanceStep(void *self, int) { Log("AdvanceStep %s", Name(self).c_str()); }
RigidBody *__fastcall FakeGetRigidBody(void *self, int, int slot) {
    Log("GetRigidBody %s %d", Name(self).c_str(), slot);
    int i = slot - kRigidSlot0;
    return &g_world.bodies[i >= 0 && i < 4 ? i : 0];
}
SimpleRigidBody *__fastcall FakeGetSimpleRigidBody(void *self, int, int slot) {
    Log("GetSimpleRigidBody %s %d", Name(self).c_str(), slot);
    int i = slot - kSimpleSlot0;
    return &g_world.simple[i >= 0 && i < 5 ? i : 0];
}
PhysicsObject *__fastcall FakeGetPlayerObject(void *self, int) {
    Log("GetPlayerObject %s", Name(self).c_str());
    return Object(0);
}
void __fastcall FakeDeletePhysicsObject(void *self, int, void *object) {
    Log("DeletePhysicsObject %s %s", Name(self).c_str(), Name(object).c_str());
}
void __fastcall FakeDeletePhysicsObjectThunk(void *self, int, void *object) {
    Log("DeletePhysicsObjectThunk %s %s", Name(self).c_str(), Name(object).c_str());
}
PhysicsObject *Spawned() {
    return g_world.answers.spawnFails ? NULL : Object(4);
}
PhysicsObject *__fastcall FakeSpawnMissile(void *self, int, int type, float speed, const void *direction,
                                           const void *position, uint32_t sig, float power, void *target,
                                           uint32_t system) {
    Log("SpawnMissileObject %s %d %08x [%s] [%s] %08x %08x %s %u", Name(self).c_str(), type, Bits(speed),
        Floats(direction, 3).c_str(), Floats(position, 3).c_str(), sig, Bits(power), Name(target).c_str(), system);
    return Spawned();
}
PhysicsObject *__fastcall FakeSpawnMine(void *self, int, int type, const void *direction, const void *position,
                                        float unknown, uint32_t sig) {
    Log("SpawnMineObject %s %d [%s] [%s] %08x %08x", Name(self).c_str(), type, Floats(direction, 4).c_str(),
        Floats(position, 4).c_str(), Bits(unknown), sig);
    return Spawned();
}
PhysicsObject *__fastcall FakeSpawnGrenade(void *self, int, int type, const void *direction, const void *position,
                                           float speed, uint32_t sig) {
    Log("SpawnGrenadeObject %s %d [%s] [%s] %08x %08x", Name(self).c_str(), type, Floats(direction, 3).c_str(),
        Floats(position, 4).c_str(), Bits(speed), sig);
    return Spawned();
}
PhysicsObject *__fastcall FakeSpawnShell(void *self, int, int type, const void *direction, const void *position,
                                         uint32_t sig, float power, float unknown6, int streak) {
    Log("SpawnShellObject %s %d [%s] [%s] %08x %08x %08x %d", Name(self).c_str(), type, Floats(direction, 4).c_str(),
        Floats(position, 4).c_str(), sig, Bits(power), Bits(unknown6), streak);
    return Spawned();
}

CARP::AIElement *__fastcall FakeRedirect(void *self, int, const void *key) {
    Log("RedirectCarpElement %s %s", Name(self).c_str(), Name(key).c_str());
    int i = int(static_cast<const uint8_t *>(key) - g_world.keys);
    return &g_world.elements[i >= 0 && i < 3 ? i : 0];
}
AIElementTracking *__fastcall FakeTracking(void *self, int, const void *key) {
    Log("GetCarpElementTracking %s %s", Name(self).c_str(), Name(key).c_str());
    int i = int(static_cast<const uint8_t *>(key) - g_world.keys);
    return &g_world.tracking[i >= 0 && i < 3 ? i : 0];
}
void __fastcall FakeForceVehicleToSleep(void *self, int, void *vehicle) {
    Log("ForceVehicleToSleep %s %s", Name(self).c_str(), Name(vehicle).c_str());
}
bool __fastcall FakeOkToSpawnHench(void *self, int) {
    Log("OkToSpawnHench %s", Name(self).c_str());
    return g_world.answers.okToSpawnHench;
}
void *FakeVehicleControllerGet() {
    Log("AIVehicleController::Get");
    return g_world.record + 0x70;
}
void *__fastcall FakeFindAgent(void *self, int, void *vehicle) {
    Log("FindAgentGroundVehiclePtr %s %s", Name(self).c_str(), Name(vehicle).c_str());
    return g_world.answers.agentFound ? g_world.aiVehicle : NULL;
}
PhysicsObject *__fastcall FakeAIVehiclePhysics(AIVehicle *self, int) {
    Log("AIVehicle::GetPhysicsObject %s", Name(self).c_str());
    return self->physicsObject;
}
Coord3 *__fastcall FakeGetTargetPos(void *self, int) {
    Log("GetTargetPos %s", Name(self).c_str());
    return reinterpret_cast<Coord3 *>(&g_world.targetPos);
}
void *FakeRoadSpawnGet() {
    Log("AIRoadSpawn::Get");
    return g_world.record + 0x74;
}
void __fastcall FakeRefreshSpawnData(void *self, int, bool flag, float a, float b) {
    Log("RefreshSpawnData %s %d %08x %08x", Name(self).c_str(), flag, Bits(a), Bits(b));
}
void __fastcall FakeRecycle(void *self, int) {
    Log("Recycle %p", self);     // a live pedestrian: the same on both sides
}
void *FakeZoneControllerGet() {
    Log("AIZoneController::Get");
    return g_world.record + 0x7c;
}
void __fastcall FakeRegisterZone(void *self, int, int type, const void *position) {
    Log("RegisterZone %s %d [%s]", Name(self).c_str(), type, Floats(position, 3).c_str());
}

void __fastcall FakeStartNuclear(void *self, int, const Coord3 *position, float strength) {
    Log("StartNuclearSequence %s [%s] %08x", Name(self).c_str(), Floats(position, 3).c_str(), Bits(strength));
}
int __fastcall FakeWeaponFromSystem(void *self, int, uint32_t system) {
    Log("GetWeaponFromSystem %s %u", Name(self).c_str(), system);
    return g_world.answers.weaponType;
}
WTargetable *__fastcall FakeSentryTarget(void *self, int) {
    Log("Sentry::GetTarget %s", Name(self).c_str());
    return &g_world.targets[1];
}
void *__fastcall FakeRayShell(void *self, int, int type, uint32_t sig, const Coord4 *position, const Coord4 *direction,
                              const Coord4 *origin, void *gfxScene, void *gfxEffect, float range, float power,
                              bool a, bool b, int streak) {
    Log("RayShell %s %d %08x [%s] [%s] [%s] %s %s %08x %08x %d %d %d", Name(self).c_str(), type, sig,
        Floats(position, 4).c_str(), Floats(direction, 4).c_str(), Floats(origin, 4).c_str(), Name(gfxScene).c_str(),
        Name(gfxEffect).c_str(), Bits(range), Bits(power), a, b, streak);
    return self;
}

void *FakeLookupReference(const char *name) {
    Log("GFX::LookupReference %s", Text(name).c_str());
    return reinterpret_cast<void *>(uintptr_t(0x51000 + strlen(name)));
}
void FakeTriggerIncidental(void *reference, const void *position, const void *rotation, const void *velocity,
                           const void *unknown5, uint32_t a, uint32_t b, uint32_t c) {
    Log("GFX::TriggerIncidental %p [%s] [%s] [%s] [%s] %u %u %u", reference, Floats(position, 3).c_str(),
        Floats(rotation, 4).c_str(), Floats(velocity, 3).c_str(), Floats(unknown5, 4).c_str(), a, b, c);
}

// EFireWeapon hands a holder's weapon's shots (not the player's) a mix name the stack left (the port an empty one):
// any name but the two the code writes is logged alike
void *__fastcall FakeOneShot(void *self, int, int bank, int patch, const void *position, const Coord3 *velocity,
                             float volume, const char *mix) {
    bool known = mix != NULL && (strcmp(mix, "Weapons Fire") == 0 || strcmp(mix, "Henchmen Fire") == 0);
    Log("AOneShotSound %s %d %d [%s] [%s] %08x %s", Name(self).c_str(), bank, patch, Floats(position, 3).c_str(),
        Floats(velocity, 3).c_str(), Bits(volume), known ? Text(mix).c_str() : "<other>");
    return self;
}
void *__fastcall FakeOneShotAt(void *self, int, int bank, int patch, const Coord3 *position, float volume,
                               const char *mix, bool loop) {
    Log("AOneShotSound(at) %s %d %d [%s] %08x %s %d", Name(self).c_str(), bank, patch, Floats(position, 3).c_str(),
        Bits(volume), Text(mix).c_str(), loop);
    return self;
}
void *__fastcall FakeASound(void *self, int, int bank, int patch, const char *mix) {
    Log("ASound %s %d %d %s", Name(self).c_str(), bank, patch, Text(mix).c_str());
    return self;
}
int __fastcall FakeLookup(void *self, int, const char *name) {
    Log("AIndex::Lookup %s %s", Name(self).c_str(), Text(name).c_str());
    int hash = 0;
    for (const char *c = name; c != NULL && *c != 0; c++)
        hash = hash * 31 + *c;
    return hash & 0xfff;
}
void FakeMenuTrigger(int bank, int patch, const char *mix, int view) {
    Log("AMenuSound::Trigger %d %d %s %d", bank, patch, Text(mix).c_str(), view);
}

void *ArenaTake(unsigned size) {
    uint32_t at = (g_world.arenaUsed + 15) & ~15u;
    if (at + size > sizeof(g_world.arena))
        at = 0;
    g_world.arenaUsed = at + size;
    return g_world.arena + at;
}
void *FakeSoundNew(unsigned size, const char *name) {
    Log("ABaseSound::OperatorNew %u %s", size, Text(name).c_str());
    return ArenaTake(size);
}
void *FakeOperatorNew(unsigned size) {
    Log("OperatorNew %u", size);
    return ArenaTake(size);
}
void *FakeFastAlloc(unsigned size, const char *name) {
    Log("FastAlloc %u %s", size, Text(name).c_str());
    return ArenaTake(size);
}
void *FakeEventNew(size_t size) {
    Log("Event::operator new %u", unsigned(size));
    uint32_t at = (g_world.eventUsed + 15) & ~15u;
    if (at + size > sizeof(g_world.eventBuf))
        at = 0;
    g_world.eventUsed = at + uint32_t(size);
    return g_world.eventBuf + at;
}
void FakeEventDelete(void *event, size_t size) {
    Log("Event::operator delete %s %u", Name(event).c_str(), unsigned(size));
}
uint32_t __fastcall FakeAddTask(Schedule *self, int, int type, uint32_t data, unsigned short spacing, bool unused,
                                int delay, int repeats) {
    Log("AddTask %s %d %s %u %d %d %d", Name(self).c_str(), type, Name((void *)(uintptr_t)data).c_str(), spacing,
        unused, delay, repeats);
    return self->numTasks;
}

void __fastcall FakeFireEvents(WTrigger *self, int, bool flag, int index, CARP::Instance *instance) {
    Log("FireEvents %s %d %d %s", Name(self).c_str(), flag, index, Name(instance).c_str());
}
void FakeEventCallback(uint32_t data) {
    Log("event callback %s", Name((void *)(uintptr_t)data).c_str());
}
TaskCallback FakeLookupEvent(int event) {
    Log("LookupEvent %d", event);
    return &FakeEventCallback;
}
void __fastcall FakeSetEnabled(void *self, int, int mode) { Log("RColorize::SetEnabled %d", mode); }
void __fastcall FakeDisableMotionBlur(void *self, int) { Log("RColorize::DisableMotionBlur"); }
CARP::TagReference *__fastcall FakeTagReference(CARP::TagReference *self, int, UGroup *shared, UGroup *local) {
    Log("TagReference %s %08x %s %s", Name(self).c_str(), self->value, Name(shared).c_str(), Name(local).c_str());
    self->value ^= 0x5a5a0000;
    return self;
}

RSceneObj *__fastcall FakeGetSceneObj(void *self, int, const CARP::Instance *instance) {
    Log("GetSceneObjFromInstance %s", Name(instance).c_str());
    return g_world.answers.sceneFound ? Scene(1) : NULL;
}
void __fastcall FakeHide(RSceneObj *self, int) { Log("Hide %s", Name(self).c_str()); }
CARP::Instance *__fastcall FakeConvertInstance(RSceneObj *self, int, CARP::Instance *instance) {
    Log("ConvertInstanceToLocal %s %s", Name(self).c_str(), Name(instance).c_str());
    return &g_world.instances[2];
}
uint32_t __fastcall FakeSystemID(RSceneObj *self, int, uint32_t index) {
    Log("GetInstanceSystemID %s %u", Name(self).c_str(), index);
    return g_world.answers.system + index;
}
void FillMatrix(MATRIX4 *m) {
    float *f = &m->mtx[0][0];
    for (int i = 0; i < 16; i++)
        f[i] = FakeFloat();
    f[15] = 1.0f;
}
uint32_t __fastcall FakeInstancePosition(RSceneObj *self, int, uint32_t index, MATRIX4 *m, bool world) {
    Log("GetInstancePosition %s %u %d", Name(self).c_str(), index, world);
    FillMatrix(m);
    return index + 1;
}
uint32_t __fastcall FakeTransformByInstance(RSceneObj *self, int, uint32_t index, MATRIX4 *m, bool world) {
    Log("TransformMatrixByInstancePosition %s %u [%s] %d", Name(self).c_str(), index, Matrix(m).c_str(), world);
    FillMatrix(m);
    return 0;
}
void __fastcall FakeLocateFX(RSceneObj *self, int, const ArticleEffect *effect, MATRIX4 *m, bool world) {
    Log("LocateFX %s %s %d", Name(self).c_str(), Name(effect).c_str(), world);
    FillMatrix(m);
}
void __fastcall FakeUseInstance(RSceneObj *self, int, RSceneObj *source, uint32_t index, const MATRIX4 *m,
                                uint8_t type, uint16_t procIndex) {
    Log("UseInstance %s %s %u [%s] %u %u", Name(self).c_str(), Name(source).c_str(), index, Matrix(m).c_str(), type,
        procIndex);
}
void __fastcall FakeTransformToWorld(RSceneObj *self, int, MATRIX4 *m) {
    Log("TransformToWorldSpace %s [%s]", Name(self).c_str(), Matrix(m).c_str());
    FillMatrix(m);
}
void __fastcall FakeTriggerFX(RSceneObj *self, int, int type, uint32_t which, uint32_t system, const Coord4 *position,
                              const Coord4 *direction, float intensity) {
    Log("TriggerFX %s %d %u %u [%s] [%s] %08x", Name(self).c_str(), type, which, system, Floats(position, 3).c_str(),
        Floats(direction, 3).c_str(), Bits(intensity));
}

Coord3 *__fastcall FakeGetPosition(PhysicsObject *self, int) {
    Log("GetPosition %s", Name(self).c_str());
    int i = ObjectIndex(self);
    return reinterpret_cast<Coord3 *>(&g_world.positions[i < 0 ? 0 : i]);
}
float __fastcall FakeGetRadius(PhysicsObject *self, int) {
    Log("GetRadius %s", Name(self).c_str());
    int i = ObjectIndex(self);
    return g_world.answers.radius[i < 0 ? 0 : i];
}
void __fastcall FakeApplyForces(PhysicsObject *self, int, const Coord3 *force, const Coord3 *torque) {
    Log("ApplyForces %s [%s] [%s]", Name(self).c_str(), Floats(force, 3).c_str(), Floats(torque, 3).c_str());
}
uint32_t *__fastcall FakeGetSig(PhysicsObject *self, int, uint32_t *result) {
    Log("GetSig %s", Name(self).c_str());
    int i = ObjectIndex(self);
    *result = g_world.answers.sig[i < 0 ? 0 : i];
    return result;
}
Coord3 *__fastcall FakeGetLinearVelocity(PhysicsObject *self, int) {
    Log("GetLinearVelocity %s", Name(self).c_str());
    int i = ObjectIndex(self);
    return reinterpret_cast<Coord3 *>(&g_world.velocities[i < 0 ? 0 : i]);
}
float __fastcall FakeGetHitPoints(PhysicsObject *self, int) {
    Log("GetHitPoints %s", Name(self).c_str());
    int i = ObjectIndex(self);
    return g_world.answers.hitPoints[i < 0 ? 0 : i];
}
void __fastcall FakeGenerateImpulse(RigidBody *self, int, void *impact, const Coord4 *normal, const Coord4 *point,
                                    float push, bool fromWorld, uint16_t tag, float scale) {
    // CollisionImpact's constructor leaves +0x38..+0x3f and +0x4e on as the stack had them: not logged
    const uint8_t *bytes = static_cast<const uint8_t *>(impact);
    Log("GenerateImpulse %s [%s] [%s %02x%02x%02x%02x%02x%02x] [%s] [%s] %08x %d %u %08x", Name(self).c_str(),
        Floats(impact, 0x38 / 4).c_str(), Floats(bytes + 0x40, 2).c_str(), bytes[0x48], bytes[0x49], bytes[0x4a],
        bytes[0x4b], bytes[0x4c], bytes[0x4d], Floats(normal, 4).c_str(), Floats(point, 4).c_str(), Bits(push),
        fromWorld, tag, Bits(scale));
}
float __fastcall FakeScalarVelocity(SimpleRigidBody *self, int) {
    Log("GetScalarVelocity %s", Name(self).c_str());
    int i = int(self - g_world.simple);
    return g_world.answers.scalarSpeed[i >= 0 && i < 5 ? i : 0];
}
int32_t __fastcall FakeLookupInt(void *self, int, const char *key, bool *found) {
    int i = ObjectIndex(static_cast<uint8_t *>(self) - 0x60);
    Log("LookupInt %d %s %s", i, Text(key).c_str(), Name(found).c_str());
    if (i < 0)
        i = 0;
    if (strcmp(key, "IS_FLYING") == 0)
        return g_world.answers.flying[i];
    if (strcmp(key, "BULLET_STREAK_TYPE") == 0)
        return g_world.answers.streak[i];
    if (strcmp(key, "AI_GOODAIM") == 0)
        return g_world.answers.goodAim[i];
    return 0;
}
uint8_t __fastcall FakeLookupBool(void *self, int, const char *key, bool *found) {
    int i = ObjectIndex(static_cast<uint8_t *>(self) - 0x60);
    Log("LookupBool %d %s %s", i, Text(key).c_str(), Name(found).c_str());
    return uint8_t(g_world.answers.hasTurret);
}

WTargetable *__fastcall FakeSelectedTarget(void *self, int) {
    Log("GetSelectedTarget");
    return g_world.answers.selected < 0 ? NULL : &g_world.targets[g_world.answers.selected];
}
void __fastcall FakeCycleTarget(void *self, int) { Log("CycleToNextTarget"); }
WTargetable *__fastcall FakeTargetConstruct(WTargetable *self, int, float x, float y, float z) {
    Log("WTargetable %s %08x %08x %08x", Name(self).c_str(), Bits(x), Bits(y), Bits(z));
    memset(self, 0, sizeof(WTargetable));
    self->position.x = x;
    self->position.y = y;
    self->position.z = z;
    return self;
}
void __fastcall FakeAddReference(WTargetable *self, int) { Log("AddReference %s", Name(self).c_str()); }
const Coord4 *__fastcall FakeForwardAim(void *self, int, float blend) {
    Log("GetForwardAimVec4 %s %08x", Name(self).c_str(), Bits(blend));
    return &g_world.aim;
}
void __fastcall FakeWeaponFired(void *self, int, int weapon) { Log("WeaponFired %s %d", Name(self).c_str(), weapon); }
uint8_t __fastcall FakeCameraAiming(void *self, int) {
    Log("CameraAiming %s", Name(self).c_str());
    return g_world.answers.cameraAiming;
}
void __fastcall FakeApplyJolt(void *self, int, float strength) {
    Log("ApplyJolt %s %08x", Name(self).c_str(), Bits(strength));
}
void __fastcall FakeSetRumble(void *self, int, float strength, float duration) {
    Log("SetRumble %s %08x %08x", Name(self).c_str(), Bits(strength), Bits(duration));
}
int FakePrintf(const char *format, ...) {
    Log("printf %s", Text(format).c_str());
    return 0;
}

// The synthetic vtables' slots
int __fastcall FakeApplyDamage(PhysicsObject *self, int, const void *a, const void *b, float amount, float split,
                               int kind, const uint32_t *source) {
    Log("ApplyDamage %s %s [%s] [%s] %08x %08x %d %s", Name(self).c_str(), Name(a).c_str(), Floats(a, 3).c_str(),
        Floats(b, 3).c_str(), Bits(amount), Bits(split), kind,
        source == reinterpret_cast<const uint32_t *>(0x00233fe8) ? "damagesig" : Name(source).c_str());
    return 0x20;
}
void *__fastcall FakeGetAIGroundVehicle(PhysicsObject *self, int) {
    Log("GetAIGroundVehiclePtr %s", Name(self).c_str());
    return g_world.aiVehicle;
}
void __fastcall FakeSetScoreable(PhysicsObject *self, int, bool scoreable) {
    Log("SetScoreable %s %d", Name(self).c_str(), scoreable);
}
void __fastcall FakeForceStopOn(PhysicsObject *self, int, uint8_t flags) {
    Log("ForceStopOn %s %u", Name(self).c_str(), flags);
}
WTargetable *__fastcall FakeGetTargetBeacon(PhysicsObject *self, int) {
    Log("GetTargetBeacon %s", Name(self).c_str());
    return &g_world.targets[2];
}
void *__fastcall FakeCharacterSlot3(void *self, int) {
    Log("character slot 3 %s", Name(self).c_str());
    return g_world.answers.attitude ? g_world.attitude : NULL;
}
int __fastcall FakeAttitudeSlot38(void *self, int) {
    Log("attitude slot 38 %s", Name(self).c_str());
    return g_world.answers.attitudeValue;
}
void __fastcall FakeUnexpectedSlot(void *self, int) {
    Log("unexpected virtual call %s", Name(self).c_str());
}

void InstallHooks() {
    HookInstall(0x000d7c10, (const void *)&FakeTheApp);
    HookInstall(0x000d9050, (const void *)&FakePauseOff);
    HookInstall(0x000d9c30, (const void *)&FakeGadgetOff);
    HookInstall(0x000d9bf0, (const void *)&FakeGadgetOn);
    HookInstall(0x000d7c30, (const void *)&FakeLava);
    HookInstall(0x000d9b70, (const void *)&FakeLetterBoxOff);
    HookInstall(0x000d9b30, (const void *)&FakeLetterBoxOn);
    HookInstall(0x000da0a0, (const void *)&FakeObjectiveMsg);
    HookInstall(0x000b7470, (const void *)&FakeTallyScores);
    HookInstall(0x000b7a40, (const void *)&FakeIncMagicCounter);
    HookInstall(0x000b7950, (const void *)&FakeClearCinematic);
    HookInstall(0x000b8160, (const void *)&FakeSetCinematic);
    HookInstall(0x000b9320, (const void *)&FakeAddObjective);
    HookInstall(0x000b93c0, (const void *)&FakeInsertObjective);
    HookInstall(0x000b87f0, (const void *)&FakePassObjective);
    HookInstall(0x000b8840, (const void *)&FakeFailObjective);
    HookInstall(0x000b8880, (const void *)&FakeSetCurrent);
    HookInstall(0x000b88c0, (const void *)&FakeSetIncomplete);
    HookInstall(0x000b6770, (const void *)&FakeIncKills);
    HookInstall(0x000b7260, (const void *)&FakeAccuracyBoost);
    HookInstall(0x000b80b0, (const void *)&FakeSetDriveMissile);
    HookInstall(0x000b2620, (const void *)&FakeAdvanceStep);
    HookInstall(0x000b2700, (const void *)&FakeGetRigidBody);
    HookInstall(0x000b2730, (const void *)&FakeGetSimpleRigidBody);
    HookInstall(0x000b2d30, (const void *)&FakeGetPlayerObject);
    HookInstall(0x000b4250, (const void *)&FakeDeletePhysicsObject);
    HookInstall(0x000b4740, (const void *)&FakeDeletePhysicsObjectThunk);
    HookInstall(0x000b5950, (const void *)&FakeSpawnMissile);
    HookInstall(0x000b5a70, (const void *)&FakeSpawnMine);
    HookInstall(0x000b5b80, (const void *)&FakeSpawnGrenade);
    HookInstall(0x000b5c90, (const void *)&FakeSpawnShell);
    HookInstall(0x000286d0, (const void *)&FakeRedirect);
    HookInstall(0x00028700, (const void *)&FakeTracking);
    HookInstall(0x000289f0, (const void *)&FakeForceVehicleToSleep);
    HookInstall(0x000286b0, (const void *)&FakeOkToSpawnHench);
    HookInstall(0x00035c00, (const void *)&FakeVehicleControllerGet);
    HookInstall(0x00035e90, (const void *)&FakeFindAgent);
    HookInstall(0x00035840, (const void *)&FakeAIVehiclePhysics);
    HookInstall(0x0002a3c0, (const void *)&FakeGetTargetPos);
    HookInstall(0x000347e0, (const void *)&FakeRoadSpawnGet);
    HookInstall(0x00034ed0, (const void *)&FakeRefreshSpawnData);
    HookInstall(0x000254c0, (const void *)&FakeRecycle);
    HookInstall(0x00036a10, (const void *)&FakeZoneControllerGet);
    HookInstall(0x00036bb0, (const void *)&FakeRegisterZone);
    HookInstall(0x000bad00, (const void *)&FakeStartNuclear);
    HookInstall(0x000babb0, (const void *)&FakeWeaponFromSystem);
    HookInstall(0x00073330, (const void *)&FakeSentryTarget);
    HookInstall(0x00072e90, (const void *)&FakeRayShell);
    HookInstall(0x000d35f0, (const void *)&FakeLookupReference);
    HookInstall(0x000d3520, (const void *)&FakeTriggerIncidental);
    HookInstall(0x000476f0, (const void *)&FakeOneShot);
    HookInstall(0x000477d0, (const void *)&FakeOneShotAt);
    HookInstall(0x00127a70, (const void *)&FakeASound);
    HookInstall(0x00126d40, (const void *)&FakeLookup);
    HookInstall(0x0011df50, (const void *)&FakeMenuTrigger);
    HookInstall(0x0011c910, (const void *)&FakeSoundNew);
    HookInstall(0x001146a0, (const void *)&FakeOperatorNew);
    HookInstall(0x00114750, (const void *)&FakeFastAlloc);
    HookInstall(0x0005a5a0, (const void *)&FakeEventNew);
    HookInstall(0x0005a5c0, (const void *)&FakeEventDelete);
    HookInstall(0x0005c490, (const void *)&FakeAddTask);
    HookInstall(0x000cf300, (const void *)&FakeFireEvents);
    HookInstall(0x0005a5e0, (const void *)&FakeLookupEvent);
    HookInstall(0x0009a500, (const void *)&FakeSetEnabled);
    HookInstall(0x0009a4f0, (const void *)&FakeDisableMotionBlur);
    HookInstall(0x00117e50, (const void *)&FakeTagReference);
    HookInstall(0x000d14a0, (const void *)&FakeGetSceneObj);
    HookInstall(0x0008db30, (const void *)&FakeHide);
    HookInstall(0x0008edd0, (const void *)&FakeConvertInstance);
    HookInstall(0x0008dcf0, (const void *)&FakeSystemID);
    HookInstall(0x0008dd20, (const void *)&FakeInstancePosition);
    HookInstall(0x0008dee0, (const void *)&FakeTransformByInstance);
    HookInstall(0x0008eca0, (const void *)&FakeLocateFX);
    HookInstall(0x00090820, (const void *)&FakeUseInstance);
    HookInstall(0x0006f330, (const void *)&FakeGetPosition);
    HookInstall(0x0006f270, (const void *)&FakeGetRadius);
    HookInstall(0x0006f2a0, (const void *)&FakeApplyForces);
    HookInstall(0x0006f390, (const void *)&FakeGetSig);
    HookInstall(0x0006f360, (const void *)&FakeGetLinearVelocity);
    HookInstall(0x0006f450, (const void *)&FakeGetHitPoints);
    HookInstall(0x000afdb0, (const void *)&FakeGenerateImpulse);
    HookInstall(0x000b1f30, (const void *)&FakeScalarVelocity);
    HookInstall(0x00058180, (const void *)&FakeLookupInt);
    HookInstall(0x000580e0, (const void *)&FakeLookupBool);
    HookInstall(0x000cdfb0, (const void *)&FakeSelectedTarget);
    HookInstall(0x000ce170, (const void *)&FakeCycleTarget);
    HookInstall(0x000cd930, (const void *)&FakeTargetConstruct);
    HookInstall(0x000cd920, (const void *)&FakeAddReference);
    HookInstall(0x00081890, (const void *)&FakeForwardAim);
    HookInstall(0x00081950, (const void *)&FakeWeaponFired);
    HookInstall(0x00081980, (const void *)&FakeCameraAiming);
    HookInstall(0x0004f8c0, (const void *)&FakeApplyJolt);
    HookInstall(0x0004f960, (const void *)&FakeSetRumble);
    HookInstall(0x00132192, (const void *)&FakePrintf);
}

// ---- the world's starting state

void SetVector(Coord4 *v, float lo, float hi) {
    v->x = Uniform(lo, hi);
    v->y = Uniform(lo, hi);
    v->z = Uniform(lo, hi);
    v->w = 1.0f;
}

void BuildWorld() {
    World &w = g_world;
    memset(&w, 0, sizeof(World));
    for (int i = 0; i < 83; i++)
        g_physVtbl[i] = (void *)&FakeUnexpectedSlot;
    g_physVtbl[2] = (void *)&FakeApplyDamage;
    g_physVtbl[PVehicle::kGetAIGroundVehiclePtr] = (void *)&FakeGetAIGroundVehicle;
    g_physVtbl[PVehicle::kSetScoreable] = (void *)&FakeSetScoreable;
    g_physVtbl[PVehicle::kForceStopOn] = (void *)&FakeForceStopOn;
    g_physVtbl[PVehicle::kGetTargetBeacon] = (void *)&FakeGetTargetBeacon;
    for (int i = 0; i < 19; i++)
        g_sceneVtbl[i] = (void *)&FakeUnexpectedSlot;
    g_sceneVtbl[6] = (void *)&FakeTransformToWorld;
    g_sceneVtbl[15] = (void *)&FakeTriggerFX;
    for (int i = 0; i < 4; i++)
        g_characterVtbl[i] = (void *)&FakeUnexpectedSlot;
    g_characterVtbl[3] = (void *)&FakeCharacterSlot3;
    for (int i = 0; i < 39; i++)
        g_attitudeVtbl[i] = (void *)&FakeUnexpectedSlot;
    g_attitudeVtbl[38] = (void *)&FakeAttitudeSlot38;

    for (int i = 0; i < 3; i++) {
        RSceneObj *scene = Scene(i);
        scene->vtable = reinterpret_cast<RSceneObj_vtbl *>(g_sceneVtbl);
        scene->pathHandle = &w.path;
    }
    for (int i = 0; i < kObjects; i++) {
        PhysicsObject *o = Object(i);
        o->vtable = g_physVtbl;
        o->feedbackObject = reinterpret_cast<IFeedback *>(w.feedback[i]);
        o->renderObject = Scene(2);
    }
    *reinterpret_cast<void **>(w.character) = g_characterVtbl;
    *reinterpret_cast<void **>(w.attitude) = g_attitudeVtbl;
    for (int i = 0; i < 4; i++)
        w.bodies[i].info = &w.info;
    for (int i = 0; i < 32; i++) {
        snprintf(w.names[i], sizeof(w.names[i]), "W%02x", i);
        w.defs[i].sound = w.names[i];
        w.defs[i].damage = int32_t(Random(400)) - 20;
        w.defs[i].soundBank = int32_t(Random(10));
    }
    At<SWeaponManager>(w.weaponManager)->weapons = w.defs;
    for (int i = 0; i < kBankSlotCount; i++) {
        w.banks[i].slot = i;
        w.banks[i].handle = 0x40 + i;
    }
    w.playerList[0] = Object(0);
    w.playerList[1] = Object(1);
    for (int i = 0; i < 3; i++)
        w.elements[i].unknown48 = 1;
}

// Random contents for the bytes the events read, the same for both sides
void Perturb() {
    World &w = g_world;
    Answers &a = w.answers;
    a.weaponType = int(Random(0x20));
    a.accuracyBoost = int(Random(80)) - 40;
    a.system = Random(40);
    a.okToSpawnHench = Chance(50);
    a.cameraAiming = Chance(30);
    a.sceneFound = Chance(75);
    a.pathFound = Chance(80);
    a.agentFound = Chance(50);
    a.spawnFails = Chance(15);
    a.attitude = Chance(60);
    a.attitudeValue = int(Random(9));
    a.selected = int(Random(4)) - 1;
    a.hasTurret = Chance(60);
    for (int i = 0; i < kObjects; i++) {
        a.flying[i] = Chance(40) ? 1 : int(Random(3));
        a.streak[i] = int(Random(7));
        a.goodAim[i] = Chance(50) ? 1 : 0;
        a.radius[i] = Uniform(0.5f, 4.0f);
        a.hitPoints[i] = Chance(20) ? 0.0f : Uniform(-1.0f, 100.0f);
        a.sig[i] = Random() | (Chance(50) ? 0x80000000u : 0);
        SetVector(&w.positions[i], -50.0f, 50.0f);
        SetVector(&w.velocities[i], Chance(40) ? -1.0f : -30.0f, Chance(40) ? 1.0f : 30.0f);
        PhysicsObject *o = Object(i);
        o->unknown68 = Random(4);
        o->flags = uint16_t(Random(4));
        w.physics[i][0x3ff] = uint8_t(Random(2));             // PBondCar::scoreable
        w.physics[i][0x138] = uint8_t(Random(2));             // PHelicopter::scoreable
    }
    for (int i = 0; i < 5; i++)
        a.scalarSpeed[i] = Uniform(0.0f, 60.0f);
    a.fakeSeed = Random();

    Object(0)->type = kObjectVehicle;
    Object(0)->rigidBodySlot = kRigidSlot0;
    Object(1)->type = kObjectVehicle;
    Object(1)->rigidBodySlot = kRigidSlot0 + 1;
    Object(2)->type = kObjectHelicopter;
    Object(2)->rigidBodySlot = kSimpleSlot0 + 2;
    Object(2)->flags |= PhysicsObject::kSimpleBody;
    *reinterpret_cast<void **>(w.physics[2] + 0x134) = w.heliAI;      // PHelicopter::ai
    Object(3)->type = Chance(50) ? kObjectSentry : 4;
    Object(3)->rigidBodySlot = int16_t(Chance(50) ? kSimpleSlot0 + 3 : kRigidSlot0 + 3);
    *reinterpret_cast<void **>(w.physics[3] + 0x70) = Chance(70) ? w.character : NULL;   // Human::character
    Object(4)->rigidBodySlot = kSimpleSlot0 + 4;
    Object(4)->flags |= PhysicsObject::kSimpleBody;

    for (int i = 0; i < 4; i++) {
        RigidBody &b = w.bodies[i];
        b.kind = int8_t(Chance(20) ? 1 : 2);
        b.position.x = Uniform(-50.0f, 50.0f);
        b.position.y = Uniform(-5.0f, 5.0f);
        b.position.z = Uniform(-50.0f, 50.0f);
        b.velocity.x = Uniform(-30.0f, 30.0f);
        b.velocity.y = Uniform(-3.0f, 3.0f);
        b.velocity.z = Uniform(-30.0f, 30.0f);
        b.radius = Uniform(0.5f, 3.0f);
    }
    for (int i = 0; i < 4; i++)
        SetVector(&w.info.worldLevers[i], -2.0f, 2.0f);
    for (int i = 0; i < 5; i++) {
        SimpleRigidBody &s = w.simple[i];
        s.position.x = Uniform(-50.0f, 50.0f);
        s.position.y = Uniform(-5.0f, 5.0f);
        s.position.z = Uniform(-50.0f, 50.0f);
        s.velocity.x = Uniform(-30.0f, 30.0f);
        s.velocity.y = Uniform(-3.0f, 3.0f);
        s.velocity.z = Uniform(-30.0f, 30.0f);
    }
    // The player's car near the kicked body: just within or beyond the radii (EKickObject's reach), or anywhere
    if (Chance(60)) {
        int body = int(Random(4));
        float reach = a.radius[0] + w.bodies[body].radius + (Chance(50) ? 0.2f : 0.3f);
        w.positions[0].x = w.bodies[body].position.x + reach;
        w.positions[0].y = w.bodies[body].position.y;
        w.positions[0].z = w.bodies[body].position.z;
    }

    for (int i = 0; i < 3; i++) {
        CARP::AIElement &e = w.elements[i];
        e.type = int32_t(Chance(10) ? 2 : Random(2));
        e.unknown48 = int32_t(Chance(80) ? 1 : 0);
        e.flags = uint8_t(Random(8));
        e.unknown77 = uint8_t(Random(2));
        if (e.type == CARP::kAIElementCharacter) {
            e.character = Chance(80) ? reinterpret_cast<AICharacter *>(w.character) : NULL;
            e.characterObject = Chance(85) ? reinterpret_cast<Human *>(w.physics[3]) : NULL;
        } else {
            e.vehicle = reinterpret_cast<AIVehicle *>(w.aiVehicle);
            e.object = Chance(85) ? Object(e.unknown77 ? 1 : 2) : NULL;
        }
        w.tracking[i].position.x = Uniform(-50.0f, 50.0f);
        w.tracking[i].position.y = Uniform(-5.0f, 5.0f);
        w.tracking[i].position.z = Uniform(-50.0f, 50.0f);
    }
    AIGroundVehicle *ai = At<AIGroundVehicle>(w.aiVehicle);
    ai->vehicleType = int32_t(1 + Random(2));
    ai->physicsObject = Object(Chance(50) ? 1 : 2);
    ai->active = Chance(80);
    At<AIHelicopter>(w.aiVehicle)->targetBeacon = Chance(70) ? &w.targets[0] : NULL;
    ai->accuracy = int8_t(int(Random(240)) - 120);
    ai->aimScale = int8_t(int(Random(240)) - 120);
    ai->aimPoint.x = Uniform(-50.0f, 50.0f);
    ai->aimPoint.y = Uniform(-5.0f, 5.0f);
    ai->aimPoint.z = Uniform(-50.0f, 50.0f);
    ai->leadPoint.x = Uniform(-1.0f, 1.0f);
    ai->leadPoint.y = Uniform(-1.0f, 1.0f);
    ai->leadPoint.z = Uniform(-1.0f, 1.0f);
    w.character[0xbd] = uint8_t(Random(2));                    // AICharacter::scoreable
    w.character[0xcc] = uint8_t(Random(3));                    // hasCommandTarget
    for (int i = 0; i < 3; i++)
        reinterpret_cast<float *>(w.character + 0xc0)[i] = Uniform(-50.0f, 50.0f);
    for (int i = 0; i < 4; i++)
        reinterpret_cast<float *>(w.heliAI + 0x100)[i] = Uniform(-1.0f, 1.0f);
    for (int i = 0; i < 3; i++)
        reinterpret_cast<float *>(w.heliAI + 0x120)[i] = Uniform(-50.0f, 50.0f);
    SetVector(&w.targetPos, -50.0f, 50.0f);
    SetVector(&w.aim, -1.0f, 1.0f);

    for (int i = 0; i < 4; i++) {
        w.triggers[i].flags = uint16_t(Random(0x4000));
        w.triggers[i].position.x = Uniform(-50.0f, 50.0f);
        w.triggers[i].position.y = Uniform(-5.0f, 5.0f);
        w.triggers[i].position.z = Uniform(-50.0f, 50.0f);
    }
    for (int i = 0; i < 3; i++)
        w.instances[i].flags = uint8_t(Random(0x100));
    w.effect.type = uint8_t(Chance(40) ? ArticleEffect::kTypeGfx : Chance(50) ? ArticleEffect::kTypeGlare : 7);
    w.effect.instance = uint8_t(Random(4));
    w.effect.position.x = Uniform(-2.0f, 2.0f);
    w.effect.position.y = Uniform(-2.0f, 2.0f);
    w.effect.position.z = Uniform(-2.0f, 2.0f);
    {
        float q[4] = {Uniform(-1.0f, 1.0f), Uniform(-1.0f, 1.0f), Uniform(-1.0f, 1.0f), Uniform(-1.0f, 1.0f)};
        memcpy(&w.effect.gfx.rotation, q, sizeof(q));
    }
    w.path.running = Chance(70);
    w.path.throttle = Uniform(0.0f, 2.0f);
    for (int i = 0; i < 3; i++) {
        w.targets[i].position.x = Uniform(-50.0f, 50.0f);
        w.targets[i].position.y = Uniform(-5.0f, 5.0f);
        w.targets[i].position.z = Uniform(-50.0f, 50.0f);
        w.targets[i].ownerType = int32_t(Chance(60) ? kTargetVehicle : Random(3));
        w.targets[i].owner = w.aiVehicle;
    }
    ActWeapon *weapon = At<ActWeapon>(w.weapon);
    weapon->flags = Random(4);
    weapon->owner = Object(3);
    weapon->accuracy = Chance(60) ? &w.accuracy : NULL;
    weapon->unknown160 = int32_t(Random(4));
    weapon->unknown164 = int32_t(Random(7));
    w.accuracy.aimScale = int32_t(Random(300)) - 150;
    w.accuracy.accuracy = int32_t(Random(140)) - 20;
    At<SWeaponManager>(w.weaponManager)->turretOnTarget = Random(2) != 0;
    SMissionManager *mm = At<SMissionManager>(w.missionManager);
    mm->autoDrive = uint8_t(Random(2));
    mm->difficulty = int32_t(Random(4));
}

// ---- the game's globals the events reach

struct Globals {
    EventDynamicData dynamic;
    SimRandom random;
    uint32_t renderSeed;
    uint8_t feedbackAllowed;
    uint8_t soundOption;
    uint8_t henchLimit;
    uint8_t pedFlag;
    int32_t simState;
    char missionName[16];
    PhysicsObject *physicsSlots[4];
    uint32_t glass, alarm, carGun, gunPick, gunShots;
    uint32_t gameGlass, gameAlarm, gameCarGun, gameGunPick, gameGunShots;
};

Globals g_live;         // as found, put back at the end
Globals g_start;        // each case's start

void SaveGlobals(Globals *g) {
    g->dynamic = gEventDynamicData;
    g->random = *fgSimulation.random;
    g->renderSeed = LiveRenderSeed;
    g->feedbackAllowed = LiveFeedbackAllowed;
    g->soundOption = LiveSoundOption;
    g->henchLimit = gLimitHenchSpawns;
    g->pedFlag = gPedRespawnFlag;
    g->simState = LiveSimState;
    memcpy(g->missionName, LiveMissionName, sizeof(g->missionName));
    for (int i = 0; i < 4; i++)
        g->physicsSlots[i] = LivePhysicsObjects[kRigidSlot0 + i];
    g->glass = gGlassShatterCount;
    g->alarm = gStoreAlarmCount;
    g->carGun = uint32_t(gCarGunShots);
    g->gunPick = gHenchGunPick;
    g->gunShots = gHenchGunShots;
    g->gameGlass = GameGlassShatterCount;
    g->gameAlarm = GameStoreAlarmCount;
    g->gameCarGun = uint32_t(GameCarGunShots);
    g->gameGunPick = GameHenchGunPick;
    g->gameGunShots = GameHenchGunShots;
}

void LoadGlobals(const Globals *g) {
    gEventDynamicData = g->dynamic;
    *fgSimulation.random = g->random;
    LiveRenderSeed = g->renderSeed;
    LiveFeedbackAllowed = g->feedbackAllowed;
    LiveSoundOption = g->soundOption;
    gLimitHenchSpawns = g->henchLimit;
    gPedRespawnFlag = g->pedFlag;
    LiveSimState = g->simState;
    memcpy(LiveMissionName, g->missionName, sizeof(g->missionName));
    for (int i = 0; i < 4; i++)
        LivePhysicsObjects[kRigidSlot0 + i] = g->physicsSlots[i];
    gGlassShatterCount = g->glass;
    gStoreAlarmCount = g->alarm;
    gCarGunShots = int32_t(g->carGun);
    gHenchGunPick = g->gunPick;
    gHenchGunShots = g->gunShots;
    GameGlassShatterCount = g->gameGlass;
    GameStoreAlarmCount = g->gameAlarm;
    GameCarGunShots = int32_t(g->gameCarGun);
    GameHenchGunPick = g->gameGunPick;
    GameHenchGunShots = g->gameGunShots;
}

// The globals after a side, as text: the owned counters from the side's own copy
std::string GlobalsText(bool original) {
    char text[512];
    const EventDynamicData &d = gEventDynamicData;
    snprintf(text, sizeof(text),
             "dyn %s %s %d %s %s %u %d | rnd %08x %08x %08x %08x | seed %08x | fb %u sim %d ped %u | counters %u %u "
             "%d %u %u",
             Name(d.instance).c_str(), Name(d.sceneObj).c_str(), d.instanceIndex, Name(d.trigger).c_str(),
             Name(d.weapon).c_str(), d.flag, d.index, fgSimulation.random->state, fgSimulation.random->lastProduct, fgSimulation.random->seed,
             fgSimulation.random->counter, LiveRenderSeed, LiveFeedbackAllowed, LiveSimState, gPedRespawnFlag,
             original ? GameGlassShatterCount : gGlassShatterCount, original ? GameStoreAlarmCount : gStoreAlarmCount,
             original ? GameCarGunShots : gCarGunShots, original ? GameHenchGunPick : gHenchGunPick,
             original ? GameHenchGunShots : gHenchGunShots);
    return text;
}

// The dynamic data a case's event is raised with
void SetDynamic(Globals *g) {
    EventDynamicData &d = g->dynamic;
    memset(&d, 0, sizeof(d));
    d.instance = Chance(85) ? &g_world.instances[0] : NULL;
    d.sceneObj = Chance(85) ? Scene(0) : NULL;
    d.instanceIndex = int32_t(Random(4));
    d.trigger = Chance(70) ? &g_world.triggers[1] : NULL;
    d.weapon = Chance(50) ? reinterpret_cast<ActWeapon *>(g_world.weapon) : NULL;
    d.flag = uint8_t(Random(2));
    // a rigid slot of the synthetic bodies, or past the rigid tables (never a live object's slot)
    d.index = int32_t(Chance(85) ? kRigidSlot0 + Random(4) : 64 + Random(10));
    g->renderSeed = Random() & 0xffff;
    g->random.state = Random() & 0xffff;
    g->random.counter = Random(1000);
    g->feedbackAllowed = uint8_t(Random(2));
    g->soundOption = uint8_t(Chance(85));
    g->henchLimit = uint8_t(Random(2));
    g->pedFlag = uint8_t(Random(2));
    if (Chance(30))
        strcpy(g->missionName, "snow1a_mis3");
    else if (Chance(50))
        strcpy(g->missionName, "SNOW1A_MIS3");
    for (int i = 0; i < 4; i++)
        g->physicsSlots[i] = Object(i);
    g->glass = g->gameGlass = Random(16);
    g->alarm = g->gameAlarm = Random(16);
    g->carGun = g->gameCarGun = Random(3);
    g->gunPick = g->gameGunPick = Random(4);
    g->gunShots = g->gameGunShots = Random(3);
}

// ---- running an event on both sides

typedef void (*MakeFunction)(const void *data);
typedef void *(__fastcall *DeleteFunction)(void *event, int, unsigned flags);

std::string WorldText() {
    std::string text;
    const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&g_world);
    text.reserve(sizeof(World) / 8);
    // compared as a checksum per 256-byte block (the differing block's index shows where)
    char word[24];
    for (size_t block = 0; block < sizeof(World); block += 256) {
        uint32_t sum = 0x811c9dc5;
        for (size_t i = block; i < block + 256 && i < sizeof(World); i++)
            sum = (sum ^ bytes[i]) * 16777619u;
        snprintf(word, sizeof(word), "%x:%08x ", unsigned(block), sum);
        text += word;
    }
    return text;
}

struct SideResult {
    std::string log;
    std::string event;
    std::string world;
    std::string globals;
};

void RunSide(bool original, uint32_t make, unsigned flags, SideResult *result) {
    memcpy(&g_world, &g_init, sizeof(World));
    LoadGlobals(&g_start);
    g_log.clear();
    bool ok;
    {
        Originals *window = original ? new Originals : NULL;
        ok = Guarded([&] {
            reinterpret_cast<MakeFunction>(uintptr_t(make))(g_world.record);
            Log("made [%s]", Floats(g_world.eventBuf, 0x60 / 4).c_str());
            Event *event = reinterpret_cast<Event *>(g_world.eventBuf);
            DeleteFunction run = reinterpret_cast<DeleteFunction>(*reinterpret_cast<void **>(event->vtable));
            if (g_world.eventUsed != 0)
                run(event, 0, flags);
        });
        delete window;
    }
    result->log = g_log;
    if (!ok) {
        result->log += "<fault>";
        // A fault inside an original destructor unwinds through its exception funclet, which puts Event's vtable
        // back; the port (no unwinding of structured exceptions) leaves its own. Faults happen only on made-up data.
        if (g_world.eventUsed != 0)
            *reinterpret_cast<uint32_t *>(g_world.eventBuf) = 0x0018c3ac;
    }
    result->event = Floats(g_world.eventBuf, 0x60 / 4);
    result->world = WorldText();
    result->globals = GlobalsText(original);
}

void RunCase(const char *what, uint32_t make, unsigned flags = 1) {
    memcpy(&g_init, &g_world, sizeof(World));
    SideResult original, port;
    RunSide(true, make, flags, &original);
    RunSide(false, make, flags, &port);
    g_cases++;
    Check(original.log == port.log, what, g_cases, original.log, port.log);
    Check(original.event == port.event, what, g_cases, original.event, port.event);
    Check(original.world == port.world, what, g_cases, original.world, port.world);
    Check(original.globals == port.globals, what, g_cases, original.globals, port.globals);
    memcpy(&g_world, &g_init, sizeof(World));
}

// A fresh world and globals for a case
void NewCase() {
    BuildWorld();
    Perturb();
    g_start = g_live;
    SetDynamic(&g_start);
}

uint32_t *Record() {
    return reinterpret_cast<uint32_t *>(g_world.record);
}
float *RecordFloats() {
    return reinterpret_cast<float *>(g_world.record);
}
void RandomRecord() {
    for (int i = 0; i < 0x80 / 4; i++)
        Record()[i] = Random(Chance(50) ? 2 : 1000);
}
uint32_t Key(int i) {
    return uint32_t(uintptr_t(&g_world.keys[i]));
}
uint32_t Address(const void *p) {
    return uint32_t(uintptr_t(p));
}

// ---- the cases

struct Simple {
    const char *name;
    uint32_t make;
};

// Events whose records are plain numbers
const Simple kSimple[] = {
    {"EExit", 0x0003fbf0}, {"EFESimFrameUpdate", 0x0003fc60}, {"EFailMissionObjective", 0x0003fc80},
    {"EGadgetOff", 0x00040250}, {"EGadgetOn", 0x000402d0}, {"EIncMagicCounter", 0x00040510},
    {"EInfraRedOff", 0x00040640}, {"EInfraRedOn", 0x000406b0}, {"EInsertObjective", 0x000406d0},
    {"ELavaHaze", 0x00040b20}, {"ELetterBoxOff", 0x00040bb0}, {"ELetterBoxOn", 0x00040c40},
    {"EObjectiveAdd", 0x00040f70}, {"EObjectiveDisplayText", 0x00041010}, {"EObjectiveFail", 0x000410b0},
    {"EObjectiveInsert", 0x00041140}, {"EObjectivePass", 0x000411f0}, {"EObjectiveSetCurrent", 0x00041290},
    {"EObjectiveSetIncomplete", 0x00041310}, {"EPassMissionObjective", 0x00041330}, {"EPassObjective", 0x00041350},
    {"EForceCarStop", 0x000400e0}, {"EForceNearPedRespawn", 0x000401a0}, {"EInflictDamage", 0x000405b0},
    {"EKickObject", 0x000408f0}, {"EHideInstance", 0x00040320},
};

void SimpleCases() {
    for (const Simple &s : kSimple) {
        for (int n = 0; n < 8; n++) {
            NewCase();
            RandomRecord();
            if (strcmp(s.name, "EGadgetOn") == 0 || strcmp(s.name, "EForceNearPedRespawn") == 0 ||
                strcmp(s.name, "EInflictDamage") == 0 || strcmp(s.name, "EKickObject") == 0) {
                RecordFloats()[1] = Uniform(-100.0f, 1000.0f);
                RecordFloats()[2] = Uniform(-100.0f, 1000.0f);
            }
            // EKickObject reads PhysicsObjects[index] unchecked: one of the synthetic bodies' slots
            if (strcmp(s.name, "EKickObject") == 0)
                g_start.dynamic.index = kRigidSlot0 + int32_t(Random(4));
            RunCase(s.name, s.make, n == 7 ? 0 : 1);
        }
    }
}

void TriggerCases() {
    for (int n = 0; n < 40; n++) {
        // EFireEventList: up to four events, their data at random offsets
        NewCase();
        TriggerEvents *list = reinterpret_cast<TriggerEvents *>(g_world.list);
        list->count = int32_t(Random(5));
        for (int i = 0; i < 4; i++) {
            TriggerEvent *e = &list->Events()[i];
            e->type = int32_t(Random(0x120));
            e->dataOffset = int32_t(Random(0x30));
        }
        Record()[1] = Address(list);
        RunCase("EFireEventList", 0x0003fd20);

        // EFireRandomTrigger: weights, triggers present or not
        NewCase();
        for (int i = 0; i < 4; i++) {
            Record()[1 + 2 * i] = Chance(80) ? Address(&g_world.triggers[i]) : 0;
            RecordFloats()[2 + 2 * i] = Chance(15) ? 0.0f : Uniform(0.0f, 40.0f);
        }
        RunCase("EFireRandomTrigger", 0x0003fe40);

        // ... its resolver directly
        NewCase();
        for (int i = 0; i < 9; i++)
            Record()[i] = Random();
        memcpy(&g_init, &g_world, sizeof(World));
        {
            SideResult o, p;
            for (int side = 0; side < 2; side++) {
                memcpy(&g_world, &g_init, sizeof(World));
                g_log.clear();
                bool original = side == 0;
                Originals *window = original ? new Originals : NULL;
                Guarded([&] {
                    ((void (*)(void *, void *, void *))0x0003feb0)(g_world.record, g_world.list, g_world.arena);
                });
                delete window;
                (original ? o : p).log = g_log + Floats(g_world.record, 9);
            }
            g_cases++;
            Check(o.log == p.log, "EFireRandomTrigger::ResolveEvent", g_cases, o.log, p.log);
        }

        // EFireTriggerSpeedCondition: the body's speed about the limit, faster or slower wanted
        NewCase();
        Record()[1] = Chance(85) ? Address(&g_world.triggers[2]) : 0;
        RecordFloats()[2] = Uniform(0.0f, 40.0f);
        Record()[3] = Random(2);
        RunCase("EFireTriggerSpeedCondition", 0x00040020);

        // EInflictDamage, EKickObject with the index a body slot or past them
        NewCase();
        RecordFloats()[1] = Uniform(-10.0f, 200.0f);
        RecordFloats()[2] = Uniform(-10.0f, 200.0f);
        RunCase("EInflictDamage", 0x000405b0);
        NewCase();
        RecordFloats()[1] = Uniform(-10.0f, 200.0f);
        RecordFloats()[2] = Uniform(-10.0f, 200.0f);
        g_start.dynamic.index = kRigidSlot0 + int32_t(Random(4));
        RunCase("EKickObject", 0x000408f0);
    }
}

void ObjectCases() {
    for (int n = 0; n < 30; n++) {
        NewCase();
        RunCase("EHideInstance", 0x00040320);
        NewCase();
        Record()[1] = Chance(85) ? Address(&g_world.instances[1]) : 0;
        RunCase("EHideInstanceStatic", 0x000403c0);
        NewCase();
        Record()[1] = Address(&g_world.instances[1]);
        Scene(1)->physics = Chance(80) ? Object(int(Random(4))) : NULL;
        Scene(0)->physics = Chance(80) ? Object(int(Random(4))) : NULL;
        RunCase("EKillObject", 0x00040a00);
        NewCase();
        Record()[1] = Chance(85) ? Address(&g_world.instances[1]) : 0;
        Scene(1)->physics = Chance(80) ? Object(int(Random(4))) : NULL;
        RunCase("EKillSentry", 0x00040aa0);

        // EHitWindow: the record has the event's layout
        NewCase();
        EHitWindow *hit = reinterpret_cast<EHitWindow *>(g_world.record);
        SetVector(&hit->point, -50.0f, 50.0f);
        if (Chance(50))
            hit->point.y = Uniform(0.0f, 20.0f);
        SetVector(&hit->normal, -1.0f, 1.0f);
        hit->hit.kind = Random(8);
        hit->hit.hits = Chance(40) ? 0 : Random(8);
        SetVector(&hit->hit.corner0, -5.0f, 5.0f);
        SetVector(&hit->hit.corner1, -5.0f, 5.0f);
        hit->flag = int32_t(Random(2));
        RunCase("EHitWindow", 0x00040450);

        NewCase();
        Record()[1] = Address(&g_world.effect);
        if (Chance(10))
            Record()[1] = 0;
        RunCase("EMuzzleFlash", 0x00040e10);

        NewCase();
        RecordFloats()[1] = Chance(30) ? Uniform(-5.0f, 0.0f) : Uniform(0.0f, 5.0f);
        Record()[2] = Address(&g_world.targetPos);
        RunCase("ENuclearBlast", 0x00040ee0);
    }
}

void AICases() {
    for (int n = 0; n < 60; n++) {
        NewCase();
        Record()[1] = Key(int(Random(3)));
        Record()[2] = Address(&g_world.targetPos);
        RecordFloats()[3] = Uniform(-2.0f, 10.0f);
        RunCase("EKickAIElement", 0x000408c0);

        NewCase();
        Record()[1] = Chance(90) ? Key(int(Random(3))) : 0;
        Record()[2] = Random(2);
        RunCase("EKillAIElement", 0x00040930);

        NewCase();
        Record()[1] = Key(int(Random(3)));
        Record()[2] = Chance(60) ? Key(int(Random(3))) : 0;
        Record()[3] = Random(110);
        if (Chance(40))
            g_start.dynamic.index = kSimpleSlot0 + int32_t(Random(5));
        RunCase("EMaybeActivateAIElement", 0x00040c60);

        NewCase();
        Record()[1] = Random(2);
        RunCase("EForceCarStop", 0x000400e0);
    }
}

void PathCases() {
    for (int n = 0; n < 30; n++) {
        NewCase();
        Record()[1] = Random(100);
        RecordFloats()[2] = Uniform(-2.0f, 5.0f);
        RecordFloats()[3] = Uniform(-2.0f, 5.0f);
        if (Chance(10))
            g_start.dynamic.instance = NULL;
        RunCase("EPathChangeThrottleDynamic", 0x00041410);
        NewCase();
        Record()[1] = Random(100);
        RecordFloats()[2] = Uniform(-2.0f, 5.0f);
        RecordFloats()[3] = Uniform(-2.0f, 5.0f);
        RunCase("EPathMultiplyThrottleDynamic", 0x000415c0);

        // The static ones reach EPathUtil_GetFromInstanceOrAI, GEV_D's: the original runs on both sides (its
        // callees are this test's fakes)
        NewCase();
        Record()[1] = Chance(50) ? Address(&g_world.instances[1]) : 0;
        Record()[2] = Chance(50) ? Key(int(Random(3))) : 0;
        Record()[3] = Random(100);
        RecordFloats()[4] = Uniform(-2.0f, 5.0f);
        RecordFloats()[5] = Uniform(-2.0f, 5.0f);
        Record()[1] = Address(&g_world.instances[1]);
        RunCase("EPathChangeThrottleStatic", 0x000414e0);
        NewCase();
        Record()[1] = Address(&g_world.instances[1]);
        Record()[2] = 0;
        Record()[3] = Random(100);
        RecordFloats()[4] = Uniform(-2.0f, 5.0f);
        RecordFloats()[5] = Uniform(-2.0f, 5.0f);
        RunCase("EPathMultiplyThrottleStatic", 0x000416a0);

        NewCase();
        Record()[1] = Address(&g_world.instances[1]);
        Record()[2] = Address("SFX_Lift");
        Record()[3] = Address("unknown0c");
        RecordFloats()[4] = Uniform(0.0f, 20.0f);
        RecordFloats()[5] = Uniform(0.0f, 200.0f);
        RunCase("EPathSFX", 0x00041810);
    }
}

void FireWeaponCases() {
    for (int n = 0; n < 640; n++) {
        NewCase();
        g_world.answers.weaponType = n % 0x20;
        Record()[1] = Chance(25) ? 0 : Address(&g_world.effect);
        // the shooter: the scene object's physics, else the weapon's owner
        int shooter = int(Random(6));
        Scene(0)->physics = shooter < 5 ? Object(shooter < 4 ? shooter : 3) : NULL;
        if (shooter == 4)
            Object(3)->type = kObjectSentry;
        g_start.dynamic.sceneObj = Scene(0);
        if (Chance(50))
            g_start.dynamic.weapon = reinterpret_cast<ActWeapon *>(g_world.weapon);
        RunCase("EFireWeapon", 0x00040090);
    }
}

void DirectCases() {
    // SimRandom::GenerateBits on the Simulation's generator
    for (int n = 0; n < 20; n++) {
        NewCase();
        memcpy(&g_init, &g_world, sizeof(World));
        std::string o, p;
        for (int side = 0; side < 2; side++) {
            LoadGlobals(&g_start);
            bool original = side == 0;
            Originals *window = original ? new Originals : NULL;
            uint32_t bits = 0;
            Guarded([&] { bits = ((uint32_t (__fastcall *)(SimRandom *, int))0x00040070)(fgSimulation.random, 0); });
            delete window;
            char text[64];
            snprintf(text, sizeof(text), "%08x %08x %08x", bits, fgSimulation.random->state,
                     fgSimulation.random->counter);
            (original ? o : p) = text;
        }
        g_cases++;
        Check(o == p, "SimRandom::GenerateBits", g_cases, o, p);
    }
    // EExit's constructor, as GOrthoHudView::PostRender calls it
    for (int n = 0; n < 2; n++) {
        std::string o, p;
        for (int side = 0; side < 2; side++) {
            uint8_t event[8] = {};
            bool original = side == 0;
            Originals *window = original ? new Originals : NULL;
            void *answer = NULL;
            Guarded([&] { answer = ((void *(__fastcall *)(void *, int))0x0003fb70)(event, 0); });
            delete window;
            char text[64];
            snprintf(text, sizeof(text), "%d %s", answer == event, Floats(event, 2).c_str());
            (original ? o : p) = text;
        }
        g_cases++;
        Check(o == p, "EExit::EExit", g_cases, o, p);
    }
}

}  // namespace

void GameEventsShadowB_Run(void) {
    const char *env = getenv("NIGHTFIRE_GAMEEVENTSSHADOWB");
    if (env == NULL || atoi(env) == 0)
        return;
    if (fgSimulation.random == NULL || LiveMissionManager == NULL || LiveWeaponManager == NULL) {
        printf("[gameeventsshadowb] skipped: no Simulation, mission manager or weapon manager yet\n");
        fflush(stdout);
        return;
    }
    FpControlGet(&g_x87, &g_sse);

    SaveGlobals(&g_live);
    void *savedMission = LiveMissionManager;
    void *savedWeapons = LiveWeaponManager;
    PhysicsObject **savedCars = LiveCarsFirst;
    ABank *savedBanks[kBankSlotCount];
    memcpy(savedBanks, fgBanks, sizeof(savedBanks));

    BuildWorld();
    LiveMissionManager = g_world.missionManager;
    LiveWeaponManager = g_world.weaponManager;
    LiveCarsFirst = g_world.playerList;
    for (int i = 0; i < kBankSlotCount; i++)
        fgBanks[i] = &g_world.banks[i];
    InstallHooks();

    SimpleCases();
    TriggerCases();
    ObjectCases();
    AICases();
    PathCases();
    // EFireWeapon reads the first player camera's frame
    if (LiveRenderHigh != NULL && *reinterpret_cast<void **>(uintptr_t(LiveRenderHigh) + 4) != NULL)
        FireWeaponCases();
    DirectCases();

    HooksRemove();
    memcpy(fgBanks, savedBanks, sizeof(savedBanks));
    LiveCarsFirst = savedCars;
    LiveWeaponManager = savedWeapons;
    LiveMissionManager = savedMission;
    LoadGlobals(&g_live);

    for (const auto &d : g_differBy)
        printf("[gameeventsshadowb]   %s: %ld differ\n", d.first.c_str(), d.second);
    printf("[gameeventsshadowb] events EExit..EPathSFX: %ld cases, %ld checks, %ld differ (%ld faults)\n", g_cases,
           g_checks, g_differ, g_faults);
    fflush(stdout);
}
