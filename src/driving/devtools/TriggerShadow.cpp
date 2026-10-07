#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "TriggerShadow.h"
#include "FpControl.h"

#include "../world/Grid.h"
#include "../world/SimpleZone.h"
#include "../world/Trigger.h"
#include "../world/TriggerManager.h"
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
// NIGHTFIRE_TRIGGERSHADOW=1, from the first simulation tick on the loaded track: the trigger ports (world/Trigger.cpp,
// TriggerManager.cpp) against the originals (their entry bytes swapped back in for each original call,
// common/xbeOriginal.h, so an original calls the other originals and a port the other ports), on identical inputs,
// compared.
//
//   - The five CheckCollide overloads and TestDirection over every real trigger and randomised copies of them
//     (every shape, directional or not, rotated or not, sizes changed): segments through, near and past them (some
//     level, some along z), points and radii round them and on their top and bottom, rigid bodies (sphere, circle
//     and height, box against box with a synthetic RigidBodyInfo) and simple bodies; the answers compared.
//   - The four Process overloads on the live trigger array: synthetic rigid bodies (each kind), simple bodies
//     (each type, the player's or not, when an owner slot is in use), path instances (dimensions packed together or
//     separately) and a ray shell (written into RayShell's table, slot 0) at every trigger, and the live bodies as
//     Update finds them. The array is put back before each run with an empty event list on every trigger (so FireEvents
//     runs no event code) and kOnce set on some (so a firing shows as kEnabled cleared), the manager's active flag
//     and the query stamp set, and compared after both: every trigger's bytes, the stamp, the hitting ray shell.
//   - FireEvents on copies of real triggers given an empty event list: the event data (0x001e47e8) with and without
//     a hitting ray shell, and the trigger's flags.
//   - WSimpleZone: SetPosition (rounding ties too) and IsZoneInRange (every shift up to 12, the edges).
// Everything written is put back at the end: the trigger array, the stamp, the event data, the ray shell slot and
// the hitting ray shell. Not covered: an event list that runs events, Init/Restart and UpdateRotPos (the grid's
// dynamic lists), which the lockstep runs test.
//
// One mutation this catches: CheckCollide(point, radius, above, below) taking `below` above the point and `above`
// below it changes its answer for the points near a trigger's top or bottom (the above and below differ at random);
// TestDirection answering false on a zero dot product shows on the segments along a trigger's right axis.
// ---------------------------------------------------------------------------------------------------------------

