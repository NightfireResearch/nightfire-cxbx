#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "RigidCollideShadow.h"
#include "FpControl.h"

#include <windows.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <bit>
#include <initializer_list>
#include <vector>

#include "../EventManager.hpp"
#include "../physics/PhysicsObject.h"
#include "../physics/RigidBodyCollide.h"
#include "../world/Collider.h"
#include "../world/CollisionManager.h"
#include "../../common/xbeOriginal.h"
#include "../../helpers.h"

// ---------------------------------------------------------------------------------------------------------------
// A shadow test of RigidBody's collision detection (physics/RigidBodyCollide.cpp), run once from the first simulation
// tick when NIGHTFIRE_RIGIDCOLLIDESHADOW is set: the mission's bodies, their colliders and the track's collision data
// exist by then. Each case runs the PORT and then the ORIGINAL (the three entries swapped back together, so an
// original reaches the other originals) on a copy of a live body (the RigidBody and its RigidBodyInfo), perturbed,
// with the state a call can write outside the copy - every live body and its info (CollideWithObject wakes them),
// the rigid-body scratch pad, the collision manager - snapshotted before and put back between the two, and compares
// the copy, that state, and a log of every call made outside these three functions:
//
//   - ResolveWorldOBBCollision on random normals, points, depths, object velocities (a third of them still) and
//     force scales, the body's velocities, momenta and kind randomised, its info's unknown4fc set in some: the answer,
//     the body, its info and the impact (pre-filled with a pattern);
//   - CollideWithWorld on each live body with a collider: as it is, lowered into the ground, sped up and slowed down,
//     as the player's kind and as an object's, placed on the collision objects (boxes and cylinders) near it, with
//     the vehicle physics' unknownC0 0 and 1 and the step count at 0 and random (the step mask);
//   - CollideWithObject with the copy placed on or near each other live body, at different start indices, as the
//     player's kind or not, two-wheeled or not, with unknown4fc set on its info or the other's.
//
// The calls out of them are replaced by recording fakes that answer deterministically from their arguments:
// ResolveCollision (its answer and the impact's damage scales and strength), GenerateImpulse (the strength, 0 to 7,
// so every stimulus threshold is reached), CalculateAndApplyWorldDamage, the animation stimuli, the mission event,
// Event::operator new and ECollision's constructor (the impact compared but for +0x38..+0x3f, which the original
// leaves as its stack had them). Every owner in PhysicsObjects is swapped for a copy whose vtable records SetInShock,
// ApplyDamage, AddDamageByPlayer, SetAgainstWallFlag, GetIsInTwoWheelMode (scripted) and GetPhysics (a scripted
// record). Vectors handed out are logged by x, y and z (the original leaves some w words as its stack had them), and
// the lever arm CollideWithWorld hands CalculateAndApplyWorldDamage after a box hit only when a vehicle's world hit
// set it earlier in the call.
//
// A mutation the test sees: the 0.25 a side point's impulse is scaled by (GenerateImpulse's logged speedScale), or
// the 0.32 in ResolveWorldOBBCollision (the body's momentum after).
//
// One summary line: [rigidcollideshadow] ...: N cases, M checks, D differ.
// ---------------------------------------------------------------------------------------------------------------

