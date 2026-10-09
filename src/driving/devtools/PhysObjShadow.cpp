#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "PhysObjShadow.h"
#include "FpControl.h"

#include "../../common/xbeOverload.h"
#include "../data/AttributeSystem.h"
#include "../engine/UMemory.hpp"
#include "../physics/PhysicsNamespace.h"
#include "../physics/PhysicsObject.h"
#include "../physics/RigidBody.h"
#include "../physics/SimpleRigidBody.h"
#include "../platform/RealMath.h"
#include "../render/RSceneObj.hpp"
#include "../world/Collider.h"
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
// NIGHTFIRE_PHYSOBJSHADOW=1, from the first simulation tick: the ports of PhysicsObject, SimpleRigidBody and
// PhysicsNamespace (physics/) against the originals (their entry bytes swapped back in for each original call,
// common/xbeOriginal.h), on identical inputs, compared.
//
//   - SimpleRigidBody's methods on random bodies (every flag combination, random steps written into the
//     Simulation's step): the body's bytes, the outputs and answers.
//   - CheckCollisions on bodies placed round the live rigid bodies and round simple bodies of our own, written into
//     the Simulation's unused simple slots with owners of our own (copies of a live object, each type, kFlag01 or
//     not); every flag combination, radii either side of 0.25, now and then a rigid body made kFrozen: the hit bits.
//   - PhysicsObject's queries on every live object, and on copies switched to our simple bodies: mass, radius,
//     position, velocity, signature, hit points, damage zones, collision geometry and bounds, IsOwnedBy over every
//     pair and over owner chains of our own (one ending on itself).
//   - Its writes on copies: hit points (negative, zero, NaN and infinite amounts), the links, the owner,
//     ApplyForces (a live rigid body, snapshotted and put back; our simple bodies), and with recording fakes in
//     front of the render, animation, feedback and slot calls (fake render and audio objects with vtables of our
//     own): SetRenderObject, Simulate, PlayAnimation, GetCollisionGeometry, the three constructors, the destructor,
//     its thunk and the deleting destructor. The calls each side made are compared as well as the bytes.
//   - PhysicsNamespace: NameLookup on the "smackable" names (the first side to look a name up may make its
//     PhysicsData; both then answer the same), the PhysicsData initialiser, the constructor (its type already
//     registered), the deleting destructor.
// Everything written is put back: the Simulation's step, slots and owners, the frozen bodies, the live body.
//
// One mutation this catches: UpdatePosition damping the velocity by 0.98 rather than 0.99 changes the bodies with
// kFlag04 and kMoves; CheckCollisions sizing a simple body's box as 2r rather than r turns misses into hits on the
// box cases.
// ---------------------------------------------------------------------------------------------------------------

