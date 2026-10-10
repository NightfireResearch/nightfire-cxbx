#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "RigidResolveShadow.h"
#include "FpControl.h"

#include "../game/BondCar.h"
#include "../physics/PhysicsObject.h"
#include "../physics/RigidBodyResolve.h"
#include "../../common/xbeOriginal.h"
#include "../../helpers.h"

#include <windows.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <type_traits>
#include <vector>

// ---------------------------------------------------------------------------------------------------------------
// NIGHTFIRE_RIGIDRESOLVESHADOW=1, from the first simulation tick: RigidBody::ResolveCollision and GenerateImpulse
// (physics/RigidBodyResolve.cpp) against the originals (their entry bytes swapped back in for each original call,
// common/xbeOriginal.h), on copies of the live bodies, compared.
//
//   - ResolveCollision on every ordered pair of live bodies (and a body against a copy of itself), several times
//     each, the copies perturbed: velocities (some zero, for the at-rest path), momenta (falling or not), masses
//     either side of 1500, kFlag1, ground contacts, a vehicle's kind changed to another vehicle kind or to an
//     object kind (4, 5) so both branches and the shares between a vehicle and an object run, the info's
//     unknown4fc now and then; the normal along the line between the bodies or random, either way round (so the
//     closing-speed test goes both ways), the point between them, the depth random; `this` either body.
//   - GenerateImpulse on every live body, many times: points round it, random normals, the push, fromWorld both
//     ways, the tag and the speed scale.
//   - For the player's kind of car (PBondCar's vtable, 0x0018f580) the owner's state the PVehicle methods read is
//     perturbed too (car class, wheels on the ground, reversing, the control's first value, the physics' +0xc0),
//     as is the mission manager's flag at +0x478; all put back after each case.
// Each run starts from the same snapshot (two bodies, their infos, an impact record as FUN_0003dc20 makes it plus
// a pattern in its untouched words); compared after both: the bodies, infos, impact (GenerateImpulse's
// relativeVelocity.w, the original's stack garbage, masked), the answer, and the PlayAnimation calls (the entry of
// our PhysicsObject::PlayAnimation - which the original reaches through the jump at 0x0006f560 and the port calls
// directly - is pointed at a recorder for the whole test, so no animation runs).
//
// One mutation this catches: ResolveCollision's second share computed from the unrounded inverse of the total mass,
// like the first, instead of the rounded one, changes the impulse's last bits for the vehicle-object pairs.
// ---------------------------------------------------------------------------------------------------------------

