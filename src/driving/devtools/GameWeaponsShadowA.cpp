#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "GameWeaponsShadowA.h"
#include "FpControl.h"

#include <windows.h>
#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vector>

#include "../anim/AnimEngine.h"
#include "../camera/PlayerCamera.h"
#include "../engine/ActionQueue.hpp"
#include "../game/Vehicle.h"
#include "../game/WeaponManager.h"
#include "../physics/PhysicsObject.h"
#include "../physics/RigidBody.h"
#include "../render/RSceneObj.hpp"
#include "../world/Targeting.h"
#include "../../common/xbeOriginal.h"
#include "../../helpers.h"

// ---------------------------------------------------------------------------------------------------------------
// A shadow test of game.weapons' package A (SWeaponManager), run once from the first simulation tick when
// NIGHTFIRE_GAMEWEAPONSSHADOWA is set. Each case copies the live manager and its weapon array into private blocks,
// perturbs them (ammunition, loaded rounds, clip sizes, flags, firing and reloading systems and their steps, the
// selections, the fire buttons, the turret, the nuclear sequence, a random action queue) and the state round them
// (the mission manager's state, auto-drive, steered missile and cinematic words, the simulation's state and step,
// the launch flags, the auto-switch option, the hands, the HUD, the selected target, the AI helicopters), points
// the game's WeaponManager at the copy, runs the ORIGINAL (the package's entries swapped back together) and then the
// PORT from the same state, and compares the manager, its weapon array and action queue, the weapon table, the
// colorizer, the steered missile, the sound block and the globals both write, and a log of every call out:
//
//   - Reset, SetPlayerCar, LoadWeapon, UnloadWeapon, SetSecondary, SetSpecialWeapon, StartNuclearSequence with
//     random arguments; SortWeaponList, GetPreviousSecondaryType, GetNextSecondaryType (their answers compared),
//     SelectSecondary both ways; DoWeaponAnim and GetWeaponFromSystem over every id;
//   - FireWeapon for every weapon (and -1), UserReloading, UpdateTurret (random targets, exact 0.25 and 0.75
//     angles), HandleNuclearSequence in each state either side of its step, Update;
//   - Construct and Destruct on a block of their own, the allocations faked.
//
// Fakes (5-byte jumps over the entries, and over our ports for the ported ones): the animation handle's
// IsSystemPlaying, GetSystemState, ProcessStimuli and SetFrame (answers from the case's seed and the system id),
// AMenuSound::Trigger, the mission manager's IncShotsFired, ProgrammerDefinedEvent, IncMagicCounter,
// SetMagicCounter and InitPlayerCar, the Simulation's DetonateRemoteMines and SpawnExplosion, AICharacterHands'
// AbleToFire, GetWeapon, ChangeWeapon, Fire and Reload, AttributeSet's LookupBool and LookupInt, GHud's TheApp and
// LavaDamageEffectEnabled, RColorize's SetEnabled and EnableMotionBlur, the player camera's CameraAiming,
// UpdateCurrentArm, SetAnchor and TriggerFixedCamera, the target picker's GetSelectedTarget, ActivateTargeting
// and DeactivateTargeting, RScorchWorld::ScorchGround (its point not logged: the original's is uninitialised
// stack), RigidBody::GenerateImpulse, the AI's helicopter and zone calls, the player car's EnableRocketBoost,
// FireLaser, EnableTwoWheelStunt and its render object's TriggerIlluminate; around their cases only,
// ABaseSound::OperatorNew, the one-shot sound's constructor and AIndex::Lookup, and the allocator and the action
// queue's constructor and destructor.
//
// A mutation the test sees: FireWeapon without the "clip size 4" decrement after the missile's single shot (the
// weapon array's loaded rounds differ in the missile cases with a clip of 4).
// ---------------------------------------------------------------------------------------------------------------