namespace {

const uint32_t kRanges[2][2] = {
    { 0x000cc840, 0x000cc8f0 }, { 0x000cf300, 0x000d0d50 },
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

typedef bool (__fastcall *SegmentCollideFn)(WTriggerManager *, int, const Coord4 *, float, WTrigger *);
typedef bool (__fastcall *RigidCollideFn)(WTriggerManager *, int, RigidBody *, WTrigger *);
typedef bool (__fastcall *SimpleCollideFn)(WTriggerManager *, int, SimpleRigidBody *, WTrigger *);
typedef bool (__fastcall *PointCollideFn)(WTriggerManager *, int, const Coord3 *, float, WTrigger *);
typedef bool (__fastcall *SpanCollideFn)(WTriggerManager *, int, const Coord3 *, float, float, float, WTrigger *);
typedef bool (__fastcall *DirectionFn)(WTrigger *, int, const Coord4 *);
typedef void (__fastcall *FireFn)(WTrigger *, int, bool, int, CARP::Instance *);
typedef void (__fastcall *ProcessRigidFn)(WTriggerManager *, int, int, RigidBody *);
typedef void (__fastcall *ProcessInstanceFn)(WTriggerManager *, int, CARP::Instance *);
typedef void (__fastcall *ProcessSimpleFn)(WTriggerManager *, int, int, SimpleRigidBody *);
typedef void (__fastcall *ProcessRayFn)(WTriggerManager *, int, int);
typedef bool (__fastcall *ZoneRangeFn)(WSimpleZone *, int, const WSimpleZone *, int);
typedef void (__fastcall *ZoneSetFn)(WSimpleZone *, int, const Coord3 *);
typedef RigidBody *(__fastcall *GetRigidBodyFn)(void *, int, int);
typedef SimpleRigidBody *(__fastcall *GetSimpleBodyFn)(void *, int, int);

#define Orig_CollideSegment ((SegmentCollideFn)0x000cf520)
#define Orig_CollideRigid ((RigidCollideFn)0x000cfae0)
#define Orig_CollideSimple ((SimpleCollideFn)0x000cfdf0)
#define Orig_CollidePoint ((PointCollideFn)0x000cfeb0)
#define Orig_CollideSpan ((SpanCollideFn)0x000cff20)
#define Orig_TestDirection ((DirectionFn)0x000cf440)
#define Orig_FireEvents ((FireFn)0x000cf300)
#define Orig_ProcessRigid ((ProcessRigidFn)0x000d04a0)
#define Orig_ProcessInstance ((ProcessInstanceFn)0x000d0630)
#define Orig_ProcessSimple ((ProcessSimpleFn)0x000d0810)
#define Orig_ProcessRay ((ProcessRayFn)0x000d0a30)
#define Orig_ZoneInRange ((ZoneRangeFn)0x000cc840)
#define Orig_ZoneSet ((ZoneSetFn)0x000cc880)
#define Sim_GetRigidBody ((GetRigidBodyFn)0x000b2700)
#define Sim_GetSimpleBody ((GetSimpleBodyFn)0x000b2730)

// ---- the game's state the tests write and restore

#define ShadowQueryStamp U32_AT(0x0023e270)
#define ShadowHittingRay I32_AT(0x001c3df4)
#define ShadowRayShells ((ActiveRayShell *)0x001e8570)
#define ShadowRigidOwners ((void **)0x002342a0)
#define ShadowSimpleOwners ((void **)0x002343a0)
#define ShadowSim ((void *)0x00233ff0)
const int kRigidBodies = 0x40;
const int kSimpleBodies = 0x60;

// ---- results

int g_cases = 0, g_checks = 0, g_differ = 0, g_details = 0, g_faults = 0;
unsigned int g_x87 = 0, g_sse = 0;

void Differ(const char *what, int index, const char *detail) {
    g_differ++;
    if (g_details++ < 10)
        printf("[trigger]   %s #%d: %s\n", what, index, detail);
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

void CheckBool(const char *what, int index, bool a, bool b) {
    g_checks++;
    if (a == b)
        return;
    char detail[64];
    snprintf(detail, sizeof(detail), "original %d, port %d", int(a), int(b));
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

uint32_t g_random = 0x7a3c91e5;

uint32_t Random() {
    g_random ^= g_random << 13;
    g_random ^= g_random >> 17;
    g_random ^= g_random << 5;
    return g_random;
}

int RandomInt(int n) { return n <= 0 ? 0 : int(Random() % uint32_t(n)); }
float Uniform(float lo, float hi) { return lo + (hi - lo) * float(Random() >> 8) * (1.0f / 16777216.0f); }

float Larger(float a, float b) { return a > b ? a : b; }

// How far round a trigger the inputs reach
float Reach(const WTrigger &trigger) {
    float reach = Larger(1.0f, Larger(trigger.radius, trigger.height));
    if (trigger.shape == WTrigger::kBox)
        reach = Larger(reach, Larger(trigger.width, trigger.depth));
    if (!(reach < 2000.0f))
        reach = 2000.0f;
    return reach * 1.5f;
}

// A point round the trigger; now and then exactly at its bottom or top
Coord3 Around(const WTrigger &trigger) {
    float reach = Reach(trigger);
    Coord3 p = { trigger.position.x + Uniform(-reach, reach), 0.0f, trigger.position.z + Uniform(-reach, reach) };
    switch (RandomInt(8)) {
    case 0: p.y = trigger.position.y; break;
    case 1: p.y = trigger.position.y + trigger.height; break;
    default: p.y = trigger.position.y + Uniform(-0.5f * reach, trigger.height + 0.5f * reach); break;
    }
    return p;
}

Coord4 Point4(const Coord3 &p, float w) { return { p.x, p.y, p.z, w }; }

// A segment: across the trigger, from round it to round it, level, along one axis, or of no length
void MakeSegment(const WTrigger &trigger, Coord4 *segment) {
    Coord3 a = Around(trigger);
    Coord3 b = Around(trigger);
    switch (RandomInt(6)) {
    case 0:   // through the centre
        b = { 2.0f * trigger.position.x - a.x, 2.0f * trigger.position.y + trigger.height - a.y,
              2.0f * trigger.position.z - a.z };
        break;
    case 1: b.y = a.y; break;                    // level
    case 2: b.x = a.x; break;                    // along z (in the world)
    case 3:                                      // along the trigger's right axis
        b = { a.x + trigger.right.x * 10.0f, a.y + trigger.right.y * 10.0f, a.z + trigger.right.z * 10.0f };
        break;
    case 4: b = a; break;
    default: break;
    }
    segment[0] = Point4(a, 1.0f);
    segment[1] = Point4(b, 1.0f);
}

void RandomAxes(WTrigger *trigger) {
    float angle = Uniform(0.0f, 6.2831853f);
    float c = cosf(angle), s = sinf(angle);
    trigger->right = { c, 0.0f, -s };
    trigger->forward = { s, 0.0f, c };
    if (RandomInt(3) == 0) {   // tilted
        float tilt = Uniform(-0.5f, 0.5f);
        trigger->forward.y = tilt;
        trigger->flags |= WTrigger::kRotated;
    }
}

// The list every trigger here is given: empty, so firing a trigger runs no event (FireEvents reads the count of
// any trigger's list; there is no NULL list in the game)
alignas(16) TriggerEvents g_noEvents;

// A randomised copy of a trigger: shape, flags, sizes, axes
WTrigger Perturbed(const WTrigger &real) {
    WTrigger trigger = real;
    trigger.events = &g_noEvents;
    trigger.shape = uint8_t(1 + RandomInt(3));
    trigger.flags ^= uint16_t(RandomInt(2) ? WTrigger::kDirectional : 0);
    if (RandomInt(2))
        trigger.flags &= ~WTrigger::kRotated;
    float scale = Uniform(0.25f, 3.0f);
    trigger.radius = Larger(0.5f, Uniform(0.0f, 20.0f) * scale);
    trigger.height = Uniform(0.0f, 20.0f) * scale;
    trigger.width = Uniform(0.0f, 40.0f) * scale;
    trigger.depth = Uniform(0.0f, 40.0f) * scale;
    if (RandomInt(2))
        RandomAxes(&trigger);
    return trigger;
}

// A synthetic rigid body's info: a rotation about y and half extents
alignas(16) RigidBodyInfo g_info;

void RandomInfo() {
    memset(&g_info, 0, sizeof(g_info));
    float angle = Uniform(0.0f, 6.2831853f);
    float c = cosf(angle), s = sinf(angle);
    float (*m)[4] = g_info.orientation.mtx;
    m[0][0] = c;    m[0][2] = -s;
    m[1][1] = 1.0f;
    m[2][0] = s;    m[2][2] = c;
    m[3][3] = 1.0f;
    g_info.halfExtents = { Uniform(0.5f, 4.0f), Uniform(0.3f, 2.0f), Uniform(1.0f, 6.0f), 0.0f };
}

void MakeRigidBody(const WTrigger &trigger, RigidBody *body) {
    memset(body, 0, sizeof(*body));
    body->position = Around(trigger);
    body->velocity = { Uniform(-30.0f, 30.0f), Uniform(-5.0f, 5.0f), Uniform(-30.0f, 30.0f) };
    body->info = &g_info;
    body->kind = int8_t(RandomInt(4));
    body->sleepState = 2;
    body->radius = Uniform(0.0f, 6.0f);
}

void MakeSimpleBody(const WTrigger &trigger, SimpleRigidBody *body, int slot) {
    memset(body, 0, sizeof(*body));
    body->position = Around(trigger);
    body->bodyType = uint8_t(RandomInt(10));
    body->slot = int8_t(slot);
    body->flags = uint16_t(RandomInt(5) != 0 ? SimpleRigidBody::kTouchesTriggers : 0);
    body->velocity = { Uniform(-30.0f, 30.0f), Uniform(-5.0f, 5.0f), Uniform(-30.0f, 30.0f) };
    body->radius = Uniform(0.0f, 3.0f);
}

// ---------------------------------------------------------------------------------------------------------------
// The collision tests

void TestCollide(const std::vector<WTrigger> &real) {
    WTriggerManager *manager = fgTriggerManager;
    int index = 0;
    for (size_t t = 0; t < real.size() * 4; t++) {
        alignas(16) WTrigger trigger = t < real.size() ? real[t] : Perturbed(real[t % real.size()]);
        trigger.events = &g_noEvents;
        for (int i = 0; i < 40; i++, index++) {
            alignas(16) Coord4 segment[2];
            MakeSegment(trigger, segment);
            float radius = RandomInt(4) == 0 ? Uniform(0.0f, 0.02f) : Uniform(0.0f, 3.0f);
            Coord3 point = Around(trigger);
            float pointRadius = Uniform(0.0f, Reach(trigger));
            float above = Uniform(0.0f, 10.0f), below = Uniform(0.0f, 10.0f);
            RandomInfo();
            alignas(16) RigidBody rigid;
            MakeRigidBody(trigger, &rigid);
            alignas(16) SimpleRigidBody simple;
            MakeSimpleBody(trigger, &simple, 0);

            bool result[2][6] = {};
            Both([&](bool original) {
                bool *r = result[original ? 0 : 1];
                if (original) {
                    r[0] = Orig_CollideSegment(manager, 0, segment, radius, &trigger);
                    r[1] = Orig_CollideRigid(manager, 0, &rigid, &trigger);
                    r[2] = Orig_CollideSimple(manager, 0, &simple, &trigger);
                    r[3] = Orig_CollidePoint(manager, 0, &point, pointRadius, &trigger);
                    r[4] = Orig_CollideSpan(manager, 0, &point, pointRadius, above, below, &trigger);
                    r[5] = Orig_TestDirection(&trigger, 0, segment);
                } else {
                    r[0] = manager->CheckCollide(segment, radius, &trigger);
                    r[1] = manager->CheckCollide(&rigid, &trigger);
                    r[2] = manager->CheckCollide(&simple, &trigger);
                    r[3] = manager->CheckCollide(&point, pointRadius, &trigger);
                    r[4] = manager->CheckCollide(&point, pointRadius, above, below, &trigger);
                    r[5] = trigger.TestDirection(segment);
                }
            });
            CheckBool("CheckCollide(segment)", index, result[0][0], result[1][0]);
            CheckBool("CheckCollide(RigidBody)", index, result[0][1], result[1][1]);
            CheckBool("CheckCollide(SimpleRigidBody)", index, result[0][2], result[1][2]);
            CheckBool("CheckCollide(point)", index, result[0][3], result[1][3]);
            CheckBool("CheckCollide(point, above, below)", index, result[0][4], result[1][4]);
            CheckBool("TestDirection", index, result[0][5], result[1][5]);
            g_cases += 6;
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Process, on the live array

struct ProcessState {
    std::vector<uint8_t> triggers;
    uint32_t stamp;
    int32_t hittingRay;
};

std::vector<uint8_t> g_work;     // the array each run starts from
uint32_t g_stamp0;

void StartRun(WTriggerManager *manager) {
    memcpy(manager->triggers, g_work.data(), g_work.size());
    ShadowQueryStamp = g_stamp0;
    ShadowHittingRay = -1;
}

void EndRun(WTriggerManager *manager, ProcessState *state) {
    state->triggers.assign(reinterpret_cast<uint8_t *>(manager->triggers),
                           reinterpret_cast<uint8_t *>(manager->triggers) + g_work.size());
    state->stamp = ShadowQueryStamp;
    state->hittingRay = ShadowHittingRay;
}

void CompareRuns(const char *what, int index, const ProcessState *state) {
    if (state[0].triggers.size() == state[1].triggers.size())
        CheckBytes(what, index, state[0].triggers.data(), state[1].triggers.data(), state[0].triggers.size());
    CheckBytes(what, index, &state[0].stamp, &state[1].stamp, sizeof(uint32_t));
    CheckBytes(what, index, &state[0].hittingRay, &state[1].hittingRay, sizeof(int32_t));
    g_cases++;
}

// The working array: the live one with empty event lists, every trigger enabled, and kOnce on about half
void PrepareWork(WTriggerManager *manager) {
    WTrigger *work = reinterpret_cast<WTrigger *>(g_work.data());
    memcpy(work, manager->triggers, g_work.size());
    for (int i = 0; i < manager->count; i++) {
        work[i].events = &g_noEvents;
        work[i].flags |= WTrigger::kEnabled;
        if (RandomInt(2))
            work[i].flags |= WTrigger::kOnce;
        else
            work[i].flags &= ~WTrigger::kOnce;
    }
    g_stamp0 = Random();
}

void TestProcess() {
    WTriggerManager *manager = fgTriggerManager;
    int count = manager->count;
    int simpleOwner = -1;
    for (int i = 0; i < kSimpleBodies && simpleOwner < 0; i++)
        if (ShadowSimpleOwners[i] != NULL)
            simpleOwner = i;

    ProcessState state[2];
    int index = 0;
    for (int round = 0; round < 3; round++) {
        PrepareWork(manager);
        manager->active = round != 1;
        const WTrigger *work = reinterpret_cast<const WTrigger *>(g_work.data());
        for (int t = 0; t < count; t++, index++) {
            const WTrigger &trigger = work[t];

            RandomInfo();
            alignas(16) RigidBody rigid;
            MakeRigidBody(trigger, &rigid);
            int bodyIndex = RandomInt(kRigidBodies);
            Both([&](bool original) {
                StartRun(manager);
                if (original)
                    Orig_ProcessRigid(manager, 0, bodyIndex, &rigid);
                else
                    manager->Process(bodyIndex, &rigid);
                EndRun(manager, &state[original ? 0 : 1]);
            });
            CompareRuns("Process(RigidBody)", index, state);

            if (simpleOwner >= 0) {
                alignas(16) SimpleRigidBody simple;
                MakeSimpleBody(trigger, &simple, simpleOwner);
                Both([&](bool original) {
                    StartRun(manager);
                    if (original)
                        Orig_ProcessSimple(manager, 0, bodyIndex, &simple);
                    else
                        manager->Process(bodyIndex, &simple);
                    EndRun(manager, &state[original ? 0 : 1]);
                });
                CompareRuns("Process(SimpleRigidBody)", index, state);
            }

            alignas(16) CARP::Instance instance;
            memset(&instance, 0, sizeof(instance));
            Coord3 at = Around(trigger);
            instance.position[0] = at.x;
            instance.position[1] = at.y;
            instance.position[2] = at.z;
            instance.packedDimensions = Random();
            if (RandomInt(2))
                instance.packedDimensions &= ~0x40000000u;   // mostly the small unit
            Both([&](bool original) {
                StartRun(manager);
                if (original)
                    Orig_ProcessInstance(manager, 0, &instance);
                else
                    manager->Process(&instance);
                EndRun(manager, &state[original ? 0 : 1]);
            });
            CompareRuns("Process(CARP::Instance)", index, state);

            alignas(16) Coord4 segment[2];
            MakeSegment(trigger, segment);
            ActiveRayShell *ray = &ShadowRayShells[0];
            memset(ray, 0, sizeof(*ray));
            ray->start = { segment[0].x, segment[0].y, segment[0].z };
            ray->end = { segment[1].x, segment[1].y, segment[1].z };
            ray->unknown1c = Uniform(0.0f, 100.0f);
            ray->radius = RandomInt(4) == 0 ? 0.0f : Uniform(0.0f, 2.0f);
            Both([&](bool original) {
                StartRun(manager);
                if (original)
                    Orig_ProcessRay(manager, 0, 0);
                else
                    manager->Process(0);
                EndRun(manager, &state[original ? 0 : 1]);
            });
            CompareRuns("Process(ray shell)", index, state);
        }
    }

    // the live bodies, as Update finds them
    PrepareWork(manager);
    manager->active = true;
    for (int i = 0; i < kRigidBodies; i++, index++) {
        if (ShadowRigidOwners[i] == NULL)
            continue;
        RigidBody *body = Sim_GetRigidBody(ShadowSim, 0, i);
        Both([&](bool original) {
            StartRun(manager);
            if (original)
                Orig_ProcessRigid(manager, 0, i, body);
            else
                manager->Process(i, body);
            EndRun(manager, &state[original ? 0 : 1]);
        });
        CompareRuns("Process(live RigidBody)", i, state);
    }
    for (int i = 0; i < kSimpleBodies; i++, index++) {
        if (ShadowSimpleOwners[i] == NULL)
            continue;
        SimpleRigidBody *body = Sim_GetSimpleBody(ShadowSim, 0, i);
        Both([&](bool original) {
            StartRun(manager);
            if (original)
                Orig_ProcessSimple(manager, 0, i, body);
            else
                manager->Process(i, body);
            EndRun(manager, &state[original ? 0 : 1]);
        });
        CompareRuns("Process(live SimpleRigidBody)", i, state);
    }
}

// ---------------------------------------------------------------------------------------------------------------
// FireEvents with an empty event list

void TestFireEvents(const std::vector<WTrigger> &real) {
    alignas(16) TriggerEvents empty;
    memset(&empty, 0, sizeof(empty));
    for (int i = 0; i < 400; i++) {
        alignas(16) WTrigger start = real[RandomInt(int(real.size()))];
        start.events = &empty;
        start.flags = uint16_t(Random());
        bool flag = RandomInt(2) != 0;
        int index = RandomInt(3) == 0 ? -1 : RandomInt(100);
        CARP::Instance *instance = RandomInt(2) ? reinterpret_cast<CARP::Instance *>(&empty) : NULL;
        bool withRay = RandomInt(2) != 0;
        ActiveRayShell ray;
        memset(&ray, 0, sizeof(ray));
        ray.start = { Uniform(-100.0f, 100.0f), Uniform(-100.0f, 100.0f), Uniform(-100.0f, 100.0f) };
        ray.end = { Uniform(-100.0f, 100.0f), Uniform(-100.0f, 100.0f), Uniform(-100.0f, 100.0f) };
        ray.unknown1c = Uniform(0.0f, 100.0f);

        alignas(16) WTrigger trigger;
        uint8_t data[2][sizeof(EventDynamicData)];
        uint8_t after[2][sizeof(WTrigger)];
        Both([&](bool original) {
            memset(&gEventDynamicData, 0xcd, sizeof(EventDynamicData));
            ShadowRayShells[0] = ray;
            ShadowHittingRay = withRay ? 0 : -1;
            trigger = start;
            if (original)
                Orig_FireEvents(&trigger, 0, flag, index, instance);
            else
                trigger.FireEvents(flag, index, instance);
            memcpy(data[original ? 0 : 1], &gEventDynamicData, sizeof(EventDynamicData));
            memcpy(after[original ? 0 : 1], &trigger, sizeof(WTrigger));
        });
        CheckBytes("FireEvents event data", i, data[0], data[1], sizeof(EventDynamicData));
        CheckBytes("FireEvents trigger", i, after[0], after[1], sizeof(WTrigger));
        g_cases++;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// WSimpleZone

void TestSimpleZone() {
    for (int i = 0; i < 4000; i++) {
        Coord3 position;
        if (RandomInt(3) == 0)   // rounding ties: multiples of 2.5
            position = { float(RandomInt(200) - 100) * 2.5f, float(RandomInt(200) - 100) * 2.5f,
                         float(RandomInt(200) - 100) * 2.5f };
        else
            position = { Uniform(-5000.0f, 5000.0f), Uniform(-500.0f, 500.0f), Uniform(-5000.0f, 5000.0f) };
        WSimpleZone zone[2];
        memset(zone, 0xcd, sizeof(zone));
        Both([&](bool original) {
            if (original)
                Orig_ZoneSet(&zone[0], 0, &position);
            else
                zone[1].SetPosition(&position);
        });
        CheckBytes("WSimpleZone::SetPosition", i, &zone[0], &zone[1], sizeof(WSimpleZone));

        int shift = RandomInt(13);
        int span = 3 << shift;
        WSimpleZone a = { RandomInt(2000) - 1000, RandomInt(200) - 100, RandomInt(2000) - 1000 };
        WSimpleZone b = { a.x + RandomInt(2 * span + 1) - span, a.y + RandomInt(2 * span + 1) - span,
                          a.z + RandomInt(2 * span + 1) - span };
        if (RandomInt(4) == 0)   // on an edge
            b.x = a.x + (RandomInt(2) ? (1 << shift) : -(1 << shift));
        bool inRange[2] = {};
        Both([&](bool original) {
            if (original)
                inRange[0] = Orig_ZoneInRange(&a, 0, &b, shift);
            else
                inRange[1] = a.IsZoneInRange(&b, shift);
        });
        CheckBool("WSimpleZone::IsZoneInRange", i, inRange[0], inRange[1]);
        g_cases += 2;
    }
}

}  // namespace

void TriggerShadow_Run(void) {
    char value[16] = "";
    DWORD length = GetEnvironmentVariableA("NIGHTFIRE_TRIGGERSHADOW", value, sizeof(value));
    if (length == 0 || length >= sizeof(value) || atoi(value) == 0)
        return;
    WTriggerManager *manager = fgTriggerManager;
    if (manager == NULL || manager->count <= 0 || TheGrid == NULL) {
        printf("[trigger] the track's triggers are not loaded - nothing tested\n");
        fflush(stdout);
        return;
    }
    FpControlGet(&g_x87, &g_sse);

    // what the tests write, put back at the end
    size_t bytes = size_t(manager->count) * sizeof(WTrigger);
    std::vector<uint8_t> savedTriggers(reinterpret_cast<uint8_t *>(manager->triggers),
                                       reinterpret_cast<uint8_t *>(manager->triggers) + bytes);
    bool savedActive = manager->active;
    uint32_t savedStamp = ShadowQueryStamp;
    int32_t savedHitting = ShadowHittingRay;
    ActiveRayShell savedRay = ShadowRayShells[0];
    EventDynamicData savedData = gEventDynamicData;

    std::vector<WTrigger> real(manager->triggers, manager->triggers + manager->count);
    g_work.resize(bytes);

    TestCollide(real);
    TestProcess();
    TestFireEvents(real);
    TestSimpleZone();

    memcpy(manager->triggers, savedTriggers.data(), bytes);
    manager->active = savedActive;
    ShadowQueryStamp = savedStamp;
    ShadowHittingRay = savedHitting;
    ShadowRayShells[0] = savedRay;
    gEventDynamicData = savedData;
    ResetFpu();

    printf("[trigger] triggers, trigger manager, simple zones vs originals: %d cases, %d checks, %d differ%s\n",
           g_cases, g_checks, g_differ, g_faults != 0 ? " (with faults)" : "");
    if (g_faults != 0)
        printf("[trigger]   %d calls faulted\n", g_faults);
    fflush(stdout);
}
