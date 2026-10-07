#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "RigidCoreShadow.h"
#include "FpControl.h"

#include "../physics/PhysicsObject.h"
#include "../physics/RigidBody.h"
#include "../platform/RealMath.h"
#include "../world/CollisionManager.h"
#include "../world/WorldPos.h"
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
// NIGHTFIRE_RIGIDCORESHADOW=1, from the first simulation tick: RigidBody's core (physics/RigidBody.cpp) against the
// originals (the entry bytes of all ten ported functions swapped back in for each original run, common/
// xbeOriginal.h, so an original calls the other originals; the calls into RigidBodyBasics go wherever they are
// installed, the same for both runs), on copies of the live bodies, compared.
//
//   - The constructor on each live body's own arguments (its slot, kind, position, velocities, orientation, mass,
//     inertia, half extents - some shrunk below the minimum - and owner; moving and the last flag at random), into
//     a scratch body: the Simulation's info for the slot is the live body's, saved and put back around each run.
//     Compared: the body, the info (with each lever's new WWorldPos compared by content and the inverse inertia's
//     row 3 masked - the original's stack leftovers), then the body after the destructor (same side) frees them.
//   - ResetObject with random orientations and positions (the inverse inertia's row 3 masked, as above).
//   - ApplyInitialForcesAndTorques with the kind, ground contacts, sleep state, the info's step count and flags,
//     the velocity, underwater and the player's gravity perturbed, and for PBondCar owners their wheels on the
//     ground, was-in-air flag, car type ("paradis_car", "jungle_truck", other) and the physics record's +0xc0/+0xcc.
//   - ResolveLeverForces on 1-16 random contacts against the body's inverse inertia.
//   - CollideWithGround with the body moved above and below the ground, falling fast or not, tilted, flag 0 and the
//     physics record's +0xc4 perturbed (each copy gets its own copies of the lever and owner WWorldPos objects, and
//     the scratch pad and the collision manager's query field are put back before each run).
//   - ModifyLevers (both ways) and ScaleObjObjForces on pairs of copies with random levers, points and forces.
//   - ControlSleep near the sleeping speeds; UpdatePositionAndOrientation with forces, torques, momenta and the
//     quaternion perturbed.
// After each run: both bodies, infos, WWorldPos copies, the vectors passed, the scratch pad, the player's gravity
// and the owners' was-in-air flags. Everything perturbed is put back.
//
// One mutation this catches: UpdatePositionAndOrientation's quaternion change.w summed as -(qx*a) - qz*c - qy*b
// instead of -(qx*a) - qy*b - qz*c changes the orientation's last bits on most perturbed steps.
// ---------------------------------------------------------------------------------------------------------------