namespace {

const uint32_t kResolveAt = 0x000aec10;
const uint32_t kImpulseAt = 0x000afdb0;
const uint32_t kPBondCarVtable = 0x0018f580;

typedef bool (__fastcall *ResolveFn)(RigidBody *, int, RigidBody *, RigidBody *, const Coord4 *, const Coord4 *,
                                     float, CollisionImpact *);
typedef void (__fastcall *ImpulseFn)(RigidBody *, int, CollisionImpact *, const Coord4 *, const Coord4 *, float,
                                     bool, uint16_t, float);
typedef RigidBody *(__fastcall *GetRigidBodyFn)(void *, int, int);

#define Orig_ResolveCollision ((ResolveFn)0x000aec10)
#define Orig_GenerateImpulse ((ImpulseFn)0x000afdb0)
#define Sim_GetRigidBody ((GetRigidBodyFn)0x000b2700)
#define ShadowSim ((void *)0x00233ff0)
#define ShadowMissionManager (*(uint8_t **)0x00239220)
const int kRigidBodies = 0x40;

// ---- results

int g_cases = 0, g_checks = 0, g_differ = 0, g_details = 0, g_faults = 0;
unsigned int g_x87 = 0, g_sse = 0;

void Differ(const char *what, int index, const char *detail) {
    g_differ++;
    if (g_details++ < 10)
        printf("[rigidresolve]   %s #%d: %s\n", what, index, detail);
}

void CheckBytes(const char *what, int index, const void *a, const void *b, size_t bytes) {
    g_checks++;
    if (memcmp(a, b, bytes) == 0)
        return;
    const uint8_t *x = static_cast<const uint8_t *>(a), *y = static_cast<const uint8_t *>(b);
    size_t at = 0;
    while (x[at] == y[at])
        at++;
    char detail[96];
    snprintf(detail, sizeof(detail), "byte 0x%x of 0x%x: original %02x, port %02x", unsigned(at), unsigned(bytes),
             x[at], y[at]);
    Differ(what, index, detail);
}

void ResetFpu() {
    _fpreset();
    FpControlSetX87(g_x87);
    FpControlSetSse(g_sse);
}

typedef void (*CaseFn)(void *context, bool original);

bool Guarded(CaseFn run, void *context, bool original) {
#ifdef _MSC_VER
    __try {
        run(context, original);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ResetFpu();
        g_faults++;
        return false;
    }
#else
    run(context, original);
    return true;
#endif
}

// ---- the PlayAnimation recorder, over our port's entry: both sides' calls reach it

struct AnimCall {
    PhysicsObject *owner;
    uint32_t stimulus;
};

const int kMaxAnimCalls = 8;
AnimCall g_anim[2][kMaxAnimCalls];
int g_animCount[2];
int g_run = 0;

void __fastcall RecordPlayAnimation(PhysicsObject *owner, int, uint32_t stimulus) {
    if (g_animCount[g_run] < kMaxAnimCalls)
        g_anim[g_run][g_animCount[g_run]] = { owner, stimulus };
    g_animCount[g_run]++;
}

uint8_t g_savedEntry[5];

void PointPlayAnimation(bool atRecorder) {
    uint8_t *at = reinterpret_cast<uint8_t *>(XbeAddress(&PhysicsObject::PlayAnimation));
    DWORD protect;
    VirtualProtect(at, 5, PAGE_EXECUTE_READWRITE, &protect);
    if (atRecorder) {
        memcpy(g_savedEntry, at, 5);
        int32_t offset = int32_t(uintptr_t(&RecordPlayAnimation) - uintptr_t(at + 5));
        at[0] = 0xe9;
        memcpy(at + 1, &offset, 4);
    } else {
        memcpy(at, g_savedEntry, 5);
    }
    VirtualProtect(at, 5, protect, &protect);
    FlushInstructionCache(GetCurrentProcess(), at, 5);
}

// ---- the state a call runs on

struct State {
    alignas(16) RigidBody body[2];
    alignas(16) RigidBodyInfo info[2];
    alignas(16) CollisionImpact impact;
};

State g_start, g_work, g_result[2];
alignas(16) Coord4 g_normal, g_point;
bool g_answer[2];

// The original (inside the window), then the port, each from g_start; false if either faulted
template <class F>
bool Both(F &&call) {
    typedef typename std::remove_reference<F>::type Call;
    CaseFn thunk = [](void *context, bool original) { (*static_cast<Call *>(context))(original); };
    bool ok = true;
    for (int run = 0; run < 2; run++) {
        const bool original = run == 0;
        g_work = g_start;
        g_run = run;
        g_animCount[run] = 0;
        g_answer[run] = false;
        if (original) {
            XbeOriginal_Restore(kResolveAt, true);
            XbeOriginal_Restore(kImpulseAt, true);
        }
        if (!Guarded(thunk, &call, original))
            ok = false;
        if (original) {
            XbeOriginal_Restore(kResolveAt, false);
            XbeOriginal_Restore(kImpulseAt, false);
        }
        g_result[run] = g_work;
    }
    return ok;
}

void CompareRuns(const char *what, int index, bool maskVelocityW) {
    g_cases++;
    if (maskVelocityW)
        g_result[0].impact.relativeVelocity.w = g_result[1].impact.relativeVelocity.w = 0.0f;
    g_checks++;
    if (g_answer[0] != g_answer[1]) {
        char detail[64];
        snprintf(detail, sizeof(detail), "answer: original %d, port %d", int(g_answer[0]), int(g_answer[1]));
        Differ(what, index, detail);
    }
    char name[64];
    for (int k = 0; k < 2; k++) {
        snprintf(name, sizeof(name), "%s body %d", what, k);
        CheckBytes(name, index, &g_result[0].body[k], &g_result[1].body[k], sizeof(RigidBody));
        snprintf(name, sizeof(name), "%s info %d", what, k);
        CheckBytes(name, index, &g_result[0].info[k], &g_result[1].info[k], sizeof(RigidBodyInfo));
    }
    snprintf(name, sizeof(name), "%s impact", what);
    CheckBytes(name, index, &g_result[0].impact, &g_result[1].impact, sizeof(CollisionImpact));
    g_checks++;
    bool same = g_animCount[0] == g_animCount[1];
    for (int i = 0; same && i < g_animCount[0] && i < kMaxAnimCalls; i++)
        same = g_anim[0][i].owner == g_anim[1][i].owner && g_anim[0][i].stimulus == g_anim[1][i].stimulus;
    if (!same) {
        char detail[64];
        snprintf(detail, sizeof(detail), "PlayAnimation calls: original %d, port %d", g_animCount[0], g_animCount[1]);
        Differ(what, index, detail);
    }
}

// ---- inputs

uint32_t g_random = 0x5e1d0c3b;

uint32_t Random() {
    g_random ^= g_random << 13;
    g_random ^= g_random >> 17;
    g_random ^= g_random << 5;
    return g_random;
}

int RandomInt(int n) { return n <= 0 ? 0 : int(Random() % uint32_t(n)); }
float Uniform(float lo, float hi) { return lo + (hi - lo) * float(Random() >> 8) * (1.0f / 16777216.0f); }

Coord4 RandomUnit() {
    for (;;) {
        Coord4 v = { Uniform(-1.0f, 1.0f), Uniform(-1.0f, 1.0f), Uniform(-1.0f, 1.0f), 0.0f };
        float length = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
        if (length > 0.05f && length <= 1.0f)
            return { v.x / length, v.y / length, v.z / length, 0.0f };
    }
}

void RandomVector(Coord3 *v, float size) {
    *v = { Uniform(-size, size), Uniform(-size, size) * 0.3f, Uniform(-size, size) };
}

std::vector<RigidBody *> g_live;

// A copy of a live body into slot k of g_start, its info pointing at g_work's copy; perturbed unless `real`
void CopyBody(int k, const RigidBody *live, bool real) {
    RigidBody &body = g_start.body[k];
    body = *live;
    g_start.info[k] = *live->info;
    body.info = &g_work.info[k];
    if (real)
        return;
    if (live->kind < 4) {
        static const int8_t kKinds[] = { 1, 2, 3, 4, 5 };
        if (RandomInt(2))
            body.kind = kKinds[RandomInt(5)];
    }
    switch (RandomInt(4)) {
    case 0: body.velocity = { 0.0f, 0.0f, 0.0f }; break;
    case 1: RandomVector(&body.velocity, 3.0f); break;
    default: RandomVector(&body.velocity, 40.0f); break;
    }
    if (RandomInt(2))
        body.mass = Uniform(200.0f, 3000.0f);
    body.momentum = { body.velocity.x * body.mass, body.velocity.y * body.mass, body.velocity.z * body.mass };
    if (RandomInt(3) == 0)
        body.momentum.y = -Uniform(0.0f, 5000.0f);
    RandomVector(&body.angularVelocity, 2.0f);
    RandomVector(&body.angularMomentum, 3000.0f);
    if (RandomInt(2))
        body.flags ^= RigidBody::kFlag1;
    if (RandomInt(2))
        body.groundContacts = uint8_t(RandomInt(5));
    if (RandomInt(10) == 0)
        g_start.info[k].unknown4fc = 1;
    else if (RandomInt(2))
        g_start.info[k].unknown4fc = 0;
}

// The impact record as FUN_0003dc20 makes it, with a pattern in the words it leaves
void FreshImpact() {
    CollisionImpact &impact = g_start.impact;
    memset(&impact, 0, sizeof(impact));
    impact.point = impact.normal = impact.relativeVelocity = { 0.0f, 0.0f, 0.0f, 1.0f };
    memset(impact.unknown38, 0xa5, sizeof(impact.unknown38));
    impact.unknown50 = -1;
}

// ---- the owners' state, perturbed for PBondCars and put back

struct OwnerSave {
    PBondCar *car;
    uint8_t saved[sizeof(PBondCar)];
    int32_t subPhysics;
};

std::vector<OwnerSave> g_owners;
uint8_t g_missionFlag = 0;

void PerturbOwners() {
    g_owners.clear();
    for (RigidBody *live : g_live) {
        PhysicsObject *owner = PhysicsObjects[live->ownerIndex];
        if (owner == NULL || uint32_t(uintptr_t(owner->vtable)) != kPBondCarVtable)
            continue;
        OwnerSave save;
        save.car = static_cast<PBondCar *>(owner);
        memcpy(save.saved, save.car, sizeof(save.saved));
        save.subPhysics = save.car->physics != NULL ? save.car->physics->subPhysics : 0;
        g_owners.push_back(save);
        if (RandomInt(2) == 0)
            continue;
        PBondCar *car = save.car;
        car->control.steering = Uniform(-1.2f, 1.2f);
        car->numWheelsOnGround = int8_t(RandomInt(5));
        car->reversing = uint8_t(RandomInt(2));
        car->carClass = RandomInt(4);
        if (car->physics != NULL)
            car->physics->subPhysics = RandomInt(3);
    }
    if (ShadowMissionManager != NULL)
        ShadowMissionManager[0x478] = uint8_t(RandomInt(2));
}

void RestoreOwners() {
    for (OwnerSave &save : g_owners) {
        memcpy(save.car, save.saved, sizeof(save.saved));
        if (save.car->physics != NULL)
            save.car->physics->subPhysics = save.subPhysics;
    }
    g_owners.clear();
    if (ShadowMissionManager != NULL)
        ShadowMissionManager[0x478] = g_missionFlag;
}

// ---- the tests

void TestResolve() {
    int index = 0;
    for (size_t i = 0; i < g_live.size(); i++) {
        for (size_t j = 0; j < g_live.size(); j++) {
            for (int repeat = 0; repeat < 8; repeat++, index++) {
                const bool real = repeat == 0;
                CopyBody(0, g_live[i], real);
                CopyBody(1, g_live[j], real);
                FreshImpact();
                const RigidBody &a = g_start.body[0], &b = g_start.body[1];
                Coord4 between = { b.position.x - a.position.x, b.position.y - a.position.y,
                                   b.position.z - a.position.z, 0.0f };
                float length = sqrtf(between.x * between.x + between.y * between.y + between.z * between.z);
                if (length > 0.01f && RandomInt(3) != 0)
                    g_normal = { between.x / length, between.y / length, between.z / length, 0.0f };
                else
                    g_normal = RandomUnit();
                if (RandomInt(2))
                    g_normal = { -g_normal.x, -g_normal.y, -g_normal.z, 0.0f };
                float t = Uniform(0.0f, 1.0f);
                g_point = { a.position.x + between.x * t + Uniform(-1.0f, 1.0f),
                            a.position.y + between.y * t + Uniform(-0.5f, 0.5f),
                            a.position.z + between.z * t + Uniform(-1.0f, 1.0f), 1.0f };
                const float depth = RandomInt(4) == 0 ? 0.0f : Uniform(0.0f, 1.5f);
                const int self = RandomInt(4) == 0 ? 1 : 0;
                if (!real)
                    PerturbOwners();
                Both([&](bool original) {
                    RigidBody *body = &g_work.body[self];
                    if (original)
                        g_answer[0] = Orig_ResolveCollision(body, 0, &g_work.body[0], &g_work.body[1], &g_normal,
                                                            &g_point, depth, &g_work.impact);
                    else
                        g_answer[1] = body->ResolveCollision(&g_work.body[0], &g_work.body[1], &g_normal, &g_point,
                                                             depth, &g_work.impact);
                });
                if (!real)
                    RestoreOwners();
                CompareRuns("ResolveCollision", index, false);
            }
        }
    }
}

void TestImpulse() {
    int index = 0;
    for (size_t i = 0; i < g_live.size(); i++) {
        for (int repeat = 0; repeat < 60; repeat++, index++) {
            const bool real = repeat == 0;
            CopyBody(0, g_live[i], real);
            CopyBody(1, g_live[i], true);
            FreshImpact();
            const RigidBody &body = g_start.body[0];
            const Coord4 &extents = g_start.info[0].halfExtents;
            g_point = { body.position.x + Uniform(-1.2f, 1.2f) * extents.x,
                        body.position.y + Uniform(-1.2f, 1.2f) * extents.y,
                        body.position.z + Uniform(-1.2f, 1.2f) * extents.z, 1.0f };
            switch (RandomInt(4)) {
            case 0: g_normal = { 0.0f, 1.0f, 0.0f, 0.0f }; break;
            case 1: g_normal = { extents.x > 0 ? 1.0f : -1.0f, 0.0f, 0.0f, 0.0f }; break;
            default: g_normal = RandomUnit(); break;
            }
            const float push = RandomInt(3) == 0 ? 0.0f : Uniform(0.0f, 2.0f);
            const bool fromWorld = RandomInt(5) != 0;
            const uint16_t tag = uint16_t(RandomInt(0x10000));
            const float speedScale = RandomInt(2) ? 1.0f : Uniform(0.5f, 1.5f);
            if (!real)
                PerturbOwners();
            Both([&](bool original) {
                RigidBody *self = &g_work.body[0];
                if (original)
                    Orig_GenerateImpulse(self, 0, &g_work.impact, &g_normal, &g_point, push, fromWorld, tag,
                                         speedScale);
                else
                    self->GenerateImpulse(&g_work.impact, &g_normal, &g_point, push, fromWorld, tag, speedScale);
            });
            if (!real)
                RestoreOwners();
            if (memcmp(&g_result[0].impact.strength, &g_result[1].impact.strength, sizeof(float)) != 0 &&
                g_details < 10) {
                const RigidBody &start = g_start.body[0];
                printf("[rigidresolve]   GenerateImpulse #%d inputs: kind %d, velocity (%a %a %a), angular velocity "
                       "(%a %a %a), normal (%a %a %a), point (%a %a %a), push %a, fromWorld %d, speedScale %a; "
                       "strength original %a, port %a\n",
                       index, start.kind, start.velocity.x, start.velocity.y, start.velocity.z,
                       start.angularVelocity.x, start.angularVelocity.y, start.angularVelocity.z, g_normal.x,
                       g_normal.y, g_normal.z, g_point.x, g_point.y, g_point.z, push, int(fromWorld), speedScale,
                       g_result[0].impact.strength, g_result[1].impact.strength);
            }
            CompareRuns("GenerateImpulse", index, true);
        }
    }
}

}  // namespace

