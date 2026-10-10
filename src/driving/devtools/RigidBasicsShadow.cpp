#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "RigidBasicsShadow.h"
#include "FpControl.h"

#include "../physics/Newton.h"
#include "../physics/PhysicsObject.h"
#include "../physics/RigidBody.h"
#include "../physics/RigidBodyBasics.h"
#include "../physics/SimpleRigidBody.h"
#include "../engine/UMemory.hpp"
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
// NIGHTFIRE_RIGIDBASICSSHADOW=1, from the first simulation tick: the ports in physics/RigidBodyBasics.cpp and
// physics/Newton.cpp against the originals (their entry bytes swapped back in for each original call,
// common/xbeOriginal.h), on identical inputs, compared byte for byte.
//
//   - Every RigidBody accessor, conversion, SetOrientation, the Resolve* methods, damping, friction, the zone and
//     ForceToSleep on copies of each live rigid body (the body and its RigidBodyInfo copied together), first as
//     they are, then perturbed: momenta, velocities, orientations, positions (zone ties), inertia, mass, ground
//     contacts, kind and sleep state (frozen bodies too). The body, its info and every output compared.
//   - CalculateAndApplyWorldDamage, TempGetHeightInformation and InitLevers with a recording stand-in owner (a copy
//     of the body's real owner with its vtable swapped, put in a free owner slot): the calls it receives with their
//     arguments, the vehicle tuning record it hands out, the outputs. TempGetHeightInformation at points round each
//     body and far off the track, from the owner's WWorldPos, an empty one and a stale one; InitLevers with the
//     real render object, none, and a stand-in with 0 to 20 collision points (car and non-car owners).
//   - ClampedForceRatio (edges, NaN, infinities), PointerVectorDeallocate (the block returns to its pool),
//     ResetRigidBodySP, Newton::ApplyDamage.
//   - Newton::Simulate on a stand-in Newton (no render object) driving a free simple body slot near the player:
//     steps from above the ground to above or below it, varied spin, velocity, mass (3.14159 among them) and
//     orientation; the Newton and the simple body compared.
// The collision manager's query stamp is left running (each side gets fresh stamps); its barrier mask, the owner
// slot, the simple body and the scratch pad's fields are put back.
//
// Not covered here (lockstep runs): InitRigidBodySystem (it reads the tuning file), Newton's constructor,
// destructors and SpawnFromEvent (they allocate, spawn and hide scene objects), Simulate's last step (it deletes the
// object).
//
// One mutation this catches: SetOrientation multiplying R^T (R S) instead of R^T (S R) changes the world inverse
// inertia of every rotated body; adding TempGetHeightInformation's x term before the z term changes the height's
// low bits at most points.
// ---------------------------------------------------------------------------------------------------------------