namespace {

// ---- the originals

const unsigned kEntries[] = {
    0x000ba7f0, 0x000ba990, 0x000ba9f0, 0x000baa70, 0x000baab0, 0x000baad0, 0x000bab20, 0x000bab80, 0x000babb0,
    0x000babf0, 0x000bad00, 0x000bad40, 0x000bad70, 0x000bae50, 0x000baee0, 0x000baf60, 0x000bb220, 0x000bb2b0,
    0x000bb9b0, 0x000bbdc0,
};

#define Orig_SortWeaponList ((void (__fastcall *)(SWeaponManager *, int))0x000ba7f0)
#define Orig_SetPlayerCar ((void (__fastcall *)(SWeaponManager *, int, RSceneObj *))0x000ba990)
#define Orig_LoadWeapon ((void (__fastcall *)(SWeaponManager *, int, int, int, int, int))0x000ba9f0)
#define Orig_UnloadWeapon ((void (__fastcall *)(SWeaponManager *, int, int))0x000baa70)
#define Orig_SetSecondary ((void (__fastcall *)(SWeaponManager *, int, int))0x000baab0)
#define Orig_GetPrevious ((int (__fastcall *)(SWeaponManager *, int))0x000baad0)
#define Orig_GetNext ((int (__fastcall *)(SWeaponManager *, int))0x000bab20)
#define Orig_DoWeaponAnim ((void (__fastcall *)(SWeaponManager *, int, int, int, int))0x000bab80)
#define Orig_GetWeaponFromSystem ((int (__fastcall *)(SWeaponManager *, int, int))0x000babb0)
#define Orig_UserReloading ((void (__fastcall *)(SWeaponManager *, int))0x000babf0)
#define Orig_StartNuclear ((void (__fastcall *)(SWeaponManager *, int, const Coord3 *, float))0x000bad00)
#define Orig_Destruct ((void (__fastcall *)(SWeaponManager *, int))0x000bad40)
#define Orig_Reset ((void (__fastcall *)(SWeaponManager *, int))0x000bad70)
#define Orig_SetSpecial ((void (__fastcall *)(SWeaponManager *, int, int, const Coord3 *, float))0x000bae50)
#define Orig_SelectSecondary ((void (__fastcall *)(SWeaponManager *, int, bool))0x000baee0)
#define Orig_UpdateTurret ((void (__fastcall *)(SWeaponManager *, int))0x000baf60)
#define Orig_Construct ((SWeaponManager *(__fastcall *)(SWeaponManager *, int))0x000bb220)
#define Orig_FireWeapon ((void (__fastcall *)(SWeaponManager *, int, int))0x000bb2b0)
#define Orig_HandleNuclear ((void (__fastcall *)(SWeaponManager *, int))0x000bb9b0)
#define Orig_Update ((void (__fastcall *)(SWeaponManager *, int))0x000bbdc0)

// ---- the game's state

#define ShadowWeaponManager (*(SWeaponManager **)0x0023923c)
#define ShadowMission (*(uint8_t **)0x00239220)
#define ShadowSimState I32_AT(0x00234e24)
#define ShadowStepsPerSecond I32_AT(0x00234e2c)
#define ShadowStepCount I32_AT(0x00234e34)
#define ShadowPlayer (*(PVehicle ***)0x00234e40)
#define ShadowCanNeverLose U8_AT(0x00239218)
#define ShadowColorize (*(uint8_t **)0x001f6898)
#define ShadowHands (*(void **)0x001dda90)
#define ShadowAIElement (*(void **)0x001ddee4)
const uint32_t kLaunchFlags = 0x00244530;
const uint32_t kLaunchCheats = 0x00244534;
const uint32_t kLaunchAutoSwitch = 0x002445d8;
const uint32_t kBondPressed = 0x001dda2c;
const uint32_t kWeaponTable = 0x001c94d8;

// The mission manager's words the manager reads (offsets)
enum {
    kMissionState = 0x474,
    kMissionAutoDrive = 0x478,
    kMissionDriveMissile = 0x47c,
    kMissionCinematic = 0x4f0,
    kMission710 = 0x710,
};

// ---- results

bool g_original;
long g_cases, g_checks, g_differ, g_faults;
int g_reported;
unsigned g_x87, g_sse;

void Report(const char *what, long index, const char *detail) {
    g_differ++;
    if (g_reported++ < 10)
        printf("[gameweaponsshadowa]   %s #%ld: %s\n", what, index, detail);
}

void CompareBytes(const char *what, long index, const void *a, const void *b, size_t bytes) {
    g_checks++;
    if (memcmp(a, b, bytes) == 0)
        return;
    const uint8_t *x = static_cast<const uint8_t *>(a), *y = static_cast<const uint8_t *>(b);
    size_t at = 0;
    while (x[at] == y[at])
        at++;
    char detail[112];
    snprintf(detail, sizeof(detail), "byte 0x%x of 0x%x: original %02x, port %02x", unsigned(at), unsigned(bytes),
             x[at], y[at]);
    Report(what, index, detail);
}

void ResetFpu() {
    _fpreset();
    FpControlSetX87(g_x87);
    FpControlSetSse(g_sse);
}

// ---- random inputs (a generator of our own)

uint32_t g_seed = 0x5a17c3e1;

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

bool Chance(int percent) {
    return Range(0, 99) < percent;
}

// The fakes' answers: a hash of the case's seed and the arguments, the same on both sides
uint32_t g_caseSeed;

uint32_t Hash(uint32_t a, uint32_t b) {
    uint32_t h = g_caseSeed ^ (a * 0x9e3779b1u) ^ (b * 0x85ebca6bu);
    h ^= h >> 15;
    h *= 0x2c1b3c6du;
    h ^= h >> 12;
    h *= 0x297a2d39u;
    return h ^ (h >> 15);
}

// Answers that depend on the call's place in the sequence (no arguments to hash): counted per side
uint32_t g_sequence;

uint32_t Sequenced(uint32_t kind) {
    return Hash(kind, g_sequence++);
}

// ---- the regions a case may change (game addresses and our blocks alike)

struct Region {
    uint8_t *at;
    uint32_t size;
    const char *name;
};

std::vector<Region> g_regions;

void Add(const void *at, uint32_t size, const char *name) {
    if (at != NULL)
        g_regions.push_back({ const_cast<uint8_t *>(static_cast<const uint8_t *>(at)), size, name });
}

void AddGame(uint32_t at, uint32_t size, const char *name) {
    Add(reinterpret_cast<const void *>(uintptr_t(at)), size, name);
}

struct Snapshot {
    std::vector<uint8_t> bytes;
};

void Capture(Snapshot *s) {
    s->bytes.clear();
    for (const Region &r : g_regions)
        s->bytes.insert(s->bytes.end(), r.at, r.at + r.size);
}

void Restore(const Snapshot &s) {
    size_t at = 0;
    for (const Region &r : g_regions) {
        memcpy(r.at, &s.bytes[at], r.size);
        at += r.size;
    }
}

void CompareResults(const char *what, long index, const Snapshot &original, const Snapshot &port) {
    size_t at = 0;
    for (const Region &r : g_regions) {
        char label[96];
        snprintf(label, sizeof(label), "%s: %s", what, r.name);
        CompareBytes(label, index, &original.bytes[at], &port.bytes[at], r.size);
        at += r.size;
    }
}

// ---- the call logs

std::vector<uint32_t> g_logs[2];

void Log(uint32_t word) {
    g_logs[g_original ? 0 : 1].push_back(word);
}

void LogFloat(float value) {
    uint32_t word;
    memcpy(&word, &value, 4);
    Log(word);
}

void LogXyz(const void *v) {
    if (v == NULL) {
        Log(0xdeadbeef);
        return;
    }
    const float *f = static_cast<const float *>(v);
    LogFloat(f[0]);
    LogFloat(f[1]);
    LogFloat(f[2]);
}

uint32_t TextHash(const char *text) {
    if (text == NULL)
        return 0;
    uint32_t hash = 2166136261u;
    for (const char *c = text; *c != '\0'; c++)
        hash = (hash ^ uint8_t(*c)) * 16777619u;
    return hash;
}

void CompareLogs(const char *what, long index) {
    g_checks++;
    const std::vector<uint32_t> &a = g_logs[0], &b = g_logs[1];
    if (a == b)
        return;
    size_t at = 0;
    while (at < a.size() && at < b.size() && a[at] == b[at])
        at++;
    char detail[128];
    snprintf(detail, sizeof(detail), "call log word %u of %u/%u: original %08x, port %08x", unsigned(at),
             unsigned(a.size()), unsigned(b.size()), at < a.size() ? a[at] : 0, at < b.size() ? b[at] : 0);
    Report(what, index, detail);
}

// ---- the private objects

alignas(16) uint8_t g_managerBlock[sizeof(SWeaponManager)];
SWeaponManager *const g_manager = reinterpret_cast<SWeaponManager *>(g_managerBlock);
// The weapon array, a record's room either side: an empty selection (-1) reads the record before it
alignas(16) uint8_t g_weaponsBlock[sizeof(WeaponRecord) * (kWeaponCount + 2)];
WeaponRecord *const g_weapons = reinterpret_cast<WeaponRecord *>(g_weaponsBlock) + 1;
alignas(16) uint8_t g_queueBlock[sizeof(ActionQueue)];
ActionQueue *const g_queue = reinterpret_cast<ActionQueue *>(g_queueBlock);
alignas(16) uint8_t g_soundBlock[0x120];
uint8_t g_missileBlock[0x710];
alignas(16) uint8_t g_targetBlock[sizeof(WTargetable)];
WTargetable *const g_target = reinterpret_cast<WTargetable *>(g_targetBlock);
alignas(16) uint8_t g_targetPhysics[0x80];
uint8_t g_hudBlock[0x2c0];
uint8_t g_handsBlock[0x10];
uint8_t g_aiElementBlock[0x10];
Coord3 g_trigger;
int32_t g_answer;               // a call's answer (compared as a region)

struct FakeHelicopter {
    uint8_t ai[0x80];           // +0x68 active
    alignas(16) uint8_t physics[0x140];     // +0x139 proximityDestructEnabled
    Coord3 position;
};
const int kHelicopters = 4;
FakeHelicopter g_helicopters[kHelicopters];

// Construct's and Destruct's blocks
alignas(16) uint8_t g_newManagerBlock[sizeof(SWeaponManager)];
alignas(16) uint8_t g_newWeaponsBlock[sizeof(WeaponRecord) * kWeaponCount];
alignas(16) uint8_t g_newQueueBlock[sizeof(ActionQueue)];

// The live state, as it was at the start
alignas(16) uint8_t g_liveManagerBlock[sizeof(SWeaponManager)];
const SWeaponManager *const g_live = reinterpret_cast<const SWeaponManager *>(g_liveManagerBlock);
WeaponRecord g_liveWeapons[kWeaponCount];
Handle *g_liveHandle;
const AttributeSet *g_playerAttributes;
bool g_hudPresent, g_targetPresent;

// ---- the fakes

enum : uint32_t {
    kTagPlaying = 0x57410001, kTagState, kTagStimuli, kTagFrame, kTagTrigger, kTagShots, kTagEvent, kTagIncMagic,
    kTagSetMagic, kTagInitCar, kTagDetonate, kTagExplosion, kTagAbleToFire, kTagGetWeapon, kTagChangeWeapon,
    kTagFire, kTagReload, kTagLookupBool, kTagLookupInt, kTagTheApp, kTagLava, kTagSetEnabled, kTagMotionBlur,
    kTagAiming, kTagArm, kTagAnchor, kTagFixedCamera, kTagSelected, kTagActivate, kTagDeactivate, kTagScorch,
    kTagImpulse, kTagSoundNew, kTagOneShot, kTagIndexLookup, kTagAIGet, kTagHelicopter, kTagHeliPhysics,
    kTagHeliPosition, kTagZoneGet, kTagZone, kTagRocketBoost, kTagLaser, kTagTwoWheels, kTagIlluminate,
    kTagNewArray, kTagDelete, kTagFastAlloc, kTagFastFree, kTagQueueConstruct, kTagQueueDestruct,
};

uint32_t HandleId(const Handle *handle) {
    return handle == g_liveHandle ? 1 : uint32_t(uintptr_t(handle));
}

bool __fastcall FakeIsSystemPlaying(Handle *handle, int, uint32_t id) {
    Log(kTagPlaying);
    Log(HandleId(handle));
    Log(id);
    return Hash(kTagPlaying, id) % 4 == 0;
}

uint32_t __fastcall FakeGetSystemState(Handle *handle, int, uint32_t id) {
    Log(kTagState);
    Log(HandleId(handle));
    Log(id);
    return Hash(kTagState, id) % 3;
}

void __fastcall FakeProcessStimuli(Handle *handle, int, uint32_t id, uint8_t stimulus, uint32_t tick, int mode) {
    Log(kTagStimuli);
    Log(HandleId(handle));
    Log(id);
    Log(stimulus);
    Log(tick);
    Log(uint32_t(mode));
}

void __fastcall FakeSetFrame(Handle *handle, int, uint32_t id, uint8_t stimulus, uint32_t time) {
    Log(kTagFrame);
    Log(HandleId(handle));
    Log(id);
    Log(stimulus);
    Log(time);
}

void FakeTrigger(int bank, const char *patch, const char *mix, int view) {
    Log(kTagTrigger);
    Log(uint32_t(bank));
    Log(TextHash(patch));
    Log(TextHash(mix));
    Log(uint32_t(view));
}

void __fastcall FakeIncShots(void *mission, int, int count) {
    Log(kTagShots);
    Log(mission == ShadowMission);
    Log(uint32_t(count));
}

void __fastcall FakeEvent(void *mission, int, int event, const char *text) {
    Log(kTagEvent);
    Log(mission == ShadowMission);
    Log(uint32_t(event));
    Log(TextHash(text));
}

void __fastcall FakeIncMagic(void *mission, int, int counter) {
    Log(kTagIncMagic);
    Log(mission == ShadowMission);
    Log(uint32_t(counter));
}

void __fastcall FakeSetMagic(void *mission, int, int counter, int value) {
    Log(kTagSetMagic);
    Log(mission == ShadowMission);
    Log(uint32_t(counter));
    Log(uint32_t(value));
}

void __fastcall FakeInitPlayerCar(void *mission, int) {
    Log(kTagInitCar);
    Log(mission == ShadowMission);
}

void __fastcall FakeDetonate(void *sim, int) {
    Log(kTagDetonate);
    Log(uint32_t(uintptr_t(sim)));
}

void __fastcall FakeSpawnExplosion(void *sim, int, int type, int owner, const Coord3 *normal, const Coord3 *position,
                                   float size, int unknown6, int unknown7, const uint32_t *source) {
    Log(kTagExplosion);
    Log(uint32_t(uintptr_t(sim)));
    Log(uint32_t(type));
    Log(uint32_t(owner));
    LogXyz(normal);
    LogXyz(position);
    LogFloat(size);
    Log(uint32_t(unknown6));
    Log(uint32_t(unknown7));
    Log(uint32_t(uintptr_t(source)));
}

bool __fastcall FakeAbleToFire(void *hands, int) {
    Log(kTagAbleToFire);
    Log(hands == g_handsBlock);
    return Sequenced(kTagAbleToFire) % 4 != 0;
}

int __fastcall FakeGetWeapon(void *hands, int) {
    Log(kTagGetWeapon);
    Log(hands == g_handsBlock);
    return int(Sequenced(kTagGetWeapon) % 10);
}

void __fastcall FakeChangeWeapon(void *hands, int, int weapon) {
    Log(kTagChangeWeapon);
    Log(hands == g_handsBlock);
    Log(uint32_t(weapon));
}

void __fastcall FakeFire(void *hands, int) {
    Log(kTagFire);
    Log(hands == g_handsBlock);
}

void __fastcall FakeReload(void *hands, int) {
    Log(kTagReload);
    Log(hands == g_handsBlock);
}

uint32_t AttributesId(const AttributeSet *set) {
    if (set == g_playerAttributes)
        return 1;
    if (static_cast<const void *>(set) == g_targetPhysics + 0x60)
        return 2;
    return uint32_t(uintptr_t(set));
}

uint8_t __fastcall FakeLookupBool(AttributeSet *set, int, const char *key, bool *found) {
    Log(kTagLookupBool);
    Log(AttributesId(set));
    Log(TextHash(key));
    uint32_t h = Hash(TextHash(key), AttributesId(set));
    if (found != NULL)
        *found = (h & 8) != 0;
    return (h & 3) != 0;
}

int32_t __fastcall FakeLookupInt(AttributeSet *set, int, const char *key, bool *found) {
    Log(kTagLookupInt);
    Log(AttributesId(set));
    Log(TextHash(key));
    uint32_t h = Hash(TextHash(key) + 1, AttributesId(set));
    if (found != NULL)
        *found = (h & 3) != 0;
    return int32_t(h >> 4) % 5;
}

void *FakeTheApp() {
    Log(kTagTheApp);
    return g_hudPresent ? g_hudBlock : NULL;
}

bool __fastcall FakeLava(void *hud, int) {
    Log(kTagLava);
    Log(hud == g_hudBlock);
    return Sequenced(kTagLava) % 5 == 0;
}

void __fastcall FakeSetEnabled(void *colorize, int, int mode) {
    Log(kTagSetEnabled);
    Log(colorize == ShadowColorize);
    Log(uint32_t(mode));
}

void __fastcall FakeMotionBlur(void *colorize, int, float amount) {
    Log(kTagMotionBlur);
    Log(colorize == ShadowColorize);
    LogFloat(amount);
}

uint8_t __fastcall FakeCameraAiming(void *camera, int) {
    Log(kTagAiming);
    Log(uint32_t(uintptr_t(camera)));
    return Sequenced(kTagAiming) % 4 == 0;
}

void __fastcall FakeUpdateArm(void *camera, int) {
    Log(kTagArm);
    Log(uint32_t(uintptr_t(camera)));
}

void __fastcall FakeSetAnchor(void *camera, int, PhysicsObject *anchor) {
    Log(kTagAnchor);
    Log(uint32_t(uintptr_t(camera)));
    Log(uint32_t(uintptr_t(anchor)));
}

void __fastcall FakeFixedCamera(void *camera, int, Coord4 *position, int index, uint16_t delay) {
    Log(kTagFixedCamera);
    Log(uint32_t(uintptr_t(camera)));
    LogXyz(position);
    Log(uint32_t(index));
    Log(delay);
}

WTargetable *__fastcall FakeSelectedTarget(void *picker, int) {
    Log(kTagSelected);
    Log(uint32_t(uintptr_t(picker)));
    return g_targetPresent ? g_target : NULL;
}

void __fastcall FakeActivate(void *picker, int, int mode) {
    Log(kTagActivate);
    Log(uint32_t(uintptr_t(picker)));
    Log(uint32_t(mode));
}

void __fastcall FakeDeactivate(void *picker, int) {
    Log(kTagDeactivate);
    Log(uint32_t(uintptr_t(picker)));
}

void FakeScorchGround(void *place, const Coord4 *point, float size, int type) {
    (void)place;
    (void)point;
    Log(kTagScorch);
    LogFloat(size);
    Log(uint32_t(type));
}

void __fastcall FakeGenerateImpulse(RigidBody *body, int, void *impact, const Coord4 *normal, const Coord4 *point,
                                    float push, bool fromWorld, uint16_t tag, float speedScale) {
    (void)impact;
    Log(kTagImpulse);
    Log(uint32_t(uintptr_t(body)));
    LogXyz(normal);
    LogFloat(normal->w);
    LogXyz(point);
    LogFloat(point->w);
    LogFloat(push);
    Log(fromWorld);
    Log(tag);
    LogFloat(speedScale);
}

void *FakeSoundNew(unsigned size, const char *name) {
    Log(kTagSoundNew);
    Log(size);
    Log(TextHash(name));
    return g_soundBlock;
}

void *__fastcall FakeOneShotAt(void *memory, int, int bank, int patch, const Coord3 *position, float volume,
                               const char *mix, bool loop) {
    Log(kTagOneShot);
    Log(memory == g_soundBlock);
    Log(uint32_t(bank));
    Log(uint32_t(patch));
    LogXyz(position);
    LogFloat(volume);
    Log(TextHash(mix));
    Log(loop);
    return memory;
}

int __fastcall FakeIndexLookup(void *index, int, const char *name) {
    Log(kTagIndexLookup);
    Log(uint32_t(uintptr_t(index)));
    Log(TextHash(name));
    return int(TextHash(name) & 0x7f);
}

uint8_t g_controllerDummy[4];

void *FakeAIGet() {
    Log(kTagAIGet);
    return g_controllerDummy;
}

void *__fastcall FakeGetHelicopter(void *controller, int, int index) {
    Log(kTagHelicopter);
    Log(controller == g_controllerDummy);
    Log(uint32_t(index));
    return g_helicopters[index & (kHelicopters - 1)].ai;
}

FakeHelicopter *HelicopterOf(void *ai) {
    for (FakeHelicopter &h : g_helicopters)
        if (ai == h.ai)
            return &h;
    return &g_helicopters[0];
}

void *__fastcall FakeHeliPhysics(void *ai, int) {
    Log(kTagHeliPhysics);
    Log(uint32_t(HelicopterOf(ai) - g_helicopters));
    return HelicopterOf(ai)->physics;
}

Coord3 *__fastcall FakeHeliPosition(void *ai, int) {
    Log(kTagHeliPosition);
    Log(uint32_t(HelicopterOf(ai) - g_helicopters));
    return &HelicopterOf(ai)->position;
}

void *FakeZoneGet() {
    Log(kTagZoneGet);
    return g_controllerDummy + 1;
}

void __fastcall FakeRegisterZone(void *controller, int, int kind, const Coord4 *position) {
    Log(kTagZone);
    Log(controller == g_controllerDummy + 1);
    Log(uint32_t(kind));
    LogXyz(position);
}

bool __fastcall FakeRocketBoost(void *car, int) {
    Log(kTagRocketBoost);
    Log(uint32_t(uintptr_t(car)));
    return Sequenced(kTagRocketBoost) % 2 == 0;
}

void __fastcall FakeLaser(void *car, int) {
    Log(kTagLaser);
    Log(uint32_t(uintptr_t(car)));
}

void __fastcall FakeTwoWheels(void *car, int) {
    Log(kTagTwoWheels);
    Log(uint32_t(uintptr_t(car)));
}

void __fastcall FakeIlluminate(void *object, int, int index, int32_t tick, int fade) {
    Log(kTagIlluminate);
    Log(uint32_t(uintptr_t(object)));
    Log(uint32_t(index));
    Log(uint32_t(tick));
    Log(uint32_t(fade));
}

void *FakeNewArray(unsigned size) {
    Log(kTagNewArray);
    Log(size);
    return g_newWeaponsBlock;
}

void FakeDelete(void *block) {
    Log(kTagDelete);
    Log(block == g_newWeaponsBlock);
}

void *FakeFastAlloc(unsigned size, const char *name) {
    Log(kTagFastAlloc);
    Log(size);
    Log(TextHash(name));
    return g_newQueueBlock;
}

void FakeFastFree(void *block, unsigned size) {
    Log(kTagFastFree);
    Log(block == g_newQueueBlock);
    Log(size);
}

ActionQueue *__fastcall FakeQueueConstruct(ActionQueue *queue, int, char *name) {
    Log(kTagQueueConstruct);
    Log(queue == reinterpret_cast<ActionQueue *>(g_newQueueBlock));
    Log(TextHash(name));
    return queue;
}

void __fastcall FakeQueueDestruct(ActionQueue *queue, int) {
    Log(kTagQueueDestruct);
    Log(queue == reinterpret_cast<ActionQueue *>(g_newQueueBlock));
}

// ---- hooks: a jump written over an entry (and over the port a patched entry jumps to)

struct Hook {
    uint32_t at;
    uint8_t saved[5];
    bool on;
};
Hook g_hooks[96];
int g_hookCount;
int g_hookFailures;

void HookOne(uint32_t at, const void *to) {
    if (g_hookCount == int(sizeof(g_hooks) / sizeof(g_hooks[0]))) {
        g_hookFailures++;
        return;
    }
    const uint8_t *code = reinterpret_cast<const uint8_t *>(uintptr_t(at));
    if (code[0] != 0xe9) {
        for (int i = 0; i < 5; i++) {
            if (code[i] == 0xc3 || code[i] == 0xc2 || code[i] == 0xcc) {    // too short to take a jump
                printf("[gameweaponsshadowa]   no hook at 0x%08x\n", at);
                g_hookFailures++;
                return;
            }
        }
    }
    Hook &h = g_hooks[g_hookCount++];
    h.at = at;
    h.on = false;
    DWORD old;
    if (!VirtualProtect((void *)uintptr_t(at), 5, PAGE_EXECUTE_READWRITE, &old)) {
        g_hookFailures++;
        return;
    }
    memcpy(h.saved, (void *)uintptr_t(at), 5);
    uint8_t jump[5] = { 0xe9 };
    int32_t relative = int32_t(uint32_t(uintptr_t(to)) - (at + 5));
    memcpy(jump + 1, &relative, 4);
    memcpy((void *)uintptr_t(at), jump, 5);
    VirtualProtect((void *)uintptr_t(at), 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)uintptr_t(at), 5);
    h.on = true;
}