void RigidResolveShadow_Run(void) {
    char value[16] = "";
    DWORD length = GetEnvironmentVariableA("NIGHTFIRE_RIGIDRESOLVESHADOW", value, sizeof(value));
    if (length == 0 || length >= sizeof(value) || atoi(value) == 0)
        return;
    g_live.clear();
    for (int i = 0; i < kRigidBodies; i++) {
        if (PhysicsObjects[i] == NULL)
            continue;
        RigidBody *body = Sim_GetRigidBody(ShadowSim, 0, i);
        if (body != NULL && body->info != NULL && body->ownerIndex == i)
            g_live.push_back(body);
    }
    if (g_live.empty()) {
        printf("[rigidresolve] no live rigid bodies - nothing tested\n");
        fflush(stdout);
        return;
    }
    FpControlGet(&g_x87, &g_sse);
    if (ShadowMissionManager != NULL)
        g_missionFlag = ShadowMissionManager[0x478];

    PointPlayAnimation(true);
    TestResolve();
    TestImpulse();
    PointPlayAnimation(false);
    ResetFpu();

    printf("[rigidresolve] ResolveCollision, GenerateImpulse vs originals (%d live bodies): %d cases, %d checks, "
           "%d differ%s\n",
           int(g_live.size()), g_cases, g_checks, g_differ, g_faults != 0 ? " (with faults)" : "");
    fflush(stdout);
}