namespace {

#define Orig_CollideWithWorld ((void (__fastcall *)(RigidBody *, int))0x000b1420)
#define Orig_CollideWithObject ((void (__fastcall *)(RigidBody *, int, int, int))0x000b0510)
#define Orig_ResolveWorldOBBCollision ((bool (__fastcall *)(RigidBody *, int, const Coord4 *, const Coord4 *, float, CollisionImpact *, const Coord3 *, float))0x000af960)
#define Game_GetRigidBody ((RigidBody *(__fastcall *)(void *, int, int slot))0x000b2700)

#define ShadowSim ((void *)0x00233ff0)
#define ShadowStepCount U32_AT(0x00234e34)

const unsigned kEntries[] = { 0x000af960, 0x000b0510, 0x000b1420 };
const int kSlots = 64;
const int kVtableSlots = 72;

long g_cases, g_checks, g_differ, g_faults;
int g_reported;
unsigned g_x87, g_sse;

void Check(bool same, const char *what, long index) {
    g_checks++;
    if (same)
        return;
    g_differ++;
    if (g_reported < 10) {
        g_reported++;
        printf("[rigidcollideshadow] DIFF %s, case %ld\n", what, index);
        fflush(stdout);
    }
}

uint32_t g_seed = 0x6a09e667;
uint32_t NextRandom() {
    g_seed = g_seed * 1664525u + 1013904223u;
    return g_seed;
}
float Uniform(float lo, float hi) {
    return lo + (hi - lo) * float(NextRandom() >> 8) * (1.0f / 16777216.0f);
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

// ---- hooks: a jump written over an entry (and over the port a patched entry jumps to)

struct Hook {
    uint32_t at;
    uint8_t saved[5];
    bool on;
};
Hook g_hooks[24];
int g_hookCount;

void HookOne(uint32_t at, const void *to) {
    if (g_hookCount == int(sizeof(g_hooks) / sizeof(g_hooks[0])))
        return;
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

// An original entry that is already ported holds a jump to the port, which ports call directly: both are hooked.
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

// ---- the call log

std::vector<uint32_t> g_log;
bool g_lastWasImpulse;      // the last fake call was GenerateImpulse (a world hit's damage follows)
bool g_armValid;            // a vehicle's world hit set CollideWithWorld's lever arm in this call

void Log(uint32_t type, std::initializer_list<uint32_t> words) {
    g_log.push_back(type);
    g_log.push_back(uint32_t(words.size()));
    g_log.insert(g_log.end(), words.begin(), words.end());
}

void LogXyz(const float *v) {
    g_log.push_back(Bits(v[0]));
    g_log.push_back(Bits(v[1]));
    g_log.push_back(Bits(v[2]));
}

uint32_t Hash(size_t from) {
    uint32_t h = 2166136261u;
    for (size_t i = from; i < g_log.size(); i++)
        h = (h ^ g_log[i]) * 16777619u;
    return h;
}

// ---- the fakes of the functions they call

alignas(16) uint8_t g_eventBytes[0x60];

void *FakeEventNew(size_t size) {
    Log(1, { uint32_t(size) });
    return g_eventBytes;
}

void *__fastcall FakeECollision(void *self, int, CollisionImpact impact) {
    uint32_t words[sizeof(CollisionImpact) / 4];
    memcpy(words, &impact, sizeof(words));
    g_log.push_back(2);
    for (int i = 0; i < int(sizeof(words) / 4); i++)
        if (i != 0x38 / 4 && i != 0x3c / 4)
            g_log.push_back(words[i]);
    g_lastWasImpulse = false;
    return self;
}

bool __fastcall FakeResolveCollision(RigidBody *self, int, RigidBody *a, RigidBody *b, const Coord4 *normal,
                                     const Coord4 *point, float depth, CollisionImpact *impact) {
    size_t from = g_log.size();
    Log(3, { uint32_t(uintptr_t(self)), uint32_t(uintptr_t(a)), uint32_t(uintptr_t(b)), Bits(depth) });
    LogXyz(&normal->x);
    LogXyz(&point->x);
    uint32_t h = Hash(from);
    impact->damageScaleA = float(h & 0xff) / 64.0f;
    impact->damageScaleB = float((h >> 8) & 0xff) / 64.0f;
    impact->strength = (h & 0x70000) == 0 ? 0.0f : float((h >> 16) & 0xff) / 200.0f;
    return (h >> 24) % 5 != 0;
}

void __fastcall FakeGenerateImpulse(RigidBody *self, int, CollisionImpact *impact, const Coord4 *normal,
                                    const Coord4 *point, float push, int fromWorld, int tag, float speedScale) {
    size_t from = g_log.size();
    Log(4, { uint32_t(uintptr_t(self)), Bits(push), uint32_t(fromWorld & 0xff), uint32_t(tag & 0xffff),
             Bits(speedScale), uint32_t(uint8_t(impact->ownerA)) });
    LogXyz(&normal->x);
    LogXyz(&point->x);
    impact->strength = float(Hash(from) % 700) / 100.0f;
    g_lastWasImpulse = true;
    g_armValid = self->kind < 4;
}

void __fastcall FakeWorldDamage(RigidBody *self, int, const Coord4 *direction, float force) {
    Log(5, { uint32_t(uintptr_t(self)), Bits(force) });
    if (g_lastWasImpulse || g_armValid)
        LogXyz(&direction->x);
}

void __fastcall FakeStimuli(void *self, int, uint32_t stimulus, uint32_t step, int unknown) {
    Log(6, { uint32_t(uintptr_t(self)), stimulus, step, uint32_t(unknown) });
}

void __fastcall FakeMissionEvent(void *self, int, int event, const char *text) {
    Log(7, { uint32_t(uintptr_t(self)), uint32_t(event), uint32_t(uint8_t(text[0])) });
}

// ---- the owners: copies whose vtable records

struct FakeOwner {
    uint8_t bytes[sizeof(PhysicsObject)];
};
FakeOwner g_fakes[kSlots];
void *g_fakeVtable[kVtableSlots];
alignas(16) uint8_t g_physics[kSlots][0x100];
PhysicsObject *g_owners[kSlots];       // the live ones
bool g_isBody[kSlots];                 // a rigid body with an info (the other owners' slots may hold anything)
uint32_t g_twoWheel;

uint32_t Slot(const void *self) {
    return uint32_t(((const uint8_t *)self - (const uint8_t *)g_fakes) / sizeof(FakeOwner));
}

void __fastcall FakeSetInShock(void *self, int, float shock) {
    Log(10, { Slot(self), Bits(shock) });
}

int __fastcall FakeApplyDamage(void *self, int, const Coord3 *point, const Coord3 *position, float amount, float factor,
                               int kind, const uint32_t *source) {
    Log(11, { Slot(self), Bits(amount), Bits(factor), uint32_t(kind), uint32_t(uintptr_t(source)) });
    LogXyz(&point->x);
    LogXyz(&position->x);
    return 0x20 + int(g_log.size() & 0xff);
}

void __fastcall FakeAddDamageByPlayer(void *self, int, float damage) {
    Log(12, { Slot(self), Bits(damage) });
}

void *__fastcall FakeGetPhysics(void *self, int) {
    Log(13, { Slot(self) });
    return g_physics[Slot(self)];
}

void __fastcall FakeSetAgainstWallFlag(void *self, int, int against) {
    Log(14, { Slot(self), uint32_t(against & 0xff) });
}

uint32_t __fastcall FakeGetIsInTwoWheelMode(void *self, int) {
    Log(15, { Slot(self) });
    return 0x12345600u | g_twoWheel;
}

void SetVehiclePhysics(int32_t unknownC0) {
    for (int i = 0; i < kSlots; i++)
        reinterpret_cast<RigidVehiclePhysics *>(g_physics[i])->unknownC0 = unknownC0;
}

void OwnersSwap(bool fake) {
    for (int i = 0; i < kSlots; i++) {
        if (g_owners[i] == NULL)
            continue;
        if (fake) {
            memcpy(g_fakes[i].bytes, g_owners[i], sizeof(PhysicsObject));
            void **vtable = g_fakeVtable;
            memcpy(g_fakes[i].bytes, &vtable, 4);
            PhysicsObjects[i] = reinterpret_cast<PhysicsObject *>(&g_fakes[i]);
        } else {
            PhysicsObjects[i] = g_owners[i];
        }
    }
}

// ---- the state outside the copy

const size_t kScratchSize = 0x3a0;

struct World {
    uint8_t bodies[kSlots][sizeof(RigidBody)];
    uint8_t infos[kSlots][sizeof(RigidBodyInfo)];
    RigidBodyInfo *infoAt[kSlots];
    uint8_t scratch[kScratchSize];
    uint8_t manager[sizeof(WCollisionMgr)];
    uint32_t stepCount;
};

void Save(World *w) {
    for (int i = 0; i < kSlots; i++) {
        w->infoAt[i] = NULL;
        if (g_owners[i] == NULL)
            continue;
        RigidBody *body = Game_GetRigidBody(ShadowSim, 0, i);
        memcpy(w->bodies[i], body, sizeof(RigidBody));
        if (!g_isBody[i])
            continue;
        w->infoAt[i] = body->info;
        if (body->info != NULL)
            memcpy(w->infos[i], body->info, sizeof(RigidBodyInfo));
    }
    memcpy(w->scratch, RigidScratchPad, kScratchSize);
    memcpy(w->manager, fgCollisionMgr, sizeof(WCollisionMgr));
    w->stepCount = ShadowStepCount;
}

void Load(const World &w) {
    for (int i = 0; i < kSlots; i++) {
        if (g_owners[i] == NULL)
            continue;
        memcpy(Game_GetRigidBody(ShadowSim, 0, i), w.bodies[i], sizeof(RigidBody));
        if (w.infoAt[i] != NULL)
            memcpy(w.infoAt[i], w.infos[i], sizeof(RigidBodyInfo));
    }
    memcpy(RigidScratchPad, w.scratch, kScratchSize);
    // the query stamp never goes back: the instances keep the stamps they were given
    uint32_t stamp = fgCollisionMgr->queryStamp;
    memcpy(fgCollisionMgr, w.manager, sizeof(WCollisionMgr));
    fgCollisionMgr->queryStamp = stamp;
    ShadowStepCount = w.stepCount;
}

bool SameWorld(const World &a, const World &b) {
    for (int i = 0; i < kSlots; i++) {
        if (g_owners[i] == NULL)
            continue;
        if (memcmp(a.bodies[i], b.bodies[i], sizeof(RigidBody)) != 0)
            return false;
        if (a.infoAt[i] != NULL && memcmp(a.infos[i], b.infos[i], sizeof(RigidBodyInfo)) != 0)
            return false;
    }
    return memcmp(a.scratch, b.scratch, kScratchSize) == 0 && memcmp(a.manager, b.manager, sizeof(a.manager)) == 0 &&
           a.stepCount == b.stepCount;
}

// ---- one case: the port, then the original, from the same start

struct alignas(16) BodyCopy {
    RigidBody body;
    RigidBodyInfo info;
};

BodyCopy g_copy;

struct Outcome {
    bool ok;
    uint32_t answer;
    BodyCopy copy;
    std::vector<uint32_t> log;
    World world;
    FakeOwner fakes[kSlots];
};

World g_before;
Outcome g_port, g_original;

enum CaseKind { kCaseResolveBox, kCaseWorld, kCaseObject };

struct Case {
    CaseKind kind;
    BodyCopy start;
    int index;                  // CollideWithObject's
    Coord4 normal, point;       // ResolveWorldOBBCollision's
    Coord3 objectVelocity;
    float depth, forceScale;
    CollisionImpact impact;
};

Case g_case;

uint32_t RunPort() {
    RigidBody *body = &g_copy.body;
    switch (g_case.kind) {
    case kCaseResolveBox: {
        CollisionImpact *impact = reinterpret_cast<CollisionImpact *>(g_eventBytes);
        return body->ResolveWorldOBBCollision(&g_case.normal, &g_case.point, g_case.depth, impact,
                                              &g_case.objectVelocity, g_case.forceScale);
    }
    case kCaseWorld:
        body->CollideWithWorld();
        return 0;
    case kCaseObject:
        body->CollideWithObject(g_case.index, 0);
        return 0;
    }
    return 0;
}

uint32_t RunOriginal() {
    RigidBody *body = &g_copy.body;
    switch (g_case.kind) {
    case kCaseResolveBox: {
        CollisionImpact *impact = reinterpret_cast<CollisionImpact *>(g_eventBytes);
        return Orig_ResolveWorldOBBCollision(body, 0, &g_case.normal, &g_case.point, g_case.depth, impact,
                                             &g_case.objectVelocity, g_case.forceScale);
    }
    case kCaseWorld:
        Orig_CollideWithWorld(body, 0);
        return 0;
    case kCaseObject:
        Orig_CollideWithObject(body, 0, g_case.index, 0);
        return 0;
    }
    return 0;
}

void RunSide(bool original, Outcome *out) {
    g_copy = g_case.start;
    g_copy.body.info = &g_copy.info;
    memcpy(g_eventBytes, &g_case.impact, sizeof(CollisionImpact));
    g_log.clear();
    g_lastWasImpulse = false;
    g_armValid = false;
    OwnersSwap(true);
    uint32_t answer = 0;
    if (original) {
        for (unsigned at : kEntries)
            XbeOriginal_Restore(at, true);
        out->ok = Guarded([&] { answer = RunOriginal(); });
        for (unsigned at : kEntries)
            XbeOriginal_Restore(at, false);
    } else {
        out->ok = Guarded([&] { answer = RunPort(); });
    }
    memcpy(out->fakes, g_fakes, sizeof(g_fakes));
    OwnersSwap(false);
    out->answer = answer & 0xff;
    out->copy = g_copy;
    if (g_case.kind == kCaseResolveBox)
        g_log.insert(g_log.end(), (const uint32_t *)g_eventBytes,
                     (const uint32_t *)g_eventBytes + sizeof(CollisionImpact) / 4);
    out->log = g_log;
    Save(&out->world);
    Load(g_before);
}

const char *const kCaseNames[] = { "ResolveWorldOBBCollision", "CollideWithWorld", "CollideWithObject" };

void RunCase() {
    Save(&g_before);
    RunSide(false, &g_port);
    RunSide(true, &g_original);
    const char *name = kCaseNames[g_case.kind];
    char what[96];
    sprintf(what, "%s faults", name);
    Check(g_port.ok == g_original.ok, what, g_cases);
    sprintf(what, "%s answer", name);
    Check(g_port.answer == g_original.answer, what, g_cases);
    sprintf(what, "%s body", name);
    Check(memcmp(&g_port.copy.body, &g_original.copy.body, sizeof(RigidBody)) == 0, what, g_cases);
    sprintf(what, "%s body info", name);
    Check(memcmp(&g_port.copy.info, &g_original.copy.info, sizeof(RigidBodyInfo)) == 0, what, g_cases);
    sprintf(what, "%s calls and events", name);
    Check(g_port.log == g_original.log, what, g_cases);
    if (g_port.log != g_original.log && g_reported < 10) {
        size_t n = g_port.log.size() < g_original.log.size() ? g_port.log.size() : g_original.log.size();
        size_t first = 0;
        while (first < n && g_port.log[first] == g_original.log[first])
            first++;
        printf("[rigidcollideshadow]   log words %u / %u, first difference at %u\n", unsigned(g_port.log.size()),
               unsigned(g_original.log.size()), unsigned(first));
    }
    sprintf(what, "%s live bodies, scratch pad, manager", name);
    Check(SameWorld(g_port.world, g_original.world), what, g_cases);
    sprintf(what, "%s owners", name);
    Check(memcmp(g_port.fakes, g_original.fakes, sizeof(g_fakes)) == 0, what, g_cases);
    g_cases++;
}

// ---- the cases

std::vector<int> g_bodies;      // live bodies with an info

void CopyBody(int slot, BodyCopy *out) {
    RigidBody *body = Game_GetRigidBody(ShadowSim, 0, slot);
    out->body = *body;
    out->info = *body->info;
    out->body.info = &out->info;
}

Coord4 RandomUnit() {
    for (;;) {
        Coord4 v = { Uniform(-1.0f, 1.0f), Uniform(-1.0f, 1.0f), Uniform(-1.0f, 1.0f), 0.0f };
        float l = v.x * v.x + v.y * v.y + v.z * v.z;
        if (l > 0.01f && l <= 1.0f) {
            float s = 1.0f / sqrtf(l);
            v.x *= s;
            v.y *= s;
            v.z *= s;
            return v;
        }
    }
}

void RandomiseMotion(RigidBody *body, float speed) {
    body->velocity = { Uniform(-speed, speed), Uniform(-speed * 0.3f, speed * 0.3f), Uniform(-speed, speed) };
    body->angularVelocity = { Uniform(-2.0f, 2.0f), Uniform(-2.0f, 2.0f), Uniform(-2.0f, 2.0f) };
    body->momentum = { body->velocity.x * body->mass, body->velocity.y * body->mass, body->velocity.z * body->mass };
    body->angularMomentum = { Uniform(-50.0f, 50.0f), Uniform(-50.0f, 50.0f), Uniform(-50.0f, 50.0f) };
}

void TestResolveBox(int slot) {
    for (int n = 0; n < 40; n++) {
        g_case.kind = kCaseResolveBox;
        CopyBody(slot, &g_case.start);
        RigidBody *body = &g_case.start.body;
        static const int8_t kinds[] = { 0, 1, 2, 3, 4, 5 };
        if (n % 3 == 1)
            body->kind = kinds[NextRandom() % 6];
        if (n > 0)
            RandomiseMotion(body, n % 4 == 0 ? 2.0f : 25.0f);
        if (n % 4 == 0)
            body->mass = Uniform(50.0f, 3000.0f);
        g_case.start.info.unknown4fc = n % 9 == 4 ? 1 : 0;
        g_case.normal = RandomUnit();
        g_case.point = { body->position.x + Uniform(-3.0f, 3.0f), body->position.y + Uniform(-2.0f, 2.0f),
                         body->position.z + Uniform(-3.0f, 3.0f), 1.0f };
        g_case.depth = n % 5 == 0 ? Uniform(0.0f, 0.05f) : Uniform(0.0f, 2.0f);
        if (n % 3 == 0)
            g_case.objectVelocity = { 0.0f, 0.0f, 0.0f };
        else
            g_case.objectVelocity = { Uniform(-0.5f, 0.5f), Uniform(-0.1f, 0.1f), Uniform(-0.5f, 0.5f) };
        g_case.forceScale = n % 2 == 0 ? 6000.0f : Uniform(10.0f, 9000.0f);
        memset(&g_case.impact, 0xa5 + n, sizeof(g_case.impact));
        RunCase();
    }
}

// The collision objects near a point (cylinders or boxes), by the manager's own lists
void ObjectsNear(const Coord3 *point, std::vector<WCollisionObject *> *cylinders,
                 std::vector<WCollisionObject *> *boxes) {
    ObjectList c, b;
    memset(&c, 0, sizeof(c));
    memset(&b, 0, sizeof(b));
    fgCollisionMgr->GetObjectLists(&c, &b, point, 150.0f);
    for (WCollisionObject **o = c.first; o != NULL && o != c.last; o++)
        cylinders->push_back(*o);
    for (WCollisionObject **o = b.first; o != NULL && o != b.last; o++)
        boxes->push_back(*o);
    if (c.first != NULL)
        UMemory::FastFree(c.first, unsigned((c.end - c.first) * sizeof(void *)));
    if (b.first != NULL)
        UMemory::FastFree(b.first, unsigned((b.end - b.first) * sizeof(void *)));
}

void TestWorld(int slot) {
    PhysicsObject *owner = g_owners[slot];
    RigidBody *live = Game_GetRigidBody(ShadowSim, 0, slot);
    if (owner->collider == NULL || !(live->flags & RigidBody::kFlag0))
        return;
    World *pristine = new World;
    Save(pristine);
    std::vector<WCollisionObject *> cylinders, boxes;
    ObjectsNear(&live->position, &cylinders, &boxes);
    Load(*pristine);
    for (int v = 0; v < 18; v++) {
        g_case.kind = kCaseWorld;
        CopyBody(slot, &g_case.start);
        RigidBody *body = &g_case.start.body;
        if (v % 4 == 1)
            body->kind = 1;
        else if (v % 4 == 2)
            body->kind = 5;
        if (v >= 4)
            RandomiseMotion(body, v % 3 == 0 ? 1.5f : 18.0f);
        if (v >= 2)
            body->position.y -= Uniform(0.0f, 1.5f);
        WCollisionObject *object = NULL;
        if ((v == 6 || v == 7 || v == 12) && !boxes.empty())
            object = boxes[NextRandom() % boxes.size()];
        else if ((v == 8 || v == 9 || v == 13) && !cylinders.empty())
            object = cylinders[NextRandom() % cylinders.size()];
        if (object != NULL) {
            body->kind = 1;
            body->position = { object->position.x + Uniform(-1.0f, 1.0f),
                               object->position.y + object->halfExtents.y + Uniform(-0.5f, 0.5f),
                               object->position.z + Uniform(-1.0f, 1.0f) };
        }
        if (v == 10)
            body->flags &= ~RigidBody::kFlag0;
        // the corners follow the position (the box's corners in the world's frame)
        float dx = body->position.x - live->position.x, dy = body->position.y - live->position.y,
              dz = body->position.z - live->position.z;
        for (int bank = 0; bank < 2; bank++)
            for (int c = 0; c < kBoxCorners; c++) {
                Coord4 &corner = g_case.start.info.corners[bank][c];
                corner.x += dx;
                corner.y += dy;
                corner.z += dz;
            }
        g_case.start.info.cornerBank = uint8_t(v & 1);
        SetVehiclePhysics((v >> 1) & 1);
        ShadowStepCount = v % 3 == 0 ? 0 : NextRandom();
        owner->collider->Refresh(&body->position, body->radius);   // both sides then find it up to date
        RunCase();
        Load(*pristine);
    }
    owner->collider->Refresh(&live->position, live->radius);
    Load(*pristine);
    delete pristine;
}

void TestObject(int slot) {
    World *pristine = new World;
    Save(pristine);
    int tested = 0;
    for (size_t t = 0; t < g_bodies.size() && tested < 10; t++) {
        int target = g_bodies[t];
        RigidBody *other = Game_GetRigidBody(ShadowSim, 0, target);
        tested++;
        for (int v = 0; v < 4; v++) {
            g_case.kind = kCaseObject;
            CopyBody(slot, &g_case.start);
            RigidBody *body = &g_case.start.body;
            float spread = v == 3 ? 12.0f : 1.2f;
            body->position = { other->position.x + Uniform(-spread, spread),
                               other->position.y + Uniform(-spread * 0.5f, spread * 0.5f),
                               other->position.z + Uniform(-spread, spread) };
            if (v == 1)
                body->kind = 1;
            if (v == 2)
                body->kind = 5;
            RandomiseMotion(body, 10.0f);
            g_twoWheel = v & 1;
            g_case.start.info.unknown4fc = NextRandom() % 8 == 0 ? 1 : 0;
            if (other->info != NULL)
                other->info->unknown4fc = NextRandom() % 8 == 0 ? 1 : 0;
            if (v == 2 && target > 0)
                other->sleepState = uint8_t(NextRandom() % 4);
            g_case.index = v == 0 ? -1 : target - 1 - int(NextRandom() % 3);
            if (g_case.index < -1)
                g_case.index = -1;
            RunCase();
            Load(*pristine);
        }
    }
    Load(*pristine);
    delete pristine;
}

} // namespace

void RigidCollideShadow_Run(void) {
    const char *env = getenv("NIGHTFIRE_RIGIDCOLLIDESHADOW");
    if (env == NULL || atoi(env) == 0)
        return;
    static bool ran;
    if (ran)
        return;
    ran = true;
    if (fgCollisionMgr == NULL || RigidScratchPad == NULL) {
        printf("[rigidcollideshadow] no collision manager or scratch pad yet: nothing tested\n");
        fflush(stdout);
        return;
    }
    FpControlGet(&g_x87, &g_sse);

    for (int i = 0; i < kSlots; i++) {
        g_owners[i] = PhysicsObjects[i];
        if (g_owners[i] == NULL || (g_owners[i]->flags & PhysicsObject::kSimpleBody))
            continue;
        RigidBody *body = Game_GetRigidBody(ShadowSim, 0, i);
        if (body->info != NULL && body->ownerIndex == i) {
            g_bodies.push_back(i);
            g_isBody[i] = true;
        }
    }
    if (g_bodies.empty()) {
        printf("[rigidcollideshadow] no rigid bodies: nothing tested\n");
        fflush(stdout);
        return;
    }

    g_fakeVtable[0x04 / 4] = (void *)&FakeSetInShock;
    g_fakeVtable[0x08 / 4] = (void *)&FakeApplyDamage;
    g_fakeVtable[0xd0 / 4] = (void *)&FakeAddDamageByPlayer;
    g_fakeVtable[0xe4 / 4] = (void *)&FakeGetPhysics;
    g_fakeVtable[0xec / 4] = (void *)&FakeSetAgainstWallFlag;
    g_fakeVtable[0x110 / 4] = (void *)&FakeGetIsInTwoWheelMode;

    HookInstall(0x0005a5a0, (const void *)&FakeEventNew);     // and Event::operator new, the port it jumps to
    HookInstall(0x0003f370, (const void *)&FakeECollision);
    HookInstall(0x000aec10, (const void *)&FakeResolveCollision);
    HookInstall(0x000afdb0, (const void *)&FakeGenerateImpulse);
    HookInstall(0x000ad520, (const void *)&FakeWorldDamage);
    HookInstall(0x00077e00, (const void *)&FakeStimuli);
    HookInstall(0x000b72f0, (const void *)&FakeMissionEvent);

    World *start = new World;
    Save(start);
    for (int slot : g_bodies)
        TestResolveBox(slot);
    for (int slot : g_bodies)
        TestWorld(slot);
    for (int slot : g_bodies)
        TestObject(slot);
    Load(*start);
    delete start;
    HooksRemove();

    printf("[rigidcollideshadow] RigidBody collision detection (%u bodies): %ld cases, %ld checks, %ld differ (%ld "
           "calls faulted)\n", unsigned(g_bodies.size()), g_cases, g_checks, g_differ, g_faults);
    fflush(stdout);
}