void HookInstall(uint32_t at, const void *to) {
    if (at == 0) {
        g_hookFailures++;
        return;
    }
    const uint8_t *entry = reinterpret_cast<const uint8_t *>(uintptr_t(at));
    uint32_t port = 0;
    if (entry[0] == 0xe9) {
        int32_t relative;
        memcpy(&relative, entry + 1, 4);
        port = at + 5 + uint32_t(relative);
    }
    HookOne(at, to);
    if (port != 0 && port != uint32_t(uintptr_t(to)))
        HookOne(port, to);
}

// Takes off the hooks installed after the first `keep`
void HooksRemove(int keep) {
    for (int i = g_hookCount; i-- > keep;) {
        Hook &h = g_hooks[i];
        if (!h.on)
            continue;
        DWORD old;
        VirtualProtect((void *)uintptr_t(h.at), 5, PAGE_EXECUTE_READWRITE, &old);
        memcpy((void *)uintptr_t(h.at), h.saved, 5);
        VirtualProtect((void *)uintptr_t(h.at), 5, old, &old);
        FlushInstructionCache(GetCurrentProcess(), (void *)uintptr_t(h.at), 5);
        h.on = false;
    }
    g_hookCount = keep;
}

uint32_t VtableSlot(const void *object, int slot) {
    const uint32_t *vtable = *static_cast<const uint32_t *const *>(object);
    return vtable[slot];
}

