#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "GameWeaponsShadowC.h"
#include "OwnedGlobals.h"
#include "FpControl.h"

#include <windows.h>
#include <float.h>
#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <initializer_list>
#include <vector>

#include "../engine/PhysicsUtil.h"
#include "../engine/StaticInit.h"
#include "../engine/UMemory.hpp"
#include "../game/Grenade.h"
#include "../game/Shell.h"
#include "../game/Vehicle.h"
#include "../game/WeaponEffects.h"
#include "../physics/RigidBody.h"
#include "../physics/SimpleRigidBody.h"
#include "../world/CollisionManager.h"
#include "../world/WorldPos.h"
#include "../../common/xbeOriginal.h"
#include "../../helpers.h"

// ---------------------------------------------------------------------------------------------------------------
// A shadow test of game.weapons' package C, run once from the first simulation tick when NIGHTFIRE_GAMEWEAPONSSHADOWC
// is set. Each case runs the ORIGINAL (every entry of the package swapped back together) and then the PORT from the
// same state, and compares what they leave - the synthetic objects and bodies, the shell under test, the faked
// sound, the game's globals both use, the game's copies of the tables we own (the original's side) against ours -
// and a log of every call out:
//
//   - RayShell's trigger queue (the five accessors) over random counts and indices;
//   - the tracers: StaticStoreTracer (full, negative kinds, kind 0's start offset about the 2-unit limit),
//     ClearTracers, DrawTracers (ages about the 60 steps, the streak past the end or not, sources whose effect is
//     located, the hands' flag set or not, perturbed step lengths) and Reset;
//   - the flashes: AddFlash (humans, sentries, others, the player, unknown signatures, a full list, repeats) and
//     DrawFlashes;
//   - ConsiderVictim, CollideWithObjects, Fire, DamagePhysicsObject and AddNewRayShellForTriggerCheck on a
//     synthetic shell among synthetic objects - rigid bodies with boxes, simple bodies (skipped ones too), humans
//     with and without a character and hit shape, owned by the shooter or not - with the shooter the player or not;
//   - CollideWithWorld against the live world round the player's car (down, across, up; ranges, victims nearer
//     than the world or not), the cheat flag and the explosion step set or not;
//   - RayShell::RayShell, the whole shot on a block of the game's heap (1 to 4 passes, spread), and the scalar
//     deleting destructor;
//   - Shell::ApplyWorldDamage on the live world's hits (from the setup's own queries) with every flag, the damage
//     about each stimulus limit; ApplyWorldEffects with every face type, shell type, damage answer and object kind,
//     straight up or not, the sparks' static set or not;
//   - Shell's constructor (both speeds, a streak kind or not), destructor, scalar deleting destructor and Simulate
//     (the lifetime out, a touched body owned or not, the world under the step, the explosion size about 1);
//   - Grenade's constructor (its render object from the game's heap, compared by value, then freed), destructor,
//     scalar deleting destructor, Render and Simulate (in the air, on the ground with and without a face, bouncing
//     once and twice, the fuse out, before the arming step).
//
// The Simulation is faked: its body, signature, ordered-body and player queries answer the synthetic objects (by
// slots of their own), its explosion, deletion and slot calls are logged; the synthetic objects' vtables hold fakes
// for ApplyDamage, AddDamageByPlayer and GetPhysics. Fakes (5-byte jumps over the code both sides reach): the
// originals GFX::LookupReference and TriggerIncidental, AIZoneController::Get and RegisterZone, AOneShotSound's
// constructor and WorldCollisionInfo's constructor; ours RBulletStreak::Add,
// RLightManager::AddPositionalLight, RSceneObj::LocateFX, Construct and Load, RScorchWorld::ScorchWorld, the
// handle's ProcessStimuli, the particle manager's CreateParticles and FindSystemViaTag, WCollisionMgr::CheckHitWindow,
// ABaseSound::OperatorNew, AIndex::Lookup, Util_GaussRandom, AttributeSet's Construct and Destruct, and
// RMissileStreak's Update and Draw. The player's car, hands, weapon manager and weapon bank are pointed at synthetic
// ones for the run.
//
// A mutation the test sees: Fire's second-victim case damaging at the second victim's impact point instead of the
// first's (ApplyWorldEffects' position in the log differs), or ApplyWorldEffects building its frame from x at a dot
// of 0.9 or less (the incidental effect's orientation differs).
// ---------------------------------------------------------------------------------------------------------------