namespace {

const uint32_t kPorted[] = { 0x000b0b20, 0x000adc80, 0x000adcd0, 0x000ade40, 0x000ae150,
                             0x000ae4c0, 0x000ae830, 0x000aea40, 0x000b09d0, 0x000b0ec0 };
const uint32_t kPBondCarVtable = 0x0018f580;
const int kRigidBodies = 0x40;
const int kPositions = kMaxLevers + 1;      // the levers', then the owner's
const size_t kScratchBytes = 0x400;

typedef RigidBody *(__fastcall *ConstructFn)(RigidBody *, int, int ownerIndex, int kind, const Coord3 *,
                                             const Coord3 *, const Coord3 *, const MATRIX4 *, float, const Coord3 *,
                                             const Coord4 *, PhysicsObject *, int moving, int flag1);
typedef void (__fastcall *BodyFn)(RigidBody *, int);
typedef void (__fastcall *ResetFn)(RigidBody *, int, const MATRIX4 *, const Coord3 *);
typedef void (__fastcall *LeverForcesFn)(RigidBody *, int, const LeverContacts *, const Coord4 *, const Coord4 *,
                                         const MATRIX4 *);
typedef void (*ModifyLeversFn)(RigidBody *, RigidBody *, Coord4 *, Coord4 *, const Coord4 *, Coord4 *, int);
typedef void (*ScaleFn)(RigidBody *, RigidBody *, Coord4 *, Coord4 *, float, float, float);
typedef void (__fastcall *UpdateFn)(RigidBody *, int, float);
typedef RigidBody *(__fastcall *GetRigidBodyFn)(void *, int, int);
typedef RigidBodyInfo *(__fastcall *GetRigidBodyInfoFn)(void *, int, int);

#define Orig_Construct ((ConstructFn)0x000b0b20)
#define Orig_Destruct ((BodyFn)0x000adc80)
#define Orig_ResetObject ((ResetFn)0x000adcd0)
#define Orig_ApplyInitialForces ((BodyFn)0x000ade40)
#define Orig_ResolveLeverForces ((LeverForcesFn)0x000ae150)
#define Orig_CollideWithGround ((BodyFn)0x000ae4c0)
#define Orig_ModifyLevers ((ModifyLeversFn)0x000ae830)
#define Orig_ScaleObjObjForces ((ScaleFn)0x000aea40)
#define Orig_ControlSleep ((BodyFn)0x000b09d0)
#define Orig_UpdatePosition ((UpdateFn)0x000b0ec0)
#define Sim_GetRigidBody ((GetRigidBodyFn)0x000b2700)
#define Sim_GetRigidBodyInfo ((GetRigidBodyInfoFn)0x000b2760)
#define ShadowSim ((void *)0x00233ff0)

// What PBondCar's PVehicle methods read and write here
struct PBondCarFields {
    uint8_t unknown000[0x23c];
    RigidVehiclePhysics *physics;   // +0x23c GetPhysics
    uint8_t unknown240[0x59];
    int8_t wheelsOnGround;          // +0x299 GetNumWheelsOnGround
    uint8_t unknown29a[0x32];
    const char *carType;            // +0x2cc GetCarType
    uint8_t unknown2d0[0x141];
    uint8_t wasInAir;               // +0x411 GetWasInAir, SetWasInAir
};
static_assert(offsetof(PBondCarFields, wheelsOnGround) == 0x299 && offsetof(PBondCarFields, carType) == 0x2cc &&
              offsetof(PBondCarFields, wasInAir) == 0x411, "PBondCar fields");

// ---- results

int g_cases = 0, g_checks = 0, g_differ = 0, g_details = 0, g_faults = 0;
unsigned int g_x87 = 0, g_sse = 0;

void Differ(const char *what, int index, const char *detail) {
    g_differ++;
    if (g_details++ < 10)
        printf("[rigidcore]   %s #%d: %s\n", what, index, detail);
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

void RestoreOriginals(bool original) {
    for (uint32_t at : kPorted)
        XbeOriginal_Restore(at, original);
}

// ---- the state a call runs on

struct State {
    alignas(16) RigidBody body[2];
    alignas(16) RigidBodyInfo info[2];
    alignas(16) WWorldPos positions[2][kPositions];
    alignas(16) Coord4 vectors[2 * kMaxLevers + 2];
    alignas(16) uint8_t scratch[kScratchBytes];
    float playerGravity;
    uint8_t underwater;
    uint8_t wasInAir[2];            // the owners', for PBondCars
    uint32_t queryField;            // fgCollisionMgr + 0x2c
};

State g_start, g_work, g_result[2];
PBondCarFields *g_car[2];           // the bodies' owners when PBondCars

uint32_t &CollisionQueryField() {
    return *reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(fgCollisionMgr) + 0x2c);
}

// The game's state the calls read and write, from g_work before a run and back into it after
void PutGameState() {
    memcpy(RigidScratchPad, g_work.scratch, kScratchBytes);
    RigidPlayerGravity = g_work.playerGravity;
    RigidUnderwater = g_work.underwater;
    for (int k = 0; k < 2; k++)
        if (g_car[k] != NULL)
            g_car[k]->wasInAir = g_work.wasInAir[k];
    CollisionQueryField() = g_work.queryField;
}

void TakeGameState() {
    memcpy(g_work.scratch, RigidScratchPad, kScratchBytes);
    g_work.playerGravity = RigidPlayerGravity;
    g_work.underwater = RigidUnderwater;
    for (int k = 0; k < 2; k++)
        if (g_car[k] != NULL)
            g_work.wasInAir[k] = g_car[k]->wasInAir;
    g_work.queryField = CollisionQueryField();
}

// The original (inside the window), then the port, each from g_start
template <class F>
void Both(F &&call) {
    typedef typename std::remove_reference<F>::type Call;
    CaseFn thunk = [](void *context, bool original) { (*static_cast<Call *>(context))(original); };
    for (int run = 0; run < 2; run++) {
        const bool original = run == 0;
        g_work = g_start;
        PutGameState();
        if (original)
            RestoreOriginals(true);
        Guarded(thunk, &call, original);
        if (original)
            RestoreOriginals(false);
        TakeGameState();
        g_result[run] = g_work;
    }
}

void CompareRuns(const char *what, int index) {
    g_cases++;
    char name[64];
    for (int k = 0; k < 2; k++) {
        snprintf(name, sizeof(name), "%s body %d", what, k);
        CheckBytes(name, index, &g_result[0].body[k], &g_result[1].body[k], sizeof(RigidBody));
        snprintf(name, sizeof(name), "%s info %d", what, k);
        CheckBytes(name, index, &g_result[0].info[k], &g_result[1].info[k], sizeof(RigidBodyInfo));
        snprintf(name, sizeof(name), "%s positions %d", what, k);
        CheckBytes(name, index, g_result[0].positions[k], g_result[1].positions[k], sizeof(g_result[0].positions[k]));
    }
    snprintf(name, sizeof(name), "%s vectors", what);
    CheckBytes(name, index, g_result[0].vectors, g_result[1].vectors, sizeof(g_result[0].vectors));
    snprintf(name, sizeof(name), "%s scratch pad", what);
    CheckBytes(name, index, g_result[0].scratch, g_result[1].scratch, kScratchBytes);
    snprintf(name, sizeof(name), "%s player gravity", what);
    CheckBytes(name, index, &g_result[0].playerGravity, &g_result[1].playerGravity, sizeof(float));
    snprintf(name, sizeof(name), "%s was in air", what);
    CheckBytes(name, index, g_result[0].wasInAir, g_result[1].wasInAir, sizeof(g_result[0].wasInAir));
    snprintf(name, sizeof(name), "%s query field", what);
    CheckBytes(name, index, &g_result[0].queryField, &g_result[1].queryField, sizeof(uint32_t));
}

// ---- inputs

uint32_t g_random = 0x2c9b6e17;

uint32_t Random() {
    g_random ^= g_random << 13;
    g_random ^= g_random >> 17;
    g_random ^= g_random << 5;
    return g_random;
}

int RandomInt(int n) { return n <= 0 ? 0 : int(Random() % uint32_t(n)); }
float Uniform(float lo, float hi) { return lo + (hi - lo) * float(Random() >> 8) * (1.0f / 16777216.0f); }

void RandomVector(Coord3 *v, float size) {
    *v = { Uniform(-size, size), Uniform(-size, size), Uniform(-size, size) };
}

Coord4 RandomQuaternion() {
    for (;;) {
        Coord4 q = { Uniform(-1.0f, 1.0f), Uniform(-1.0f, 1.0f), Uniform(-1.0f, 1.0f), Uniform(-1.0f, 1.0f) };
        float length = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
        if (length > 0.1f && length <= 1.0f)
            return { q.x / length, q.y / length, q.z / length, q.w / length };
    }
}

// A rotation near upright (small tilts) or anywhere
void RandomRotation(MATRIX4 *m) {
    Coord4 q = RandomQuaternion();
    if (RandomInt(2)) {
        q.x *= 0.15f;
        q.z *= 0.15f;
        float length = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
        q = { q.x / length, q.y / length, q.z / length, q.w / length };
    }
    VU0_quattom4(m, &q);
}

std::vector<RigidBody *> g_live;

PBondCarFields *Car(const RigidBody *live) {
    PhysicsObject *owner = PhysicsObjects[live->ownerIndex];
    if (owner == NULL || uint32_t(uintptr_t(owner->vtable)) != kPBondCarVtable)
        return NULL;
    return reinterpret_cast<PBondCarFields *>(owner);
}

// Whether the code may call the owner's PVehicle methods for this body (they exist, and what they write is known)
bool OwnerCallsSafe(const RigidBody *live) {
    return live->kind >= 4 || Car(live) != NULL;
}

// A copy of a live body into slot k of g_start, its info and WWorldPos objects g_work's copies; perturbed unless
// `real`
void CopyBody(int k, const RigidBody *live, bool real) {
    RigidBody &body = g_start.body[k];
    RigidBodyInfo &info = g_start.info[k];
    body = *live;
    info = *live->info;
    body.info = &g_work.info[k];
    for (int i = 0; i < kMaxLevers; i++) {
        if (live->info->leverPositions[i] != NULL)
            g_start.positions[k][i] = *live->info->leverPositions[i];
        else
            memset(&g_start.positions[k][i], 0, sizeof(WWorldPos));
        info.leverPositions[i] = &g_work.positions[k][i];
    }
    g_start.positions[k][kMaxLevers] = *live->info->worldPosition;
    info.worldPosition = &g_work.positions[k][kMaxLevers];
    g_car[k] = Car(live);
    if (g_car[k] != NULL)
        g_start.wasInAir[k] = g_car[k]->wasInAir;
    if (real)
        return;

    if (live->kind < 4) {
        if (g_car[k] != NULL && RandomInt(2))
            body.kind = int8_t(1 + RandomInt(3));
    } else if (RandomInt(2)) {
        body.kind = int8_t(4 + RandomInt(3));
    }
    switch (RandomInt(4)) {
    case 0: body.velocity = { 0.0f, 0.0f, 0.0f }; break;
    case 1: RandomVector(&body.velocity, 1.0f); break;
    default: RandomVector(&body.velocity, 40.0f); break;
    }
    RandomVector(&body.angularVelocity, RandomInt(2) ? 0.5f : 4.0f);
    body.momentum = { body.velocity.x * body.mass, body.velocity.y * body.mass, body.velocity.z * body.mass };
    RandomVector(&body.angularMomentum, 3000.0f);
    if (RandomInt(2))
        body.flags ^= RigidBody::kFlag0;
    if (RandomInt(2))
        body.flags ^= RigidBody::kFlag1;
    body.groundContacts = uint8_t(RandomInt(6));
    body.sleepState = uint8_t(1 + RandomInt(3));
    if (RandomInt(3) == 0)
        info.unknown4fd = uint8_t(RandomInt(2));
    if (RandomInt(3) == 0)
        info.unknown4fe = uint8_t(RandomInt(2));
    if (RandomInt(3) == 0)
        info.unknown4de = RandomInt(2) ? 0xffff : uint16_t(RandomInt(0x10000));
    if (g_car[k] != NULL && RandomInt(2))
        g_start.wasInAir[k] = uint8_t(RandomInt(2));
}

void FreshGameState() {
    memcpy(g_start.scratch, RigidScratchPad, kScratchBytes);
    g_start.playerGravity = RigidPlayerGravity;
    g_start.underwater = RigidUnderwater;
    g_start.queryField = CollisionQueryField();
    g_car[0] = g_car[1] = NULL;
    memset(g_start.vectors, 0, sizeof(g_start.vectors));
}

void PerturbGameState() {
    if (RandomInt(4) == 0)
        g_start.underwater = uint8_t(!g_start.underwater);
    switch (RandomInt(4)) {
    case 0: g_start.playerGravity = -34.0f; break;
    case 1: g_start.playerGravity = Rigid_BODGE_PLAYER_GRAVITY; break;
    case 2: g_start.playerGravity = Uniform(-60.0f, -10.0f); break;
    default: break;
    }
}

// ---- the PBondCars' state, perturbed and put back

struct CarSave {
    PBondCarFields *car;
    int8_t wheelsOnGround;
    const char *carType;
    RigidVehiclePhysics physics;
};

std::vector<CarSave> g_cars;
const char *const kCarTypes[] = { "paradis_car", "jungle_truck", "shadow_car" };

void SaveCars() {
    g_cars.clear();
    for (RigidBody *live : g_live) {
        PBondCarFields *car = Car(live);
        if (car == NULL)
            continue;
        CarSave save;
        save.car = car;
        save.wheelsOnGround = car->wheelsOnGround;
        save.carType = car->carType;
        if (car->physics != NULL)
            save.physics = *car->physics;
        g_cars.push_back(save);
    }
}

void PerturbCars() {
    for (CarSave &save : g_cars) {
        if (RandomInt(2) == 0)
            continue;
        PBondCarFields *car = save.car;
        car->wheelsOnGround = int8_t(RandomInt(5));
        if (RandomInt(2))
            car->carType = kCarTypes[RandomInt(3)];
        if (car->physics != NULL) {
            car->physics->unknownC0 = RandomInt(3) == 0 ? 1 : 0;
            car->physics->unknownC4 = RandomInt(4) == 0 ? 1 : 0;
            car->physics->unknownC8 = RandomInt(2);
            car->physics->unknownCC = RandomInt(3) == 0 ? 1 : 0;
        }
    }
}

void RestoreCars() {
    for (CarSave &save : g_cars) {
        save.car->wheelsOnGround = save.wheelsOnGround;
        save.car->carType = save.carType;
        if (save.car->physics != NULL)
            *save.car->physics = save.physics;
    }
}

// ---- the tests

void TestConstruct() {
    int index = 0;
    for (RigidBody *live : g_live) {
        if (!OwnerCallsSafe(live) || Sim_GetRigidBodyInfo(ShadowSim, 0, live->ownerIndex) != live->info)
            continue;
        RigidBodyInfo *liveInfo = live->info;
        static RigidBodyInfo saved;
        saved = *liveInfo;
        PhysicsObject *owner = PhysicsObjects[live->ownerIndex];
        PBondCarFields *car = Car(live);
        RigidVehiclePhysics physics = {};
        if (car != NULL && car->physics != NULL)
            physics = *car->physics;
        for (int repeat = 0; repeat < 6; repeat++, index++) {
            alignas(16) Coord4 extents = saved.halfExtents;
            if (repeat > 0 && RandomInt(3) == 0)
                extents.y = Uniform(0.0f, 0.002f);
            if (repeat > 0 && RandomInt(4) == 0)
                extents.x = Uniform(0.0f, 0.002f);
            const Coord3 inertia = { live->inverseInertiaX, live->inverseInertiaY, live->inverseInertiaZ };
            const MATRIX4 orientation = saved.orientation;
            const bool moving = repeat == 0 ? live->sleepState == RigidBody::kAwake : RandomInt(2) != 0;
            const bool flag1 = repeat == 0 ? (live->flags & RigidBody::kFlag1) != 0 : RandomInt(2) != 0;
            FreshGameState();
            memset(g_start.body, 0, sizeof(g_start.body));
            memset(g_start.info, 0, sizeof(g_start.info));
            memset(g_start.positions, 0, sizeof(g_start.positions));
            Both([&](bool original) {
                *liveInfo = saved;
                RigidBody *body = &g_work.body[0];
                if (original)
                    Orig_Construct(body, 0, live->ownerIndex, live->kind, &live->position, &live->velocity,
                                   &live->angularVelocity, &orientation, live->mass, &inertia, &extents, owner,
                                   moving, flag1);
                else
                    body->Construct(live->ownerIndex, live->kind, &live->position, &live->velocity,
                                    &live->angularVelocity, &orientation, live->mass, &inertia, &extents, owner,
                                    moving, flag1);
                g_work.info[0] = *liveInfo;
                for (int i = 0; i < kMaxLevers; i++) {
                    if (liveInfo->leverPositions[i] != NULL)
                        g_work.positions[0][i] = *liveInfo->leverPositions[i];
                    g_work.info[0].leverPositions[i] = NULL;
                }
                memset(g_work.info[0].worldInverseInertia.mtx[3], 0, sizeof(g_work.info[0].worldInverseInertia.mtx[3]));
                g_work.body[1] = *body;
                if (original)
                    Orig_Destruct(&g_work.body[1], 0);
                else
                    g_work.body[1].Destruct();
                if (car != NULL && car->physics != NULL)
                    *car->physics = physics;
            });
            CompareRuns("Construct", index);
        }
        *liveInfo = saved;
    }
}

void TestReset() {
    int index = 0;
    for (RigidBody *live : g_live) {
        for (int repeat = 0; repeat < 10; repeat++, index++) {
            FreshGameState();
            CopyBody(0, live, repeat == 0);
            CopyBody(1, live, true);
            static MATRIX4 orientation;
            orientation = live->info->orientation;
            Coord3 position = live->position;
            if (repeat > 0) {
                RandomRotation(&orientation);
                position.x += Uniform(-5.0f, 5.0f);
                position.y += Uniform(-2.0f, 2.0f);
                position.z += Uniform(-5.0f, 5.0f);
            }
            Both([&](bool original) {
                if (original)
                    Orig_ResetObject(&g_work.body[0], 0, &orientation, &position);
                else
                    g_work.body[0].ResetObject(&orientation, &position);
                // the inverse inertia's row 3: the original's stack leftovers
                memset(g_work.info[0].worldInverseInertia.mtx[3], 0, sizeof(g_work.info[0].worldInverseInertia.mtx[3]));
            });
            CompareRuns("ResetObject", index);
        }
    }
}

void TestInitialForces() {
    int index = 0;
    for (RigidBody *live : g_live) {
        if (!OwnerCallsSafe(live))
            continue;
        for (int repeat = 0; repeat < 30; repeat++, index++) {
            const bool real = repeat == 0;
            FreshGameState();
            CopyBody(0, live, real);
            CopyBody(1, live, true);
            if (!real) {
                PerturbGameState();
                PerturbCars();
            }
            Both([&](bool original) {
                if (original)
                    Orig_ApplyInitialForces(&g_work.body[0], 0);
                else
                    g_work.body[0].ApplyInitialForcesAndTorques();
            });
            RestoreCars();
            CompareRuns("ApplyInitialForcesAndTorques", index);
        }
    }
}

void TestLeverForces() {
    int index = 0;
    for (RigidBody *live : g_live) {
        if (!OwnerCallsSafe(live))
            continue;
        for (int repeat = 0; repeat < 30; repeat++, index++) {
            FreshGameState();
            CopyBody(0, live, repeat == 0);
            CopyBody(1, live, true);
            PerturbCars();
            static LeverContacts contacts;
            contacts.unknown00 = 0.0f;
            contacts.friction = RandomInt(2) ? live->info->groundFriction : Uniform(0.0f, 100.0f);
            contacts.unknown08 = live->info->unknown4d0;
            contacts.count = 1 + RandomInt(kMaxLevers);
            const Coord4 &extents = live->info->halfExtents;
            for (int i = 0; i < kMaxLevers; i++) {
                g_start.vectors[i] = { Uniform(-1.2f, 1.2f) * extents.x, Uniform(-1.2f, 1.0f) * extents.y,
                                       Uniform(-1.2f, 1.2f) * extents.z, 0.0f };
                Coord4 normal = RandomQuaternion();
                if (RandomInt(2))
                    normal = { normal.x * 0.2f, 1.0f, normal.z * 0.2f, 0.0f };
                float length = sqrtf(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
                g_start.vectors[kMaxLevers + i] = { normal.x / length, normal.y / length, normal.z / length,
                                                    Uniform(0.0f, 0.5f) };
            }
            Both([&](bool original) {
                RigidBody *body = &g_work.body[0];
                const MATRIX4 *inertia = &g_work.info[0].worldInverseInertia;
                if (original)
                    Orig_ResolveLeverForces(body, 0, &contacts, &g_work.vectors[0], &g_work.vectors[kMaxLevers],
                                            inertia);
                else
                    body->ResolveLeverForces(&contacts, &g_work.vectors[0], &g_work.vectors[kMaxLevers], inertia);
            });
            RestoreCars();
            CompareRuns("ResolveLeverForces", index);
        }
    }
}

void TestGround() {
    int index = 0;
    for (RigidBody *live : g_live) {
        if (!OwnerCallsSafe(live))
            continue;
        for (int repeat = 0; repeat < 30; repeat++, index++) {
            const bool real = repeat == 0;
            FreshGameState();
            CopyBody(0, live, real);
            CopyBody(1, live, true);
            if (!real) {
                RigidBody &body = g_start.body[0];
                body.position.y += RandomInt(2) ? Uniform(-0.5f, 0.5f) : Uniform(-2.0f, 2.0f);
                if (RandomInt(3) == 0)
                    body.velocity.y = Uniform(-45.0f, -25.0f);
                if (RandomInt(3) == 0) {
                    MATRIX4 &orientation = g_start.info[0].orientation;
                    RandomRotation(&orientation);
                    VU0_MATRIX4_vect4multarray(g_start.info[0].levers, &orientation, g_start.info[0].worldLevers,
                                               g_start.info[0].leverCount);
                }
                if (RandomInt(5) == 0)
                    g_start.underwater = 1;
                PerturbCars();
            }
            Both([&](bool original) {
                if (original)
                    Orig_CollideWithGround(&g_work.body[0], 0);
                else
                    g_work.body[0].CollideWithGround();
            });
            RestoreCars();
            CompareRuns("CollideWithGround", index);
        }
    }
}

void TestPairs() {
    int index = 0;
    for (size_t i = 0; i < g_live.size(); i++) {
        for (size_t j = 0; j < g_live.size(); j++) {
            for (int repeat = 0; repeat < 6; repeat++, index++) {
                FreshGameState();
                CopyBody(0, g_live[i], repeat == 0);
                CopyBody(1, g_live[j], repeat == 0);
                if (RandomInt(3) == 0) {
                    // aligned along their forward axes, one behind the other
                    g_start.info[1].orientation = g_start.info[0].orientation;
                    const float *forward = g_start.info[0].orientation.mtx[2];
                    float distance = Uniform(-6.0f, 6.0f);
                    g_start.body[1].position = { g_start.body[0].position.x + forward[0] * distance,
                                                 g_start.body[0].position.y + forward[1] * distance,
                                                 g_start.body[0].position.z + forward[2] * distance };
                }
                for (int v = 0; v < 4; v++)
                    g_start.vectors[v] = { Uniform(-3.0f, 3.0f), Uniform(-1.5f, 1.5f), Uniform(-3.0f, 3.0f), 0.0f };
                g_start.vectors[2].x += g_start.body[0].position.x;
                g_start.vectors[2].y += g_start.body[0].position.y;
                g_start.vectors[2].z += g_start.body[0].position.z;
                const bool modify = RandomInt(2) != 0;
                Both([&](bool original) {
                    RigidBody *a = &g_work.body[0], *b = &g_work.body[1];
                    Coord4 *v = g_work.vectors;
                    if (original)
                        Orig_ModifyLevers(a, b, &v[0], &v[1], &v[2], &v[3], modify);
                    else
                        RigidBody::ModifyLevers(a, b, &v[0], &v[1], &v[2], &v[3], modify);
                });
                CompareRuns("ModifyLevers", index);

                for (int v = 0; v < 2; v++) {
                    float size = RandomInt(2) ? 500.0f : 50000.0f;
                    g_start.vectors[v] = { Uniform(-size, size), Uniform(-size, size), Uniform(-size, size), 0.0f };
                }
                const float limit = RandomInt(2) ? 225.0f : Uniform(0.0f, 400.0f);
                const float scale = RandomInt(2) ? 1.05f : Uniform(0.5f, 2.0f);
                const float boost = RandomInt(2) ? Uniform(0.0f, 0.5f) : Uniform(0.4f, 1.5f);
                Both([&](bool original) {
                    RigidBody *a = &g_work.body[0], *b = &g_work.body[1];
                    Coord4 *v = g_work.vectors;
                    if (original)
                        Orig_ScaleObjObjForces(a, b, &v[0], &v[1], limit, scale, boost);
                    else
                        RigidBody::ScaleObjObjForces(a, b, &v[0], &v[1], limit, scale, boost);
                });
                CompareRuns("ScaleObjObjForces", index);
            }
        }
    }
}

void TestSleep() {
    int index = 0;
    for (RigidBody *live : g_live) {
        for (int repeat = 0; repeat < 30; repeat++, index++) {
            FreshGameState();
            CopyBody(0, live, repeat == 0);
            CopyBody(1, live, true);
            if (repeat > 0) {
                RigidBody &body = g_start.body[0];
                float speed = Rigid_SLEEP_VEL * (RandomInt(2) ? 0.25f : 1.0f);
                RandomVector(&body.velocity, speed * Uniform(0.0f, 0.8f));
                RandomVector(&body.angularVelocity, speed * Uniform(0.0f, 0.8f));
                if (RandomInt(3) == 0)
                    RandomRotation(&g_start.info[0].orientation);
            }
            Both([&](bool original) {
                if (original)
                    Orig_ControlSleep(&g_work.body[0], 0);
                else
                    g_work.body[0].ControlSleep();
            });
            CompareRuns("ControlSleep", index);
        }
    }
}

void TestUpdate() {
    int index = 0;
    for (RigidBody *live : g_live) {
        for (int repeat = 0; repeat < 30; repeat++, index++) {
            FreshGameState();
            CopyBody(0, live, repeat == 0);
            CopyBody(1, live, true);
            if (repeat > 0) {
                RigidBody &body = g_start.body[0];
                RandomVector(&body.force, RandomInt(2) ? 5000.0f : 200000.0f);
                RandomVector(&body.torque, RandomInt(2) ? 5000.0f : 200000.0f);
                if (RandomInt(4) == 0)
                    RandomVector(&body.momentum, 300000.0f);
                Coord4 q = RandomQuaternion();
                float stretch = Uniform(0.98f, 1.02f);
                if (RandomInt(2))
                    body.orientation = { q.x * stretch, q.y * stretch, q.z * stretch, q.w * stretch };
            }
            Both([&](bool original) {
                if (original)
                    Orig_UpdatePosition(&g_work.body[0], 0, 0.0f);
                else
                    g_work.body[0].UpdatePositionAndOrientation(0.0f);
            });
            CompareRuns("UpdatePositionAndOrientation", index);
        }
    }
}

}  // namespace

void RigidCoreShadow_Run(void) {
    char value[16] = "";
    DWORD length = GetEnvironmentVariableA("NIGHTFIRE_RIGIDCORESHADOW", value, sizeof(value));
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
    if (g_live.empty() || RigidScratchPad == NULL || fgCollisionMgr == NULL) {
        printf("[rigidcore] no live rigid bodies - nothing tested\n");
        fflush(stdout);
        return;
    }
    FpControlGet(&g_x87, &g_sse);

    // the game's state the tests write, put back at the end
    static uint8_t scratch[kScratchBytes];
    memcpy(scratch, RigidScratchPad, kScratchBytes);
    const float playerGravity = RigidPlayerGravity;
    const uint8_t underwater = RigidUnderwater;
    const uint32_t queryField = CollisionQueryField();
    std::vector<uint8_t> wasInAir;
    for (RigidBody *live : g_live)
        wasInAir.push_back(Car(live) != NULL ? Car(live)->wasInAir : 0);
    SaveCars();

    TestConstruct();
    TestReset();
    TestInitialForces();
    TestLeverForces();
    TestGround();
    TestPairs();
    TestSleep();
    TestUpdate();

    RestoreCars();
    for (size_t i = 0; i < g_live.size(); i++)
        if (Car(g_live[i]) != NULL)
            Car(g_live[i])->wasInAir = wasInAir[i];
    memcpy(RigidScratchPad, scratch, kScratchBytes);
    RigidPlayerGravity = playerGravity;
    RigidUnderwater = underwater;
    CollisionQueryField() = queryField;
    ResetFpu();

    printf("[rigidcore] RigidBody core vs originals (%d live bodies): %d cases, %d checks, %d differ%s\n",
           int(g_live.size()), g_cases, g_checks, g_differ, g_faults != 0 ? " (with faults)" : "");
    fflush(stdout);
}