void InstallBaseHooks() {
    HookInstall(0x000774a0, (const void *)&FakeIsSystemPlaying);
    HookInstall(0x00077500, (const void *)&FakeGetSystemState);
    HookInstall(0x00077d70, (const void *)&FakeProcessStimuli);
    HookInstall(0x00077c50, (const void *)&FakeSetFrame);
    HookInstall(0x0011e010, (const void *)&FakeTrigger);
    HookInstall(0x000b66f0, (const void *)&FakeIncShots);
    HookInstall(0x000b72f0, (const void *)&FakeEvent);
    HookInstall(0x000b7a40, (const void *)&FakeIncMagic);
    HookInstall(0x000b7ac0, (const void *)&FakeSetMagic);
    HookInstall(0x000b8090, (const void *)&FakeInitPlayerCar);
    HookInstall(0x000b2c10, (const void *)&FakeDetonate);
    HookInstall(0x000b4cd0, (const void *)&FakeSpawnExplosion);
    HookInstall(0x00022830, (const void *)&FakeAbleToFire);
    HookInstall(0x00022860, (const void *)&FakeGetWeapon);
    HookInstall(0x00022cc0, (const void *)&FakeChangeWeapon);
    HookInstall(0x00022dd0, (const void *)&FakeFire);
    HookInstall(0x00022e20, (const void *)&FakeReload);
    HookInstall(0x000580e0, (const void *)&FakeLookupBool);
    HookInstall(0x00058180, (const void *)&FakeLookupInt);
    HookInstall(0x000d7c10, (const void *)&FakeTheApp);
    HookInstall(0x000d7c60, (const void *)&FakeLava);
    HookInstall(0x0009a500, (const void *)&FakeSetEnabled);
    HookInstall(0x0009a4d0, (const void *)&FakeMotionBlur);
    HookInstall(0x00081980, (const void *)&FakeCameraAiming);
    HookInstall(0x00084680, (const void *)&FakeUpdateArm);
    HookInstall(0x00097530, (const void *)&FakeSetAnchor);
    HookInstall(0x00084850, (const void *)&FakeFixedCamera);
    HookInstall(0x000cdfb0, (const void *)&FakeSelectedTarget);
    HookInstall(0x000ce0f0, (const void *)&FakeActivate);
    HookInstall(0x000ce150, (const void *)&FakeDeactivate);
    HookInstall(0x000a5530, (const void *)&FakeScorchGround);
    HookInstall(0x000afdb0, (const void *)&FakeGenerateImpulse);
    HookInstall(0x00035c00, (const void *)&FakeAIGet);
    HookInstall(0x00035ef0, (const void *)&FakeGetHelicopter);
    HookInstall(0x00035840, (const void *)&FakeHeliPhysics);
    HookInstall(0x000359e0, (const void *)&FakeHeliPosition);
    HookInstall(0x00036a10, (const void *)&FakeZoneGet);
    HookInstall(0x00036bb0, (const void *)&FakeRegisterZone);
    PVehicle *car = *ShadowPlayer;
    HookInstall(VtableSlot(car, PVehicle::kEnableRocketBoost), (const void *)&FakeRocketBoost);
    HookInstall(VtableSlot(car, PVehicle::kFireLaser), (const void *)&FakeLaser);
    HookInstall(VtableSlot(car, PVehicle::kEnableTwoWheelStunt), (const void *)&FakeTwoWheels);
    HookInstall(VtableSlot(car->renderObject, 12), (const void *)&FakeIlluminate);
}

