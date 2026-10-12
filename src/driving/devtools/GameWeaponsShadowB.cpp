#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "GameWeaponsShadowB.h"
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
#include "../game/MissionManager.h"
#include "../engine/SimRandom.h"
#include "../game/Explosion.h"
#include "../game/Missile.h"
#include "../game/Vehicle.h"
#include "../game/WeaponManager.h"
#include "../physics/PhysicsObject.h"
#include "../physics/RigidBody.h"
#include "../physics/SimpleRigidBody.h"
#include "../game/Simulation.h"
#include "../platform/RealMath.h"
#include "../render/RSceneObj.hpp"
#include "../world/CollisionTypes.h"
#include "../world/Targeting.h"
#include "../world/WorldPos.h"
#include "../../common/xbeOriginal.h"
#include "../../helpers.h"

// ---------------------------------------------------------------------------------------------------------------
// A shadow test of game.weapons' package B, run once from the first simulation tick when NIGHTFIRE_GAMEWEAPONSSHADOWB
// is set. No missile or explosion exists at the first tick, so every case is synthetic: each runs the ORIGINAL
// (every entry of the package swapped back together) and then the PORT from the same state, and compares the
// objects, their simple bodies, the scratch blocks the fakes hand out, the targets, a scratch mission manager, the
// Simulation's random generator, the game's copies of the tuning globals we own (the original's side) against ours,
// and a log of every call out of the package:
//
//   - StripTriangle::GetNormal (FUN_0005d3f0) on random triangles: degenerate, flat, upside down, near vertical;
//   - Explosion: Construct (every type), Destruct, Delete(0/1), Simulate (random radii, sizes, hit sets over 160
//     bodies, objects with and without bounds, thin, heavy and weightless, the player under the mission manager's
//     autoDrive, vehicles of type "tunnel", the direct hit, the flash);
//   - Missile: Construct (every weapon and others, players' and others' owners, alpha_sub near and far, helicopter
//     and sentry owners through the simple-body signature, the snow1a levels, the wobble flip and pitch globals),
//     Destruct, Delete(0/1), Render (locator or body, the Q-Pulse's light, the light's quarter second, paused),
//     SetUserSteerX/Y (the dead zone's edge, -0, NaN), SetTarget, CheckForCollision (detonation, the soft zone,
//     arming, rigid bodies' boxes, EMP, player hits on vehicles, level flight over found and missing ground, world
//     hits and windows, face type 14), SteerMissile (the drive missile's steering, homing with and without the
//     target's velocity, the lock sound, proximity, the torpedo giving up, alignment before homing, the wobble) and
//     Simulate (flying, lingering, deleted).
//
// Fakes (5-byte jumps over the game's entry and, for a port, over ours): the Simulation's GetSimpleRigidBody,
// GetRigidBody, FindPhysicsObjectSignature, GetOrderedBody, DeletePhysicsObject and SpawnExplosion; PhysicsObject's
// six-argument constructor, Destruct, Simulate, SetRenderObject, ApplyForces, GetCollisionBounds, GetMass,
// GetPosition, GetSig and IsOwnedBy; RSceneObj's constructor, Hide, StopFX and LocateFX; UMemory::FastAlloc and
// FastFree; ABaseSound::OperatorNew, ASound's and AOneShotSound's constructors, AMenuSound::Trigger;
// WTargetPicker::GetWorldTarget, WTargetable::RemoveReference and GetVelocity; the light manager's
// AddPositionalLight and AddMissile; RMissileStreak's Update (both) and Draw; WWorldPos::FindClosestFace and
// HeightAtPoint; WCollisionMgr::CheckHitWorld and CheckHitWindow; SMissionManager::SetDriveMissile; and
// Shell::ApplyWorldDamage. The other objects' virtual methods (ApplyDamage, GetCarType, AddDamageByPlayer,
// AttackWithEmp, GetTargetBeacon) and the sounds' deleting destructor are fakes in vtables of the test's own.
// Values the originals leave as uninitialised stack (vectors' w) are not logged.
//
// A mutation the test sees: SteerMissile's homing slerp fraction rounded to float before the product
// (float(ramp) * float(rate)): the simple body's orientation and velocity differ in their low bits.
// ---------------------------------------------------------------------------------------------------------------