namespace {

const uint32_t kRanges[2][2] = {
    { 0x00060b60, 0x00061a50 }, { 0x000acde0, 0x000adc80 },
};

struct OriginalWindow {
    OriginalWindow() {
        for (const uint32_t *range : kRanges)
            XbeOriginal_RestoreRange(range[0], range[1], true);
    }
    ~OriginalWindow() {
        for (const uint32_t *range : kRanges)
            XbeOriginal_RestoreRange(range[0], range[1], false);
    }
};

// ---- the originals

typedef PhysicsObject *(__fastcall *OwnerFn)(RigidBody *, int);
typedef double (__fastcall *OrientFn)(RigidBody *, int);
typedef void (__fastcall *BodyFn)(RigidBody *, int);
typedef void (__fastcall *BodyVectorFn)(RigidBody *, int, void *);
typedef Coord3 *(__fastcall *LocalFn)(RigidBody *, int, Coord3 *);
typedef void (__fastcall *DamageFn)(RigidBody *, int, const Coord4 *, float);
typedef bool (__fastcall *HeightFn)(RigidBody *, int, bool, const Coord3 *, Coord4 *, WWorldPos *);
typedef void (__fastcall *LeversFn)(RigidBody *, int, PhysicsObject *, const Coord4 *);
typedef void (__stdcall *DeallocateFn)(void **, uint32_t);
typedef double (*RatioFn)(float, float, float);
typedef void (*StaticFn)(void);
typedef void (__fastcall *NewtonStepFn)(Newton *, int);
typedef int (__fastcall *NewtonDamageFn)(Newton *, int, const void *, const void *, float, float, int, const uint32_t *);
typedef RigidBody *(__fastcall *GetRigidBodyFn)(void *, int, int);
typedef SimpleRigidBody *(__fastcall *GetSimpleBodyFn)(void *, int, int);
typedef PhysicsObject *(__fastcall *GetPlayerFn)(void *, int);

#define Orig_GetOwner ((OwnerFn)0x000ad100)
#define Orig_GetOrientToGround ((OrientFn)0x000ad110)
#define Orig_CalculateAndApplyWorldDamage ((DamageFn)0x000ad520)
#define Orig_TempGetHeightInformation ((HeightFn)0x000ad690)
#define Orig_InitLevers ((LeversFn)0x000ad840)
#define Orig_PointerVectorDeallocate ((DeallocateFn)0x000ad630)
#define Orig_ClampedForceRatio ((RatioFn)0x000ad650)
#define Orig_ResetRigidBodySP ((StaticFn)0x000ad0e0)
#define Orig_NewtonSimulate ((NewtonStepFn)0x00060b70)
#define Orig_NewtonApplyDamage ((NewtonDamageFn)0x00061050)
#define Sim_GetRigidBody ((GetRigidBodyFn)0x000b2700)
#define Sim_GetSimpleBody ((GetSimpleBodyFn)0x000b2730)
#define Sim_GetPlayerObject ((GetPlayerFn)0x000b2d30)

#define ShadowSim ((void *)0x00233ff0)
#define ShadowSimpleOwners ((void **)0x002343a0)
#define ShadowDamageSourceSig ((const uint32_t *)0x00233fe8)
const int kRigidBodies = 0x40;
const int kSimpleBodies = 0x60;

// ---- results

int g_cases = 0, g_checks = 0, g_differ = 0, g_details = 0, g_faults = 0;
unsigned int g_x87 = 0, g_sse = 0;

void Differ(const char *what, int index, const char *detail) {
    g_differ++;
    if (g_details++ < 10)
        printf("[rigidbasics]   %s #%d: %s\n", what, index, detail);
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
    snprintf(detail, sizeof(detail), "byte %u of %u: original %02x, port %02x", unsigned(at), unsigned(bytes), x[at],
             y[at]);
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

// The original (inside the window), then the port; false if either faulted
template <class F>
bool Both(F &&run) {
    typedef typename std::remove_reference<F>::type Run;
    CaseFn thunk = [](void *context, bool original) { (*static_cast<Run *>(context))(original); };
    bool ok;
    {
        OriginalWindow window;
        ok = Guarded(thunk, &run, true);
    }
    return Guarded(thunk, &run, false) && ok;
}

// ---- inputs

uint32_t g_random = 0x5eed1e55;

uint32_t Random() {
    g_random ^= g_random << 13;
    g_random ^= g_random >> 17;
    g_random ^= g_random << 5;
    return g_random;
}

int RandomInt(int n) { return n <= 0 ? 0 : int(Random() % uint32_t(n)); }
float Uniform(float lo, float hi) { return lo + (hi - lo) * float(Random() >> 8) * (1.0f / 16777216.0f); }

// Now and then a value the code meets rarely
float Special() {
    static const uint32_t kBits[] = {
        0x00000000, 0x80000000, 0x3f800000, 0xbf800000, 0x7f800000, 0xff800000, 0x7fc00000, 0x00000001, 0x3e4ccccd,
        0x3d4ccccd, 0x40490fd0, 0x41200000,
    };
    uint32_t bits = kBits[RandomInt(int(sizeof(kBits) / sizeof(kBits[0])))];
    float f;
    memcpy(&f, &bits, 4);
    return f;
}

Coord3 RandomVector(float range) { return { Uniform(-range, range), Uniform(-range, range), Uniform(-range, range) }; }

Coord4 RandomQuaternion(bool unit) {
    Coord4 q = { Uniform(-1.0f, 1.0f), Uniform(-1.0f, 1.0f), Uniform(-1.0f, 1.0f), Uniform(-1.0f, 1.0f) };
    if (unit) {
        float length = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
        if (length > 0.0f) {
            q.x /= length;
            q.y /= length;
            q.z /= length;
            q.w /= length;
        }
    }
    return q;
}

// ---- a body and its info, copied together

struct BodyState {
    alignas(16) RigidBody body;
    alignas(16) RigidBodyInfo info;
};

void Prepare(BodyState *state, const BodyState &snapshot) {
    memcpy(state, &snapshot, sizeof(*state));
    state->body.info = &state->info;
}

void Perturb(BodyState *state) {
    RigidBody &b = state->body;
    if (RandomInt(2)) b.orientation = RandomQuaternion(RandomInt(4) != 0);
    if (RandomInt(2)) b.position = { b.position.x + Uniform(-20.0f, 20.0f), b.position.y + Uniform(-3.0f, 3.0f),
                                     b.position.z + Uniform(-20.0f, 20.0f) };
    if (RandomInt(6) == 0) b.position.x = 2.5f * float(2 * RandomInt(400) - 399);   // x * 0.2 near a tie
    if (RandomInt(2)) b.velocity = RandomVector(40.0f);
    if (RandomInt(2)) b.angularVelocity = RandomVector(6.0f);
    if (RandomInt(2)) b.momentum = RandomVector(4000.0f);
    if (RandomInt(2)) b.angularMomentum = RandomVector(2000.0f);
    if (RandomInt(2)) b.force = RandomVector(1000.0f);
    if (RandomInt(2)) b.torque = RandomVector(1000.0f);
    if (RandomInt(3) == 0) {
        b.inverseInertiaX = Uniform(0.0f, 0.01f);
        b.inverseInertiaY = Uniform(0.0f, 0.01f);
        b.inverseInertiaZ = Uniform(0.0f, 0.01f);
    }
    if (RandomInt(3) == 0) b.mass = Uniform(1.0f, 3000.0f);
    if (RandomInt(2)) b.groundContacts = uint8_t(RandomInt(5));
    if (RandomInt(2)) b.kind = int8_t(RandomInt(7));
    if (RandomInt(2)) b.sleepState = uint8_t(1 + RandomInt(3));
    if (RandomInt(3) == 0)
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 4; c++)
                state->info.orientation.mtx[r][c] = Uniform(-1.0f, 1.0f);
    if (RandomInt(12) == 0) {
        float *fields[] = { &b.velocity.x, &b.angularMomentum.y, &b.position.z, &b.mass, &b.force.x,
                            &state->info.orientation.mtx[1][0], &state->info.orientation.mtx[1][2] };
        *fields[RandomInt(7)] = Special();
    }
}

// The live rigid bodies: those whose owner names them
std::vector<RigidBody *> LiveBodies() {
    std::vector<RigidBody *> bodies;
    for (int slot = 0; slot < kRigidBodies; slot++) {
        RigidBody *body = Sim_GetRigidBody(ShadowSim, 0, slot);
        if (body == NULL || body->info == NULL || body->ownerIndex < 0 || body->ownerIndex >= kRigidBodies)
            continue;
        PhysicsObject *owner = PhysicsObjects[body->ownerIndex];
        if (owner == NULL || (owner->flags & PhysicsObject::kSimpleBody) || owner->rigidBodySlot != slot)
            continue;
        bodies.push_back(body);
    }
    return bodies;
}

int FreeOwnerSlot() {
    for (int i = kRigidBodies - 1; i >= 0; i--)
        if (PhysicsObjects[i] == NULL)
            return i;
    return -1;
}

// ---------------------------------------------------------------------------------------------------------------
// The accessors, conversions and force methods

enum Method {
    kGetOwner, kOrientToGround, kAngularDamping, kHeavyFriction, kModifyAngularMomentum, kLocalAngularMomentum,
    kLocalVelocity, kLocalAngularVelocity, kWorldToLocal, kLocalToWorld, kSetAngularMomentum, kSetOrientation,
    kResolveForce, kResolveTorque, kResolveForce4, kResolveTorque4, kZonalInfo, kForceToSleep, kMethodCount,
};

const char *const kMethodNames[kMethodCount] = {
    "GetOwner", "GetOrientToGround", "ApplyAngularDamping", "ApplyHeavyFriction", "ModifyAngularMomentum",
    "GetLocalAngularMomentum", "GetLocalVelocity", "GetLocalAngularVelocity", "ConvertWorldToLocal",
    "ConvertLocalToWorld", "SetAngularMomentum", "SetOrientation", "ResolveForce", "ResolveTorque",
    "ResolveMassScaledForce4", "ResolveMassScaledTorque4", "UpdateZonalInfo", "ForceToSleep",
};

const uint32_t kMethodAddress[kMethodCount] = {
    0x000ad100, 0x000ad110, 0x000ad130, 0x000ad170, 0x000ad1a0, 0x000ad1d0, 0x000ad220, 0x000ad270, 0x000ad2c0,
    0x000ad2f0, 0x000ad310, 0x000ad330, 0x000ad3f0, 0x000ad420, 0x000ad440, 0x000ad470, 0x000ad4a0, 0x000ad600,
};

struct MethodSide {
    BodyState state;
    alignas(16) Coord4 vector;      // the argument, written in place by some
    alignas(16) uint8_t out[32];
};

void RunMethod(int method, MethodSide *side, bool original) {
    RigidBody *b = &side->state.body;
    uint32_t at = kMethodAddress[method];
    Coord3 *v3 = reinterpret_cast<Coord3 *>(&side->vector);
    switch (method) {
    case kGetOwner: {
        PhysicsObject *owner = original ? ((OwnerFn)at)(b, 0) : b->GetOwner();
        memcpy(side->out, &owner, sizeof(owner));
        break;
    }
    case kOrientToGround: {
        double r = original ? ((OrientFn)at)(b, 0) : b->GetOrientToGround();
        memcpy(side->out, &r, sizeof(r));
        break;
    }
    case kAngularDamping: original ? ((BodyFn)at)(b, 0) : b->ApplyAngularDamping(); break;
    case kHeavyFriction: original ? ((BodyFn)at)(b, 0) : b->ApplyHeavyFriction(); break;
    case kModifyAngularMomentum: original ? ((BodyVectorFn)at)(b, 0, v3) : b->ModifyAngularMomentum(v3); break;
    case kLocalAngularMomentum:
    case kLocalVelocity:
    case kLocalAngularVelocity: {
        Coord3 *result = reinterpret_cast<Coord3 *>(side->out);
        Coord3 *r;
        if (original)
            r = ((LocalFn)at)(b, 0, result);
        else if (method == kLocalAngularMomentum)
            r = b->GetLocalAngularMomentum(result);
        else if (method == kLocalVelocity)
            r = b->GetLocalVelocity(result);
        else
            r = b->GetLocalAngularVelocity(result);
        side->out[16] = r == result;
        break;
    }
    case kWorldToLocal:
        original ? ((BodyVectorFn)at)(b, 0, &side->vector) : b->ConvertWorldToLocal(&side->vector);
        break;
    case kLocalToWorld: original ? ((BodyVectorFn)at)(b, 0, v3) : b->ConvertLocalToWorld(v3); break;
    case kSetAngularMomentum: original ? ((BodyVectorFn)at)(b, 0, v3) : b->SetAngularMomentum(v3); break;
    case kSetOrientation: original ? ((BodyVectorFn)at)(b, 0, &side->vector) : b->SetOrientation(&side->vector); break;
    case kResolveForce: original ? ((BodyVectorFn)at)(b, 0, v3) : b->ResolveForce(v3); break;
    case kResolveTorque: original ? ((BodyVectorFn)at)(b, 0, v3) : b->ResolveTorque(v3); break;
    case kResolveForce4:
        original ? ((BodyVectorFn)at)(b, 0, &side->vector) : b->ResolveMassScaledForce4(&side->vector);
        break;
    case kResolveTorque4:
        original ? ((BodyVectorFn)at)(b, 0, &side->vector) : b->ResolveMassScaledTorque4(&side->vector);
        break;
    case kZonalInfo: original ? ((BodyFn)at)(b, 0) : b->UpdateZonalInfo(); break;
    case kForceToSleep: original ? ((BodyFn)at)(b, 0) : b->ForceToSleep(); break;
    }
}

void CompareStates(const char *what, int index, BodyState *a, BodyState *b) {
    a->body.info = NULL;    // each points at its own copy
    b->body.info = NULL;
    CheckBytes(what, index, &a->body, &b->body, sizeof(a->body));
    CheckBytes(what, index, &a->info, &b->info, sizeof(a->info));
}

void TestMethods(const std::vector<RigidBody *> &bodies) {
    int index = 0;
    for (RigidBody *live : bodies) {
        for (int round = 0; round < 60; round++) {
            alignas(16) BodyState snapshot;
            memcpy(&snapshot.body, live, sizeof(RigidBody));
            memcpy(&snapshot.info, live->info, sizeof(RigidBodyInfo));
            if (round != 0)
                Perturb(&snapshot);
            Coord4 vector;
            switch (RandomInt(3)) {
            case 0: vector = RandomQuaternion(true); break;
            case 1: vector = { Uniform(-100.0f, 100.0f), Uniform(-100.0f, 100.0f), Uniform(-100.0f, 100.0f),
                               Uniform(-2.0f, 2.0f) }; break;
            default: vector = RandomQuaternion(false); break;
            }
            for (int method = 0; method < kMethodCount; method++, index++) {
                static MethodSide a, b;
                memset(&a, 0, sizeof(a));
                memset(&b, 0, sizeof(b));
                Prepare(&a.state, snapshot);
                Prepare(&b.state, snapshot);
                a.vector = b.vector = vector;
                if (!Both([&](bool original) { RunMethod(method, original ? &a : &b, original); }))
                    continue;
                g_cases++;
                CompareStates(kMethodNames[method], index, &a.state, &b.state);
                CheckBytes(kMethodNames[method], index, &a.vector, &b.vector, sizeof(a.vector));
                CheckBytes(kMethodNames[method], index, a.out, b.out, sizeof(a.out));
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------
// The stand-in owner: a copy of a real owner whose vtable records the calls and answers as each case sets

struct FakeCall {
    uint32_t slot;
    float values[8];
    int32_t ints[2];
};

struct FakeState {
    CarPhysics physics;
    uint8_t physicsTail[0x40];
    void *splinePath;
    uint32_t timer;
    float renderOffset;
    uint32_t callCount;
    FakeCall calls[8];
    uint32_t unexpected;
};

FakeState g_fake;
FakeCall g_spareCall;
void *g_fakeVtable[0x150 / 4];
void *g_fakeRenderVtable[19];
alignas(16) PhysicsObject g_fakeOwner;

// A stand-in render object for InitLevers: its BaseDesc's collision primitive count and points
struct FakeRender {
    void **vtable;
    uint8_t unknown04[0xc];
    uint8_t *baseDesc;          // +0x10
    uint8_t unknown14[0x2c];
};
static_assert(sizeof(FakeRender) == 0x40, "a scene object is 0x40 bytes");
alignas(16) FakeRender g_fakeRender;
alignas(16) uint8_t g_fakeBaseDesc[0x60 + 24 * sizeof(Coord4)];

FakeCall *NextCall(uint32_t slot) {
    FakeCall *call = g_fake.callCount < 8 ? &g_fake.calls[g_fake.callCount++] : &g_spareCall;
    call->slot = slot;
    return call;
}

int __fastcall FakeApplyDamage(PhysicsObject *, int, const Coord3 *direction, const Coord3 *position, float amount,
                               float unknown4, int kind, const uint32_t *sourceSig) {
    FakeCall *call = NextCall(2);
    call->values[0] = direction->x;
    call->values[1] = direction->y;
    call->values[2] = direction->z;
    call->values[3] = position->x;
    call->values[4] = position->y;
    call->values[5] = position->z;
    call->values[6] = amount;
    call->values[7] = unknown4;
    call->ints[0] = kind;
    call->ints[1] = sourceSig == ShadowDamageSourceSig;
    return 0x20;
}

void *__fastcall FakeGetSplinePath(PhysicsObject *, int) {
    NextCall(11);
    return g_fake.splinePath;
}

void __fastcall FakeAddDamageByPlayer(PhysicsObject *, int, float damage) {
    NextCall(52)->values[0] = damage;
}

bool __fastcall FakeGetDamageByPlayerTimer(PhysicsObject *, int) {
    NextCall(53);
    return g_fake.timer != 0;
}

CarPhysics *__fastcall FakeGetPhysics(PhysicsObject *, int) {
    NextCall(57);
    return &g_fake.physics;
}

void __fastcall FakeUnexpected(void *, int) {
    g_fake.unexpected++;
}

float __fastcall FakeRenderOffset(void *, int) {
    return g_fake.renderOffset;
}

void InitFakes() {
    for (void *&slot : g_fakeVtable)
        slot = reinterpret_cast<void *>(&FakeUnexpected);
    g_fakeVtable[2] = reinterpret_cast<void *>(&FakeApplyDamage);
    g_fakeVtable[11] = reinterpret_cast<void *>(&FakeGetSplinePath);
    g_fakeVtable[52] = reinterpret_cast<void *>(&FakeAddDamageByPlayer);
    g_fakeVtable[53] = reinterpret_cast<void *>(&FakeGetDamageByPlayerTimer);
    g_fakeVtable[57] = reinterpret_cast<void *>(&FakeGetPhysics);
    for (void *&slot : g_fakeRenderVtable)
        slot = reinterpret_cast<void *>(&FakeUnexpected);
    g_fakeRenderVtable[9] = reinterpret_cast<void *>(&FakeRenderOffset);
    memset(&g_fakeRender, 0, sizeof(g_fakeRender));
    g_fakeRender.vtable = g_fakeRenderVtable;
    g_fakeRender.baseDesc = g_fakeBaseDesc;
}

FakeState RandomFake() {
    FakeState fake;
    memset(&fake, 0, sizeof(fake));
    uint8_t *bytes = reinterpret_cast<uint8_t *>(&fake.physics);
    for (size_t i = 0; i < sizeof(fake.physics); i++)
        bytes[i] = uint8_t(Random());
    fake.physics.isSnowmobile = RandomInt(2) ? int32_t(Random()) : 0;
    fake.physics.isBoat = RandomInt(2) ? int32_t(Random()) : 0;
    fake.splinePath = RandomInt(3) == 0 ? &g_fake : NULL;
    fake.timer = RandomInt(2);
    fake.renderOffset = Uniform(-1.0f, 1.0f);
    return fake;
}

// The stand-in owner in a free owner slot for the life of a case
struct FakeOwnerScope {
    int slot;
    explicit FakeOwnerScope(int s) : slot(s) { PhysicsObjects[slot] = &g_fakeOwner; }
    ~FakeOwnerScope() { PhysicsObjects[slot] = NULL; }
};

void MakeFakeOwner(const PhysicsObject *real) {
    memcpy(&g_fakeOwner, real, sizeof(g_fakeOwner));
    g_fakeOwner.vtable = g_fakeVtable;
}

struct OwnerSide {
    BodyState state;
    alignas(16) WWorldPos position;
    alignas(16) Coord4 ground;
    FakeState fake;
    uint32_t barrierMask;
    uint32_t answer;
};

void TestOwnerMethods(const std::vector<RigidBody *> &bodies) {
    int slot = FreeOwnerSlot();
    if (slot < 0) {
        printf("[rigidbasics]   no free owner slot: the owner tests are skipped\n");
        return;
    }
    WCollisionMgr *manager = fgCollisionMgr;
    uint32_t savedMask = manager->barrierMask;
    int index = 0;
    for (RigidBody *live : bodies) {
        PhysicsObject *realOwner = PhysicsObjects[live->ownerIndex];
        for (int round = 0; round < 120; round++, index++) {
            alignas(16) BodyState snapshot;
            memcpy(&snapshot.body, live, sizeof(RigidBody));
            memcpy(&snapshot.info, live->info, sizeof(RigidBodyInfo));
            if (round >= 3)
                Perturb(&snapshot);
            snapshot.body.ownerIndex = int8_t(slot);
            MakeFakeOwner(realOwner);
            FakeState fake = RandomFake();
            static OwnerSide a, b;

            // CalculateAndApplyWorldDamage
            {
                Coord3 random = RandomVector(1.0f);
                Coord4 direction = { random.x, random.y, random.z, 0.0f };
                float force = RandomInt(4) == 0 ? Uniform(-0.1f, 0.3f) : Uniform(0.0f, 40.0f);
                if (RandomInt(16) == 0) force = Special();
                memset(&a, 0, sizeof(a));
                memset(&b, 0, sizeof(b));
                Prepare(&a.state, snapshot);
                Prepare(&b.state, snapshot);
                FakeOwnerScope scope(slot);
                bool ok = Both([&](bool original) {
                    OwnerSide &s = original ? a : b;
                    g_fake = fake;
                    if (original)
                        Orig_CalculateAndApplyWorldDamage(&s.state.body, 0, &direction, force);
                    else
                        s.state.body.CalculateAndApplyWorldDamage(&direction, force);
                    s.fake = g_fake;
                });
                if (ok) {
                    g_cases++;
                    CompareStates("CalculateAndApplyWorldDamage", index, &a.state, &b.state);
                    CheckBytes("CalculateAndApplyWorldDamage calls", index, &a.fake, &b.fake, sizeof(a.fake));
                }
            }

            // TempGetHeightInformation
            for (int probe = 0; probe < 4; probe++) {
                Coord3 point = { live->position.x + Uniform(-6.0f, 6.0f), live->position.y + Uniform(-3.0f, 3.0f),
                                 live->position.z + Uniform(-6.0f, 6.0f) };
                if (RandomInt(10) == 0)
                    point.x += 20000.0f;    // off the track: no instance
                alignas(16) WWorldPos position;
                switch (RandomInt(3)) {
                case 0: memset(&position, 0, sizeof(position)); break;
                case 1:
                    memcpy(&position, &realOwner->worldPos, sizeof(position));
                    position.article = NULL;    // stale
                    break;
                default: memcpy(&position, &realOwner->worldPos, sizeof(position)); break;
                }
                if (RandomInt(4) == 0)
                    position.face.corner[2].tag.type = uint8_t(1 + RandomInt(10));
                Coord4 ground = { Uniform(-1.0f, 1.0f), Uniform(-1.0f, 1.0f), Uniform(-1.0f, 1.0f), Uniform(-9.0f, 9.0f) };
                bool flag = RandomInt(2) != 0;
                memset(&a, 0, sizeof(a));
                memset(&b, 0, sizeof(b));
                Prepare(&a.state, snapshot);
                Prepare(&b.state, snapshot);
                a.position = b.position = position;
                a.ground = b.ground = ground;
                FakeOwnerScope scope(slot);
                bool ok = Both([&](bool original) {
                    OwnerSide &s = original ? a : b;
                    g_fake = fake;
                    manager->barrierMask = savedMask;
                    bool answer = original ? Orig_TempGetHeightInformation(&s.state.body, 0, flag, &point, &s.ground,
                                                                           &s.position)
                                           : s.state.body.TempGetHeightInformation(flag, &point, &s.ground,
                                                                                   &s.position);
                    s.answer = answer;
                    s.barrierMask = manager->barrierMask;
                    s.fake = g_fake;
                });
                manager->barrierMask = savedMask;
                if (ok) {
                    g_cases++;
                    // the face search's step stamp is the same for both; the instance list stamps are fresh on each
                    CompareStates("TempGetHeightInformation", index, &a.state, &b.state);
                    // A new face's first corner's fourth word is WWorldPos::FindClosestFace's uninitialised stack
                    // (its candidate face; FindFaceInCInst does not write it), which differs with the caller's depth:
                    // masked. Nothing reads it.
                    a.position.face.corner[0].count = 0;
                    b.position.face.corner[0].count = 0;
                    CheckBytes("TempGetHeightInformation position", index, &a.position, &b.position,
                               sizeof(a.position));
                    CheckBytes("TempGetHeightInformation ground", index, &a.ground, &b.ground, sizeof(a.ground));
                    CheckBytes("TempGetHeightInformation answer", index, &a.answer, &b.answer, sizeof(a.answer));
                    CheckBytes("TempGetHeightInformation mask", index, &a.barrierMask, &b.barrierMask,
                               sizeof(a.barrierMask));
                    CheckBytes("TempGetHeightInformation calls", index, &a.fake, &b.fake, sizeof(a.fake));
                }
            }

            // InitLevers: the real render object, none, or the stand-in with 0-20 points; car or not
            {
                switch (RandomInt(4)) {
                case 0: g_fakeOwner.renderObject = NULL; break;
                case 1: break;
                default: {
                    g_fakeOwner.renderObject = reinterpret_cast<RSceneObj *>(&g_fakeRender);
                    uint32_t count = uint32_t(RandomInt(21));
                    memcpy(g_fakeBaseDesc + 0x3c, &count, 4);
                    Coord4 *points = reinterpret_cast<Coord4 *>(g_fakeBaseDesc + 0x60);
                    for (int i = 0; i < 24; i++)
                        points[i] = { Uniform(-3.0f, 3.0f), Uniform(-2.0f, 2.0f), Uniform(-5.0f, 5.0f),
                                      Uniform(-1.0f, 1.0f) };
                    break;
                }
                }
                if (RandomInt(2))
                    g_fakeOwner.type = RandomInt(2) ? 1 : RandomInt(12);
                Coord4 halfExtents = snapshot.info.halfExtents;
                if (RandomInt(2))
                    halfExtents = { Uniform(0.2f, 4.0f), Uniform(0.2f, 2.0f), Uniform(0.5f, 6.0f), Uniform(-1.0f, 1.0f) };
                memset(&a, 0, sizeof(a));
                memset(&b, 0, sizeof(b));
                Prepare(&a.state, snapshot);
                Prepare(&b.state, snapshot);
                FakeOwnerScope scope(slot);
                bool ok = Both([&](bool original) {
                    OwnerSide &s = original ? a : b;
                    g_fake = fake;
                    if (original)
                        Orig_InitLevers(&s.state.body, 0, &g_fakeOwner, &halfExtents);
                    else
                        s.state.body.InitLevers(&g_fakeOwner, &halfExtents);
                    s.fake = g_fake;
                });
                if (ok) {
                    g_cases++;
                    CompareStates("InitLevers", index, &a.state, &b.state);
                    CheckBytes("InitLevers calls", index, &a.fake, &b.fake, sizeof(a.fake));
                }
            }
        }
    }
    manager->barrierMask = savedMask;
}

// ---------------------------------------------------------------------------------------------------------------
// The free functions, ResetRigidBodySP, Newton::ApplyDamage

void TestHelpers() {
    for (int i = 0; i < 4000; i++) {
        float value = RandomInt(8) == 0 ? Special() : Uniform(-100.0f, 100.0f);
        float limit = RandomInt(8) == 0 ? Special() : Uniform(0.0f, 60.0f);
        float scale = RandomInt(8) == 0 ? Special() : Uniform(0.0f, 4.0f);
        if (RandomInt(10) == 0)
            scale = limit / (value != 0.0f ? fabsf(value) : 1.0f);  // a ratio of about 1
        double a = 0.0, b = 0.0;
        if (!Both([&](bool original) {
                (original ? a : b) = original ? Orig_ClampedForceRatio(value, limit, scale)
                                              : ClampedForceRatio(value, limit, scale);
            }))
            continue;
        g_cases++;
        CheckBytes("ClampedForceRatio", i, &a, &b, sizeof(a));
    }

    static const uint32_t kCounts[] = { 1, 2, 4, 7, 16, 64, 255 };
    for (int i = 0; i < int(sizeof(kCounts) / sizeof(kCounts[0])); i++) {
        uint32_t count = kCounts[i];
        void *reused[2] = {};
        void *first[2] = {};
        Both([&](bool original) {
            void **block = static_cast<void **>(UMemory::FastAlloc(count * sizeof(void *), "RigidBasicsShadow"));
            first[original ? 0 : 1] = block;
            if (original)
                Orig_PointerVectorDeallocate(block, count);
            else
                PointerVectorDeallocate(block, count);
            void *again = UMemory::FastAlloc(count * sizeof(void *), "RigidBasicsShadow");
            reused[original ? 0 : 1] = reinterpret_cast<void *>(uintptr_t(again == block));
            UMemory::FastFree(again, count * sizeof(void *));
            if (original)
                Orig_PointerVectorDeallocate(NULL, count);
            else
                PointerVectorDeallocate(NULL, count);
        });
        g_cases++;
        CheckBytes("PointerVectorDeallocate", i, &reused[0], &reused[1], sizeof(reused[0]));
    }

    RigidScratchPadFields *scratch = RigidScratchPad;
    if (scratch != NULL) {
        uint8_t *byte398 = reinterpret_cast<uint8_t *>(&scratch->bodyBoxValid);    // written and read raw
        int32_t saved390 = scratch->unknown390;
        uint8_t saved398 = *byte398, saved399 = scratch->unknown399;
        for (int i = 0; i < 8; i++) {
            uint8_t after[2][6];
            int32_t v390 = int32_t(Random());
            uint8_t v398 = uint8_t(Random()), v399 = uint8_t(Random());
            if (!Both([&](bool original) {
                    scratch->unknown390 = v390;
                    *byte398 = v398;
                    scratch->unknown399 = v399;
                    if (original)
                        Orig_ResetRigidBodySP();
                    else
                        RigidBody::ResetRigidBodySP();
                    uint8_t *out = after[original ? 0 : 1];
                    memcpy(out, &scratch->unknown390, 4);
                    out[4] = *byte398;
                    out[5] = scratch->unknown399;
                }))
                continue;
            g_cases++;
            CheckBytes("ResetRigidBodySP", i, after[0], after[1], sizeof(after[0]));
        }
        scratch->unknown390 = saved390;
        *byte398 = saved398;
        scratch->unknown399 = saved399;
    }

    for (int i = 0; i < 16; i++) {
        Coord3 x = RandomVector(10.0f), y = RandomVector(10.0f);
        float amount = Uniform(-50.0f, 50.0f), unknown4 = Uniform(0.0f, 1.0f);
        int kind = RandomInt(4);
        int answers[2] = {};
        alignas(16) uint8_t object[sizeof(Newton)];
        memset(object, 0, sizeof(object));
        Newton *newton = reinterpret_cast<Newton *>(object);
        if (!Both([&](bool original) {
                answers[original ? 0 : 1] =
                    original ? Orig_NewtonApplyDamage(newton, 0, &x, &y, amount, unknown4, kind, ShadowDamageSourceSig)
                             : newton->ApplyDamage(&x, &y, amount, unknown4, kind, ShadowDamageSourceSig);
            }))
            continue;
        g_cases++;
        CheckBytes("Newton::ApplyDamage", i, &answers[0], &answers[1], sizeof(answers[0]));
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Newton::Simulate on a stand-in

struct NewtonSide {
    alignas(16) uint8_t newton[sizeof(Newton)];
    alignas(16) SimpleRigidBody body;
};

void TestNewtonSimulate() {
    int slot = -1;
    for (int i = kSimpleBodies - 1; i >= 0; i--)
        if (ShadowSimpleOwners[i] == NULL) {
            slot = i;
            break;
        }
    PhysicsObject *player = Sim_GetPlayerObject(ShadowSim, 0);
    if (slot < 0 || player == NULL || (player->flags & PhysicsObject::kSimpleBody)) {
        printf("[rigidbasics]   no free simple body slot or no player body: Newton::Simulate is skipped\n");
        return;
    }
    RigidBody *playerBody = Sim_GetRigidBody(ShadowSim, 0, player->rigidBodySlot);
    if (playerBody->info == NULL)
        return;
    SimpleRigidBody *live = Sim_GetSimpleBody(ShadowSim, 0, slot);
    alignas(16) SimpleRigidBody saved;
    memcpy(&saved, live, sizeof(saved));
    float groundY = playerBody->position.y - playerBody->info->ground.w;

    for (int i = 0; i < 1500; i++) {
        alignas(16) uint8_t object[sizeof(Newton)];
        memset(object, 0, sizeof(object));
        Newton *newton = reinterpret_cast<Newton *>(object);
        newton->vtable = reinterpret_cast<void **>(0x0018ef7c);
        if (RandomInt(4) != 0)
            memcpy(&newton->worldPos, &player->worldPos, sizeof(newton->worldPos));
        newton->type = 9;
        newton->flags = PhysicsObject::kSimpleBody;
        newton->rigidBodySlot = int16_t(slot);
        newton->stepsLeft = 2 + RandomInt(300);
        Coord3 start = { playerBody->position.x + Uniform(-8.0f, 8.0f), groundY + Uniform(0.05f, 3.0f),
                         playerBody->position.z + Uniform(-8.0f, 8.0f) };
        newton->lastPosition = { start.x, start.y, start.z, 1.0f };

        alignas(16) SimpleRigidBody body;
        memset(&body, 0, sizeof(body));
        body.orientation = RandomQuaternion(RandomInt(5) != 0);
        body.position = { start.x + Uniform(-1.0f, 1.0f), groundY + Uniform(-1.5f, 2.0f), start.z + Uniform(-1.0f, 1.0f) };
        if (RandomInt(8) == 0)
            body.position = start;
        body.bodyType = kSimpleNewton;
        body.slot = int8_t(slot);
        body.flags = uint16_t(RandomInt(5) != 0 ? SimpleRigidBody::kMoves : 0);
        body.velocity = RandomVector(15.0f);
        body.radius = 0.25f;
        body.acceleration = RandomVector(RandomInt(2) ? 1.0f : 20.0f);
        static const float kMasses[] = { 3.14159f, 1.0f, 25.0f, 0.5f };
        body.mass = RandomInt(3) == 0 ? kMasses[RandomInt(4)] : Uniform(0.1f, 200.0f);

        static NewtonSide a, b;
        memset(&a, 0, sizeof(a));
        memset(&b, 0, sizeof(b));
        bool ok = Both([&](bool original) {
            NewtonSide &s = original ? a : b;
            memcpy(s.newton, object, sizeof(object));
            memcpy(live, &body, sizeof(body));
            Newton *n = reinterpret_cast<Newton *>(s.newton);
            if (original)
                Orig_NewtonSimulate(n, 0);
            else
                n->Simulate();
            memcpy(&s.body, live, sizeof(s.body));
        });
        if (!ok)
            continue;
        g_cases++;
        // the same uninitialised word of a new face as in the TempGetHeightInformation test: masked
        reinterpret_cast<Newton *>(a.newton)->worldPos.face.corner[0].count = 0;
        reinterpret_cast<Newton *>(b.newton)->worldPos.face.corner[0].count = 0;
        CheckBytes("Newton::Simulate newton", i, a.newton, b.newton, sizeof(a.newton));
        CheckBytes("Newton::Simulate body", i, &a.body, &b.body, sizeof(a.body));
    }
    memcpy(live, &saved, sizeof(saved));
}

}  // namespace

void RigidBasicsShadow_Run(void) {
    char value[16] = "";
    DWORD length = GetEnvironmentVariableA("NIGHTFIRE_RIGIDBASICSSHADOW", value, sizeof(value));
    if (length == 0 || length >= sizeof(value) || atoi(value) == 0)
        return;
    if (fgCollisionMgr == NULL) {
        printf("[rigidbasics] no track loaded - nothing tested\n");
        fflush(stdout);
        return;
    }
    FpControlGet(&g_x87, &g_sse);
    InitFakes();

    std::vector<RigidBody *> bodies = LiveBodies();
    TestMethods(bodies);
    TestOwnerMethods(bodies);
    TestHelpers();
    TestNewtonSimulate();
    ResetFpu();

    printf("[rigidbasics] RigidBody basics, Newton vs originals (%u bodies): %d cases, %d checks, %d differ%s\n",
           unsigned(bodies.size()), g_cases, g_checks, g_differ, g_faults != 0 ? " (with faults)" : "");
    fflush(stdout);
}