void InstallSoundHooks() {
    HookInstall(0x0011c910, (const void *)&FakeSoundNew);
    HookInstall(0x000477d0, (const void *)&FakeOneShotAt);
    HookInstall(0x00126d40, (const void *)&FakeIndexLookup);
}

void InstallAllocationHooks() {
    HookInstall(0x00114710, (const void *)&FakeNewArray);
    HookInstall(0x001146e0, (const void *)&FakeDelete);
    HookInstall(0x00114750, (const void *)&FakeFastAlloc);
    HookInstall(0x001147d0, (const void *)&FakeFastFree);
    HookInstall(0x0004f320, (const void *)&FakeQueueConstruct);
    HookInstall(0x0004f1b0, (const void *)&FakeQueueDestruct);
}

// ---- running a case

typedef void (*SideFn)(void);

void Window(bool original) {
    for (unsigned at : kEntries)
        XbeOriginal_Restore(at, original);
}

void RunSide(SideFn run, bool original) {
    g_original = original;
    g_sequence = 0;
    if (original)
        Window(true);
#ifdef _MSC_VER
    __try {
        run();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_faults++;
    }
#else
    run();
#endif
    if (original)
        Window(false);
    ResetFpu();
}

void RunCase(const char *what, long index, SideFn run, const Snapshot &before) {
    Snapshot inputs, original, port;
    Capture(&inputs);
    g_logs[0].clear();
    g_logs[1].clear();
    RunSide(run, true);
    Capture(&original);
    Restore(inputs);
    RunSide(run, false);
    Capture(&port);
    g_cases++;
    CompareResults(what, index, original, port);
    CompareLogs(what, index);
    Restore(before);
}