namespace {

// ---- the originals

const unsigned kEntries[] = {
    0x0005cd60, 0x0005cd70, 0x0005ce80, 0x0005ceb0, 0x0005d3f0, 0x0005e8c0, 0x0005f350, 0x0005f3d0,
    0x0005f600, 0x0005f640, 0x0005f680, 0x0005f6a0, 0x0005f6d0, 0x00060110, 0x00060a30,
};

#define Orig_ExplosionDestruct ((void (__fastcall *)(void *, int))0x0005cd60)
#define Orig_ExplosionConstruct ((void *(__fastcall *)(void *, int, int, int, const Coord3 *, float, PhysicsObject *, const uint32_t *))0x0005cd70)
#define Orig_ExplosionDelete ((void *(__fastcall *)(void *, int, unsigned))0x0005ce80)
#define Orig_ExplosionSimulate ((void (__fastcall *)(void *, int))0x0005ceb0)
#define Orig_FaceNormal ((void (__fastcall *)(const void *, int, Coord3 *))0x0005d3f0)
#define Orig_MissileConstruct ((void *(__fastcall *)(void *, int, int, float, const Coord3 *, const Coord3 *, uint32_t, float, WTargetable *, int))0x0005e8c0)
#define Orig_MissileDestruct ((void (__fastcall *)(void *, int))0x0005f350)
#define Orig_MissileRender ((void (__fastcall *)(void *, int, bool))0x0005f3d0)
#define Orig_SetUserSteerX ((void (__fastcall *)(void *, int, float))0x0005f600)
#define Orig_SetUserSteerY ((void (__fastcall *)(void *, int, float))0x0005f640)
#define Orig_SetTarget ((void (__fastcall *)(void *, int, WTargetable *))0x0005f680)
#define Orig_MissileDelete ((void *(__fastcall *)(void *, int, unsigned))0x0005f6a0)
#define Orig_CheckForCollision ((void (__fastcall *)(void *, int))0x0005f6d0)
#define Orig_SteerMissile ((void (__fastcall *)(void *, int))0x00060110)
#define Orig_MissileSimulate ((void (__fastcall *)(void *, int))0x00060a30)

// ---- the game's state both sides read

#define ShadowSimTimeStep FLOAT_AT(0x00234e30)
#define ShadowSimStepCount I32_AT(0x00234e34)
#define ShadowSimState I32_AT(0x00234e24)
#define ShadowPlayerObjects PTR_AT(0x00234e40)
#define ShadowMissionManager PTR_AT(0x00239220)
#define ShadowSimulationRandom (*(SimRandom **)0x00233ff0)
#define ShadowTuningLevel ((char *)0x00244084)

// ---- results

bool g_original;
long g_cases, g_checks, g_differ, g_faults;
int g_reported;
unsigned g_x87, g_sse;

void Report(const char *what, long index, const char *detail) {
    g_differ++;
    if (g_reported++ < 10)
        printf("[gameweaponsshadowb]   %s #%ld: %s\n", what, index, detail);
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

uint32_t g_seed = 0x3b1e5c27;

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

Coord4 RandomUnit(float w) {
    for (;;) {
        Coord4 v = { Uniform(-1.0f, 1.0f), Uniform(-1.0f, 1.0f), Uniform(-1.0f, 1.0f), w };
        float length = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
        if (length > 0.1f) {
            v.x /= length;
            v.y /= length;
            v.z /= length;
            return v;
        }
    }
}

Coord4 RandomQuaternion() {
    for (;;) {
        Coord4 q = { Uniform(-1.0f, 1.0f), Uniform(-1.0f, 1.0f), Uniform(-1.0f, 1.0f), Uniform(-1.0f, 1.0f) };
        float length = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
        if (length > 0.1f) {
            q.x /= length;
            q.y /= length;
            q.z /= length;
            q.w /= length;
            return q;
        }
    }
}

void FillRandom(void *block, size_t bytes) {
    uint8_t *p = static_cast<uint8_t *>(block);
    for (size_t i = 0; i < bytes; i++)
        p[i] = uint8_t(Next());
}

// ---- the regions a case may change: the game's addresses both sides use, and our globals with their game copies

struct Region {
    uint8_t *game;
    uint8_t *ours;          // NULL: both sides use the game's address
    uint32_t size;
    bool compare;
    const char *name;
};

std::vector<Region> g_regions;

void AddShared(const void *at, uint32_t size, const char *name, bool compare = true) {
    if (at != NULL)
        g_regions.push_back({ (uint8_t *)at, NULL, size, compare, name });
}

void AddOwned(void *ours, uint32_t game, uint32_t size, const char *name) {
    g_regions.push_back({ reinterpret_cast<uint8_t *>(uintptr_t(game)), static_cast<uint8_t *>(ours), size, true,
                          name });
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

// Our globals into the game's copies, so an original starts where the port does
void Lend() {
    for (const Region &r : g_regions)
        if (r.ours != NULL)
            memcpy(r.game, r.ours, r.size);
}

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

void LogPointer(const void *pointer) {
    Log(uint32_t(uintptr_t(pointer)));
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

void LogXyzw(const void *v) {
    LogXyz(v);
    if (v != NULL)
        LogFloat(static_cast<const float *>(v)[3]);
}

void LogString(const char *text) {
    if (text == NULL) {
        Log(0);
        return;
    }
    uint32_t hash = 2166136261u;
    for (const char *c = text; *c != '\0'; c++)
        hash = (hash ^ uint8_t(*c)) * 16777619u;
    Log(hash);
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

// ---- the synthetic objects

constexpr int kSlots = 8;               // the simple bodies the fakes hand out, by slot
constexpr int kOthers = 12;             // the other objects
constexpr int kOwnerSlot = 90;          // fgSimulation.simpleRigidBodyOwners' rows lent to a signature's owner
constexpr uint32_t kSelfSig = 0x5e1f0000;

alignas(16) uint8_t g_missileBlock[sizeof(Missile)];
alignas(16) uint8_t g_explosionBlock[0xa0];
alignas(16) uint8_t g_simpleBlock[kSlots][sizeof(SimpleRigidBody)];
alignas(16) uint8_t g_rigidBlock[sizeof(RigidBody)];
alignas(16) uint8_t g_infoBlock[sizeof(RigidBodyInfo)];
alignas(16) uint8_t g_otherBlocks[kOthers][0x80];
alignas(16) uint8_t g_targetBlocks[3][sizeof(WTargetable)];
alignas(16) uint8_t g_soundBlock[0x120];
alignas(16) uint8_t g_sceneBlock[0x80];
alignas(16) uint8_t g_handleBlock[sizeof(Handle)];
alignas(16) uint8_t g_effectBlock[0x40];
alignas(16) uint8_t g_managerBlock[0x800];
alignas(16) Coord4 g_out[2];

Missile *const g_missile = reinterpret_cast<Missile *>(g_missileBlock);
Explosion *const g_explosion = reinterpret_cast<Explosion *>(g_explosionBlock);
SimpleRigidBody *Simple(int slot) { return reinterpret_cast<SimpleRigidBody *>(g_simpleBlock[slot & (kSlots - 1)]); }
RigidBody *const g_rigid = reinterpret_cast<RigidBody *>(g_rigidBlock);
RigidBodyInfo *const g_info = reinterpret_cast<RigidBodyInfo *>(g_infoBlock);
PhysicsObject *Other(int i) { return reinterpret_cast<PhysicsObject *>(g_otherBlocks[i]); }
WTargetable *Target(int i) { return reinterpret_cast<WTargetable *>(g_targetBlocks[i]); }
SMissionManager *const g_manager = reinterpret_cast<SMissionManager *>(g_managerBlock);
PhysicsObject *g_playerList[2];

// The scratch heap FastAlloc and the sounds' operator new hand out from
struct Heap {
    uint32_t used;
    uint32_t pad[3];
    alignas(16) uint8_t bytes[0x1000];
} g_heap;

void *HeapAlloc(unsigned size) {
    if (g_heap.used + size > sizeof(g_heap.bytes))
        return NULL;
    void *block = &g_heap.bytes[g_heap.used];
    g_heap.used += (size + 15) & ~15u;
    return block;
}

// What the fakes answer, set for each case (inputs only)
struct Answers {
    int slot;                       // the slot PhysicsObject's constructor gives
    PhysicsObject *signatureOwner;  // FindPhysicsObjectSignature's
    PhysicsObject *ownedBy;         // IsOwnedBy is true for this one ...
    bool ownedByPlayer;             // ... and for the player when set
    PhysicsObject *ordered[160];    // GetOrderedBody's
    alignas(16) Coord4 positions[kOthers];
    float masses[kOthers];
    bool hasBounds[kOthers];
    alignas(16) Coord4 bounds[kOthers];
    uint32_t sigs[kOthers];
    const char *carTypes[kOthers];
    WTargetable *beacon;
    bool hasVelocity;
    alignas(16) Coord4 targetVelocity;
    bool faceValid;
    double ground;
    bool hit, window;
    uint8_t faceType;
    alignas(16) Coord4 hitPoint;
    alignas(16) MATRIX4 locator;
} g_answers;

int OtherIndex(const void *object) {
    for (int i = 0; i < kOthers; i++)
        if (object == g_otherBlocks[i])
            return i;
    return -1;
}

// ---- the fakes

enum : uint32_t {
    kTagConstruct = 0x50430001, kTagDestruct, kTagSimulate, kTagSetRender, kTagForces, kTagOwnedBy,
    kTagScene, kTagHide, kTagStopFX, kTagLocate, kTagAlloc, kTagFree, kTagSoundNew, kTagSound, kTagMenu,
    kTagWorldTarget, kTagRemoveRef, kTagVelocity, kTagLight, kTagMissileLight, kTagTick, kTagUpdate, kTagDraw,
    kTagFindFace, kTagHeight, kTagHitWorld, kTagHitWindow, kTagFindSig, kTagDelete, kTagExplosion, kTagDrive,
    kTagOneShot, kTagWorldDamage, kTagDamage, kTagCarType, kTagByPlayer, kTagEmp, kTagBeacon, kTagSoundDelete,
    kTagStray,
};

SimpleRigidBody *__fastcall FakeGetSimple(void *, int, int slot) {
    return Simple(slot);
}

RigidBody *__fastcall FakeGetRigid(void *, int, int) {
    return g_rigid;
}

PhysicsObject *__fastcall FakeFindSig(void *, int, uint32_t sig) {
    Log(kTagFindSig);
    Log(sig);
    return g_answers.signatureOwner;
}

PhysicsObject *__fastcall FakeOrdered(void *, int, int index) {
    return index >= 0 && index < 160 ? g_answers.ordered[index] : NULL;
}

void __fastcall FakeDeleteObject(void *, int, PhysicsObject *object) {
    Log(kTagDelete);
    LogPointer(object);
}

void __fastcall FakeSpawnExplosion(void *, int, int type, int weapon, const Coord4 *direction, const float *position,
                                   float damage, int faceType, PhysicsObject *directHit, const uint32_t *sig) {
    Log(kTagExplosion);
    Log(uint32_t(type));
    Log(uint32_t(weapon));
    LogXyz(direction);
    LogXyz(position);
    LogFloat(damage);
    Log(uint32_t(faceType));
    LogPointer(directHit);
    LogPointer(sig);
}

void __fastcall FakeSetDrive(void *, int, Missile *missile) {
    Log(kTagDrive);
    LogPointer(missile);
}

void *__fastcall FakeOneShot(void *block, int, int bank, int patch, const Coord3 *position, const Coord3 *velocity,
                             float volume, const char *mix) {
    Log(kTagOneShot);
    LogPointer(block);
    Log(uint32_t(bank));
    Log(uint32_t(patch));
    LogXyz(position);
    LogXyz(velocity);
    LogFloat(volume);
    LogString(mix);
    memset(block, 0, 0x120);
    return block;
}

void FakeWorldDamage(float damage, WWorldPos *place, float size, int type, const WorldCollisionInfo *info) {
    Log(kTagWorldDamage);
    LogFloat(damage);
    LogPointer(place);
    LogFloat(size);
    Log(uint32_t(type));
    LogXyzw(&info->point);
    Log(info->faceType);
    Log(info->hitType);
}

PhysicsObject *__fastcall FakePOConstruct(PhysicsObject *self, int, const char *className, const char *name, int type,
                                          uint32_t ownerSig, bool flag2, bool flag4) {
    Log(kTagConstruct);
    LogString(className);
    LogString(name);
    Log(uint32_t(type));
    Log(ownerSig);
    Log(flag2);
    Log(flag4);
    memset(self, 0, sizeof(PhysicsObject));
    self->vtable = (void **)0x0018f9a0;
    self->type = type;
    self->flags = PhysicsObject::kSimpleBody;
    self->rigidBodySlot = int16_t(g_answers.slot);
    self->ownerSig = ownerSig;
    return self;
}

void __fastcall FakePODestruct(PhysicsObject *self, int) {
    Log(kTagDestruct);
    LogPointer(self);
}

void __fastcall FakePOSimulate(PhysicsObject *self, int) {
    Log(kTagSimulate);
    LogPointer(self);
}

void __fastcall FakeSetRender(PhysicsObject *self, int, RSceneObj *object) {
    Log(kTagSetRender);
    LogPointer(self);
    LogPointer(object);
    self->renderObject = object;
}

void __fastcall FakeApplyForces(PhysicsObject *self, int, const Coord3 *force, const Coord3 *torque) {
    Log(kTagForces);
    LogPointer(self);
    LogXyz(force);
    LogXyz(torque);
}

bool __fastcall FakeBounds(PhysicsObject *self, int, Coord4 *bounds) {
    int i = OtherIndex(self);
    if (i < 0 || !g_answers.hasBounds[i])
        return false;
    *bounds = g_answers.bounds[i];
    return true;
}

float __fastcall FakeMass(PhysicsObject *self, int) {
    int i = OtherIndex(self);
    return i < 0 ? 1.0f : g_answers.masses[i];
}

Coord3 *__fastcall FakePosition(PhysicsObject *self, int) {
    int i = OtherIndex(self);
    if (i < 0)
        return reinterpret_cast<Coord3 *>(&g_answers.hitPoint);
    return reinterpret_cast<Coord3 *>(&g_answers.positions[i]);
}

uint32_t *__fastcall FakeGetSig(PhysicsObject *self, int, uint32_t *result) {
    int i = OtherIndex(self);
    *result = i < 0 ? kSelfSig | uint32_t(self->rigidBodySlot) : g_answers.sigs[i];
    return result;
}

bool __fastcall FakeOwnedBy(PhysicsObject *self, int, PhysicsObject *owner) {
    Log(kTagOwnedBy);
    LogPointer(self);
    LogPointer(owner);
    if (owner == NULL)
        return false;
    return owner == g_answers.ownedBy || (owner == g_playerList[0] && g_answers.ownedByPlayer);
}

RSceneObj *__fastcall FakeSceneConstruct(RSceneObj *self, int, void *instance) {
    Log(kTagScene);
    LogPointer(self);
    LogPointer(instance);
    return self;
}

void __fastcall FakeHide(RSceneObj *self, int) {
    Log(kTagHide);
    LogPointer(self);
}

void __fastcall FakeStopFX(RSceneObj *self, int) {
    Log(kTagStopFX);
    LogPointer(self);
}

void __fastcall FakeLocateFX(RSceneObj *self, int, const void *effect, MATRIX4 *transform, bool world) {
    Log(kTagLocate);
    LogPointer(self);
    LogPointer(effect);
    Log(world);
    *transform = g_answers.locator;
}

void *FakeFastAlloc(unsigned size, const char *name) {
    Log(kTagAlloc);
    Log(size);
    LogString(name);
    return HeapAlloc(size);
}

void FakeFastFree(void *block, unsigned size) {
    Log(kTagFree);
    LogPointer(block);
    Log(size);
}

void *FakeSoundNew(unsigned size, const char *name) {
    Log(kTagSoundNew);
    Log(size);
    LogString(name);
    return HeapAlloc(size);
}

void *g_soundVtable[8];

void *__fastcall FakeSoundConstruct(void *self, int, int bank, int patch, const char *mix) {
    Log(kTagSound);
    LogPointer(self);
    Log(uint32_t(bank));
    Log(uint32_t(patch));
    LogString(mix);
    memset(self, 0, 0x120);
    *static_cast<void ***>(self) = g_soundVtable;
    return self;
}

void FakeMenuTrigger(int bank, const char *patch, const char *mix, int view) {
    Log(kTagMenu);
    Log(uint32_t(bank));
    LogString(patch);
    LogString(mix);
    Log(uint32_t(view));
}

WTargetable *__fastcall FakeWorldTarget(void *, int) {
    Log(kTagWorldTarget);
    return Target(2);
}

void __fastcall FakeRemoveRef(WTargetable *self, int) {
    Log(kTagRemoveRef);
    LogPointer(self);
}

bool __fastcall FakeGetVelocity(WTargetable *self, int, Coord3 *velocity) {
    Log(kTagVelocity);
    LogPointer(self);
    velocity->x = g_answers.targetVelocity.x;
    velocity->y = g_answers.targetVelocity.y;
    velocity->z = g_answers.targetVelocity.z;
    return g_answers.hasVelocity;
}

void __fastcall FakeAddLight(void *manager, int, const Coord4 *position, const Coord4 *colour) {
    Log(kTagLight);
    LogPointer(manager);
    LogXyzw(position);
    LogXyzw(colour);
}

void __fastcall FakeAddMissileLight(void *manager, int, const Coord4 *position, int type, float scale) {
    Log(kTagMissileLight);
    LogPointer(manager);
    LogXyzw(position);
    Log(uint32_t(type));
    LogFloat(scale);
}

void __fastcall FakeStreakTick(RMissileStreak *self, int) {
    Log(kTagTick);
    LogPointer(self);
}

void __fastcall FakeStreakUpdate(RMissileStreak *self, int, const Coord4 *position) {
    Log(kTagUpdate);
    LogPointer(self);
    LogXyzw(position);
}

void __fastcall FakeStreakDraw(RMissileStreak *self, int) {
    Log(kTagDraw);
    LogPointer(self);
}

bool __fastcall FakeFindFace(WWorldPos *self, int, const Coord3 *point, bool keepCurrent) {
    Log(kTagFindFace);
    LogPointer(self);
    LogXyz(point);
    Log(keepCurrent);
    self->valid = g_answers.faceValid;
    return true;
}

double __fastcall FakeHeight(WWorldPos *self, int, const Coord3 *point, bool flag) {
    Log(kTagHeight);
    LogPointer(self);
    LogXyz(point);
    Log(flag);
    return g_answers.ground;
}

int __fastcall FakeHitWorld(void *manager, int, const Coord4 *segment, WorldCollisionInfo *info) {
    Log(kTagHitWorld);
    LogPointer(manager);
    // The start's w is CheckForCollision's stack in the original (VU0_v4sub at 0x0005fd92 writes xyz): not logged
    LogXyz(&segment[0]);
    LogXyzw(&segment[1]);
    // What the caller set (the normal and the words it leaves alone are its stack's)
    LogXyzw(&info->point);
    LogXyz(&info->segmentStart);
    LogXyzw(&info->segmentEnd);
    LogPointer(info->instance);
    Log(info->groupId);
    LogPointer(info->object);
    Log(info->faceType | info->faceSubType << 8 | info->flag52 << 16 | uint32_t(info->hitType) << 24);
    Log(info->unknown54);
    LogPointer(info->faceInstance);
    if (!g_answers.hit)
        return 0;
    info->point = g_answers.hitPoint;
    info->faceType = g_answers.faceType;
    info->hitType = 1;
    return 1;
}

bool __fastcall FakeHitWindow(void *manager, int, WorldCollisionInfo *info, bool create, int kind) {
    Log(kTagHitWindow);
    LogPointer(manager);
    LogXyzw(&info->point);
    Log(create);
    Log(uint32_t(kind));
    return g_answers.window;
}

// The other objects' vtable
int __fastcall FakeApplyDamage(PhysicsObject *self, int, const void *unknown1, const void *unknown2, float amount,
                               float unknown4, int kind, const uint32_t *sourceSig) {
    Log(kTagDamage);
    LogPointer(self);
    LogXyz(unknown1);
    LogXyz(unknown2);
    LogFloat(amount);
    LogFloat(unknown4);
    Log(uint32_t(kind));
    Log(sourceSig != NULL ? *sourceSig : 0xdeadbeef);
    return 0x20;
}

const char *__fastcall FakeGetCarType(PhysicsObject *self, int) {
    Log(kTagCarType);
    LogPointer(self);
    int i = OtherIndex(self);
    return i < 0 ? "none" : g_answers.carTypes[i];
}

void __fastcall FakeAddDamageByPlayer(PhysicsObject *self, int, float damage) {
    Log(kTagByPlayer);
    LogPointer(self);
    LogFloat(damage);
}

void __fastcall FakeAttackWithEmp(PhysicsObject *self, int) {
    Log(kTagEmp);
    LogPointer(self);
}

WTargetable *__fastcall FakeGetTargetBeacon(PhysicsObject *self, int) {
    Log(kTagBeacon);
    LogPointer(self);
    return g_answers.beacon;
}

void __fastcall FakeStraySlot(void *self, int) {
    Log(kTagStray);
    LogPointer(self);
}

void *__fastcall FakeSoundDelete(void *self, int, unsigned flags) {
    Log(kTagSoundDelete);
    LogPointer(self);
    Log(flags);
    return self;
}

void *g_otherVtable[PVehicle::kSlotCount];

void BuildVtables() {
    for (void *&slot : g_otherVtable)
        slot = (void *)&FakeStraySlot;
    g_otherVtable[2] = (void *)&FakeApplyDamage;
    g_otherVtable[PVehicle::kGetCarType] = (void *)&FakeGetCarType;
    g_otherVtable[PVehicle::kAddDamageByPlayer] = (void *)&FakeAddDamageByPlayer;
    g_otherVtable[PVehicle::kAttackWithEmp] = (void *)&FakeAttackWithEmp;
    g_otherVtable[PVehicle::kGetTargetBeacon] = (void *)&FakeGetTargetBeacon;
    for (void *&slot : g_soundVtable)
        slot = (void *)&FakeStraySlot;
    g_soundVtable[0] = (void *)&FakeSoundDelete;
}

// ---- hooks: a jump written over an entry and, when the entry jumps to our port, over the port

struct Hook {
    uint32_t at;
    uint8_t saved[5];
    bool on;
};
Hook g_hooks[96];
int g_hookCount;
int g_hooksMissing;

bool Patchable(uint32_t at) {
    const uint8_t *code = reinterpret_cast<const uint8_t *>(uintptr_t(at));
    for (int i = 0; i < 5; i++)
        if (code[i] == 0xc3 || code[i] == 0xc2 || code[i] == 0xcc)
            return false;
    return true;
}

void HookOne(uint32_t at, const void *to) {
    Hook &h = g_hooks[g_hookCount++];
    h.at = at;
    h.on = false;
    DWORD old;
    if (!VirtualProtect((void *)uintptr_t(at), 5, PAGE_EXECUTE_READWRITE, &old))
        return;
    memcpy(h.saved, (void *)uintptr_t(at), 5);
    uint8_t jump[5] = { 0xe9 };
    int32_t relative = int32_t(uint32_t(uintptr_t(to)) - (at + 5));
    memcpy(jump + 1, &relative, 4);
    memcpy((void *)uintptr_t(at), jump, 5);
    VirtualProtect((void *)uintptr_t(at), 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)uintptr_t(at), 5);
    h.on = true;
}

// A port too short to take a jump is left alone, and its entry with it: both sides then reach the real code
void HookInstall(uint32_t at, const void *to) {
    if (g_hookCount + 2 > int(sizeof(g_hooks) / sizeof(g_hooks[0]))) {
        g_hooksMissing++;
        return;
    }
    const uint8_t *entry = reinterpret_cast<const uint8_t *>(uintptr_t(at));
    uint32_t port = 0;
    if (entry[0] == 0xe9) {
        int32_t relative;
        memcpy(&relative, entry + 1, 4);
        port = at + 5 + uint32_t(relative);
    }
    if ((port != 0 && !Patchable(port)) || !Patchable(at)) {
        printf("[gameweaponsshadowb]   no fake at 0x%08x (ours at 0x%08x)\n", at, port);
        g_hooksMissing++;
        return;
    }
    HookOne(at, to);
    if (port != 0)
        HookOne(port, to);
}

void HooksRemove() {
    for (int i = g_hookCount; i-- > 0;) {
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
    g_hookCount = 0;
}

void InstallHooks() {
    HookInstall(0x000b2730, (const void *)&FakeGetSimple);
    HookInstall(0x000b2700, (const void *)&FakeGetRigid);
    HookInstall(0x000b27d0, (const void *)&FakeFindSig);
    HookInstall(0x000b28d0, (const void *)&FakeOrdered);
    HookInstall(0x000b4740, (const void *)&FakeDeleteObject);
    HookInstall(0x000b4cd0, (const void *)&FakeSpawnExplosion);
    HookInstall(0x000b80b0, (const void *)&FakeSetDrive);
    HookInstall(0x000476f0, (const void *)&FakeOneShot);
    HookInstall(0x00074670, (const void *)&FakeWorldDamage);
    HookInstall(0x0006f190, (const void *)&FakePOConstruct);
    HookInstall(0x0006f680, (const void *)&FakePODestruct);
    HookInstall(0x0006f4c0, (const void *)&FakePOSimulate);
    HookInstall(0x0006f3f0, (const void *)&FakeSetRender);
    HookInstall(0x0006f2a0, (const void *)&FakeApplyForces);
    HookInstall(0x0006f520, (const void *)&FakeBounds);
    HookInstall(0x0006f240, (const void *)&FakeMass);
    HookInstall(0x0006f330, (const void *)&FakePosition);
    HookInstall(0x0006f390, (const void *)&FakeGetSig);
    HookInstall(0x0006f5b0, (const void *)&FakeOwnedBy);
    HookInstall(0x0008eec0, (const void *)&FakeSceneConstruct);
    HookInstall(0x0008db30, (const void *)&FakeHide);
    HookInstall(0x0008eb60, (const void *)&FakeStopFX);
    HookInstall(0x0008eca0, (const void *)&FakeLocateFX);
    HookInstall(0x00114750, (const void *)&FakeFastAlloc);
    HookInstall(0x001147d0, (const void *)&FakeFastFree);
    HookInstall(0x0011c910, (const void *)&FakeSoundNew);
    HookInstall(0x00127a70, (const void *)&FakeSoundConstruct);
    HookInstall(0x0011e010, (const void *)&FakeMenuTrigger);
    HookInstall(0x000cdfd0, (const void *)&FakeWorldTarget);
    HookInstall(0x000cdb80, (const void *)&FakeRemoveRef);
    HookInstall(0x000cd740, (const void *)&FakeGetVelocity);
    HookInstall(0x0007e440, (const void *)&FakeAddLight);
    HookInstall(0x0007e6f0, (const void *)&FakeAddMissileLight);
    HookInstall(0x000a0e90, (const void *)&FakeStreakTick);
    HookInstall(0x000a0ea0, (const void *)&FakeStreakUpdate);
    HookInstall(0x000a1330, (const void *)&FakeStreakDraw);
    HookInstall(0x000d31f0, (const void *)&FakeFindFace);
    HookInstall(0x000d2f90, (const void *)&FakeHeight);
    HookInstall(0x000c3b40, (const void *)&FakeHitWorld);
    HookInstall(0x000c4d70, (const void *)&FakeHitWindow);
    if (g_hooksMissing != 0)
        printf("[gameweaponsshadowb] %d fakes could not be installed - both sides reach the real code there\n",
               g_hooksMissing);
}

// ---- running a case

typedef void (*SideFn)(void);

void Window(bool original) {
    for (unsigned at : kEntries)
        XbeOriginal_Restore(at, original);
}

void RunSide(SideFn run, bool original) {
    g_original = original;
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

// The inputs are in place (ours): both sides from them, the results compared, everything put back
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

// ---- one case's inputs

struct Inputs {
    alignas(16) Coord4 corners[3];          // the face
    int type, weapon, variant;
    float size, speed, damage, steer;
    alignas(16) Coord4 position, direction;
    PhysicsObject *directHit;
    uint32_t sig;
    WTargetable *target;
    unsigned flags;
    bool firstView;
} g_in;

// The common state: the scratch mission manager and player list in place, the step's globals, the other objects
void SetWorld() {
    memset(g_managerBlock, 0, sizeof(g_managerBlock));
    ShadowMissionManager = g_manager;
    g_playerList[0] = Other(0);
    g_playerList[1] = NULL;
    ShadowPlayerObjects = g_playerList;
    ShadowSimStepCount = Range(100, 100000);
    ShadowSimTimeStep = Chance(80) ? 1.0f / 60.0f : Uniform(0.005f, 0.05f);
    ShadowSimState = Chance(80) ? 1 : Range(0, 4);
    fgSimulation.stepsPerSecond = Chance(80) ? 60 : Range(10, 120);
    memset(g_answers.ordered, 0, sizeof(g_answers.ordered));
    for (int i = 0; i < kOthers; i++) {
        PhysicsObject *other = Other(i);
        FillRandom(other, 0x80);
        other->vtable = g_otherVtable;
        other->type = i == 0 || Chance(40) ? 1 : Range(2, 11);
        other->flags = uint16_t(Chance(50) ? PhysicsObject::kSimpleBody : 0);
        other->rigidBodySlot = int16_t(Range(0, kSlots - 1));
        g_answers.masses[i] = Chance(10) ? 0.0f : Uniform(1.0f, 3000.0f);
        g_answers.hasBounds[i] = Chance(70);
        float a = Uniform(0.05f, 3.0f), b = Uniform(0.05f, 3.0f), c = Chance(30) ? Uniform(10.0f, 80.0f) : Uniform(0.05f, 3.0f);
        int order = Range(0, 2);
        g_answers.bounds[i] = order == 0 ? Coord4{ a, b, c, 0.0f } : order == 1 ? Coord4{ c, a, b, 0.0f } : Coord4{ b, c, a, 0.0f };
        g_answers.sigs[i] = Next();
        g_answers.carTypes[i] = Chance(15) ? "tunnel" : Chance(20) ? "alpha_sub" : "car";
    }
    for (int i = 0; i < 3; i++) {
        WTargetable *target = Target(i);
        FillRandom(target, sizeof(WTargetable));
        target->refCount = 100;
        target->isTemporary = uint8_t(Chance(30));
        target->ownerType = Chance(40) ? kTargetPoint : Range(1, 3);
    }
    g_answers.ownedBy = NULL;
    g_answers.ownedByPlayer = false;
    g_answers.signatureOwner = NULL;
    g_answers.beacon = NULL;
}

// A simple body at `slot`, near `centre`
void SetBody(int slot, const Coord4 &centre) {
    SimpleRigidBody *body = Simple(slot);
    FillRandom(body, sizeof(SimpleRigidBody));
    body->orientation = RandomQuaternion();
    body->position = { centre.x + Uniform(-5.0f, 5.0f), centre.y + Uniform(-5.0f, 5.0f), centre.z + Uniform(-5.0f, 5.0f) };
    body->slot = int8_t(slot);
    Coord4 heading = RandomUnit(0.0f);
    float speed = Chance(10) ? 0.0f : Uniform(1.0f, 300.0f);
    body->velocity = { heading.x * speed, (Chance(60) ? heading.y * 0.1f : heading.y) * speed, heading.z * speed };
    body->radius = Uniform(0.1f, 2.0f);
    body->acceleration = { 0.0f, 0.0f, 0.0f };
    body->mass = 4.0f;
    body->flags = uint16_t(Next());
}

// Bits for some of the other objects in the slot's row of fgSimulation.simpleBodyHits, and the ordered bodies they map to
void SetHits(int slot, int count) {
    BitSet192 &row = fgSimulation.simpleBodyHits[slot];
    row = {};
    for (int n = 0; n < count; n++) {
        int index = Range(0, 159);
        int other = Range(0, kOthers - 1);
        g_answers.ordered[index] = Other(other);
        row.words[index / 32] |= 1u << (index % 32);
    }
}

void PlaceOthers(const Coord4 &centre, float spread) {
    for (int i = 0; i < kOthers; i++)
        g_answers.positions[i] = { centre.x + Uniform(-spread, spread), centre.y + Uniform(-spread, spread),
                                   centre.z + Uniform(-spread, spread), 1.0f };
}

// ---- the face normal

void SideFaceNormal() {
    Coord3 *normal = reinterpret_cast<Coord3 *>(&g_out[0]);
    if (g_original)
        Orig_FaceNormal(g_in.corners, 0, normal);
    else
        reinterpret_cast<const StripTriangle *>(g_in.corners)->GetNormal(normal);
}

void TestFaceNormal(const Snapshot &before) {
    for (long i = 0; i < 2000; i++) {
        for (int c = 0; c < 3; c++)
            g_in.corners[c] = { Uniform(-100.0f, 100.0f), Uniform(-20.0f, 20.0f), Uniform(-100.0f, 100.0f),
                                Uniform(-1.0f, 1.0f) };
        int shape = Range(0, 9);
        if (shape == 0) {
            g_in.corners[1] = g_in.corners[0];                      // degenerate
        } else if (shape == 1) {
            for (int c = 0; c < 3; c++)
                g_in.corners[c].y = g_in.corners[0].y;              // flat: straight up, clamped
        } else if (shape == 2) {
            g_in.corners[2] = { 2 * g_in.corners[1].x - g_in.corners[0].x, 2 * g_in.corners[1].y - g_in.corners[0].y,
                                2 * g_in.corners[1].z - g_in.corners[0].z, 0.0f };   // collinear
        } else if (shape == 3) {
            for (int c = 0; c < 3; c++)
                g_in.corners[c].y = g_in.corners[0].y + Uniform(-0.01f, 0.01f);     // nearly flat
        } else if (shape == 4) {
            for (int c = 0; c < 3; c++)
                g_in.corners[c].x = g_in.corners[0].x;              // vertical wall
        }
        g_out[0] = { -7.0f, -7.0f, -7.0f, 42.0f };
        RunCase("FUN_0005d3f0", i, SideFaceNormal, before);
    }
}

// ---- Explosion

void SideExplosionConstruct() {
    const Coord3 *position = reinterpret_cast<const Coord3 *>(&g_in.position);
    if (g_original)
        Orig_ExplosionConstruct(g_explosion, 0, g_in.type, int(Next()), position, g_in.size, g_in.directHit, &g_in.sig);
    else
        g_explosion->Construct(g_in.type, 0, position, g_in.size, g_in.directHit, &g_in.sig);
}

void SideExplosionDestruct() {
    if (g_original)
        Orig_ExplosionDestruct(g_explosion, 0);
    else
        g_explosion->Destruct();
}

void SideExplosionDelete() {
    if (g_original)
        Orig_ExplosionDelete(g_explosion, 0, g_in.flags);
    else
        g_explosion->Delete(g_in.flags);
}

void SideExplosionSimulate() {
    if (g_original)
        Orig_ExplosionSimulate(g_explosion, 0);
    else
        g_explosion->Simulate();
}

void SetExplosion(int slot) {
    FillRandom(g_explosionBlock, sizeof(g_explosionBlock));
    g_explosion->vtable = (void **)0x0018ed58;
    g_explosion->type = 3;
    g_explosion->flags = PhysicsObject::kSimpleBody;
    g_explosion->rigidBodySlot = int16_t(slot);
    g_explosion->renderObject = NULL;
    g_explosion->explosionType = Range(0, 7);
    g_explosion->size = Chance(10) ? 0.0f : Chance(70) ? Uniform(1.0f, 400.0f) : Uniform(400.0f, 30000.0f);
    g_explosion->directHit = Chance(30) ? Other(Range(0, kOthers - 1)) : NULL;
    for (uint32_t &word : g_explosion->bodiesHit.words)
        word = Chance(50) ? 0 : Next() & Next();
}

void TestExplosions(const Snapshot &before) {
    for (long i = 0; i < 300; i++) {
        SetWorld();
        g_answers.slot = Range(0, kSlots - 1);
        FillRandom(g_explosionBlock, sizeof(g_explosionBlock));
        FillRandom(Simple(g_answers.slot), sizeof(SimpleRigidBody));
        g_in.type = Range(0, 7);
        g_in.position = { Uniform(-500.0f, 500.0f), Uniform(-50.0f, 50.0f), Uniform(-500.0f, 500.0f), 1.0f };
        g_in.size = Uniform(0.0f, 2000.0f);
        g_in.directHit = Chance(50) ? Other(Range(0, kOthers - 1)) : NULL;
        g_in.sig = Next();
        RunCase("Explosion::Construct", i, SideExplosionConstruct, before);
    }
    for (long i = 0; i < 40; i++) {
        SetWorld();
        SetExplosion(Range(0, kSlots - 1));
        g_in.flags = unsigned(Range(0, 3));
        RunCase(i % 2 ? "Explosion::Delete" : "Explosion::Destruct", i,
                i % 2 ? SideExplosionDelete : SideExplosionDestruct, before);
    }
    for (long i = 0; i < 1500; i++) {
        SetWorld();
        int slot = Range(0, kSlots - 1);
        SetExplosion(slot);
        Coord4 centre = { Uniform(-500.0f, 500.0f), Uniform(-50.0f, 50.0f), Uniform(-500.0f, 500.0f), 1.0f };
        SetBody(slot, centre);
        Simple(slot)->radius = Chance(10) ? Uniform(140.0f, 160.0f) : Uniform(0.0f, 60.0f);
        Simple(slot)->velocity = { 0.0f, 0.0f, 0.0f };
        PlaceOthers(centre, Chance(50) ? 3.0f : 60.0f);
        SetHits(slot, Range(0, 8));
        g_manager->autoDrive = uint8_t(Chance(50));
        RunCase("Explosion::Simulate", i, SideExplosionSimulate, before);
    }
}

// ---- Missile

const int kWeapons[] = {
    kWeaponRocket, kWeaponMissile, kWeaponQPulse, kWeaponTorpedo, kWeaponRemoteTorpedo, kWeaponSnowmobileRocket,
    kWeaponUltralightRocket, kWeaponFodRocket, kWeaponCh6RemoteRocket,
};

int RandomWeapon() {
    if (Chance(10))
        return Range(-2, 36);
    return kWeapons[Range(0, int(sizeof(kWeapons) / sizeof(kWeapons[0])) - 1)];
}

void SideMissileConstruct() {
    const Coord3 *direction = reinterpret_cast<const Coord3 *>(&g_in.direction);
    const Coord3 *position = reinterpret_cast<const Coord3 *>(&g_in.position);
    if (g_original)
        Orig_MissileConstruct(g_missile, 0, g_in.weapon, g_in.speed, direction, position, g_in.sig, g_in.damage,
                              g_in.target, g_in.variant);
    else
        g_missile->Construct(g_in.weapon, g_in.speed, direction, position, g_in.sig, g_in.damage, g_in.target,
                             g_in.variant);
}

void SideMissileDestruct() {
    if (g_original)
        Orig_MissileDestruct(g_missile, 0);
    else
        g_missile->Destruct();
}

void SideMissileDelete() {
    if (g_original)
        Orig_MissileDelete(g_missile, 0, g_in.flags);
    else
        g_missile->Delete(g_in.flags);
}

void SideMissileRender() {
    if (g_original)
        Orig_MissileRender(g_missile, 0, g_in.firstView);
    else
        g_missile->Render(g_in.firstView);
}

void SideSteerX() {
    if (g_original)
        Orig_SetUserSteerX(g_missile, 0, g_in.steer);
    else
        g_missile->SetUserSteerX(g_in.steer);
}

void SideSteerY() {
    if (g_original)
        Orig_SetUserSteerY(g_missile, 0, g_in.steer);
    else
        g_missile->SetUserSteerY(g_in.steer);
}

void SideSetTarget() {
    if (g_original)
        Orig_SetTarget(g_missile, 0, g_in.target);
    else
        g_missile->SetTarget(g_in.target);
}

void SideCheckForCollision() {
    if (g_original)
        Orig_CheckForCollision(g_missile, 0);
    else
        g_missile->CheckForCollision();
}

void SideSteerMissile() {
    if (g_original)
        Orig_SteerMissile(g_missile, 0);
    else
        g_missile->SteerMissile();
}

void SideMissileSimulate() {
    if (g_original)
        Orig_MissileSimulate(g_missile, 0);
    else
        g_missile->Simulate();
}

// A flying missile at `slot` near `centre`, its target one of the scratch ones (or none)
void SetMissile(int slot, const Coord4 &centre) {
    FillRandom(g_missileBlock, sizeof(g_missileBlock));
    Missile *m = g_missile;
    m->vtable = (void **)0x0018eef8;
    m->worldPos.valid = uint8_t(Chance(50));
    m->type = 6;
    m->flags = PhysicsObject::kSimpleBody;
    m->rigidBodySlot = int16_t(slot);
    m->renderObject = reinterpret_cast<RSceneObj *>(g_sceneBlock);
    m->ownerSig = Next();
    m->target = Chance(25) ? NULL : Target(Range(0, 2));
    m->maxSpeed = Uniform(20.0f, 800.0f);
    m->thrust = Chance(20) ? m->maxSpeed * Uniform(1.0f, 2.0f) : Uniform(0.0f, 125.0f);
    m->turnRate = Chance(50) ? 1.0f : Uniform(0.0f, 1.0f);
    m->steerScale = 0.004f;
    int now = ShadowSimStepCount;
    m->homingDelay = Range(0, 40);
    m->launchStep = now - Range(0, 300);
    m->armStep = now + Range(-50, 20);
    m->lingerSteps = Chance(30) ? 0 : Range(1, 120);
    m->lifeSteps = Chance(10) ? 1 : Range(2, 600);
    m->sound = reinterpret_cast<ASound *>(g_soundBlock);
    m->weapon = RandomWeapon();
    m->steerX = Uniform(-0.05f, 0.05f);
    m->steerY = Uniform(-0.05f, 0.05f);
    m->userSteerX = Chance(30) ? 0.0f : Uniform(-0.006f, 0.006f);
    m->userSteerY = Chance(30) ? 0.0f : Uniform(-0.006f, 0.006f);
    m->homingFull = uint8_t(Chance(50));
    m->detonate = uint8_t(Chance(10));
    m->exploded = uint8_t(Chance(15));
    m->fadeIn = Chance(30) ? 1.0f : Uniform(0.0f, 1.2f);
    m->damage = Uniform(0.0f, 2000.0f);
    m->lockSoundStep = now + Range(-30, 30);
    m->wobbleX = Chance(40) ? 0.0f : Uniform(-1.0f, 1.0f);
    m->wobbleY = Uniform(-0.5f, 0.5f);
    m->wobblePhase = Uniform(0.0f, 2.5f);
    SetBody(slot, centre);
    memset(g_soundBlock, 0, sizeof(g_soundBlock));
    *reinterpret_cast<void ***>(g_soundBlock) = g_soundVtable;
    if (m->target != NULL) {
        float distance = Chance(30) ? Uniform(0.0f, 2.5f) : Uniform(0.0f, 200.0f);
        Coord4 away = RandomUnit(0.0f);
        const Coord3 &p = Simple(slot)->position;
        m->target->position = { p.x + away.x * distance, p.y + away.y * distance, p.z + away.z * distance };
    }
    // The other objects' rigid body for the box test
    FillRandom(g_rigidBlock, sizeof(g_rigidBlock));
    g_rigid->info = g_info;
    Coord4 q = RandomQuaternion();
    VU0_quattom4(&g_info->orientation, &q);
    g_info->halfExtents = { Uniform(0.5f, 3.0f), Uniform(0.5f, 3.0f), Uniform(0.5f, 3.0f), 0.0f };
}

// A render object with or without an effect to locate
void SetScene() {
    FillRandom(g_sceneBlock, sizeof(g_sceneBlock));
    FillRandom(g_handleBlock, sizeof(g_handleBlock));
    RSceneObj *scene = reinterpret_cast<RSceneObj *>(g_sceneBlock);
    Handle *handle = reinterpret_cast<Handle *>(g_handleBlock);
    scene->animHandle = handle;
    handle->effectCount = uint8_t(Chance(50) ? 0 : Range(1, 3));
    handle->effects = reinterpret_cast<ArticleEffect *>(g_effectBlock);
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            g_answers.locator.mtx[r][c] = Uniform(-300.0f, 300.0f);
}

void TestMissileConstruct(const Snapshot &before) {
    const char *levels[] = { "snow1a_mis3", "SNOW1A", "snow1a", "city1", "snow1a_mis2" };
    for (long i = 0; i < 1500; i++) {
        SetWorld();
        g_answers.slot = Range(0, kSlots - 1);
        FillRandom(g_missileBlock, sizeof(g_missileBlock));
        FillRandom(Simple(g_answers.slot), sizeof(SimpleRigidBody));
        g_in.weapon = RandomWeapon();
        g_in.speed = Uniform(-5.0f, 60.0f);
        g_in.direction = Chance(15) ? Coord4{ Uniform(-0.2f, 0.2f), 1.0f, Uniform(-0.2f, 0.2f), 0.0f }
                                    : RandomUnit(Uniform(-1.0f, 1.0f));
        g_in.position = { Uniform(-500.0f, 500.0f), Uniform(-50.0f, 50.0f), Uniform(-500.0f, 500.0f), 1.0f };
        g_in.sig = Next();
        g_in.damage = Uniform(0.0f, 1000.0f);
        g_in.target = Chance(40) ? NULL : Target(Range(0, 1));
        g_in.variant = Range(0, 10);
        if (g_in.target != NULL && Chance(50)) {
            float distance = Chance(50) ? Uniform(0.0f, 99.0f) : Uniform(101.0f, 300.0f);
            Coord4 away = RandomUnit(0.0f);
            g_in.target->position = { g_in.position.x + away.x * distance, g_in.position.y + away.y * distance,
                                      g_in.position.z + away.z * distance };
        }
        int owner = Range(0, 4);
        g_answers.signatureOwner = owner == 0 ? NULL : owner == 1 ? g_playerList[0] : Other(Range(1, kOthers - 1));
        // A shooter on a simple body: the signature's top bit and slot
        if (Chance(40)) {
            int row = kOwnerSlot + Range(0, 5);
            PhysicsObject *shooter = Other(Range(1, kOthers - 1));
            shooter->type = Chance(40) ? 10 : Chance(60) ? 11 : 1;
            fgSimulation.simpleRigidBodyOwners[row] = shooter;
            g_in.sig = 0x80000000u | uint32_t(row) << 24 | (Next() & 0xffffff);
        }
        strcpy(ShadowTuningLevel, levels[Range(0, 4)]);
        MissileWobbleFlip = Chance(50);
        MissileSoundPitch = Chance(80) ? 1.0f : Chance(50) ? Uniform(-0.2f, 0.035f) : Uniform(0.9f, 1.1f);
        if (Chance(30)) {
            RemoteRocketSpeed = Uniform(10.0f, 60.0f);
            RemoteRocketThrust = Uniform(1.0f, 20.0f);
            RemoteRocketLaunchSpeed = Uniform(0.0f, 10.0f);
            RemoteRocketTurn = Uniform(0.0f, 5.0f);
            RemoteRocketLife = Range(1, 10);
            MissileSteerScale = Uniform(0.001f, 0.01f);
        }
        RunCase("Missile::Missile", i, SideMissileConstruct, before);
    }
}

void TestMissileSmall(const Snapshot &before) {
    for (long i = 0; i < 60; i++) {
        SetWorld();
        SetMissile(Range(0, kSlots - 1), Coord4{ 0.0f, 0.0f, 0.0f, 1.0f });
        if (Chance(30))
            g_missile->sound = NULL;
        g_in.flags = unsigned(Range(0, 3));
        RunCase(i % 2 ? "Missile::Delete" : "Missile::~Missile", i, i % 2 ? SideMissileDelete : SideMissileDestruct,
                before);
    }
    const float edges[] = { 0.2f, -0.2f, 0.20000002f, -0.20000002f, -0.0f, 0.0f, NAN, 1.0f, -1.0f };
    for (long i = 0; i < 400; i++) {
        SetWorld();
        SetMissile(Range(0, kSlots - 1), Coord4{ 0.0f, 0.0f, 0.0f, 1.0f });
        g_missile->steerScale = Chance(50) ? 0.004f : Uniform(-0.1f, 0.1f);
        g_in.steer = Chance(20) ? edges[Range(0, 8)] : Uniform(-1.5f, 1.5f);
        RunCase(i % 2 ? "Missile::SetUserSteerY" : "Missile::SetUserSteerX", i, i % 2 ? SideSteerY : SideSteerX,
                before);
    }
    for (long i = 0; i < 60; i++) {
        SetWorld();
        SetMissile(Range(0, kSlots - 1), Coord4{ 0.0f, 0.0f, 0.0f, 1.0f });
        g_in.target = Chance(20) ? NULL : Target(Range(0, 2));
        RunCase("Missile::SetTarget", i, SideSetTarget, before);
    }
}

void TestMissileRender(const Snapshot &before) {
    for (long i = 0; i < 1000; i++) {
        SetWorld();
        SetMissile(Range(0, kSlots - 1), Coord4{ Uniform(-500.0f, 500.0f), 0.0f, Uniform(-500.0f, 500.0f), 1.0f });
        SetScene();
        if (Chance(20))
            g_missile->renderObject = NULL;
        if (Chance(30))
            g_missile->weapon = kWeaponQPulse;
        int quarter = fgSimulation.stepsPerSecond >> 2;
        g_missile->launchStep = ShadowSimStepCount - (Chance(50) ? Range(0, quarter + 2) : Range(-5, 400));
        g_in.firstView = Chance(70);
        RunCase("Missile::Render", i, SideMissileRender, before);
    }
}

// The answers a flight's fakes give
void SetFlight() {
    g_answers.ownedBy = Chance(20) ? Other(Range(0, kOthers - 1)) : NULL;
    g_answers.ownedByPlayer = Chance(50);
    g_answers.beacon = Chance(50) ? g_missile->target : Target(2);
    g_answers.hasVelocity = Chance(50);
    g_answers.targetVelocity = { Uniform(-60.0f, 60.0f), Uniform(-5.0f, 5.0f), Uniform(-60.0f, 60.0f), 0.0f };
    g_answers.faceValid = Chance(70);
    const Coord3 &p = Simple(g_missile->rigidBodySlot)->position;
    g_answers.ground = double(p.y) + double(Uniform(-7.0f, 2.0f));
    g_answers.hit = Chance(40);
    g_answers.window = Chance(20);
    g_answers.faceType = uint8_t(Chance(30) ? 0xe : Range(0, 20));
    g_answers.hitPoint = { p.x + Uniform(-2.0f, 2.0f), p.y + Uniform(-2.0f, 2.0f), p.z + Uniform(-2.0f, 2.0f), 1.0f };
    if (Chance(30))
        g_manager->driveMissile = g_missile;
    g_manager->cinematicMode = Chance(10) ? 1 : 0;
}

void TestMissileFlight(const Snapshot &before) {
    for (long i = 0; i < 2000; i++) {
        SetWorld();
        int slot = Range(0, kSlots - 1);
        Coord4 centre = { Uniform(-500.0f, 500.0f), Uniform(-50.0f, 50.0f), Uniform(-500.0f, 500.0f), 1.0f };
        if (Chance(10))     // over the first soft zone
            centre = { Uniform(100.0f, 400.0f), Uniform(70.0f, 95.0f), Uniform(-100.0f, 500.0f), 1.0f };
        SetMissile(slot, centre);
        g_missile->exploded = 0;
        PlaceOthers(Coord4{ Simple(slot)->position.x, Simple(slot)->position.y, Simple(slot)->position.z, 1.0f },
                    Chance(50) ? 2.0f : 20.0f);
        SetHits(slot, Chance(40) ? 0 : Range(1, 5));
        SetFlight();
        RunCase("Missile::CheckForCollision", i, SideCheckForCollision, before);
    }
    for (long i = 0; i < 2000; i++) {
        SetWorld();
        int slot = Range(0, kSlots - 1);
        SetMissile(slot, Coord4{ Uniform(-500.0f, 500.0f), Uniform(-50.0f, 50.0f), Uniform(-500.0f, 500.0f), 1.0f });
        g_missile->launchStep = ShadowSimStepCount - (Chance(50) ? Range(0, 50) : Range(50, 300));
        SetFlight();
        RunCase("Missile::SteerMissile", i, SideSteerMissile, before);
    }
    for (long i = 0; i < 1000; i++) {
        SetWorld();
        int slot = Range(0, kSlots - 1);
        SetMissile(slot, Coord4{ Uniform(-500.0f, 500.0f), Uniform(-50.0f, 50.0f), Uniform(-500.0f, 500.0f), 1.0f });
        SetScene();
        PlaceOthers(Coord4{ Simple(slot)->position.x, Simple(slot)->position.y, Simple(slot)->position.z, 1.0f },
                    Chance(50) ? 2.0f : 20.0f);
        SetHits(slot, Chance(50) ? 0 : Range(1, 3));
        SetFlight();
        RunCase("Missile::Simulate", i, SideMissileSimulate, before);
    }
}

// ---- the regions

void BuildRegions() {
    AddOwned(&MissileSteerScale, 0x001c3680, 4, "MissileSteerScale");
    AddOwned(&MissileSteerRate, 0x001c3684, 4, "MissileSteerRate");
    AddOwned(&RemoteRocketSpeed, 0x001c3688, 4, "RemoteRocketSpeed");
    AddOwned(&RemoteRocketThrust, 0x001c368c, 4, "RemoteRocketThrust");
    AddOwned(&RemoteRocketLaunchSpeed, 0x001c3690, 4, "RemoteRocketLaunchSpeed");
    AddOwned(&RemoteRocketTurn, 0x001c3694, 4, "RemoteRocketTurn");
    AddOwned(&RemoteRocketLife, 0x001c3698, 4, "RemoteRocketLife");
    AddOwned(&MissileGroundClearance, 0x001c369c, 4, "MissileGroundClearance");
    AddOwned(&MissileGroundFollowHeight, 0x001c36a0, 4, "MissileGroundFollowHeight");
    AddOwned(&MissileSoundPitch, 0x001c36a8, 4, "MissileSoundPitch");
    AddOwned(&MissileWobbleFlip, 0x001c36ac, 1, "MissileWobbleFlip");
    AddOwned(&ExplosionInverseScale, 0x001e7a60, 4, "ExplosionInverseScale");
    AddShared(g_missileBlock, sizeof(g_missileBlock), "the missile");
    AddShared(g_explosionBlock, sizeof(g_explosionBlock), "the explosion");
    AddShared(g_simpleBlock, sizeof(g_simpleBlock), "the simple bodies");
    AddShared(g_rigidBlock, sizeof(g_rigidBlock), "the rigid body");
    AddShared(g_infoBlock, sizeof(g_infoBlock), "the rigid body's info");
    AddShared(g_otherBlocks, sizeof(g_otherBlocks), "the other objects");
    AddShared(g_targetBlocks, sizeof(g_targetBlocks), "the targets");
    AddShared(g_soundBlock, sizeof(g_soundBlock), "the sound");
    AddShared(g_sceneBlock, sizeof(g_sceneBlock), "the scene object");
    AddShared(g_handleBlock, sizeof(g_handleBlock), "the handle");
    AddShared(g_managerBlock, sizeof(g_managerBlock), "the mission manager");
    AddShared(&g_heap, sizeof(g_heap), "the scratch heap");
    AddShared(g_out, sizeof(g_out), "the outputs");
    AddShared(g_playerList, sizeof(g_playerList), "the player list", false);
    if (ShadowSimulationRandom != NULL)
        AddShared(ShadowSimulationRandom, sizeof(SimRandom), "the Simulation's SimRandom");
    // Both sides read these at the game's addresses: put back, not compared
    AddShared((const void *)0x00234e24, 4, "SimState", false);
    AddShared((const void *)0x00234e2c, 4, "SimStepsPerSecond", false);
    AddShared((const void *)0x00234e30, 8, "SimTimeStep, SimStepCount", false);
    AddShared((const void *)0x00234e40, 4, "the player list pointer", false);
    AddShared((const void *)0x00239220, 4, "glbMissionManager", false);
    AddShared(fgSimulation.simpleBodyHits, kSlots * sizeof(BitSet192), "SimpleBodyHits");
    AddShared(&fgSimulation.simpleRigidBodyOwners[kOwnerSlot], 6 * sizeof(PhysicsObject *), "SimpleBodyOwners", false);
    AddShared(ShadowTuningLevel, 0x20, "TuningLevel", false);
}

}  // namespace

void GameWeaponsShadowB_Run(void) {
    char value[16] = "";
    DWORD length = GetEnvironmentVariableA("NIGHTFIRE_GAMEWEAPONSSHADOWB", value, sizeof(value));
    if (length == 0 || length >= sizeof(value) || atoi(value) == 0)
        return;
    FpControlGet(&g_x87, &g_sse);

    BuildVtables();
    InstallHooks();
    BuildRegions();
    Snapshot before;
    Capture(&before);

    TestFaceNormal(before);
    TestExplosions(before);
    TestMissileConstruct(before);
    TestMissileSmall(before);
    TestMissileRender(before);
    TestMissileFlight(before);

    Restore(before);
    HooksRemove();
    ResetFpu();
    printf("[gameweaponsshadowb] face normal, explosions, missiles vs originals: %ld cases, %ld checks, %ld differ%s\n",
           g_cases, g_checks, g_differ, g_faults != 0 ? " (with faults)" : "");
    if (g_faults != 0)
        printf("[gameweaponsshadowb]   %ld calls faulted\n", g_faults);
    fflush(stdout);
}