namespace {

// ---- the originals

const unsigned kEntries[] = {
    0x0005d4f0, 0x0005d550, 0x0005d5c0, 0x0005d7a0, 0x0005d7d0, 0x00071930, 0x00071940, 0x00071960, 0x00071970,
    0x00071980, 0x000719a0, 0x00071a60, 0x00071c80, 0x00071d40, 0x00071d80, 0x00071e40, 0x00071fb0, 0x000720d0,
    0x00072160, 0x00072190, 0x00072790, 0x00072ba0, 0x00072d60, 0x00072e90, 0x00074660, 0x00074670, 0x00074770,
    0x00074980, 0x000749b0, 0x00074e80,
};

#define Orig_GrenadeDestruct ((void (__fastcall *)(Grenade *, int))0x0005d4f0)
#define Orig_GrenadeRender ((void (__fastcall *)(Grenade *, int, bool))0x0005d550)
#define Orig_GrenadeConstruct ((Grenade *(__fastcall *)(Grenade *, int, int, const Coord3 *, const Coord3 *, float, uint32_t))0x0005d5c0)
#define Orig_GrenadeDelete ((Grenade *(__fastcall *)(Grenade *, int, unsigned))0x0005d7a0)
#define Orig_GrenadeSimulate ((void (__fastcall *)(Grenade *, int))0x0005d7d0)
#define Orig_GetNumActive ((int (*)(void))0x00071930)
#define Orig_GetActive ((ActiveRayShell *(*)(int))0x00071940)
#define Orig_ClearActive ((void (*)(void))0x00071960)
#define Orig_SetHitting ((void (*)(int))0x00071970)
#define Orig_GetHitting ((ActiveRayShell *(*)(void))0x00071980)
#define Orig_ClearTracers ((void (*)(uint32_t))0x000719a0)
#define Orig_DrawTracers ((void (*)(void))0x00071a60)
#define Orig_DrawFlashes ((void (*)(void))0x00071c80)
#define Orig_RayReset ((void (*)(void))0x00071d40)
#define Orig_AddTrigger ((void (__fastcall *)(RayShell *, int, float))0x00071d80)
#define Orig_ConsiderVictim ((void (__fastcall *)(RayShell *, int, uint32_t, const Coord3 *, const Coord3 *, float))0x00071e40)
#define Orig_StoreTracer ((void (*)(const Coord3 *, const Coord3 *, int, RSceneObj *, const ArticleEffect *))0x00071fb0)
#define Orig_AddFlash ((void (__fastcall *)(RayShell *, int, uint32_t, const Coord3 *))0x000720d0)
#define Orig_RayDelete ((RayShell *(__fastcall *)(RayShell *, int, unsigned))0x00072160)
#define Orig_CollideObjects ((uint8_t (__fastcall *)(RayShell *, int))0x00072190)
#define Orig_CollideWorld ((uint8_t (__fastcall *)(RayShell *, int, float *))0x00072790)
#define Orig_DamageObject ((void (__fastcall *)(RayShell *, int, uint32_t, const Coord3 *, const Coord3 *))0x00072ba0)
#define Orig_Fire ((void (__fastcall *)(RayShell *, int, float *))0x00072d60)
#define Orig_RayConstruct ((RayShell *(__fastcall *)(RayShell *, int, int, uint32_t, const Coord3 *, const Coord3 *, const Coord3 *, RSceneObj *, const ArticleEffect *, float, float, int, float, int))0x00072e90)
#define Orig_ShellDestruct ((void (__fastcall *)(Shell *, int))0x00074660)
#define Orig_WorldDamage ((void (*)(float, WWorldPos *, float, int, const WorldCollisionInfo *))0x00074670)
#define Orig_ShellConstruct ((Shell *(__fastcall *)(Shell *, int, int, const Coord3 *, const Coord3 *, uint32_t, float, float, int))0x00074770)
#define Orig_ShellDelete ((Shell *(__fastcall *)(Shell *, int, unsigned))0x00074980)
#define Orig_WorldEffects ((void (*)(const Coord3 *, const Coord4 *, int, int, const uint8_t *, int, PhysicsObject *))0x000749b0)
#define Orig_ShellSimulate ((void (__fastcall *)(Shell *, int))0x00074e80)

// The originals faked (the jump written over their own entry, or over our port behind it)
const uint32_t kGetRigidBody = 0x000b2700;
const uint32_t kGetSimpleRigidBody = 0x000b2730;
const uint32_t kFindSignature = 0x000b27d0;
const uint32_t kGetPlayerObject = 0x000b2d30;
const uint32_t kGetOrderedBody = 0x000b28d0;
const uint32_t kDeleteObject = 0x000b4740;
const uint32_t kSpawnExplosion = 0x000b4cd0;
const uint32_t kAssignSlot = 0x000b2630;
const uint32_t kReleaseSlot = 0x000b2980;
const uint32_t kLookupReference = 0x000d35f0;
const uint32_t kTriggerIncidental = 0x000d3520;
const uint32_t kZoneGet = 0x00036a10;
const uint32_t kRegisterZone = 0x00036bb0;
const uint32_t kInfoConstruct = 0x0001d9f0;
// Ours faked (the jump at the entry followed to our code)
const uint32_t kOneShotConstruct = 0x000477d0;
const uint32_t kBulletAdd = 0x0009a3b0;
const uint32_t kAddLight = 0x0007e440;
const uint32_t kLocateFX = 0x0008eca0;
const uint32_t kScorchWorld = 0x000a50c0;
const uint32_t kStimuli = 0x00077e00;
const uint32_t kCreateParticles = 0x000a1c60;
const uint32_t kFindSystem = 0x000a2100;
const uint32_t kCheckHitWindow = 0x000c4d70;
const uint32_t kOperatorNew = 0x0011c910;
const uint32_t kIndexLookup = 0x00126d40;
const uint32_t kGauss = 0x000bd130;
const uint32_t kAttrConstruct = 0x00058f00;
const uint32_t kAttrDestruct = 0x00057a40;
const uint32_t kSceneLoad = 0x00090a60;
const uint32_t kSceneConstruct = 0x0008eec0;
const uint32_t kStreakUpdate = 0x000a0ea0;
const uint32_t kStreakDraw = 0x000a1330;

// ---- the game's state

#define ShadowRandomSeed U32_AT(OwnedGlobals_Ours(0x001c45c4))
#define ShadowRandomMultiplier U32_AT(OwnedGlobals_Ours(0x001c45c8))
#define ShadowSimStepsPerSecond I32_AT(OwnedGlobals_Ours(0x00234e2c))
#define ShadowSimTimeStep FLOAT_AT(OwnedGlobals_Ours(0x00234e30))
#define ShadowSimStepCount I32_AT(OwnedGlobals_Ours(0x00234e34))
#define ShadowPlayerVehicles (*(PhysicsObject ***)OwnedGlobals_Ours(0x00234e40))
#define ShadowPlayerHands (*(uint8_t **)OwnedGlobals_Ours(0x001dda90))
#define ShadowLaunchFlags U32_AT(OwnedGlobals_Ours(0x00244530))
#define ShadowWeaponBank (*(void **)0x001d83a8)
#define ShadowWeaponManager (*(void **)OwnedGlobals_Ours(0x0023923c))
#define ShadowSimRandom (*(uint8_t **)OwnedGlobals_Ours(0x00233ff0))
#define ShadowRigidSigs ((uint32_t *)OwnedGlobals_Ours(0x00234020))
#define ShadowSimpleSigs ((uint32_t *)OwnedGlobals_Ours(0x00234120))
#define ShadowSimpleHits ((uint64_t (*)[3])OwnedGlobals_Ours(0x00234520))

// ---- results

bool g_original;
long g_cases, g_checks, g_differ, g_faults;
int g_reported;
unsigned g_x87, g_sse;

void Report(const char *what, long index, const char *detail) {
    g_differ++;
    if (g_reported++ < 10)
        printf("[gameweaponsshadowc]   %s #%ld: %s\n", what, index, detail);
}

void CompareBytes(const char *what, long index, const void *a, const void *b, size_t bytes) {
    g_checks++;
    size_t at = OwnedGlobals_FirstDifference(a, b, bytes);   // a vtable or callback: ours, or the game's
    if (at == bytes)
        return;
    const uint8_t *x = static_cast<const uint8_t *>(a), *y = static_cast<const uint8_t *>(b);
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

uint32_t g_seed = 0x3c51e0a9;

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

Coord3 RandomUnit3() {
    for (;;) {
        Coord3 v = { Uniform(-1.0f, 1.0f), Uniform(-1.0f, 1.0f), Uniform(-1.0f, 1.0f) };
        float length = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
        if (length > 0.1f) {
            v.x /= length;
            v.y /= length;
            v.z /= length;
            return v;
        }
    }
}

Coord3 Offset(const Coord3 &from, float spread, float lift) {
    Coord3 v = { from.x + Uniform(-spread, spread), from.y + lift + Uniform(-spread, spread) * 0.25f,
                 from.z + Uniform(-spread, spread) };
    return v;
}

Coord3 Towards(const Coord3 &from, const Coord3 &to, float noise) {
    Coord3 d = { to.x - from.x + Uniform(-noise, noise), to.y - from.y + Uniform(-noise, noise),
                 to.z - from.z + Uniform(-noise, noise) };
    float length = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
    if (length < 0.01f)
        return RandomUnit3();
    d.x /= length;
    d.y /= length;
    d.z /= length;
    return d;
}

// ---- the regions a case may change

struct Region {
    uint8_t *game;
    uint8_t *ours;          // NULL: both sides use the game's address
    uint32_t size;
    bool compare;
    const char *name;
};

std::vector<Region> g_regions;

void AddShared(const void *game, uint32_t size, const char *name, bool compare = true) {
    if (game != NULL)
        g_regions.push_back({ (uint8_t *)game, NULL, size, compare, name });
}

void AddOwned(void *ours, uint32_t game, uint32_t size, const char *name) {
    g_regions.push_back({ reinterpret_cast<uint8_t *>(uintptr_t(game)), static_cast<uint8_t *>(ours), size, true,
                          name });
}

// An address in one of our globals as the same byte of the game's copy
uint32_t Canon(const void *pointer) {
    const uint8_t *p = static_cast<const uint8_t *>(pointer);
    for (const Region &r : g_regions)
        if (r.ours != NULL && p >= r.ours && p < r.ours + r.size)
            return uint32_t(uintptr_t(r.game + (p - r.ours)));
    return uint32_t(uintptr_t(pointer));
}

struct Snapshot {
    std::vector<uint8_t> bytes;
};

void Capture(Snapshot *s) {
    s->bytes.clear();
    for (const Region &r : g_regions) {
        s->bytes.insert(s->bytes.end(), r.game, r.game + r.size);
        if (r.ours != NULL)
            s->bytes.insert(s->bytes.end(), r.ours, r.ours + r.size);
    }
}

void Restore(const Snapshot &s) {
    size_t at = 0;
    for (const Region &r : g_regions) {
        memcpy(r.game, &s.bytes[at], r.size);
        at += r.size;
        if (r.ours != NULL) {
            memcpy(r.ours, &s.bytes[at], r.size);
            at += r.size;
        }
    }
}

void Lend() {
    for (const Region &r : g_regions)
        if (r.ours != NULL)
            memcpy(r.game, r.ours, r.size);
}

// The active ray shells hold no pointers; a tracer source holds a scene object and an effect (both synthetic, the
// same on both sides). Nothing in the owned tables needs translating before the byte compare.
void CompareResults(const char *what, long index, const Snapshot &original, const Snapshot &port) {
    size_t at = 0;
    for (const Region &r : g_regions) {
        const uint8_t *a = &original.bytes[at];
        const uint8_t *b = r.ours != NULL ? &port.bytes[at + r.size] : &port.bytes[at];
        at += r.ours != NULL ? 2 * r.size : r.size;
        if (!r.compare)
            continue;
        char label[96];
        snprintf(label, sizeof(label), "%s: %s", what, r.name);
        CompareBytes(label, index, a, b, r.size);
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

void LogBytes(const void *bytes, uint32_t size) {
    if (bytes == NULL) {
        Log(0xdeadbeef);
        return;
    }
    const uint8_t *p = static_cast<const uint8_t *>(bytes);
    for (uint32_t i = 0; i < size; i += 4) {
        uint32_t word = 0;
        memcpy(&word, p + i, size - i < 4 ? size - i : 4);
        Log(word);
    }
}

void LogXyz(const void *v) {
    if (v == NULL) {
        Log(0xdeadbeef);
        return;
    }
    LogBytes(v, 12);
}

uint32_t Hash(const char *text) {
    if (text == NULL)
        return 0;
    uint32_t hash = 2166136261u;
    for (const char *c = text; *c != '\0'; c++)
        hash = (hash ^ uint8_t(*c)) * 16777619u;
    return hash;
}

void CompareLogs(const char *what, long index) {
    g_checks++;
    for (std::vector<uint32_t> &log : g_logs)   // vtables and callbacks alike on either side
        OwnedGlobals_CanonicalWords(log.data(), log.size() * 4);
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

// ---- the synthetic world

constexpr int kObjects = 6;
constexpr int kRigidBase = 40;          // the synthetic objects' rigid slots
constexpr int kSimpleBase = 60;         // ... and simple ones
constexpr int kShellSlot = 90;          // the shell's and the grenade's own
constexpr int kGrenadeSlot = 91;
constexpr uint32_t kSigBase = 0x51a0;
constexpr uint32_t kShellSig = 0x52a0;
constexpr uint32_t kGrenadeSig = 0x52a1;
constexpr uint32_t kDeadSig = 0x5dead;

struct alignas(16) ObjectBlock {
    uint8_t bytes[0x100];
};

struct alignas(16) CharacterBlock {     // an AI character: transform +0x10, centre +0x50, hit shape +0xb4
    uint8_t bytes[0xc0];
};

struct HitShape {
    uint8_t unknown00[0xc];
    float radius, lift, stretch;
};

alignas(16) ObjectBlock g_objects[kObjects];
alignas(16) SimpleRigidBody g_simple[kObjects];
alignas(16) RigidBody g_rigid[kObjects];
alignas(16) RigidBodyInfo g_info[kObjects];
alignas(16) CharacterBlock g_characters[kObjects];
HitShape g_shapes[kObjects];
int g_damageAnswers[kObjects];
int g_objectCount;
PhysicsObject *g_player;

alignas(16) SimpleRigidBody g_shellBody, g_grenadeBody, g_spareSimple;
alignas(16) RigidBody g_spareRigid;
alignas(16) uint8_t g_shellBlock[sizeof(Shell)];
alignas(16) uint8_t g_grenadeBlock[sizeof(Grenade)];
alignas(16) uint8_t g_rayBlock[sizeof(RayShell)];
Shell *const g_shell = reinterpret_cast<Shell *>(g_shellBlock);
Grenade *const g_grenade = reinterpret_cast<Grenade *>(g_grenadeBlock);
RayShell *const g_ray = reinterpret_cast<RayShell *>(g_rayBlock);

alignas(16) uint8_t g_soundBlock[0x120];
alignas(16) CarPhysics g_carPhysics;
alignas(16) ObjectBlock g_playerCar;
PhysicsObject *g_playerCars[2];
uint8_t g_hands[0x160];
uint8_t g_bank[0x20];
struct FakeWeaponManager {
    uint8_t unknown00[0x10];
    WeaponRecord *slots;
} g_weaponManager;
WeaponRecord g_weapons[32];
uint8_t g_sceneObjects[2][0x40];        // tracer sources (LocateFX is faked)
uint32_t g_zoneController;
uint32_t g_sparksData;                  // the particle system FindSystemViaTag answers (by address)
bool g_sparksFound;
void *g_vtable[96];

Coord3 g_base;                          // the player's car at the first tick
std::vector<WorldCollisionInfo> g_hits; // the live world's, from the setup's queries

PhysicsObject *Obj(int k) {
    return reinterpret_cast<PhysicsObject *>(g_objects[k].bytes);
}

int ObjectIndex(const void *object) {
    for (int k = 0; k < kObjects; k++)
        if (object == Obj(k))
            return k;
    return -1;
}

uint32_t ObjectSig(int k) {
    return kSigBase + uint32_t(k);
}

int OrderedIndex(int k) {
    return (Obj(k)->flags & PhysicsObject::kSimpleBody) ? 64 + kSimpleBase + k : kRigidBase + k;
}

// ---- the Simulation's fakes

RigidBody *__fastcall FakeGetRigidBody(void *, int, int slot) {
    if (slot >= kRigidBase && slot < kRigidBase + kObjects)
        return &g_rigid[slot - kRigidBase];
    Log(0x52420000 | uint32_t(slot & 0xffff));
    return &g_spareRigid;
}

SimpleRigidBody *__fastcall FakeGetSimpleRigidBody(void *, int, int slot) {
    if (slot >= kSimpleBase && slot < kSimpleBase + kObjects)
        return &g_simple[slot - kSimpleBase];
    if (slot == kShellSlot)
        return &g_shellBody;
    if (slot == kGrenadeSlot)
        return &g_grenadeBody;
    Log(0x53420000 | uint32_t(slot & 0xffff));
    return &g_spareSimple;
}

PhysicsObject *__fastcall FakeFind(void *, int, uint32_t sig) {
    if (sig >= kSigBase && sig < kSigBase + uint32_t(g_objectCount))
        return Obj(int(sig - kSigBase));
    if (sig == kShellSig)
        return g_shell;
    if (sig == kGrenadeSig)
        return g_grenade;
    return NULL;
}

PhysicsObject *__fastcall FakeGetPlayer(void *, int) {
    return g_player;
}

PhysicsObject *__fastcall FakeGetOrderedBody(void *, int, int index) {
    for (int k = 0; k < g_objectCount; k++)
        if (OrderedIndex(k) == index)
            return Obj(k);
    return NULL;
}

void __fastcall FakeDeleteObject(void *, int, PhysicsObject *object) {
    Log(0x44454c00);
    Log(uint32_t(uintptr_t(object)));
}

void __fastcall FakeSpawnExplosion(void *, int, int type, int kind, const Coord3 *direction, const Coord3 *position,
                                   float size, int unknown6, PhysicsObject *directHit, const uint32_t *ownerSig) {
    Log(0x45585000);
    Log(uint32_t(type));
    Log(uint32_t(kind));
    LogXyz(direction);
    LogXyz(position);
    LogFloat(size);
    Log(uint32_t(unknown6));
    Log(uint32_t(uintptr_t(directHit)));
    Log(uint32_t(uintptr_t(ownerSig)));
}

int __fastcall FakeAssignSlot(void *, int, PhysicsObject *object, int simple) {
    Log(0x41534700);
    Log(uint32_t(simple));
    return object == g_grenade ? kGrenadeSlot : kShellSlot;
}

void __fastcall FakeReleaseSlot(void *, int, int slot, int simple) {
    Log(0x52534c00);
    Log(uint32_t(slot));
    Log(uint32_t(simple));
}

// ---- the other originals' fakes

void *FakeLookupReference(const char *name) {
    Log(0x4c4b5000);
    Log(Hash(name));
    return reinterpret_cast<void *>(uintptr_t(Hash(name) | 1));
}

void FakeTriggerIncidental(void *reference, const Coord3 *position, const Coord4 *orientation, const Coord3 *unknown4,
                           const Coord4 *unknown5, int unknown6, int unknown7, int unknown8) {
    Log(0x54524700);
    Log(uint32_t(uintptr_t(reference)));
    LogXyz(position);
    LogBytes(orientation, 16);
    LogXyz(unknown4);
    LogBytes(unknown5, 16);
    Log(uint32_t(unknown6));
    Log(uint32_t(unknown7));
    Log(uint32_t(unknown8));
}

void *FakeZoneGet() {
    return &g_zoneController;
}

void __fastcall FakeRegisterZone(void *controller, int, int zone, const Coord3 *position) {
    Log(0x5a4f4e00);
    Log(controller == &g_zoneController);
    Log(uint32_t(zone));
    LogXyz(position);
}

void *__fastcall FakeOneShot(void *self, int, int bank, int patch, const Coord3 *position, float volume,
                             const char *mix, bool loop) {
    Log(0x4f4e4500);
    Log(self == g_soundBlock);
    Log(uint32_t(bank));
    Log(uint32_t(patch));
    LogXyz(position);
    LogFloat(volume);
    Log(Hash(mix));
    Log(loop);
    return self;
}

WorldCollisionInfo *__fastcall FakeInfoConstruct(WorldCollisionInfo *info, int) {
    memset(info, 0, sizeof(*info));
    info->point.w = 1.0f;
    return info;
}

// ---- our code's fakes

void FakeBulletAdd(const Coord3 *position, const Coord3 *direction, int kind) {
    Log(0x42554c00);
    LogXyz(position);
    LogXyz(direction);
    Log(uint32_t(kind));
}

void __fastcall FakeAddLight(void *, int, const Coord4 *position, const Coord4 *colour) {
    Log(0x4c495400);
    LogBytes(position, 16);
    LogBytes(colour, 16);
}

void __fastcall FakeLocateFX(RSceneObj *object, int, const ArticleEffect *effect, MATRIX4 *transform, bool world) {
    Log(0x4c4f4300);
    Log(uint32_t(uintptr_t(object)));
    Log(uint32_t(uintptr_t(effect)));
    Log(world);
    uint32_t key = uint32_t(uintptr_t(object)) * 31u + uint32_t(uintptr_t(effect));
    for (int i = 0; i < 16; i++)
        (&transform->mtx[0][0])[i] = float(int((key >> (i % 24)) & 0xff) - 128) * 0.125f;
}

void FakeScorchWorld(WWorldPos *place, const Coord4 *point, const Coord4 *normal, float size, int type) {
    Log(0x53434f00);
    // Words WWorldPos::Construct leaves as the caller's stack had them (CollideWithWorld's place, on both sides):
    // the first two corners' fourth words and the padding after `valid`
    WWorldPos copy = *place;
    copy.face.corner[0].count = 0;
    copy.face.corner[1].flags = 0;
    memset(copy.unknown31, 0, sizeof(copy.unknown31));
    LogBytes(&copy, sizeof(WWorldPos));
    LogBytes(point, 16);
    LogBytes(normal, 16);
    LogFloat(size);
    Log(uint32_t(type));
}

void __fastcall FakeStimuli(void *handle, int, uint8_t stimulus, uint32_t tick, int mode) {
    Log(0x53544900);
    Log(uint32_t(uintptr_t(handle)));
    Log(stimulus);
    Log(tick);
    Log(uint32_t(mode));
}

void __fastcall FakeCreateParticles(void *, int, RParticleSystemData *data, const Coord3 *position,
                                    const Coord3 *direction, int count) {
    Log(0x50415200);
    Log(uint32_t(uintptr_t(data)));
    LogXyz(position);
    LogXyz(direction);
    Log(uint32_t(count));
}

RParticleSystemData *__fastcall FakeFindSystem(void *, int, uint32_t tag) {
    Log(0x46534400);
    Log(tag);
    return g_sparksFound ? reinterpret_cast<RParticleSystemData *>(&g_sparksData) : NULL;
}

bool __fastcall FakeCheckHitWindow(void *, int, WorldCollisionInfo *info, bool create, int kind) {
    Log(0x57494e00);
    LogBytes(&info->point, 16);
    Log(info->hitType);
    Log(create);
    Log(uint32_t(kind));
    return false;
}

void *FakeOperatorNew(unsigned size, const char *name) {
    Log(0x4e455700);
    Log(size);
    Log(Hash(name));
    return g_soundBlock;
}

int __fastcall FakeIndexLookup(void *, int, const char *name) {
    return int(Hash(name) & 0xffff);
}

uint32_t g_gaussIndex;
double FakeGauss() {
    g_gaussIndex++;
    Log(0x47415500 | (g_gaussIndex & 0xff));
    return double(int((g_gaussIndex * 2654435761u) >> 20) - 2048) / 1024.0;
}

void *__fastcall FakeAttrConstruct(uint32_t *set, int, const char *className, const char *name) {
    Log(0x41545400);
    Log(Hash(className));
    Log(Hash(name));
    *set = 0x41545452;
    return set;
}

void __fastcall FakeAttrDestruct(uint32_t *set, int) {
    Log(0x41544400);
    Log(*set);
}

void __fastcall FakeSceneLoad(RSceneObj *, int, const char *directory, const char *file, const char *label,
                              uint32_t article) {
    Log(0x4c4f4100);
    Log(Hash(directory));
    Log(Hash(file));
    Log(Hash(label));
    Log(article);
}

RSceneObj *__fastcall FakeSceneConstruct(RSceneObj *object, int, CARP::Instance *instance) {
    Log(0x53434e00);
    Log(uint32_t(reinterpret_cast<uint8_t *>(instance) - reinterpret_cast<uint8_t *>(object)));
    LogBytes(instance, 0x40);
    memset(object, 0, 0x40);
    return object;
}

void __fastcall FakeStreakUpdate(RMissileStreak *streak, int, const Coord4 *position) {
    Log(0x53545500);
    Log(uint32_t(uintptr_t(streak)));
    LogBytes(position, 16);
}

void __fastcall FakeStreakDraw(RMissileStreak *streak, int) {
    Log(0x53544400);
    Log(uint32_t(uintptr_t(streak)));
}

// ---- the synthetic objects' vtable

int __fastcall FakeApplyDamage(PhysicsObject *object, int, const void *unknown1, const void *unknown2, float amount,
                               float unknown4, int kind, const uint32_t *sourceSig) {
    Log(0x444d4700);
    Log(uint32_t(uintptr_t(object)));
    LogXyz(unknown1);
    LogXyz(unknown2);
    LogFloat(amount);
    LogFloat(unknown4);
    Log(uint32_t(kind));
    Log(sourceSig != NULL ? *sourceSig : 0xdeadbeef);
    int k = ObjectIndex(object);
    return k >= 0 ? g_damageAnswers[k] : 0x20;
}

void __fastcall FakeAddDamageByPlayer(PhysicsObject *object, int, float damage) {
    Log(0x42595000);
    Log(uint32_t(uintptr_t(object)));
    LogFloat(damage);
}

CarPhysics *__fastcall FakeGetPhysics(PhysicsObject *object, int) {
    Log(0x50485900);
    Log(uint32_t(uintptr_t(object)));
    return &g_carPhysics;
}

void FakeTrap() {
    Log(0x54524150);
}

// ---- 5-byte jumps

struct Hook {
    uint32_t at;
    uint8_t saved[5];
    bool on;
};

uint32_t JumpTarget(uint32_t at) {
    const uint8_t *code = reinterpret_cast<const uint8_t *>(uintptr_t(at));
    if (code[0] != 0xe9)
        return 0;
    int32_t relative;
    memcpy(&relative, code + 1, 4);
    return at + 5 + uint32_t(relative);
}

bool WriteJump(Hook *hook, uint32_t at, const void *to) {
    hook->on = false;
    if (at == 0)
        return false;
    hook->at = at;
    DWORD old;
    if (!VirtualProtect((void *)uintptr_t(at), 5, PAGE_EXECUTE_READWRITE, &old))
        return false;
    memcpy(hook->saved, (void *)uintptr_t(at), 5);
    uint8_t jump[5] = { 0xe9 };
    int32_t relative = int32_t(uint32_t(uintptr_t(to)) - (at + 5));
    memcpy(jump + 1, &relative, 4);
    memcpy((void *)uintptr_t(at), jump, 5);
    VirtualProtect((void *)uintptr_t(at), 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)uintptr_t(at), 5);
    hook->on = true;
    return true;
}

bool HookOurs(Hook *hook, uint32_t entry, const void *to);

// Over an original's own entry, or over our port where it has one (the originals' calls reach it through the
// entry's jump, our ports' calls directly)
bool HookOriginal(Hook *hook, uint32_t entry, const void *to) {
    if (JumpTarget(entry) != 0)
        return HookOurs(hook, entry, to);
    return WriteJump(hook, entry, to);
}

// Over our code, where the entry's jump goes (both sides reach it)
bool HookOurs(Hook *hook, uint32_t entry, const void *to) {
    uint32_t at = JumpTarget(entry);
    if (at == 0) {
        hook->on = false;
        return false;
    }
    const uint8_t *code = reinterpret_cast<const uint8_t *>(uintptr_t(at));
    // a first instruction of five bytes (a tail jump, a load of an address: its operand may hold any byte) is room
    bool five = code[0] == 0xe9 || code[0] == 0xe8 || code[0] == 0xa1 || code[0] == 0xa3 || code[0] == 0x68 ||
                (code[0] >= 0xb8 && code[0] <= 0xbf);
    for (int i = 0; i < 5 && !five; i++)
        if (code[i] == 0xc3 || code[i] == 0xc2 || code[i] == 0xcc) {
            hook->on = false;
            return false;
        }
    return WriteJump(hook, at, to);
}

void HookRemove(Hook *hook) {
    if (!hook->on)
        return;
    DWORD old;
    VirtualProtect((void *)uintptr_t(hook->at), 5, PAGE_EXECUTE_READWRITE, &old);
    memcpy((void *)uintptr_t(hook->at), hook->saved, 5);
    VirtualProtect((void *)uintptr_t(hook->at), 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)uintptr_t(hook->at), 5);
    hook->on = false;
}

std::vector<Hook> g_hooks;

bool InstallHooks() {
    struct Entry {
        uint32_t entry;
        const void *to;
        bool ours;
    };
    const Entry entries[] = {
        { kGetRigidBody, (const void *)&FakeGetRigidBody, false },
        { kGetSimpleRigidBody, (const void *)&FakeGetSimpleRigidBody, false },
        { kFindSignature, (const void *)&FakeFind, false },
        { kGetPlayerObject, (const void *)&FakeGetPlayer, false },
        { kGetOrderedBody, (const void *)&FakeGetOrderedBody, false },
        { kDeleteObject, (const void *)&FakeDeleteObject, false },
        { kSpawnExplosion, (const void *)&FakeSpawnExplosion, false },
        { kAssignSlot, (const void *)&FakeAssignSlot, false },
        { kReleaseSlot, (const void *)&FakeReleaseSlot, false },
        { kLookupReference, (const void *)&FakeLookupReference, false },
        { kTriggerIncidental, (const void *)&FakeTriggerIncidental, false },
        { kZoneGet, (const void *)&FakeZoneGet, false },
        { kRegisterZone, (const void *)&FakeRegisterZone, false },
        { kOneShotConstruct, (const void *)&FakeOneShot, true },
        { kInfoConstruct, (const void *)&FakeInfoConstruct, false },
        { kBulletAdd, (const void *)&FakeBulletAdd, true },
        { kAddLight, (const void *)&FakeAddLight, true },
        { kLocateFX, (const void *)&FakeLocateFX, true },
        { kScorchWorld, (const void *)&FakeScorchWorld, true },
        { kStimuli, (const void *)&FakeStimuli, true },
        { kCreateParticles, (const void *)&FakeCreateParticles, true },
        { kFindSystem, (const void *)&FakeFindSystem, true },
        { kCheckHitWindow, (const void *)&FakeCheckHitWindow, true },
        { kOperatorNew, (const void *)&FakeOperatorNew, true },
        { kIndexLookup, (const void *)&FakeIndexLookup, true },
        { kGauss, (const void *)&FakeGauss, true },
        { kAttrConstruct, (const void *)&FakeAttrConstruct, true },
        { kAttrDestruct, (const void *)&FakeAttrDestruct, true },
        { kSceneLoad, (const void *)&FakeSceneLoad, true },
        { kSceneConstruct, (const void *)&FakeSceneConstruct, true },
        { kStreakUpdate, (const void *)&FakeStreakUpdate, true },
        { kStreakDraw, (const void *)&FakeStreakDraw, true },
    };
    bool all = true;
    for (const Entry &e : entries) {
        Hook hook;
        bool ok = e.ours ? HookOurs(&hook, e.entry, e.to) : HookOriginal(&hook, e.entry, e.to);
        if (ok) {
            g_hooks.push_back(hook);
        } else {
            printf("[gameweaponsshadowc] could not fake 0x%08x\n", e.entry);
            all = false;
        }
    }
    return all;
}

void RemoveHooks() {
    for (size_t i = g_hooks.size(); i-- > 0;)
        HookRemove(&g_hooks[i]);
    g_hooks.clear();
}

// ---- running a case

typedef void (*SideFn)(void);

void Window(bool original) {
    for (unsigned at : kEntries)
        XbeOriginal_Restore(at, original);
}

void RunSide(SideFn run, bool original) {
    g_original = original;
    g_gaussIndex = 0;
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
    Lend();
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

// ---- building the world

void PlaceObject(int k, const Coord3 &position, int type, bool simple) {
    PhysicsObject *object = Obj(k);
    memset(g_objects[k].bytes, 0, sizeof(g_objects[k].bytes));
    object->vtable = g_vtable;
    object->type = type;
    object->flags = simple ? PhysicsObject::kSimpleBody : 0;
    uint32_t sig = ObjectSig(k);
    if (simple) {
        int slot = kSimpleBase + k;
        object->rigidBodySlot = int16_t(slot);
        SimpleRigidBody &body = g_simple[k];
        memset(&body, 0, sizeof(body));
        body.orientation = Coord4{0.0f, 0.0f, 0.0f, 1.0f};
        body.position = position;
        body.slot = int8_t(slot);
        body.flags = Chance(15) ? SimpleRigidBody::kFlag01 : 0;
        body.radius = Uniform(0.3f, 2.5f);
        body.velocity = Coord3{Uniform(-5.0f, 5.0f), Uniform(-2.0f, 2.0f), Uniform(-5.0f, 5.0f)};
        body.mass = 1.0f;
        ShadowSimpleSigs[slot] = sig;
    } else {
        int slot = kRigidBase + k;
        object->rigidBodySlot = int16_t(slot);
        RigidBody &body = g_rigid[k];
        memset(&body, 0, sizeof(body));
        body.position = position;
        body.velocity = Coord3{Uniform(-20.0f, 20.0f), Uniform(-2.0f, 2.0f), Uniform(-20.0f, 20.0f)};
        body.ownerIndex = int8_t(slot);
        body.info = &g_info[k];
        RigidBodyInfo &info = g_info[k];
        memset(&info, 0, sizeof(info));
        Coord3 facing = RandomUnit3();
        facing.y *= 0.2f;
        Util_GenerateMatrix(&info.orientation, &facing);
        info.halfExtents = Coord4{Uniform(0.5f, 1.5f), Uniform(0.4f, 1.2f), Uniform(1.0f, 3.0f), 1.0f};
        float radius = sqrtf(info.halfExtents.x * info.halfExtents.x + info.halfExtents.y * info.halfExtents.y +
                             info.halfExtents.z * info.halfExtents.z);
        body.radius = Chance(80) ? radius : Uniform(0.5f, 4.0f);
        ShadowRigidSigs[slot] = sig;
    }
    g_damageAnswers[k] = Chance(20) ? 0x80 : (Chance(50) ? 0x20 : Range(0, 3));
    if (type == 4) {
        uint8_t *human = g_objects[k].bytes;
        human[0x8c] = Chance(60) ? 1 : 0;
        if (Chance(80)) {
            CharacterBlock &character = g_characters[k];
            memset(&character, 0, sizeof(character));
            MATRIX4 transform;
            Coord3 facing = RandomUnit3();
            Util_GenerateMatrix(&transform, &facing);
            transform.mtx[3][0] = position.x;
            transform.mtx[3][1] = position.y;
            transform.mtx[3][2] = position.z;
            memcpy(character.bytes + 0x10, &transform, sizeof(transform));
            Coord4 centre = {Uniform(-0.2f, 0.2f), Uniform(0.0f, 1.0f), Uniform(-0.2f, 0.2f), Uniform(0.0f, 1.0f)};
            memcpy(character.bytes + 0x50, &centre, sizeof(centre));
            HitShape *shape = NULL;
            if (Chance(60)) {
                shape = &g_shapes[k];
                shape->radius = Uniform(0.2f, 0.6f);
                shape->lift = Uniform(-0.5f, 1.0f);
                shape->stretch = Uniform(1.0f, 3.5f);
            }
            memcpy(character.bytes + 0xb4, &shape, 4);
            CharacterBlock *pointer = &character;
            memcpy(human + 0x70, &pointer, 4);
        }
    }
}

// Up to six objects about `centre`, owners and the player among them
void BuildWorld(const Coord3 &centre, float spread) {
    static const int kTypes[] = { 1, 1, 4, 4, 10, 11, 3, 2 };
    g_objectCount = Range(0, kObjects);
    for (int k = 0; k < g_objectCount; k++) {
        int type = kTypes[Range(0, 7)];
        bool simple = type == 4 || type == 11 || (type != 1 && Chance(50));
        PlaceObject(k, Offset(centre, spread, Uniform(0.0f, 2.0f)), type, simple);
    }
    // Owners among the objects before (or the object itself): IsOwnedBy walks the chain, and a longer cycle,
    // which the game never makes, never ends
    for (int k = 0; k < g_objectCount; k++)
        if (Chance(25))
            Obj(k)->ownerSig = ObjectSig(Range(0, k));
    g_player = g_objectCount > 0 && Chance(75) ? Obj(Range(0, g_objectCount - 1)) : NULL;
}

uint32_t RandomSig() {
    int pick = Range(0, 9);
    if (pick < 6 && g_objectCount > 0)
        return ObjectSig(Range(0, g_objectCount - 1));
    if (pick == 6)
        return 0;
    if (pick == 7)
        return kDeadSig;
    if (pick == 8 && g_player != NULL)
        return ObjectSig(ObjectIndex(g_player));
    return kSigBase + uint32_t(kObjects);       // a signature with no object
}

PhysicsObject *RandomObject() {
    if (g_objectCount == 0 || Chance(15))
        return NULL;
    return Obj(Range(0, g_objectCount - 1));
}

Coord3 ObjectPosition(int k) {
    PhysicsObject *object = Obj(k);
    return (object->flags & PhysicsObject::kSimpleBody) ? g_simple[k].position : g_rigid[k].position;
}

// A synthetic ray shell, from `origin` towards an object (or anywhere)
void BuildRay(const Coord3 &origin, bool aim) {
    memset(g_rayBlock, 0, sizeof(g_rayBlock));
    g_ray->vtable = (void **)0x0018ffe4;
    g_ray->origin = Coord4{origin.x, origin.y, origin.z, 0.0f};
    g_ray->position = origin;
    Coord3 direction = aim && g_objectCount > 0 ? Towards(origin, ObjectPosition(Range(0, g_objectCount - 1)), 1.5f)
                                                : RandomUnit3();
    g_ray->direction = Coord4{direction.x, direction.y, direction.z, 0.0f};
    g_ray->startDirection = direction;
    g_ray->damage = Chance(20) ? Uniform(0.0f, 600.0f) : Uniform(5.0f, 60.0f);
    g_ray->range = Uniform(1.0f, 220.0f);
    g_ray->muzzle = Offset(origin, 0.5f, 0.0f);
    static const int kShellTypes[] = { 0, 3, 0x10, 0x1a, 0x1b, 0x1d, 7 };
    g_ray->shellType = kShellTypes[Range(0, 6)];
    g_ray->ownerSig = RandomSig();
    g_ray->streakKind = Range(-1, 6);
    g_ray->muzzleObject = Chance(50) ? reinterpret_cast<RSceneObj *>(g_sceneObjects[Range(0, 1)]) : NULL;
    g_ray->muzzleEffect = reinterpret_cast<const ArticleEffect *>(uintptr_t(0x100 * Range(1, 4)));
}

void RandomVictims() {
    g_ray->numVictims = Range(0, 2);
    for (int i = 0; i < 2; i++) {
        RayShellVictim &victim = g_ray->victims[i];
        victim.hitPoint = Offset(g_base, 20.0f, 1.0f);
        victim.impactPoint = Offset(victim.hitPoint, 0.5f, 0.0f);
        victim.sig = RandomSig();
        victim.distance = Uniform(0.5f, 200.0f);
    }
    if (g_ray->victims[1].distance < g_ray->victims[0].distance && Chance(70)) {
        float swap = g_ray->victims[0].distance;
        g_ray->victims[0].distance = g_ray->victims[1].distance;
        g_ray->victims[1].distance = swap;
    }
}

void RandomGlobals() {
    ShadowSimStepCount = Range(100, 5000);
    ShadowSimTimeStep = Chance(50) ? 1.0f / 60.0f : Uniform(0.01f, 0.04f);
    ShadowSimStepsPerSecond = Chance(70) ? 60 : Range(25, 120);
    ShadowLaunchFlags = Chance(30) ? 0x1000 : 0;
    RayShellExplosionStep = Chance(30) ? ShadowSimStepCount : ShadowSimStepCount - Range(0, 30);
    g_hands[0x150] = Chance(50) ? 1 : 0;
    g_carPhysics.subPhysics = Chance(30) ? 1 : 0;
    g_sparksFound = Chance(80);
    ShellSparksGuard = Chance(50) ? 1 : 0;
    ShellSparks = ShellSparksGuard && Chance(80) ? reinterpret_cast<RParticleSystemData *>(&g_sparksData) : NULL;
    ShellRicochetSound = Range(0, 2);
    ShellConcreteSounds = Next();
}

// ---- one case's inputs

struct Inputs {
    int op;
    int index;
    uint32_t value, sig;
    float distance;
    Coord3 a, b, c;
    int kind;
    RSceneObj *object;
    const ArticleEffect *effect;
    bool flag;
    PhysicsObject *physics;
    Coord4 normal;
    uint8_t face[2];
    int passes;
    float range, damage, spread, size;
    WorldCollisionInfo info;
    alignas(16) uint8_t place[sizeof(WWorldPos)];
} g_in;
uint32_t g_answer;
float g_distanceOut;
uint8_t g_copy[0x100];                  // a freed block's bytes, or a render object's

void LogAnswer(uint32_t answer) {
    Log(0x414e5300);
    Log(answer);
}

// ---- RayShell: the trigger queue, tracers and flashes

void SideQueue() {
    switch (g_in.op) {
    case 0:
        LogAnswer(uint32_t(g_original ? Orig_GetNumActive() : RayShell::GetNumActiveRayShells()));
        break;
    case 1:
        LogAnswer(Canon(g_original ? Orig_GetActive(g_in.index) : RayShell::GetActiveRayShell(g_in.index)));
        break;
    case 2:
        g_original ? Orig_ClearActive() : RayShell::ClearActiveRayShells();
        break;
    case 3:
        g_original ? Orig_SetHitting(g_in.index) : RayShell::SetTriggerHittingRayShell(g_in.index);
        break;
    default: {
        ActiveRayShell *ray = g_original ? Orig_GetHitting() : RayShell::GetTriggerHittingRayShell();
        LogAnswer(ray != NULL ? Canon(ray) : 0);
        break;
    }
    }
}

void TestQueue(const Snapshot &before) {
    for (long i = 0; i < 60; i++) {
        ActiveRayShellCount = Range(0, 63);
        TriggerHittingRayShell = Chance(30) ? -1 : Range(0, 63);
        g_in.op = Range(0, 4);
        g_in.index = Chance(10) ? -1 : Range(0, 63);
        RunCase("RayShell trigger queue", i, SideQueue, before);
    }
}

void RandomTracers() {
    RayShellTracerCount = Chance(10) ? kRayShellTracers : Range(0, kRayShellTracers);
    for (int i = 0; i < kRayShellTracers; i++) {
        RayShellTracer &tracer = RayShellTracers[i];
        tracer.start = Offset(g_base, 30.0f, 1.0f);
        Coord3 direction = RandomUnit3();
        float length = Chance(20) ? Uniform(0.0f, 3.0f) : Uniform(1.0f, 200.0f);
        tracer.end = Coord3{tracer.start.x + direction.x * length, tracer.start.y + direction.y * length,
                            tracer.start.z + direction.z * length};
        tracer.time = uint32_t(ShadowSimStepCount - Range(-5, 80));
        tracer.kind = Range(0, kBulletStreakKinds - 1);
        RayShellTracerSources[i].object = Chance(30) ? reinterpret_cast<RSceneObj *>(g_sceneObjects[Range(0, 1)]) : NULL;
        RayShellTracerSources[i].effect = reinterpret_cast<const ArticleEffect *>(uintptr_t(0x100 * Range(1, 4)));
    }
}

void SideTracers() {
    switch (g_in.op) {
    case 0:
        g_original ? Orig_ClearTracers(g_in.value) : RayShell::ClearTracers(g_in.value);
        break;
    case 1:
        g_original ? Orig_DrawTracers() : RayShell::DrawTracers();
        break;
    case 2:
        g_original ? Orig_RayReset() : RayShell::Reset();
        break;
    default:
        if (g_original)
            Orig_StoreTracer(&g_in.a, &g_in.b, g_in.kind, g_in.object, g_in.effect);
        else
            RayShell::StaticStoreTracer(&g_in.a, &g_in.b, g_in.kind, g_in.object, g_in.effect);
        break;
    }
}

void TestTracers(const Snapshot &before) {
    for (long i = 0; i < 160; i++) {
        RandomGlobals();
        RandomTracers();
        g_in.op = Range(0, 4);
        g_in.value = uint32_t(ShadowSimStepCount - Range(-5, 80));
        g_in.a = Offset(g_base, 20.0f, 1.0f);
        Coord3 direction = RandomUnit3();
        float length = Chance(30) ? Uniform(1.5f, 2.5f) : Uniform(0.0f, 100.0f);
        g_in.b = Coord3{g_in.a.x + direction.x * length, g_in.a.y + direction.y * length,
                        g_in.a.z + direction.z * length};
        g_in.kind = Chance(40) ? 0 : Range(-2, 6);
        g_in.object = Chance(50) ? reinterpret_cast<RSceneObj *>(g_sceneObjects[0]) : NULL;
        g_in.effect = reinterpret_cast<const ArticleEffect *>(uintptr_t(0x100 * Range(1, 4)));
        static const char *const kNames[] = { "RayShell::ClearTracers", "RayShell::DrawTracers", "RayShell::Reset",
                                              "RayShell::StaticStoreTracer", "RayShell::StaticStoreTracer" };
        RunCase(kNames[g_in.op], i, SideTracers, before);
    }
}

void SideFlashes() {
    if (g_in.op == 0) {
        g_original ? Orig_DrawFlashes() : RayShell::DrawFlashes();
    } else if (g_original) {
        Orig_AddFlash(g_ray, 0, g_in.sig, &g_in.a);
    } else {
        g_ray->AddFlash(g_in.sig, &g_in.a);
    }
}

void TestFlashes(const Snapshot &before) {
    for (long i = 0; i < 120; i++) {
        RandomGlobals();
        BuildWorld(g_base, 15.0f);
        BuildRay(g_base, false);
        RayShellFlashCount = Chance(10) ? kRayShellFlashes : Range(0, kRayShellFlashes);
        for (int f = 0; f < kRayShellFlashes; f++) {
            RayShellFlashes[f].position = Offset(g_base, 20.0f, 1.0f);
            RayShellFlashes[f].ownerSig = RandomSig();
        }
        g_in.op = Range(0, 3) == 0 ? 0 : 1;
        g_in.sig = RandomSig();
        g_in.a = Offset(g_base, 20.0f, 1.0f);
        RunCase(g_in.op == 0 ? "RayShell::DrawFlashes" : "RayShell::AddFlash", i, SideFlashes, before);
    }
}

// ---- RayShell: the shot's parts

void SideRay() {
    switch (g_in.op) {
    case 0:
        if (g_original)
            Orig_ConsiderVictim(g_ray, 0, g_in.sig, &g_in.a, &g_in.b, g_in.distance);
        else
            g_ray->ConsiderVictim(g_in.sig, &g_in.a, &g_in.b, g_in.distance);
        break;
    case 1:
        LogAnswer((g_original ? Orig_CollideObjects(g_ray, 0) : uint8_t(g_ray->CollideWithObjects())) & 0xff);
        break;
    case 2:
        g_distanceOut = g_in.distance;
        LogAnswer((g_original ? Orig_CollideWorld(g_ray, 0, &g_distanceOut)
                              : uint8_t(g_ray->CollideWithWorld(&g_distanceOut))) & 0xff);
        LogFloat(g_distanceOut);
        break;
    case 3:
        g_distanceOut = g_in.distance;
        g_original ? Orig_Fire(g_ray, 0, &g_distanceOut) : g_ray->Fire(&g_distanceOut);
        LogFloat(g_distanceOut);
        break;
    case 4:
        if (g_original)
            Orig_DamageObject(g_ray, 0, g_in.sig, &g_in.a, &g_in.b);
        else
            g_ray->DamagePhysicsObject(g_in.sig, &g_in.a, &g_in.b);
        break;
    default:
        g_original ? Orig_AddTrigger(g_ray, 0, g_in.distance) : g_ray->AddNewRayShellForTriggerCheck(g_in.distance);
        break;
    }
}

void TestRayParts(const Snapshot &before) {
    static const char *const kNames[] = { "RayShell::ConsiderVictim", "RayShell::CollideWithObjects",
                                          "RayShell::CollideWithWorld", "RayShell::Fire",
                                          "RayShell::DamagePhysicsObject", "RayShell::AddNewRayShellForTriggerCheck" };
    for (long i = 0; i < 600; i++) {
        RandomGlobals();
        RandomTracers();
        g_in.op = int(i % 6);
        bool world = g_in.op == 2;
        Coord3 centre = world ? g_base : Offset(g_base, 5.0f, 0.0f);
        BuildWorld(centre, world ? 30.0f : 8.0f);
        Coord3 origin = Offset(centre, world ? 15.0f : 12.0f, Uniform(0.5f, 3.0f));
        BuildRay(origin, !world || Chance(30));
        if (world) {
            // down at the ground, across, or up
            int aim = Range(0, 2);
            Coord3 direction = RandomUnit3();
            if (aim == 0)
                direction = Towards(origin, Coord3{origin.x + Uniform(-8.0f, 8.0f), origin.y - 5.0f,
                                                   origin.z + Uniform(-8.0f, 8.0f)}, 0.0f);
            else if (aim == 1)
                direction.y *= 0.1f;
            g_ray->direction = Coord4{direction.x, direction.y, direction.z, 0.0f};
        }
        RandomVictims();
        if (g_in.op == 1)
            g_ray->numVictims = 0;
        if (g_in.op == 3 && g_ray->numVictims == 2 && g_objectCount > 0 && Chance(60)) {
            // through a car
            int car = Range(0, g_objectCount - 1);
            Obj(car)->type = Chance(50) ? 1 : 10;
            g_ray->victims[0].sig = ObjectSig(car);
        }
        ActiveRayShellCount = Chance(10) ? 63 : Range(0, 62);
        g_in.sig = RandomSig();
        g_in.a = Offset(origin, 10.0f, 0.0f);
        g_in.b = Offset(g_in.a, 0.5f, 0.0f);
        g_in.distance = g_ray->numVictims > 0 && Chance(60)
                            ? g_ray->victims[Range(0, g_ray->numVictims - 1)].distance + Uniform(-2.0f, 2.0f)
                            : Uniform(0.0f, 220.0f);
        if (g_in.op == 0 && Chance(10))
            g_in.distance = g_ray->victims[0].distance;
        RunCase(kNames[g_in.op], i, SideRay, before);
    }
}

// ---- RayShell: the whole shot

void SideRayConstruct() {
    void *block = UMemory::FastAlloc(sizeof(RayShell), "RayShell");
    memset(block, 0, sizeof(RayShell));
    RayShell *ray = static_cast<RayShell *>(block);
    if (g_original)
        Orig_RayConstruct(ray, 0, g_in.kind, g_in.sig, &g_in.a, &g_in.b, &g_in.c, g_in.object, g_in.effect, g_in.range,
                          g_in.damage, g_in.passes, g_in.spread, g_in.index);
    else
        ray->Construct(g_in.kind, g_in.sig, &g_in.a, &g_in.b, &g_in.c, g_in.object, g_in.effect, g_in.range,
                       g_in.damage, g_in.passes, g_in.spread, g_in.index);
    // freed by itself: its bytes after the first word (the free list's)
    LogBytes(static_cast<uint8_t *>(block) + 4, sizeof(RayShell) - 4);
}

void SideRayDelete() {
    if (g_in.flag) {
        void *block = UMemory::FastAlloc(sizeof(RayShell), "RayShell");
        memcpy(block, g_rayBlock, sizeof(RayShell));
        RayShell *ray = static_cast<RayShell *>(block);
        LogAnswer(uint32_t(g_original ? Orig_RayDelete(ray, 0, 1) == ray : ray->Delete(1) == ray));
    } else {
        LogAnswer(uint32_t(g_original ? Orig_RayDelete(g_ray, 0, g_in.value) == g_ray
                                      : g_ray->Delete(g_in.value) == g_ray));
    }
}

void TestRayShot(const Snapshot &before) {
    static const int kShellTypes[] = { 0, 3, 0x10, 0x1a, 0x1b, 0x1d, 7 };
    for (long i = 0; i < 120; i++) {
        RandomGlobals();
        RandomTracers();
        Coord3 centre = Chance(50) ? g_base : Offset(g_base, 5.0f, 0.0f);
        BuildWorld(centre, 12.0f);
        ActiveRayShellCount = Chance(5) ? 63 : Range(0, 40);
        RayShellFlashCount = Range(0, kRayShellFlashes);
        g_in.kind = kShellTypes[Range(0, 6)];
        g_in.sig = RandomSig();
        g_in.a = Offset(centre, 10.0f, Uniform(0.5f, 3.0f));
        g_in.b = g_objectCount > 0 && Chance(70) ? Towards(g_in.a, ObjectPosition(Range(0, g_objectCount - 1)), 1.0f)
                                                 : RandomUnit3();
        if (Chance(30))
            g_in.b.y = -fabsf(g_in.b.y) - 0.3f;
        float scale = Uniform(0.5f, 3.0f);      // not normalised: the constructor does it
        g_in.b.x *= scale;
        g_in.b.y *= scale;
        g_in.b.z *= scale;
        g_in.c = Offset(g_in.a, 0.5f, 0.0f);
        g_in.object = Chance(50) ? reinterpret_cast<RSceneObj *>(g_sceneObjects[1]) : NULL;
        g_in.effect = reinterpret_cast<const ArticleEffect *>(uintptr_t(0x300));
        g_in.range = Chance(20) ? Uniform(200.0f, 400.0f) : Uniform(5.0f, 150.0f);
        g_in.damage = Uniform(1.0f, 100.0f);
        g_in.passes = Range(0, 4);
        g_in.spread = Uniform(0.0f, 0.05f);
        g_in.index = Range(-1, 6);
        RunCase("RayShell::RayShell", i, SideRayConstruct, before);
    }
    for (long i = 0; i < 12; i++) {
        BuildRay(g_base, false);
        g_in.flag = (i & 1) != 0;
        g_in.value = uint32_t(i & 2) / 2 * 2;
        RunCase("RayShell::scalar_deleting_destructor", i, SideRayDelete, before);
    }
}

// ---- Shell: the impacts

void SideWorldEffects() {
    if (g_original)
        Orig_WorldEffects(&g_in.a, &g_in.normal, g_in.index, g_in.kind, g_in.face, int(g_in.value), g_in.physics);
    else
        Shell::ApplyWorldEffects(&g_in.a, &g_in.normal, g_in.index, g_in.kind, g_in.face, int(g_in.value),
                                 g_in.physics);
}

void SideWorldDamage() {
    WWorldPos *place = reinterpret_cast<WWorldPos *>(g_in.place);
    if (g_original)
        Orig_WorldDamage(g_in.damage, place, g_in.size, g_in.kind, &g_in.info);
    else
        Shell::ApplyWorldDamage(g_in.damage, place, g_in.size, g_in.kind, &g_in.info);
}

void TestImpacts(const Snapshot &before) {
    static const uint8_t kFaces[] = { 0x00, 0x06, 0x08, 0x09, 0x0c, 0x0d, 0x10, 0x15, 0x17, 0x1d, 0x20, 0x30,
                                      0x40, 0x50, 0x70, 0x80, 0x90, 0x9d, 0x27, 0x5f };
    static const int kShellTypes[] = { 0, 3, 0x10, 0x1a, 0x1b, 0x1d, 7 };
    for (long i = 0; i < 300; i++) {
        RandomGlobals();
        BuildWorld(g_base, 10.0f);
        g_in.a = Offset(g_base, 20.0f, 1.0f);
        if (Chance(30)) {
            float tilt = Uniform(0.0f, 0.3f);
            g_in.normal = Coord4{Uniform(-tilt, tilt), 1.0f, Uniform(-tilt, tilt), Chance(50) ? 0.0f : Uniform(-1.0f, 1.0f)};
        } else {
            Coord3 n = RandomUnit3();
            g_in.normal = Coord4{n.x, n.y, n.z, Chance(50) ? 0.0f : 1.0f};
        }
        g_in.index = Range(0, 2);
        g_in.kind = kShellTypes[Range(0, 6)];
        g_in.face[0] = kFaces[Range(0, 19)];
        g_in.face[1] = uint8_t(Range(0, 3));
        g_in.value = Chance(30) ? 0x80 : uint32_t(Range(0, 0x20));
        int pick = Range(0, 3);
        g_in.physics = pick == 0 ? NULL : pick == 1 ? g_player : RandomObject();
        RunCase("Shell::ApplyWorldEffects", i, SideWorldEffects, before);
    }
    WWorldPos *place = reinterpret_cast<WWorldPos *>(g_in.place);
    for (long i = 0; i < 160; i++) {
        RandomGlobals();
        if (!g_hits.empty() && Chance(85)) {
            g_in.info = g_hits[size_t(Range(0, int(g_hits.size()) - 1))];
        } else {
            memset(&g_in.info, 0, sizeof(g_in.info));
            g_in.info.point = Coord4{g_base.x, g_base.y, g_base.z, 1.0f};
            g_in.info.normal = Coord4{0.0f, 1.0f, 0.0f, 0.0f};
        }
        if (Chance(15))
            g_in.info.flag52 = 1;
        if (Chance(15))
            g_in.info.faceType = 0x70;
        if (Chance(15))
            g_in.info.faceSubType |= 2;
        if (Chance(10))
            g_in.info.faceInstance = NULL;
        static const float kDamages[] = { 9.99f, 10.0f, 49.0f, 50.0f, 249.0f, 250.0f, 499.0f, 500.0f, 1000.0f, 0.0f };
        g_in.damage = Chance(60) ? kDamages[Range(0, 9)] : Uniform(0.0f, 700.0f);
        g_in.size = Uniform(0.0f, 4.0f);
        g_in.kind = Range(0, 5);
        place->Construct();
        RunCase("Shell::ApplyWorldDamage", i, SideWorldDamage, before);
    }
}

// ---- Shell: the object

void ResetShell() {
    memset(g_shellBlock, 0, sizeof(g_shellBlock));
    memset(&g_shellBody, 0, sizeof(g_shellBody));
    ShadowSimpleSigs[kShellSlot] = kShellSig;
}

void SideShell() {
    switch (g_in.op) {
    case 0:
        if (g_original)
            Orig_ShellConstruct(g_shell, 0, g_in.kind, &g_in.b, &g_in.a, g_in.sig, g_in.damage, g_in.size, g_in.index);
        else
            g_shell->Construct(g_in.kind, &g_in.b, &g_in.a, g_in.sig, g_in.damage, g_in.size, g_in.index);
        break;
    case 1:
        g_original ? Orig_ShellDestruct(g_shell, 0) : g_shell->Destruct();
        break;
    case 2: {
        if (g_in.flag) {
            void *block = UMemory::FastAlloc(sizeof(Shell), "Shell");
            memcpy(block, g_shellBlock, sizeof(Shell));
            Shell *shell = static_cast<Shell *>(block);
            LogAnswer(uint32_t(g_original ? Orig_ShellDelete(shell, 0, 1) == shell : shell->Delete(1) == shell));
        } else {
            LogAnswer(uint32_t(g_original ? Orig_ShellDelete(g_shell, 0, 0) == g_shell : g_shell->Delete(0) == g_shell));
        }
        break;
    }
    default:
        g_original ? Orig_ShellSimulate(g_shell, 0) : g_shell->Simulate();
        break;
    }
}

// A shell in flight about `centre`
void BuildShell(const Coord3 &centre) {
    ResetShell();
    PhysicsObject *object = g_shell;
    object->vtable = (void **)0x0019006c;
    object->type = 7;
    object->flags = PhysicsObject::kSimpleBody;
    object->rigidBodySlot = kShellSlot;
    object->ownerSig = RandomSig();
    object->worldPos.Construct();
    static const int kShellTypes[] = { 0, 3, 0x10, 0x1a, 0x1b, 0x1d, 7 };
    g_shell->shellType = kShellTypes[Range(0, 6)];
    g_shell->damage = Uniform(1.0f, 300.0f);
    g_shell->explosionSize = Chance(20) ? 1.0f : Uniform(0.0f, 3.0f);
    g_shell->stepsLeft = Chance(15) ? Range(-1, 1) : Range(2, 60);
    g_shell->origin = Offset(centre, 10.0f, 1.0f);
    g_shell->streakKind = Range(-1, 6);
    g_shellBody.orientation = Coord4{Uniform(-1.0f, 1.0f), Uniform(-1.0f, 1.0f), Uniform(-1.0f, 1.0f), 1.0f};
    g_shellBody.position = Offset(centre, 6.0f, Uniform(0.2f, 3.0f));
    Coord3 heading = RandomUnit3();
    if (Chance(50))
        heading.y = -fabsf(heading.y) - 0.5f;
    float speed = Uniform(50.0f, 250.0f);
    g_shellBody.velocity = Coord3{heading.x * speed, heading.y * speed, heading.z * speed};
    g_shellBody.slot = int8_t(kShellSlot);
    g_shellBody.radius = 0.3f;
    for (int w = 0; w < 3; w++)
        ShadowSimpleHits[kShellSlot][w] = 0;
    for (int k = 0; k < g_objectCount; k++)
        if (Chance(35)) {
            int index = OrderedIndex(k);
            ShadowSimpleHits[kShellSlot][index / 64] |= uint64_t(1) << (index % 64);
        }
    if (Chance(10))
        ShadowSimpleHits[kShellSlot][1] |= uint64_t(1) << Range(0, 63);     // a body with no object
}

void TestShell(const Snapshot &before) {
    static const char *const kNames[] = { "Shell::Shell", "Shell::~Shell", "Shell::scalar_deleting_destructor",
                                          "Shell::Simulate" };
    static const int kShellTypes[] = { 0, 3, 0x10, 0x1a, 0x1b, 0x1d, 7 };
    for (long i = 0; i < 300; i++) {
        RandomGlobals();
        BuildWorld(g_base, 6.0f);
        g_in.op = i < 40 ? 0 : i < 50 ? 1 : i < 60 ? 2 : 3;
        if (g_in.op == 0) {
            ResetShell();
            g_in.kind = kShellTypes[Range(0, 6)];
            g_in.b = RandomUnit3();
            g_in.a = Offset(g_base, 10.0f, 1.0f);
            g_in.sig = RandomSig();
            g_in.damage = Uniform(1.0f, 100.0f);
            g_in.size = Uniform(0.0f, 3.0f);
            g_in.index = Chance(40) ? -1 : Range(-3, 6);
            ShadowRandomSeed = Next();
            ShadowRandomMultiplier = Chance(80) ? 0x343fd : Next();
        } else {
            BuildShell(g_base);
            g_in.flag = (i & 1) != 0;
        }
        RunCase(kNames[g_in.op], i, SideShell, before);
    }
}

// ---- Grenade

void ResetGrenade() {
    memset(g_grenadeBlock, 0, sizeof(g_grenadeBlock));
    memset(&g_grenadeBody, 0, sizeof(g_grenadeBody));
    ShadowSimpleSigs[kGrenadeSlot] = kGrenadeSig;
}

void SideGrenade() {
    switch (g_in.op) {
    case 0: {
        if (g_original)
            Orig_GrenadeConstruct(g_grenade, 0, g_in.kind, &g_in.b, &g_in.a, g_in.size, g_in.sig);
        else
            g_grenade->Construct(g_in.kind, &g_in.b, &g_in.a, g_in.size, g_in.sig);
        // the render object, by value, then back to the heap
        RSceneObj *render = g_grenade->renderObject;
        if (render != NULL) {
            LogBytes(render, 0x80);
            UMemory::FastFree(render, 0x80);
            g_grenade->renderObject = reinterpret_cast<RSceneObj *>(uintptr_t(0x52454e44));
        }
        break;
    }
    case 1:
        g_original ? Orig_GrenadeDestruct(g_grenade, 0) : g_grenade->Destruct();
        break;
    case 2: {
        if (g_in.flag) {
            void *block = UMemory::FastAlloc(sizeof(Grenade), "Grenade");
            memcpy(block, g_grenadeBlock, sizeof(Grenade));
            Grenade *grenade = static_cast<Grenade *>(block);
            LogAnswer(uint32_t(g_original ? Orig_GrenadeDelete(grenade, 0, 1) == grenade
                                          : grenade->Delete(1) == grenade));
        } else {
            LogAnswer(uint32_t(g_original ? Orig_GrenadeDelete(g_grenade, 0, 0) == g_grenade
                                          : g_grenade->Delete(0) == g_grenade));
        }
        break;
    }
    case 3:
        g_original ? Orig_GrenadeRender(g_grenade, 0, g_in.flag) : g_grenade->Render(g_in.flag);
        break;
    default:
        g_original ? Orig_GrenadeSimulate(g_grenade, 0) : g_grenade->Simulate();
        break;
    }
}

// A grenade in flight near the ground about `centre`
void BuildGrenade(const Coord3 &centre) {
    ResetGrenade();
    PhysicsObject *object = g_grenade;
    object->vtable = (void **)0x0018ed98;
    object->type = 8;
    object->flags = PhysicsObject::kSimpleBody;
    object->rigidBodySlot = kGrenadeSlot;
    object->ownerSig = RandomSig();
    object->worldPos.Construct();
    g_grenade->weaponType = Range(0, 25);
    g_grenade->unknown6c4 = 0.5f;
    g_grenade->fuse = Chance(20) ? Range(-1, 1) : Range(2, 180);
    g_grenade->armedStep = ShadowSimStepCount + Range(-6, 3);
    g_grenade->bounces = Range(0, 2);
    g_grenade->explode = uint8_t(Range(0, 1));
    g_grenade->streak.count = Range(0, 40);
    g_grenade->streak.tick = Range(0, 100);
    g_grenade->streak.type = Range(0, 2);
    g_grenadeBody.orientation = Coord4{0.0f, 0.0f, 0.0f, 1.0f};
    g_grenadeBody.position = Offset(centre, 12.0f, Chance(50) ? Uniform(-0.6f, 0.4f) : Uniform(0.5f, 5.0f));
    g_grenadeBody.velocity = Coord3{Uniform(-15.0f, 15.0f), Chance(60) ? Uniform(-40.0f, -5.0f) : Uniform(-5.0f, 15.0f),
                                    Uniform(-15.0f, 15.0f)};
    g_grenadeBody.acceleration = Coord3{Uniform(-1.0f, 1.0f), Uniform(-1.0f, 1.0f), Uniform(-1.0f, 1.0f)};
    g_grenadeBody.slot = int8_t(kGrenadeSlot);
    g_grenadeBody.radius = 0.5f;
    g_grenadeBody.mass = 0.25f;
    g_grenadeBody.flags = SimpleRigidBody::kMoves | (Chance(80) ? SimpleRigidBody::kFlag04 : 0);
}

void TestGrenade(const Snapshot &before) {
    static const char *const kNames[] = { "Grenade::Grenade", "Grenade::~Grenade",
                                          "Grenade::scalar_deleting_destructor", "Grenade::Render",
                                          "Grenade::Simulate" };
    for (long i = 0; i < 260; i++) {
        RandomGlobals();
        BuildWorld(g_base, 6.0f);
        g_in.op = i < 30 ? 0 : i < 40 ? 1 : i < 50 ? 2 : i < 70 ? 3 : 4;
        for (int w = 0; w < 32; w++)
            g_weapons[w].damage = Range(0, 6000);
        if (g_in.op == 0) {
            ResetGrenade();
            g_in.kind = Range(0, 25);
            g_in.b = RandomUnit3();
            g_in.a = Offset(g_base, 10.0f, 1.0f);
            g_in.size = Uniform(0.0f, 60.0f);
            g_in.sig = RandomSig();
        } else {
            BuildGrenade(g_base);
            g_in.flag = Chance(50);
        }
        RunCase(kNames[g_in.op], i, SideGrenade, before);
    }
}

// ---- setup

void BuildRegions() {
    AddShared(g_objects, sizeof(g_objects), "the synthetic objects");
    AddShared(g_simple, sizeof(g_simple), "their simple bodies");
    AddShared(g_rigid, sizeof(g_rigid), "their rigid bodies");
    AddShared(g_info, sizeof(g_info), "the rigid bodies' info", false);
    AddShared(g_characters, sizeof(g_characters), "the characters", false);
    AddShared(g_shapes, sizeof(g_shapes), "the hit shapes", false);
    AddShared(g_damageAnswers, sizeof(g_damageAnswers), "the damage answers", false);
    AddShared(&g_objectCount, sizeof(g_objectCount), "the object count", false);
    AddShared(&g_player, sizeof(g_player), "the player", false);
    AddShared(&g_shellBody, sizeof(g_shellBody), "the shell's body");
    AddShared(&g_grenadeBody, sizeof(g_grenadeBody), "the grenade's body");
    AddShared(&g_spareSimple, sizeof(g_spareSimple), "the spare simple body");
    AddShared(&g_spareRigid, sizeof(g_spareRigid), "the spare rigid body");
    AddShared(g_shellBlock, sizeof(g_shellBlock), "the shell");
    AddShared(g_grenadeBlock, sizeof(g_grenadeBlock), "the grenade");
    AddShared(g_rayBlock, sizeof(g_rayBlock), "the ray shell");
    AddShared(g_soundBlock, sizeof(g_soundBlock), "the sound");
    AddShared(&g_carPhysics, sizeof(g_carPhysics), "the player's car physics", false);
    AddShared(g_hands, sizeof(g_hands), "the hands", false);
    AddShared(g_weapons, sizeof(g_weapons), "the weapons", false);
    AddShared(&g_in, sizeof(g_in), "the inputs");
    AddShared(&g_sparksFound, sizeof(g_sparksFound), "the sparks' answer", false);

    AddShared((void *)OwnedGlobals_Ours(0x00234e2c), 12, "the Simulation's step globals", false);
    AddShared((void *)OwnedGlobals_Ours(0x001c45c4), 8, "the renderer's random generator");
    AddShared(ShadowSimRandom, 16, "the Simulation's random generator");
    AddShared(ShadowRigidSigs, 0x100, "the rigid bodies' signatures");
    AddShared(ShadowSimpleSigs, 0x180, "the simple bodies' signatures");
    AddShared(ShadowSimpleHits, 0x900, "the simple bodies' hits", false);
    AddShared((void *)OwnedGlobals_Ours(0x00234e40), 4, "the player's cars", false);
    AddShared((void *)OwnedGlobals_Ours(0x001dda90), 4, "the player's hands", false);
    AddShared((void *)OwnedGlobals_Ours(0x00244530), 4, "the launch flags", false);
    AddShared((void *)0x001d83a8, 4, "the weapon bank", false);
    AddShared((void *)OwnedGlobals_Ours(0x0023923c), 4, "the weapon manager", false);

    AddOwned(RayShellTracerSources, 0x001e7ad0, sizeof(RayShellTracerSources), "RayShellTracerSources");
    AddOwned(&RayShellTracerCount, 0x001e7cd0, 4, "RayShellTracerCount");
    AddOwned(RayShellTracers, 0x001e7ce0, sizeof(RayShellTracers), "RayShellTracers");
    AddOwned(&RayShellFlashCount, 0x001e84e0, 4, "RayShellFlashCount");
    AddOwned(&ActiveRayShellCount, 0x001e84e4, 4, "ActiveRayShellCount");
    AddOwned(&RayShellExplosionStep, 0x001e84e8, 4, "RayShellExplosionStep");
    AddOwned(RayShellFlashes, 0x001e84f0, sizeof(RayShellFlashes), "RayShellFlashes");
    AddOwned(ActiveRayShells, 0x001e8570, sizeof(ActiveRayShells), "ActiveRayShells");
    AddOwned(&TriggerHittingRayShell, 0x001c3df4, 4, "TriggerHittingRayShell");
    AddOwned(&ShellConcreteSounds, 0x001e9280, 4, "ShellConcreteSounds");
    AddOwned(&ShellRicochetSound, 0x001e9284, 4, "ShellRicochetSound");
    AddOwned(&ShellSparks, 0x001e9288, 4, "ShellSparks");
    AddOwned(&ShellSparksGuard, 0x001e928c, 4, "ShellSparksGuard");
}

// The game's state the fakes stand in for, pointed at the synthetic ones (the regions put it back)
void PointGlobals() {
    for (int i = 0; i < 96; i++)
        g_vtable[i] = (void *)&FakeTrap;
    g_vtable[2] = (void *)&FakeApplyDamage;
    g_vtable[52] = (void *)&FakeAddDamageByPlayer;
    g_vtable[57] = (void *)&FakeGetPhysics;
    memset(&g_playerCar, 0, sizeof(g_playerCar));
    reinterpret_cast<PhysicsObject *>(g_playerCar.bytes)->vtable = g_vtable;
    g_playerCars[0] = reinterpret_cast<PhysicsObject *>(g_playerCar.bytes);
    ShadowPlayerVehicles = g_playerCars;
    ShadowPlayerHands = g_hands;
    int32_t handle = 7;
    memcpy(g_bank + 8, &handle, 4);
    ShadowWeaponBank = g_bank;
    g_weaponManager.slots = g_weapons;
    ShadowWeaponManager = &g_weaponManager;
}

// Hits of the live world round the player's car, for ApplyWorldDamage
void GatherHits() {
    for (int i = 0; i < 200 && g_hits.size() < 48; i++) {
        Coord3 from = Offset(g_base, 25.0f, Uniform(0.5f, 4.0f));
        Coord3 direction = RandomUnit3();
        if (Chance(60))
            direction.y = -fabsf(direction.y) - 0.5f;
        float length = Uniform(5.0f, 40.0f);
        Coord4 segment[2] = { { from.x, from.y, from.z, 1.0f },
                              { from.x + direction.x * length, from.y + direction.y * length,
                                from.z + direction.z * length, 1.0f } };
        WorldCollisionInfo info;
        FakeInfoConstruct(&info, 0);
        if (fgCollisionMgr->CheckHitWorld(segment, &info) > 0)
            g_hits.push_back(info);
    }
}

}  // namespace

void GameWeaponsShadowC_Run(void) {
    char value[16] = "";
    DWORD length = GetEnvironmentVariableA("NIGHTFIRE_GAMEWEAPONSSHADOWC", value, sizeof(value));
    if (length == 0 || length >= sizeof(value) || atoi(value) == 0)
        return;
    FpControlGet(&g_x87, &g_sse);

    // the player's car, before the Simulation is faked
    PhysicsObject **cars = ShadowPlayerVehicles;
    if (cars == NULL || cars[0] == NULL || ShadowSimRandom == NULL || fgCollisionMgr == NULL) {
        printf("[gameweaponsshadowc] no player car, Simulation or collision manager at this tick - not run\n");
        fflush(stdout);
        return;
    }
    g_base = *cars[0]->GetPosition();
    g_hits.clear();
    GatherHits();

    BuildRegions();
    Snapshot before;
    Capture(&before);
    bool hooked = InstallHooks();
    if (hooked) {
        PointGlobals();
        Snapshot pointed;
        Capture(&pointed);
        TestQueue(pointed);
        TestTracers(pointed);
        TestFlashes(pointed);
        TestRayParts(pointed);
        TestRayShot(pointed);
        TestImpacts(pointed);
        TestShell(pointed);
        TestGrenade(pointed);
    }
    RemoveHooks();
    Restore(before);
    ResetFpu();
    printf("[gameweaponsshadowc] ray shells, shells, grenades vs originals (%u world hits): %ld cases, %ld checks, "
           "%ld differ%s%s\n",
           unsigned(g_hits.size()), g_cases, g_checks, g_differ, g_faults != 0 ? " (with faults)" : "",
           hooked ? "" : " - not run: fakes missing");
    if (g_faults != 0)
        printf("[gameweaponsshadowc]   %ld calls faulted\n", g_faults);
    fflush(stdout);
}