// ---- the case's inputs

// How many animation systems each weapon has (the port's WeaponSystems)
const int kSystemCounts[kWeaponCount] = {
    1, 2, 4, 1, 1, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 1, 0, 0, 1, 0, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1,
};

int SystemCount(int weapon) {
    return kSystemCounts[weapon];
}

void PerturbRecord(WeaponRecord *record, int weapon, int32_t step) {
    record->id = weapon;
    record->flags = Next() & 0x67;
    record->ammo = Range(-2, 12);
    record->loaded = Range(-1, 8);
    record->clipSize = Chance(30) ? 4 : Range(0, 8);
    for (int i = 0; i < 4; i++)
        record->fireSteps[i] = step - Range(-5, 400);
    if (Chance(30))
        record->refireSteps = Range(0, 300);
    record->countsShots = Chance(50);
    if (Chance(15))
        record->soundBank = -1;
    record->shotsFired = Range(0, 100);
    int systems = SystemCount(weapon);
    int last = systems > 0 ? systems - 1 : 0;
    if (weapon == kWeaponMissile) {
        record->fireSystem = Range(0, 1) * 2;
        record->reloadSystem = Range(0, last);
    } else {
        record->fireSystem = Range(0, last);
        record->reloadSystem = Range(0, last);
    }
}

const int32_t kActions[] = {
    kActionFirePrimary, kActionFirePrimaryRelease, kActionFireSecondary, kActionFireSecondaryRelease,
    kActionToggleSecondary, kActionToggleSecondaryUp, kActionToggleSecondaryDown, kActionFireGadget,
    kActionFireGadgetRelease, kActionPovReload, kActionGas, kActionChangeCamera, kNullAction,
};

void Prepare(uint32_t seed) {
    g_caseSeed = seed;
    int32_t step = ShadowStepCount + Range(0, 2000);
    ShadowStepCount = step;
    if (Chance(10))
        ShadowSimState = 3;

    // The manager and its weapons
    memcpy(g_managerBlock, g_liveManagerBlock, sizeof(g_managerBlock));
    memcpy(g_weapons, g_liveWeapons, sizeof(g_liveWeapons));
    WeaponRecord *before = g_weapons - 1;
    WeaponRecord *after = g_weapons + kWeaponCount;
    memset(before, 0x5a, sizeof(WeaponRecord));
    before->ammo = 0;
    before->flags = 0;
    memset(after, 0xa5, sizeof(WeaponRecord));
    SWeaponManager *m = g_manager;
    m->weapons = g_weapons;
    m->actionQueue = g_queue;
    for (int i = 0; i < kWeaponCount; i++)
        if (Chance(85))
            PerturbRecord(&g_weapons[i], i, step);
    m->primary = Chance(80) ? Range(0, kWeaponCount - 1) : -1;
    m->secondary = Chance(95) ? Range(0, kWeaponCount - 1) : -1;
    if (Chance(25))
        m->secondary = kWeaponGun;
    m->special = Chance(50) ? Range(0, kWeaponCount - 1) : -1;
    m->otherSecondary = Range(0, kWeaponCount - 1);
    m->lastFired = Chance(30) ? kWeaponQSmoke : Range(-1, kWeaponCount - 1);
    m->firingPrimary = Chance(40);
    m->firingSecondary = Chance(50);
    m->firingSpecial = Chance(40);
    m->handsFired = Chance(30);
    m->reloading = Chance(50);
    m->boostBlur = Chance(50) ? Range(0, 150) : 0;
    m->doubleShot = Chance(40);
    m->switchStep = step + Range(-60, 20);
    m->nuclearState = Range(0, 3);
    m->nuclearStep = step + Range(-150, 150);
    m->nuclearPosition = { Uniform(-500.0f, 500.0f), Uniform(-20.0f, 60.0f), Uniform(-500.0f, 500.0f) };
    m->nuclearScale = Uniform(0.0f, 3.0f);
    m->clickCount = Range(0, 60);
    switch (Range(0, 5)) {
    case 0: m->turretAngle = 0.25f; break;
    case 1: m->turretAngle = 0.75f; break;
    default: m->turretAngle = Uniform(-0.05f, 1.05f); break;
    }
    g_trigger = { Uniform(-300.0f, 300.0f), Uniform(-10.0f, 30.0f), Uniform(-300.0f, 300.0f) };
    m->specialTrigger = Chance(50) ? &g_trigger : NULL;
    m->specialRange = Uniform(0.0f, 600.0f);
    if (Chance(30)) {
        m->sortedCount = 0;
    } else {
        m->sortedCount = Range(1, 12);
        for (int i = 0; i < m->sortedCount; i++)
            m->sorted[i] = Range(0, kWeaponCount - 1);
    }

    // The action queue
    memset(g_queueBlock, 0, sizeof(g_queueBlock));
    ActionQueueRing &ring = g_queue->ring;
    ring.capacity = 200;
    ring.count = Chance(20) ? 0 : Range(1, 10);
    ring.head = Range(0, 199);
    ring.tail = (ring.head + ring.count + 199) % 200;
    for (int i = 0; i < ring.count; i++) {
        ActionData &a = ring.items[(ring.head + i) % 200];
        a.action = kActions[Range(0, int(sizeof(kActions) / sizeof(kActions[0])) - 1)];
        a.source = Range(0, 3);
        a.value = Uniform(0.0f, 1.0f);
    }

    // The mission manager, the launch page, the hands, the HUD, the target, the helicopters
    uint8_t *mission = ShadowMission;
    I32_AT(mission + kMissionState) = Chance(80) ? 0 : Range(1, 5);
    U8_AT(mission + kMissionAutoDrive) = Chance(40);
    PTR_AT(mission + kMissionDriveMissile) = Chance(20) ? g_missileBlock : NULL;
    I32_AT(mission + kMissionCinematic) = Chance(10);
    U8_AT(mission + kMission710) = Chance(30);
    ShadowCanNeverLose = Chance(20);
    U32_AT(kLaunchFlags) = (U32_AT(kLaunchFlags) & ~0xau) | (Chance(20) ? 2 : 0) | (Chance(30) ? 8 : 0);
    U32_AT(kLaunchCheats) = (U32_AT(kLaunchCheats) & ~2u) | (Chance(20) ? 2 : 0);
    I32_AT(kLaunchAutoSwitch) = Chance(50);
    ShadowHands = Chance(60) ? g_handsBlock : NULL;
    g_hudPresent = Chance(90);
    g_hudBlock[0x2b0] = Chance(15);
    g_targetPresent = Chance(70);
    memset(g_targetBlock, 0, sizeof(g_targetBlock));
    const Coord3 *player = (*ShadowPlayer)->GetPosition();
    g_target->position = { player->x + Uniform(-80.0f, 80.0f), player->y + Uniform(-10.0f, 10.0f),
                           player->z + Uniform(-80.0f, 80.0f) };
    g_target->ownerType = Range(0, 3);
    g_target->physics = Chance(80) ? reinterpret_cast<PhysicsObject *>(g_targetPhysics) : NULL;
    I32_AT(g_aiElementBlock + 8) = Range(0, kHelicopters);
    ShadowAIElement = g_aiElementBlock;
    for (FakeHelicopter &h : g_helicopters) {
        h.ai[0x68] = Chance(70);
        h.physics[0x139] = Chance(60);
        h.position = { player->x + Uniform(-150.0f, 150.0f), player->y + Uniform(0.0f, 40.0f),
                       player->z + Uniform(-150.0f, 150.0f) };
    }

    ShadowWeaponManager = g_manager;
    g_answer = 0;
}