namespace {

const uint32_t kRanges[5][2] = {
    { 0x0001c180, 0x0001c190 }, { 0x00062110, 0x00062120 }, { 0x0006ed40, 0x0006f810 },
    { 0x00097a90, 0x00097aa0 }, { 0x000b1e10, 0x000b25e0 },
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

typedef SimpleRigidBody *(__fastcall *SrbConstructFn)(SimpleRigidBody *, int, int, int, const Coord3 *,
                                                      const Coord3 *, const Coord3 *, const MATRIX4 *, float, float);
typedef void (__fastcall *SrbVoidFn)(SimpleRigidBody *, int);
typedef void (__fastcall *SrbPtrFn)(SimpleRigidBody *, int, const void *);
typedef PhysicsObject *(__fastcall *SrbOwnerFn)(SimpleRigidBody *, int);
typedef float (__fastcall *SrbFloatFn)(SimpleRigidBody *, int);
typedef bool (__fastcall *SrbBoolFn)(SimpleRigidBody *, int);
typedef void (__fastcall *SrbHitsFn)(SimpleRigidBody *, int, uint64_t *);
typedef void (__fastcall *SrbFlagFn)(SimpleRigidBody *, int, int);

typedef PhysicsObject *(__fastcall *PoConstructSetFn)(PhysicsObject *, int, const AttributeSet *, int);
typedef PhysicsObject *(__fastcall *PoConstructNameFn)(PhysicsObject *, int, const char *, const char *, int);
typedef PhysicsObject *(__fastcall *PoConstructSimpleFn)(PhysicsObject *, int, const char *, const char *, int,
                                                         uint32_t, int, int);
typedef void (__fastcall *PoVoidFn)(PhysicsObject *, int);
typedef PhysicsObject *(__fastcall *PoDeleteFn)(PhysicsObject *, int, unsigned);
typedef RigidBody *(__fastcall *PoRigidFn)(PhysicsObject *, int);
typedef float (__fastcall *PoFloatFn)(PhysicsObject *, int);
typedef void (__fastcall *PoForcesFn)(PhysicsObject *, int, const Coord3 *, const Coord3 *);
typedef Coord3 *(__fastcall *PoVectorFn)(PhysicsObject *, int);
typedef uint32_t *(__fastcall *PoSigFn)(PhysicsObject *, int, uint32_t *);
typedef DamageZone *(__fastcall *PoZonesFn)(PhysicsObject *, int, uint32_t *);
typedef void (__fastcall *PoPtrFn)(PhysicsObject *, int, void *);
typedef void (__fastcall *PoAmountFn)(PhysicsObject *, int, float);
typedef void *(__fastcall *PoGeometryFn)(PhysicsObject *, int, uint32_t *, float *);
typedef bool (__fastcall *PoBoundsFn)(PhysicsObject *, int, Coord4 *);
typedef void (__fastcall *PoUintFn)(PhysicsObject *, int, uint32_t);
typedef bool (__fastcall *PoOwnedFn)(PhysicsObject *, int, PhysicsObject *);
typedef int (__fastcall *PoDamageFn)(PhysicsObject *, int, const void *, const void *, float, float, int,
                                     const uint32_t *);
typedef void (__fastcall *PoImpulseFn)(PhysicsObject *, int, const Coord3 *, const Coord3 *);

typedef void *(__fastcall *NsLookupFn)(PhysicsNamespace *, int, const char *, uint32_t *);
typedef PhysicsNamespace *(__fastcall *NsConstructFn)(PhysicsNamespace *, int);
typedef PhysicsNamespace *(__fastcall *NsDeleteFn)(PhysicsNamespace *, int, unsigned);
typedef void (*InitDataFn)(const char *, const char *, const char *, uint32_t, void *);

typedef SimpleRigidBody *(__fastcall *GetSimpleBodyFn)(void *, int, int);
typedef RigidBody *(__fastcall *GetRigidBodyFn)(void *, int, int);

#define Sim_GetRigidBody ((GetRigidBodyFn)0x000b2700)
#define Sim_GetSimpleBody ((GetSimpleBodyFn)0x000b2730)

// ---- the game's state the tests read, write and restore

#define ShadowSim ((void *)0x00233ff0)
#define ShadowTimeStep FLOAT_AT(0x00234e30)
#define ShadowSimpleOwners ((PhysicsObject **)0x002343a0)
#define ShadowSimpleSigs ((uint32_t *)0x00234120)
const int kRigidBodies = 64;
const int kSimpleBodies = 96;

// ---- results

int g_cases = 0, g_checks = 0, g_differ = 0, g_details = 0, g_faults = 0;
unsigned int g_x87 = 0, g_sse = 0;

void Differ(const char *what, int index, const char *detail) {
    g_differ++;
    if (g_details++ < 10)
        printf("[physobj]   %s #%d: %s\n", what, index, detail);
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

template <class T> void Check(const char *what, int index, const T &a, const T &b) {
    CheckBytes(what, index, &a, &b, sizeof(T));
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

// ---- the calls the code under test makes into code it must not reach for real: recorded

struct Call {
    uint32_t function, a, b, c, d;
};

std::vector<Call> g_calls[2];
int g_side = 0;
int16_t g_assignedSlot = 0;
double g_renderOffset = 0.0;

void Record(uint32_t function, uint32_t a = 0, uint32_t b = 0, uint32_t c = 0, uint32_t d = 0) {
    Call call = { function, a, b, c, d };
    g_calls[g_side].push_back(call);
}

uint32_t Word(const void *p) { return uint32_t(uintptr_t(p)); }

void __fastcall FakeSetPhysics(RSceneObj *object, int, PhysicsObject *physics) {
    Record(0x0008f580, Word(object), Word(physics));
}
void __fastcall FakeHandleStop(Handle *handle, int) { Record(0x00077be0, Word(handle)); }
void __fastcall FakeProcessStimuli(Handle *handle, int, uint32_t stimulus, uint32_t step, int unknown) {
    Record(0x00077e00, Word(handle), stimulus, step, uint32_t(unknown));
}
void __fastcall FakeFeedbackDestruct(IFeedback *feedback, int) { Record(0x0004fb10, Word(feedback)); }
void __fastcall FakeReleaseSlot(void *simulation, int, int slot, int simple) {
    Record(0x000b2980, Word(simulation), uint32_t(slot), uint32_t(simple));
}
int __fastcall FakeAssignSlot(void *simulation, int, PhysicsObject *object, int simple) {
    Record(0x000b2630, Word(simulation), Word(object), uint32_t(simple));
    return g_assignedSlot;
}
void *__fastcall FakeRenderDelete(void *object, int, unsigned flags) {
    Record(0x100, Word(object), flags);
    return object;
}
double __fastcall FakeRenderOffset(void *object, int) {
    Record(0x109, Word(object));
    return g_renderOffset;
}
void __fastcall FakeRenderUpdate(void *object, int, int flag) { Record(0x10e, Word(object), uint32_t(flag)); }
void *__fastcall FakeAudioDelete(void *object, int, unsigned flags) {
    Record(0x200, Word(object), flags);
    return object;
}

void *g_fakeRenderVtable[19];
void *g_fakeAudioVtable[1];

struct Hook {
    uint32_t at;
    uint8_t saved[5];
    bool on;
};
Hook g_hooks[16];
int g_hookCount = 0;

void HookOne(uint32_t at, const void *to) {
    Hook &h = g_hooks[g_hookCount++];
    h.at = at;
    DWORD old;
    h.on = VirtualProtect((void *)(uintptr_t)at, 5, PAGE_EXECUTE_READWRITE, &old) != 0;
    if (!h.on)
        return;
    memcpy(h.saved, (void *)(uintptr_t)at, 5);
    uint8_t jump[5] = { 0xe9 };
    int32_t rel = int32_t(uint32_t(uintptr_t(to)) - (at + 5));
    memcpy(jump + 1, &rel, 4);
    memcpy((void *)(uintptr_t)at, jump, 5);
    VirtualProtect((void *)(uintptr_t)at, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)(uintptr_t)at, 5);
}

// The original's entry, and the port its jump leads to (ported callers call the port directly)
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
    }
    g_hookCount = 0;
}

struct Fakes {
    Fakes() {
        HookInstall(0x0008f580, (const void *)&FakeSetPhysics);
        // ours, which PhysicsObject::SetRenderObject calls directly
        HookInstall(uint32_t(XbeAddress(&RSceneObj::SetPhysics)), (const void *)&FakeSetPhysics);
        HookInstall(0x00077be0, (const void *)&FakeHandleStop);
        HookInstall(0x00077e00, (const void *)&FakeProcessStimuli);
        HookInstall(0x0004fb10, (const void *)&FakeFeedbackDestruct);
        HookInstall(0x000b2980, (const void *)&FakeReleaseSlot);
        HookInstall(0x000b2630, (const void *)&FakeAssignSlot);
    }
    ~Fakes() { HooksRemove(); }
};

// The original (inside the window), then the port, each with its own call record; false if either faulted
template <class F>
bool Both(F &&run) {
    typedef typename std::remove_reference<F>::type Run;
    CaseFn thunk = [](void *context, bool original) { (*static_cast<Run *>(context))(original); };
    g_calls[0].clear();
    g_calls[1].clear();
    bool ok;
    {
        OriginalWindow window;
        g_side = 0;
        ok = Guarded(thunk, &run, true);
    }
    g_side = 1;
    ok = Guarded(thunk, &run, false) && ok;
    g_side = 0;
    return ok;
}

void CheckCalls(const char *what, int index) {
    g_checks++;
    if (g_calls[0].size() != g_calls[1].size()) {
        char detail[64];
        snprintf(detail, sizeof(detail), "original made %u calls, port %u", unsigned(g_calls[0].size()),
                 unsigned(g_calls[1].size()));
        Differ(what, index, detail);
        return;
    }
    for (size_t i = 0; i < g_calls[0].size(); i++) {
        if (memcmp(&g_calls[0][i], &g_calls[1][i], sizeof(Call)) != 0) {
            char detail[96];
            snprintf(detail, sizeof(detail), "call %u: original %x(%x, %x), port %x(%x, %x)", unsigned(i),
                     g_calls[0][i].function, g_calls[0][i].a, g_calls[0][i].b, g_calls[1][i].function,
                     g_calls[1][i].a, g_calls[1][i].b);
            Differ(what, index, detail);
            return;
        }
    }
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

float Odd() {
    switch (RandomInt(8)) {
    case 0: return 0.0f;
    case 1: return -0.0f;
    case 2: return HUGE_VALF;
    case 3: return -HUGE_VALF;
    case 4: return nanf("");
    default: return Uniform(-1e6f, 1e6f);
    }
}

// Mostly ordinary, now and then an edge value
float Amount(float lo, float hi) { return RandomInt(12) == 0 ? Odd() : Uniform(lo, hi); }

Coord3 RandomVector(float range) { return { Uniform(-range, range), Uniform(-range, range), Uniform(-range, range) }; }

Coord4 RandomQuaternion() {
    Coord4 q = { Uniform(-1, 1), Uniform(-1, 1), Uniform(-1, 1), Uniform(-1, 1) };
    float length = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (length < 1e-3f)
        return { 0, 0, 0, 1 };
    return { q.x / length, q.y / length, q.z / length, q.w / length };
}

void RandomMatrix(MATRIX4 *matrix) {
    alignas(16) Coord4 q = RandomQuaternion();
    VU0_quattom4(matrix, &q);
    if (RandomInt(4) == 0)
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 4; c++)
                matrix->mtx[r][c] += Uniform(-0.1f, 0.1f);
}

void RandomBody(SimpleRigidBody *body, float range) {
    body->orientation = RandomQuaternion();
    body->position = RandomVector(range);
    body->bodyType = uint8_t(RandomInt(11));
    body->slot = int8_t(RandomInt(kSimpleBodies));
    body->flags = uint16_t(Random());
    body->velocity = RandomVector(RandomInt(3) == 0 ? 0.5f : 60.0f);
    body->radius = RandomInt(4) == 0 ? Uniform(0.0f, 0.3f) : Uniform(0.0f, 6.0f);
    body->acceleration = RandomVector(20.0f);
    body->mass = Uniform(0.5f, 2000.0f);
    if (RandomInt(20) == 0)
        body->velocity.x = Odd();
}

// ---- the live objects

std::vector<PhysicsObject *> g_objects;     // every owner of a body
PhysicsObject *g_template = NULL;            // a live object with a render object, for copies
std::vector<int> g_freeSlots;                // the simple slots not in use

alignas(16) uint8_t g_fakeOwners[kSimpleBodies][0x200];
alignas(16) SimpleRigidBody g_savedBodies[kSimpleBodies];
PhysicsObject *g_savedOwners[kSimpleBodies];
uint32_t g_savedSigs[kSimpleBodies];

PhysicsObject *FakeOwner(int slot) { return reinterpret_cast<PhysicsObject *>(g_fakeOwners[slot]); }

// Our simple bodies into the free slots, each owned by a copy of the template switched to it
void InstallFakeBodies(const Coord3 &around, float range) {
    for (int slot : g_freeSlots) {
        SimpleRigidBody *body = Sim_GetSimpleBody(ShadowSim, 0, slot);
        RandomBody(body, 0.0f);
        body->position = { around.x + Uniform(-range, range), around.y + Uniform(-range * 0.3f, range * 0.3f),
                           around.z + Uniform(-range, range) };
        body->slot = int8_t(slot);
        body->bodyType = uint8_t(1 + RandomInt(9));
        body->flags = uint16_t(Random()) & ~uint16_t(RandomInt(3) == 0 ? 0 : SimpleRigidBody::kFlag01);
        uint8_t *owner = g_fakeOwners[slot];
        memset(owner, 0, sizeof(g_fakeOwners[slot]));
        memcpy(owner, g_template, sizeof(PhysicsObject));
        PhysicsObject *object = FakeOwner(slot);
        object->flags = PhysicsObject::kSimpleBody;
        object->rigidBodySlot = int16_t(slot);
        if (RandomInt(4) == 0)
            object->renderObject = NULL;
        owner[0x139] = uint8_t(RandomInt(2));
        ShadowSimpleOwners[slot] = object;
        ShadowSimpleSigs[slot] = 0x7e570000u + uint32_t(slot);
    }
}

void SaveSlots() {
    for (int i = 0; i < kSimpleBodies; i++) {
        g_savedBodies[i] = *Sim_GetSimpleBody(ShadowSim, 0, i);
        g_savedOwners[i] = ShadowSimpleOwners[i];
        g_savedSigs[i] = ShadowSimpleSigs[i];
    }
}

void RestoreSlots() {
    for (int i = 0; i < kSimpleBodies; i++) {
        *Sim_GetSimpleBody(ShadowSim, 0, i) = g_savedBodies[i];
        ShadowSimpleOwners[i] = g_savedOwners[i];
        ShadowSimpleSigs[i] = g_savedSigs[i];
    }
}

// ---------------------------------------------------------------------------------------------------------------
// SimpleRigidBody's methods

void TestSimpleBodyMethods() {
    float savedStep = ShadowTimeStep;
    for (int i = 0; i < 6000; i++) {
        alignas(16) SimpleRigidBody start;
        RandomBody(&start, 1000.0f);
        ShadowTimeStep = RandomInt(3) == 0 ? Amount(0.0f, 0.1f) : 1.0f / 30.0f;
        alignas(16) SimpleRigidBody body[2];
        alignas(16) MATRIX4 matrix[2];
        alignas(16) Coord4 vector[2];
        uint32_t answer[2] = {};
        memset(matrix, 0xcd, sizeof(matrix));
        memset(vector, 0xcd, sizeof(vector));
        int op = i % 13;
        alignas(16) MATRIX4 input;
        RandomMatrix(&input);
        alignas(16) Coord4 quaternion = RandomQuaternion();
        Coord3 a = RandomVector(100.0f), b = RandomVector(100.0f), c = RandomVector(100.0f);
        float radius = Amount(0.0f, 5.0f), mass = Amount(0.0f, 500.0f);
        int slot = RandomInt(kSimpleBodies), type = RandomInt(12), flag = RandomInt(2);
        Both([&](bool original) {
            int side = original ? 0 : 1;
            SimpleRigidBody *s = &body[side];
            *s = start;
            switch (op) {
            case 0:
                memset(s, 0xcd, sizeof(*s));
                answer[side] = Word(original ? ((SrbConstructFn)0x000b1e10)(s, 0, slot, type, &a, &b, &c, &input,
                                                                            radius, mass)
                                             : s->Construct(int8_t(slot), uint8_t(type), &a, &b, &c, &input, radius,
                                                            mass)) - Word(s);
                break;
            case 1: original ? ((SrbVoidFn)0x000b1e90)(s, 0) : s->Destruct(); break;
            case 2:
                s->slot = int8_t(slot);
                answer[side] = Word(original ? ((SrbOwnerFn)0x000b1ea0)(s, 0) : s->GetOwner());
                break;
            case 3: original ? ((SrbPtrFn)0x000b1eb0)(s, 0, &matrix[side]) : s->RecalcOrientMat(&matrix[side]); break;
            case 4:
                original ? ((SrbPtrFn)0x000b1ec0)(s, 0, &vector[side])
                         : s->GetForwardVector(reinterpret_cast<Coord3 *>(&vector[side]));
                break;
            case 5:
                original ? ((SrbPtrFn)0x000b1ee0)(s, 0, &vector[side])
                         : s->GetRightVector(reinterpret_cast<Coord3 *>(&vector[side]));
                break;
            case 6: original ? ((SrbPtrFn)0x000b1f00)(s, 0, &input) : s->SetOrientMat(&input); break;
            case 7: original ? ((SrbPtrFn)0x000b1f10)(s, 0, &quaternion) : s->SetOrientation(&quaternion); break;
            case 8: {
                float speed = original ? ((SrbFloatFn)0x000b1f30)(s, 0) : s->GetScalarVelocity();
                memcpy(&answer[side], &speed, 4);
                break;
            }
            case 9: original ? ((SrbPtrFn)0x000b1f40)(s, 0, &a) : s->Accelerate(&a); break;
            case 10: original ? ((SrbVoidFn)0x000b1f80)(s, 0) : s->UpdatePosition(); break;
            case 11: answer[side] = original ? ((SrbBoolFn)0x000b1fe0)(s, 0) : s->NeedsCollisionCheck(); break;
            default: original ? ((SrbFlagFn)0x000b25c0)(s, 0, flag) : s->SetCanHitTrigger(flag != 0); break;
            }
        });
        static const char *const kNames[13] = {
            "Construct", "Destruct", "GetOwner", "RecalcOrientMat", "GetForwardVector", "GetRightVector",
            "SetOrientMat", "SetOrientation", "GetScalarVelocity", "Accelerate", "UpdatePosition",
            "NeedsCollisionCheck", "SetCanHitTrigger",
        };
        Check(kNames[op], i, body[0], body[1]);
        Check(kNames[op], i, matrix[0], matrix[1]);
        Check(kNames[op], i, vector[0], vector[1]);
        Check(kNames[op], i, answer[0], answer[1]);
        g_cases++;
    }
    ShadowTimeStep = savedStep;
}

// ---------------------------------------------------------------------------------------------------------------
// CheckCollisions

void TestCheckCollisions() {
    std::vector<int> rigid;
    for (int i = 0; i < kRigidBodies; i++)
        if (PhysicsObjects[i] != NULL)
            rigid.push_back(i);
    if (rigid.empty() || g_template == NULL)
        return;
    Coord3 centre = Sim_GetRigidBody(ShadowSim, 0, rigid[0])->position;
    for (int i = 0; i < 3000; i++) {
        if (i % 50 == 0)
            InstallFakeBodies(centre, 12.0f);
        // round a live rigid body, or round our simple bodies
        Coord3 target = centre;
        float spread = 12.0f;
        if (RandomInt(2) == 0) {
            target = Sim_GetRigidBody(ShadowSim, 0, rigid[RandomInt(int(rigid.size()))])->position;
            spread = 8.0f;
        }
        alignas(16) SimpleRigidBody body;
        RandomBody(&body, 0.0f);
        body.position = { target.x + Uniform(-spread, spread), target.y + Uniform(-3.0f, 3.0f),
                          target.z + Uniform(-spread, spread) };
        body.flags = uint16_t((Random() & 0xf0) | (Random() & 0x10f));
        // its slot one with an owner (a helicopter's body reads the checking body's owner)
        do
            body.slot = int8_t(RandomInt(kSimpleBodies));
        while (ShadowSimpleOwners[body.slot] == NULL);
        RigidBody *frozen = NULL;
        uint8_t frozenState = 0;
        if (RandomInt(10) == 0) {
            frozen = Sim_GetRigidBody(ShadowSim, 0, rigid[RandomInt(int(rigid.size()))]);
            frozenState = frozen->sleepState;
            frozen->sleepState = RigidBody::kFrozen;
        }
        uint64_t seed[3] = { uint64_t(Random()) << 32 | Random(), 0, uint64_t(Random()) };
        uint64_t hits[2][3];
        alignas(16) SimpleRigidBody after[2];
        Both([&](bool original) {
            int side = original ? 0 : 1;
            alignas(16) SimpleRigidBody s = body;
            memcpy(hits[side], seed, sizeof(seed));
            original ? ((SrbHitsFn)0x000b1ff0)(&s, 0, hits[side]) : s.CheckCollisions(hits[side]);
            after[side] = s;
        });
        if (frozen != NULL)
            frozen->sleepState = frozenState;
        Check("CheckCollisions hits", i, hits[0], hits[1]);
        Check("CheckCollisions body", i, after[0], after[1]);
        g_cases++;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// PhysicsObject's queries

void QueryObject(PhysicsObject *object, int index) {
    struct Answers {
        uint32_t mass, radius, position, velocity, rigid, sig, sigResult, hitPoints, zones, zoneCount;
        uint32_t geometry, geometryCount, offset, bounds;
        Coord4 box;
    } answers[2];
    memset(answers, 0xcd, sizeof(answers));
    Both([&](bool original) {
        Answers &r = answers[original ? 0 : 1];
        float f;
        f = original ? ((PoFloatFn)0x0006f240)(object, 0) : object->GetMass();
        memcpy(&r.mass, &f, 4);
        f = original ? ((PoFloatFn)0x0006f270)(object, 0) : object->GetRadius();
        memcpy(&r.radius, &f, 4);
        r.position = Word(original ? ((PoVectorFn)0x0006f330)(object, 0) : object->GetPosition());
        r.velocity = Word(original ? ((PoVectorFn)0x0006f360)(object, 0) : object->GetLinearVelocity());
        if (!(object->flags & PhysicsObject::kSimpleBody))
            r.rigid = Word(original ? ((PoRigidFn)0x0001c180)(object, 0) : object->GetRigidBody());
        r.sigResult = Word(original ? ((PoSigFn)0x0006f390)(object, 0, &r.sig) : object->GetSig(&r.sig)) - Word(&r);
        f = original ? ((PoFloatFn)0x0006f450)(object, 0) : object->GetHitPoints();
        memcpy(&r.hitPoints, &f, 4);
        r.zones = Word(original ? ((PoZonesFn)0x0006f3e0)(object, 0, &r.zoneCount)
                                : object->GetDamageZones(&r.zoneCount));
        float offset;
        r.geometry = Word(original ? ((PoGeometryFn)0x0006f4d0)(object, 0, &r.geometryCount, &offset)
                                   : object->GetCollisionGeometry(&r.geometryCount, &offset));
        memcpy(&r.offset, &offset, 4);
        r.bounds = original ? ((PoBoundsFn)0x0006f520)(object, 0, &r.box) : object->GetCollisionBounds(&r.box);
        original ? ((PoVoidFn)0x0006f660)(object, 0) : object->DebugObject();
        Coord3 impulse = { 1.0f, 2.0f, 3.0f };
        original ? ((PoImpulseFn)0x00097a90)(object, 0, &impulse, &impulse) : object->ComputeImpulse(&impulse, &impulse);
    });
    Check("PhysicsObject queries", index, answers[0], answers[1]);
    g_cases++;
}

bool OwnedBy(PhysicsObject *object, PhysicsObject *owner, int index) {
    bool owned[2] = {};
    Both([&](bool original) {
        owned[original ? 0 : 1] = original ? ((PoOwnedFn)0x0006f5b0)(object, 0, owner) : object->IsOwnedBy(owner);
    });
    Check("IsOwnedBy", index, owned[0], owned[1]);
    g_cases++;
    return owned[0];
}

uint32_t SigOf(PhysicsObject *object) {
    uint32_t sig;
    return *object->GetSig(&sig);
}

void TestQueries() {
    int index = 0;
    for (PhysicsObject *object : g_objects)
        QueryObject(object, index++);
    // copies switched to our simple bodies
    for (int slot : g_freeSlots) {
        alignas(16) uint8_t copy[0x200];
        memcpy(copy, g_fakeOwners[slot], sizeof(copy));
        QueryObject(reinterpret_cast<PhysicsObject *>(copy), index++);
    }
    // every pair, and the null owner
    for (PhysicsObject *object : g_objects) {
        OwnedBy(object, NULL, index++);
        for (PhysicsObject *owner : g_objects)
            OwnedBy(object, owner, index++);
    }
    // chains of our own: a copy owned by each live object; a live object owning itself
    for (PhysicsObject *owner : g_objects) {
        alignas(16) PhysicsObject copy = *g_objects[RandomInt(int(g_objects.size()))];
        copy.ownerSig = SigOf(owner);
        for (PhysicsObject *other : g_objects)
            OwnedBy(&copy, other, index++);
        copy.ownerSig = Random();
        OwnedBy(&copy, owner, index++);
    }
    for (PhysicsObject *object : g_objects) {
        uint32_t saved = object->ownerSig;
        object->ownerSig = SigOf(object);
        OwnedBy(object, g_objects[RandomInt(int(g_objects.size()))], index++);
        object->ownerSig = saved;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// PhysicsObject's writes

struct FakeRender {
    alignas(16) uint8_t bytes[0x80];
    alignas(16) uint8_t desc[0x80];

    RSceneObj *Object() { return reinterpret_cast<RSceneObj *>(bytes); }
    void Make(PhysicsObject *physics) {
        for (size_t i = 0; i < sizeof(desc); i += 4) {
            float f = Uniform(-50.0f, 50.0f);
            memcpy(desc + i, &f, 4);
        }
        memset(bytes, 0, sizeof(bytes));
        RSceneObj *object = Object();
        object->vtable = reinterpret_cast<RSceneObj_vtbl *>(g_fakeRenderVtable);
        object->baseDesc = desc;
        object->physics = physics;
        object->animHandle = reinterpret_cast<Handle *>(uintptr_t(0x0badf00d));
    }
};

struct FakeAudio {
    void **vtable;
    uint8_t unknown04[0xc];
};

void TestWrites() {
    if (g_template == NULL)
        return;
    Fakes fakes;
    FakeRender render[2];
    FakeAudio audio = { g_fakeAudioVtable };
    // one object and one hit point store for both sides, so the recorded calls name the same addresses
    alignas(16) PhysicsObject work;
    float hitPoints = 0.0f;
    for (int i = 0; i < 6000; i++) {
        alignas(16) PhysicsObject start = *g_objects[RandomInt(int(g_objects.size()))];
        render[0].Make(RandomInt(2) ? &work : NULL);
        render[1].Make(RandomInt(2) ? &work : NULL);
        start.renderObject = RandomInt(5) == 0 ? NULL : render[0].Object();
        g_renderOffset = Amount(-5.0f, 5.0f);
        float startHitPoints = Amount(-10.0f, 100.0f);
        float amount = Amount(-20.0f, 60.0f);
        bool withHitPoints = RandomInt(8) != 0;
        ABaseSound *audioObject = RandomInt(2) ? reinterpret_cast<ABaseSound *>(&audio) : NULL;
        IFeedback *feedback = reinterpret_cast<IFeedback *>(uintptr_t(Random() & 0xfffc));
        int renderChoice = RandomInt(12);
        RSceneObj *newRender = renderChoice < 4 ? NULL : renderChoice < 7 ? start.renderObject : render[1].Object();
        int op = i % 12;
        if ((op == 9 || op == 10) && !g_freeSlots.empty() && RandomInt(2) == 0) {
            start.flags = PhysicsObject::kSimpleBody;
            start.rigidBodySlot = int16_t(g_freeSlots[RandomInt(int(g_freeSlots.size()))]);
        }
        alignas(16) Coord3 force = RandomVector(5000.0f), torque = RandomVector(5000.0f);
        uint32_t stimulus = Random();
        PhysicsObject *owner = RandomInt(4) == 0 ? NULL : g_objects[RandomInt(int(g_objects.size()))];

        // ApplyForces writes the body: each side starts from the same body, and the live one is put back
        RigidBody *body = NULL;
        alignas(16) RigidBody bodyLive, bodyStart;
        SimpleRigidBody *simple = NULL;
        alignas(16) SimpleRigidBody simpleLive;
        if (op == 9 && !(start.flags & PhysicsObject::kSimpleBody)) {
            body = Sim_GetRigidBody(ShadowSim, 0, start.rigidBodySlot);
            bodyLive = *body;
            bodyStart = bodyLive;
            if (RandomInt(4) == 0)
                bodyStart.sleepState = uint8_t(RandomInt(4));
        }
        if (op == 9 && (start.flags & PhysicsObject::kSimpleBody)) {
            simple = Sim_GetSimpleBody(ShadowSim, 0, start.rigidBodySlot);
            simpleLive = *simple;
        }

        alignas(16) PhysicsObject object[2];
        alignas(16) RigidBody bodyAfter[2];
        alignas(16) SimpleRigidBody simpleAfter[2];
        float hitPointsAfter[2] = {};
        uint32_t answer[2] = {};
        memset(bodyAfter, 0, sizeof(bodyAfter));
        memset(simpleAfter, 0, sizeof(simpleAfter));
        Both([&](bool original) {
            int side = original ? 0 : 1;
            PhysicsObject *o = &work;
            *o = start;
            hitPoints = startHitPoints;
            if (body != NULL)
                *body = bodyStart;
            if (simple != NULL)
                *simple = simpleLive;
            switch (op) {
            case 0:
                o->hitPointLoc = withHitPoints ? &hitPoints : NULL;
                original ? ((PoAmountFn)0x0006f470)(o, 0, amount) : o->LoseHitPoints(amount);
                break;
            case 1:
                o->hitPointLoc = withHitPoints ? &hitPoints : NULL;
                answer[side] = uint32_t(original ? ((PoDamageFn)0x0006f780)(o, 0, &force, &torque, amount, 1.0f, 2,
                                                                            &stimulus)
                                                 : o->ApplyDamage(&force, &torque, amount, 1.0f, 2, &stimulus));
                break;
            case 2: original ? ((PoPtrFn)0x0006f7e0)(o, 0, &hitPoints) : o->SetHitPointLoc(&hitPoints); break;
            case 3: original ? ((PoPtrFn)0x0006f430)(o, 0, audioObject) : o->SetAudioObject(audioObject); break;
            case 4: original ? ((PoPtrFn)0x0006f440)(o, 0, feedback) : o->SetFeedbackObject(feedback); break;
            case 5: original ? ((PoPtrFn)0x0006f580)(o, 0, owner) : o->SetOwnerObject(owner); break;
            case 6: original ? ((PoPtrFn)0x0006f3f0)(o, 0, newRender) : o->SetRenderObject(newRender); break;
            case 7: original ? ((PoVoidFn)0x0006f4c0)(o, 0) : o->Simulate(); break;
            case 8: original ? ((PoUintFn)0x0006f560)(o, 0, stimulus) : o->PlayAnimation(stimulus); break;
            case 9:
                original ? ((PoForcesFn)0x0006f2a0)(o, 0, &force, &torque) : o->ApplyForces(&force, &torque);
                break;
            case 10: {
                float offset = 0.0f;
                uint32_t count = 0xcdcdcdcd;
                answer[side] = Word(original ? ((PoGeometryFn)0x0006f4d0)(o, 0, &count, &offset)
                                             : o->GetCollisionGeometry(&count, &offset));
                memcpy(&hitPoints, &offset, 4);
                answer[side] ^= count;
                break;
            }
            default: {
                uint32_t count = 0xcdcdcdcd;
                answer[side] = Word(original ? ((PoZonesFn)0x0006f3e0)(o, 0, &count) : o->GetDamageZones(&count));
                answer[side] ^= count;
                break;
            }
            }
            object[side] = *o;
            hitPointsAfter[side] = hitPoints;
            if (body != NULL)
                bodyAfter[side] = *body;
            if (simple != NULL)
                simpleAfter[side] = *simple;
        });
        if (body != NULL)
            *body = bodyLive;
        if (simple != NULL)
            *simple = simpleLive;
        static const char *const kNames[12] = {
            "LoseHitPoints", "ApplyDamage", "SetHitPointLoc", "SetAudioObject", "SetFeedbackObject", "SetOwnerObject",
            "SetRenderObject", "Simulate", "PlayAnimation", "ApplyForces", "GetCollisionGeometry", "GetDamageZones",
        };
        Check(kNames[op], i, object[0], object[1]);
        Check(kNames[op], i, hitPointsAfter[0], hitPointsAfter[1]);
        Check(kNames[op], i, answer[0], answer[1]);
        Check(kNames[op], i, bodyAfter[0], bodyAfter[1]);
        Check(kNames[op], i, simpleAfter[0], simpleAfter[1]);
        CheckCalls(kNames[op], i);
        g_cases++;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// The constructors and destructors

void TestLifecycle() {
    if (g_template == NULL)
        return;
    Fakes fakes;
    FakeRender render;
    FakeAudio audio = { g_fakeAudioVtable };
    alignas(16) PhysicsObject work;
    for (int i = 0; i < 600; i++) {
        PhysicsObject *source = g_objects[RandomInt(int(g_objects.size()))];
        const AttributeCollection *collection = source->attributes.collection;
        g_assignedSlot = int16_t(RandomInt(64));
        int op = i % 6;
        int type = RandomInt(20);
        uint32_t ownerSig = Random();
        int flag2 = RandomInt(2), flag4 = RandomInt(2);
        alignas(16) PhysicsObject start = *source;
        render.Make(RandomInt(2) ? &work : NULL);
        start.renderObject = RandomInt(4) == 0 ? NULL : render.Object();
        start.audioObject = RandomInt(3) == 0 ? NULL : reinterpret_cast<ABaseSound *>(&audio);
        start.collider = NULL;
        start.feedbackObject = NULL;
        bool withCollider = RandomInt(2) == 0, withFeedback = RandomInt(2) == 0;
        alignas(16) PhysicsObject object[2];
        uint32_t answer[2] = {};
        Both([&](bool original) {
            int side = original ? 0 : 1;
            PhysicsObject *o = &work;
            if (op <= 2) {
                memset(o, 0xcd, sizeof(*o));
            } else {
                *o = start;
                o->attributes.ConstructCopy(source->attributes);
                if (withCollider) {
                    o->collider = static_cast<WCollider *>(UMemory::FastAlloc(sizeof(WCollider), "WCollider"));
                    memset(o->collider, 0, sizeof(WCollider));
                }
                if (withFeedback)
                    o->feedbackObject = static_cast<IFeedback *>(UMemory::FastAlloc(4, "IFeedback"));
            }
            PhysicsObject *result = NULL;
            switch (op) {
            case 0:
                result = original ? ((PoConstructSetFn)0x0006f070)(o, 0, &source->attributes, type)
                                  : o->Construct(source->attributes, type);
                break;
            case 1:
                result = original ? ((PoConstructNameFn)0x0006f100)(o, 0, collection->className, collection->name,
                                                                    type)
                                  : o->Construct(collection->className, collection->name, type);
                break;
            case 2:
                result = original ? ((PoConstructSimpleFn)0x0006f190)(o, 0, collection->className, collection->name,
                                                                      type, ownerSig, flag2, flag4)
                                  : o->Construct(collection->className, collection->name, type, ownerSig, flag2 != 0,
                                                 flag4 != 0);
                break;
            case 3: original ? ((PoVoidFn)0x0006f680)(o, 0) : o->Destruct(); result = o; break;
            case 4: original ? ((PoVoidFn)0x00062110)(o, 0) : o->DestructThunk(); result = o; break;
            default:
                // the deleting destructor frees only a block of its own: here, flag 0
                result = original ? ((PoDeleteFn)0x0006f7f0)(o, 0, 0) : o->Delete(0);
                break;
            }
            answer[side] = Word(result) - Word(o);
            object[side] = *o;
            if (op <= 2)
                o->attributes.Destruct();
        });
        static const char *const kNames[6] = {
            "Construct(set)", "Construct(name)", "Construct(simple)", "Destruct", "DestructThunk", "Delete",
        };
        Check(kNames[op], i, object[0], object[1]);
        Check(kNames[op], i, answer[0], answer[1]);
        // the feedback block is each side's own allocation
        for (int side = 0; side < 2; side++)
            for (Call &call : g_calls[side])
                if (call.function == 0x0004fb10)
                    call.a = 1;
        CheckCalls(kNames[op], i);
        g_cases++;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// PhysicsNamespace

void TestNamespace() {
    std::vector<const char *> names;
    for (PhysicsObject *object : g_objects) {
        const AttributeCollection *collection = object->attributes.collection;
        if (collection != NULL && collection->className != NULL && strcmp(collection->className, "smackable") == 0)
            names.push_back(collection->name);
    }
    const char *name = NULL;
    for (int i = 0; i < 40; i++) {
        name = AttributeSystemInstance->GetClassNextName("smackable", name);
        if (name == NULL)
            break;
        names.push_back(name);
    }
    names.push_back("Bench");
    int index = 0;
    for (const char *lookup : names) {
        PhysicsNamespace space = { reinterpret_cast<void **>(0x0018f8fc) };
        void *data[2] = {};
        uint32_t size[2] = {};
        Both([&](bool original) {
            int side = original ? 0 : 1;
            data[side] = original ? ((NsLookupFn)0x0006ed40)(&space, 0, lookup, &size[side])
                                  : space.NameLookup(lookup, &size[side]);
        });
        Check("NameLookup", index, data[0], data[1]);
        Check("NameLookup size", index, size[0], size[1]);
        g_cases++;

        alignas(16) uint8_t physics[2][sizeof(PhysicsData)];
        Both([&](bool original) {
            int side = original ? 0 : 1;
            memset(physics[side], 0xcd, sizeof(PhysicsData));
            original ? ((InitDataFn)0x0006ee10)("smackable", lookup, "PhysicsData", 1, physics[side])
                     : InitPhysicsData("smackable", lookup, "PhysicsData", 1, physics[side]);
            reinterpret_cast<PhysicsData *>(physics[side])->attributes.Destruct();
        });
        Check("InitPhysicsData", index, physics[0], physics[1]);
        g_cases++;
        index++;
    }
    Both([&](bool original) {
        original ? ((InitDataFn)0x0006ee10)("smackable", "Bench", "PhysicsData", 1, NULL)
                 : InitPhysicsData("smackable", "Bench", "PhysicsData", 1, NULL);
    });
    g_cases++;

    PhysicsNamespace space[2];
    uint32_t answer[2] = {};
    Both([&](bool original) {
        int side = original ? 0 : 1;
        space[side].vtable = NULL;
        answer[side] = Word(original ? ((NsConstructFn)0x0006ee90)(&space[side], 0) : space[side].Construct()) -
                       Word(&space[side]);
    });
    Check("PhysicsNamespace::Construct", 0, space[0], space[1]);
    Check("PhysicsNamespace::Construct", 0, answer[0], answer[1]);
    Both([&](bool original) {
        int side = original ? 0 : 1;
        space[side].vtable = NULL;
        answer[side] = Word(original ? ((NsDeleteFn)0x0006f050)(&space[side], 0, 0) : space[side].Delete(0)) -
                       Word(&space[side]);
    });
    Check("PhysicsNamespace::Delete", 0, space[0], space[1]);
    Check("PhysicsNamespace::Delete", 0, answer[0], answer[1]);
    g_cases += 2;
}

}  // namespace

void PhysObjShadow_Run(void) {
    char value[16] = "";
    DWORD length = GetEnvironmentVariableA("NIGHTFIRE_PHYSOBJSHADOW", value, sizeof(value));
    if (length == 0 || length >= sizeof(value) || atoi(value) == 0)
        return;
    FpControlGet(&g_x87, &g_sse);

    g_fakeRenderVtable[0] = (void *)&FakeRenderDelete;
    g_fakeRenderVtable[9] = (void *)&FakeRenderOffset;
    g_fakeRenderVtable[14] = (void *)&FakeRenderUpdate;
    g_fakeAudioVtable[0] = (void *)&FakeAudioDelete;

    g_objects.clear();
    g_freeSlots.clear();
    g_template = NULL;
    for (int i = 0; i < kRigidBodies; i++)
        if (PhysicsObjects[i] != NULL)
            g_objects.push_back(PhysicsObjects[i]);
    for (int i = 0; i < kSimpleBodies; i++) {
        if (ShadowSimpleOwners[i] != NULL)
            g_objects.push_back(ShadowSimpleOwners[i]);
        else
            g_freeSlots.push_back(i);
    }
    for (PhysicsObject *object : g_objects)
        if (g_template == NULL && object->renderObject != NULL && !(object->flags & PhysicsObject::kSimpleBody))
            g_template = object;
    if (g_objects.empty() || g_template == NULL) {
        printf("[physobj] no physics objects with render objects - nothing tested\n");
        fflush(stdout);
        return;
    }

    SaveSlots();
    InstallFakeBodies(g_template->GetRigidBody()->position, 12.0f);
    TestSimpleBodyMethods();
    TestCheckCollisions();
    TestQueries();
    TestWrites();
    RestoreSlots();
    TestLifecycle();
    TestNamespace();
    ResetFpu();

    printf("[physobj] physics objects, simple rigid bodies, physics namespace vs originals: %d cases, %d checks, "
           "%d differ%s\n",
           g_cases, g_checks, g_differ, g_faults != 0 ? " (with faults)" : "");
    if (g_faults != 0)
        printf("[physobj]   %d calls faulted\n", g_faults);
    fflush(stdout);
}