// ---- the cases (arguments in globals: the runner takes plain functions)

int g_a, g_b, g_c, g_d;
float g_f;

void SideSort() {
    if (g_original)
        Orig_SortWeaponList(g_manager, 0);
    else
        g_manager->SortWeaponList();
}

void SidePrevious() {
    g_answer = g_original ? Orig_GetPrevious(g_manager, 0) : g_manager->GetPreviousSecondaryType();
}

void SideNext() {
    g_answer = g_original ? Orig_GetNext(g_manager, 0) : g_manager->GetNextSecondaryType();
}

void SideSelect() {
    if (g_original)
        Orig_SelectSecondary(g_manager, 0, g_a != 0);
    else
        g_manager->SelectSecondary(g_a != 0);
}

void SideReset() {
    if (g_original)
        Orig_Reset(g_manager, 0);
    else
        g_manager->Reset();
}

void SideSetPlayerCar() {
    RSceneObj *car = reinterpret_cast<RSceneObj *>(uintptr_t(g_a));
    if (g_original)
        Orig_SetPlayerCar(g_manager, 0, car);
    else
        g_manager->SetPlayerCar(car);
}

void SideLoad() {
    if (g_original)
        Orig_LoadWeapon(g_manager, 0, g_a, g_b, g_c, g_d);
    else
        g_manager->LoadWeapon(g_a, g_b, g_c, g_d);
}

void SideUnload() {
    if (g_original)
        Orig_UnloadWeapon(g_manager, 0, g_a);
    else
        g_manager->UnloadWeapon(g_a);
}

void SideSetSecondary() {
    if (g_original)
        Orig_SetSecondary(g_manager, 0, g_a);
    else
        g_manager->SetSecondary(g_a);
}

void SideSetSpecial() {
    const Coord3 *trigger = g_b != 0 ? &g_trigger : NULL;
    if (g_original)
        Orig_SetSpecial(g_manager, 0, g_a, trigger, g_f);
    else
        g_manager->SetSpecialWeapon(g_a, trigger, g_f);
}

void SideStartNuclear() {
    if (g_original)
        Orig_StartNuclear(g_manager, 0, &g_trigger, g_f);
    else
        g_manager->StartNuclearSequence(&g_trigger, g_f);
}

void SideAnim() {
    if (g_original)
        Orig_DoWeaponAnim(g_manager, 0, g_a, g_b, g_c);
    else
        g_manager->DoWeaponAnim(g_a, g_b, g_c);
}

void SideFromSystem() {
    g_answer = g_original ? Orig_GetWeaponFromSystem(g_manager, 0, g_a) : g_manager->GetWeaponFromSystem(g_a);
}

void SideFire() {
    if (g_original)
        Orig_FireWeapon(g_manager, 0, g_a);
    else
        g_manager->FireWeapon(g_a);
}

void SideUserReloading() {
    if (g_original)
        Orig_UserReloading(g_manager, 0);
    else
        g_manager->UserReloading();
}

void SideTurret() {
    if (g_original)
        Orig_UpdateTurret(g_manager, 0);
    else
        g_manager->UpdateTurret();
}

void SideNuclear() {
    if (g_original)
        Orig_HandleNuclear(g_manager, 0);
    else
        g_manager->HandleNuclearSequence();
}

void SideUpdate() {
    if (g_original)
        Orig_Update(g_manager, 0);
    else
        g_manager->Update();
}

SWeaponManager *const g_newManager = reinterpret_cast<SWeaponManager *>(g_newManagerBlock);

void SideConstruct() {
    SWeaponManager *made = g_original ? Orig_Construct(g_newManager, 0) : g_newManager->Construct();
    g_answer = made == g_newManager;
}

void SideDestruct() {
    if (g_original)
        Orig_Destruct(g_newManager, 0);
    else
        g_newManager->Destruct();
}

// ---- the tests

const int kCases = 300;

void TestSmall(const Snapshot &before) {
    for (int i = 0; i < kCases; i++) {
        Prepare(Next());
        RunCase("SortWeaponList", i, SideSort, before);
        Prepare(Next());
        RunCase("GetPreviousSecondaryType", i, SidePrevious, before);
        Prepare(Next());
        RunCase("GetNextSecondaryType", i, SideNext, before);
        Prepare(Next());
        g_a = Range(0, 1);
        RunCase("SelectSecondary", i, SideSelect, before);
        Prepare(Next());
        g_a = int(uintptr_t(Chance(50) ? static_cast<void *>(g_live->playerCar) : g_targetBlock));
        RunCase("SetPlayerCar", i, SideSetPlayerCar, before);
        Prepare(Next());
        g_a = Range(0, kWeaponCount - 1);
        g_b = Range(-3, 300);
        g_c = Range(-2, 10);
        g_d = Range(-2, 10);
        RunCase("LoadWeapon", i, SideLoad, before);
        Prepare(Next());
        g_a = Range(0, kWeaponCount - 1);
        RunCase("UnloadWeapon", i, SideUnload, before);
        Prepare(Next());
        g_a = Range(-1, kWeaponCount - 1);
        RunCase("SetSecondary", i, SideSetSecondary, before);
        Prepare(Next());
        g_a = Chance(30) ? kWeaponMissile : Range(-1, kWeaponCount - 1);
        g_b = Range(0, 1);
        g_f = Uniform(0.0f, 500.0f);
        RunCase("SetSpecialWeapon", i, SideSetSpecial, before);
        Prepare(Next());
        g_f = Uniform(0.0f, 4.0f);
        RunCase("StartNuclearSequence", i, SideStartNuclear, before);
        Prepare(Next());
        g_a = Range(0, kWeaponCount - 1);
        g_b = Range(0, 12);
        g_c = Range(0, SystemCount(g_a) > 0 ? SystemCount(g_a) - 1 : 0);
        RunCase("DoWeaponAnim", i, SideAnim, before);
        Prepare(Next());
        g_a = Range(-2, 80);
        RunCase("GetWeaponFromSystem", i, SideFromSystem, before);
        Prepare(Next());
        RunCase("UserReloading", i, SideUserReloading, before);
        Prepare(Next());
        RunCase("UpdateTurret", i, SideTurret, before);
    }
    for (int id = 0; id < 80; id++) {
        Prepare(Next());
        g_a = id;
        RunCase("GetWeaponFromSystem (every id)", id, SideFromSystem, before);
    }
    for (int i = 0; i < 60; i++) {
        Prepare(Next());
        ShadowStepsPerSecond = Range(20, 120);
        RunCase("Reset", i, SideReset, before);
    }
}

void TestFire(const Snapshot &before) {
    for (int i = 0; i < kCases * 4; i++) {
        Prepare(Next());
        g_a = i % (kWeaponCount + 1) - 1;
        RunCase("FireWeapon", i, SideFire, before);
    }
}

void TestNuclearAndUpdate(const Snapshot &before) {
    int keep = g_hookCount;
    InstallSoundHooks();
    for (int i = 0; i < kCases; i++) {
        Prepare(Next());
        g_manager->nuclearState = i % 3;
        RunCase("HandleNuclearSequence", i, SideNuclear, before);
    }
    for (int i = 0; i < kCases * 3; i++) {
        Prepare(Next());
        RunCase("Update", i, SideUpdate, before);
    }
    HooksRemove(keep);
}

void TestConstruct(const Snapshot &before) {
    int keep = g_hookCount;
    InstallAllocationHooks();
    for (int i = 0; i < 20; i++) {
        Prepare(Next());
        memset(g_newManagerBlock, 0xcd, sizeof(g_newManagerBlock));
        memset(g_newWeaponsBlock, 0xcd, sizeof(g_newWeaponsBlock));
        memset(g_newQueueBlock, 0xcd, sizeof(g_newQueueBlock));
        ShadowStepsPerSecond = Range(20, 120);
        RunCase("Construct", i, SideConstruct, before);
        Prepare(Next());
        memcpy(g_newManagerBlock, g_managerBlock, sizeof(g_newManagerBlock));
        g_newManager->weapons = reinterpret_cast<WeaponRecord *>(g_newWeaponsBlock);
        g_newManager->actionQueue = Chance(80) ? reinterpret_cast<ActionQueue *>(g_newQueueBlock) : NULL;
        RunCase("Destruct", i, SideDestruct, before);
    }
    HooksRemove(keep);
}

void BuildRegions() {
    g_regions.clear();
    Add(g_managerBlock, sizeof(g_managerBlock), "the manager");
    Add(g_weaponsBlock, sizeof(g_weaponsBlock), "the weapons");
    Add(g_queueBlock, sizeof(g_queueBlock), "the action queue");
    Add(g_soundBlock, sizeof(g_soundBlock), "the one-shot sound");
    Add(g_missileBlock, sizeof(g_missileBlock), "the steered missile");
    Add(g_targetBlock, sizeof(g_targetBlock), "the target");
    Add(g_hudBlock, sizeof(g_hudBlock), "the HUD");
    Add(g_aiElementBlock, sizeof(g_aiElementBlock), "the AI element controller");
    Add(g_helicopters, sizeof(g_helicopters), "the helicopters");
    Add(&g_trigger, sizeof(g_trigger), "the trigger point");
    Add(&g_answer, sizeof(g_answer), "the answer");
    Add(g_newManagerBlock, sizeof(g_newManagerBlock), "the new manager");
    Add(g_newWeaponsBlock, sizeof(g_newWeaponsBlock), "the new weapons");
    Add(g_newQueueBlock, sizeof(g_newQueueBlock), "the new action queue");
    AddGame(0x0023923c, 4, "WeaponManager");
    AddGame(0x00234e24, 4, "the simulation's state");
    AddGame(0x00234e2c, 4, "the steps a second");
    AddGame(0x00234e34, 4, "the simulation's step");
    AddGame(0x00239218, 1, "gPlayerCanNeverLose");
    AddGame(kLaunchFlags, 8, "the launch flags");
    AddGame(kLaunchAutoSwitch, 4, "the auto-switch option");
    AddGame(0x001dda90, 4, "the hands");
    AddGame(0x001ddee4, 4, "the AI element controller pointer");
    AddGame(kBondPressed, 2, "the fire and select bytes");
    AddGame(kWeaponTable, sizeof(WeaponRecord) * kWeaponCount, "the weapon table");
    Add(ShadowMission + kMissionState, kMission710 + 4 - kMissionState, "the mission manager");
    Add(ShadowColorize, 0x60, "the colorizer");
}

}  // namespace

void GameWeaponsShadowA_Run(void) {
    char value[16] = "";
    DWORD length = GetEnvironmentVariableA("NIGHTFIRE_GAMEWEAPONSSHADOWA", value, sizeof(value));
    if (length == 0 || length >= sizeof(value) || atoi(value) == 0)
        return;
    SWeaponManager *live = ShadowWeaponManager;
    if (live == NULL || live->weapons == NULL || live->playerCar == NULL || ShadowMission == NULL ||
        ShadowPlayer == NULL || *ShadowPlayer == NULL || ShadowColorize == NULL) {
        printf("[gameweaponsshadowa] no weapon manager, player car or mission yet - skipped\n");
        fflush(stdout);
        return;
    }
    FpControlGet(&g_x87, &g_sse);
    memcpy(g_liveManagerBlock, live, sizeof(g_liveManagerBlock));
    memcpy(g_liveWeapons, live->weapons, sizeof(g_liveWeapons));
    g_liveHandle = live->playerCar->animHandle;
    g_playerAttributes = &(*ShadowPlayer)->attributes;

    InstallBaseHooks();
    if (g_hookFailures != 0)
        printf("[gameweaponsshadowa] %d hooks could not be installed\n", g_hookFailures);
    BuildRegions();
    Snapshot before;
    Capture(&before);

    TestSmall(before);
    TestFire(before);
    TestNuclearAndUpdate(before);
    TestConstruct(before);

    Restore(before);
    HooksRemove(0);
    ResetFpu();
    printf("[gameweaponsshadowa] weapon manager (selection, loading, firing, reloading, turret, nuclear sequence, "
           "update) vs originals: %ld cases, %ld checks, %ld differ%s\n",
           g_cases, g_checks, g_differ, g_faults != 0 ? " (with faults)" : "");
    if (g_faults != 0)
        printf("[gameweaponsshadowa]   %ld calls faulted\n", g_faults);
    fflush(stdout);
}
