#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "GameEffectsShadowB.h"
#include "FpControl.h"

#include <windows.h>
#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <bit>
#include <string>
#include <vector>

#include "../data/AttributeContainers.h"   // StringSet::BuyHead
#include "../engine/CoreContainers.h"      // GameVector
#include "../engine/UMemory.hpp"
#include "../game/Vehicle.h"
#include "../game/VehicleParticle.h"
#include "../physics/RigidBody.h"
#include "../physics/Simulation.h"
#include "../render/Particles.h"
#include "../../common/xbeOriginal.h"
#include "../../helpers.h"

// ---------------------------------------------------------------------------------------------------------------
// A shadow test of package GE_B (game/VehicleParticle.cpp), run once from the first simulation tick when
// NIGHTFIRE_GAMEEFFECTSSHADOWB is set. Each case runs the ORIGINAL (the package's twenty entries swapped back, so
// the originals call each other) and then the PORT on the same state, put back between the two, and compares logs:
//
//   - TriggerTireSmoke and TriggerSparks on copies of a live RVehicleParticle (the first in the set, which holds
//     the cars drawn last frame; one constructed when it is empty): random ticks, positions, velocities (zero,
//     vertical, nearly vertical, along x, random) and systems, the sparks switch on and off;
//   - UpdateCar(active or not) on the copies, its vehicle a stand-in whose virtual methods (surface, shredded tyre,
//     wheel on the ground, slip, speed, physics) answer perturbed values and log their calls; the live car's rigid
//     body perturbed (velocity, the levers, the orientation's third row) and put back; the tyre smoke's end and
//     fade random; a private set (random keys, sometimes the object already in it);
//   - UpdateAll and ResetAll over one to three copies in a private set, the systems' vtable a recording fake (their
//     Update calls logged), the last tick equal to the step count or not; Kill on a private set;
//   - the constructor with the stand-in vehicle, then the destructor, the deleting destructor (flags 0 and 1):
//     the emitters, the systems' fields (pointers as emitter indices; lastPosition, from the uninitialised emitter
//     position, left out), the set without the object after;
//   - the set's own code on random keys: insert, erase at an iterator, equal_range then erase over it, erase over a
//     random range, min and max, the destructor - the keys in order with the tree's shape and colours, the answers
//     as positions.
//
// The copies' state compared: the object's fields, the emitters' and systems' bytes, the calls logged, the set.
// Every call into code outside the package (GetRigidBody, SetSystem, the VU0 maths, the allocator) is the same on
// both sides. Both copies of the package's owned globals (the set, the last tick, the sparks switch) are written
// before each side, and the side's own copy is read after.
//
// A mutation the test sees: UpdateCar clamping road dirt's rate to 1 as it does the other dirt (the stand-in's
// speeds reach 40, a road dirt rate up to 2, so the dirt system's rateScale differs), or TriggerSparks without its
// steep-direction case (vertical velocities give another axis).
//
// One summary line: [gameeffectsshadowb] ...: N cases, M checks, D differ.
// ---------------------------------------------------------------------------------------------------------------

namespace {

#define Sim ((void *)0x00233ff0)
#define SimCars (*(GameVector<PVehicle *> *)0x00234e3c)
#define SimStepCount U32_AT(0x00234e34)
#define GameTick U32_AT(0x001f2a4c)

// The game's copies of the package's owned globals
#define GameSetCopy (*(VehicleParticleSet **)0x00201834)
#define GameTickCopy (*(uint32_t *)0x00201838)
#define GameSparksCopy (*(bool *)0x001c50d0)

#define Orig_TriggerTireSmoke ((void (__fastcall *)(RVehicleParticle *, int, int))0x000a6f10)
#define Orig_TriggerSparks ((void (__fastcall *)(RVehicleParticle *, int, const Coord3 *, Coord3, float, int))0x000a6f70)
#define Orig_UpdateCar ((void (__fastcall *)(RVehicleParticle *, int, bool))0x000a7ac0)
#define Orig_UpdateAll ((void (*)(void))0x000a8120)
#define Orig_ResetAll ((void (*)(void))0x000a7300)
#define Orig_Kill ((void (*)(void))0x000a8430)
#define Orig_Construct ((RVehicleParticle *(__fastcall *)(RVehicleParticle *, int, PVehicle *))0x000a6de0)
#define Orig_Destruct ((void (__fastcall *)(RVehicleParticle *, int))0x000a79f0)
#define Orig_Delete ((RVehicleParticle *(__fastcall *)(RVehicleParticle *, int, unsigned))0x000a8220)
#define Orig_Max ((VehicleParticleNode *(*)(VehicleParticleNode *))0x000a6f30)
#define Orig_Min ((VehicleParticleNode *(*)(VehicleParticleNode *))0x000a6f50)
#define Orig_EqualRange ((VehicleParticleSet::Range *(__fastcall *)(VehicleParticleSet *, int, VehicleParticleSet::Range *, RVehicleParticle *const *))0x000a7350)
#define Orig_Erase ((VehicleParticleSet::Iterator *(__fastcall *)(VehicleParticleSet *, int, VehicleParticleSet::Iterator *, VehicleParticleNode *))0x000a73c0)
#define Orig_InsertUnique ((VehicleParticleSet::InsertResult *(__fastcall *)(VehicleParticleSet *, int, VehicleParticleSet::InsertResult *, RVehicleParticle *const *))0x000a7870)
#define Orig_EraseRange ((VehicleParticleSet::Iterator *(__fastcall *)(VehicleParticleSet *, int, VehicleParticleSet::Iterator *, VehicleParticleNode *, VehicleParticleNode *))0x000a7930)
#define Orig_SetDestruct ((void (__fastcall *)(VehicleParticleSet *, int))0x000a81e0)

const unsigned kEntries[] = {
    0x000a6db0, 0x000a6de0, 0x000a6f10, 0x000a6f30, 0x000a6f50, 0x000a6f70, 0x000a72c0, 0x000a7300, 0x000a7350,
    0x000a73c0, 0x000a7690, 0x000a7870, 0x000a7930, 0x000a79f0, 0x000a7ac0, 0x000a8120, 0x000a81e0, 0x000a8220,
    0x000a8240, 0x000a8430,
};

const int kSystems = RVehicleParticle::kSystemCount;

long g_cases, g_checks, g_differ, g_faults;
int g_reported;
unsigned g_x87, g_sse;

void Check(bool same, const char *what, long index, const std::string &original, const std::string &port) {
    g_checks++;
    if (same)
        return;
    g_differ++;
    if (g_reported < 10) {
        g_reported++;
        size_t at = 0;
        while (at < original.size() && at < port.size() && original[at] == port[at])
            at++;
        size_t from = at > 60 ? at - 60 : 0;
        printf("[gameeffectsshadowb] DIFF %s, case %ld at %u:\n  orig ...%s\n  port ...%s\n", what, index,
               unsigned(at), original.substr(from, 160).c_str(), port.substr(from, 160).c_str());
        fflush(stdout);
    }
}

uint32_t g_seed = 0x6b8b4567;
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

uint32_t Bits(float f) {
    return std::bit_cast<uint32_t>(f);
}

void Append(std::string &log, const char *format, ...) {
    char line[512];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    log += line;
}

void AppendBytes(std::string &log, const void *bytes, size_t size) {
    const uint8_t *at = static_cast<const uint8_t *>(bytes);
    for (size_t i = 0; i < size; i++)
        Append(log, "%02x", at[i]);
    log += ' ';
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

// Runs `run(original, log)` for the original (inside the window) and then the port, `reset` before each.
template <class Reset, class Run>
void Both(const Reset &reset, const Run &run, std::string &original, std::string &port) {
    reset();
    {
        Originals window;
        original.clear();
        if (!Guarded([&] { run(true, original); }))
            original += "<fault>";
    }
    reset();
    port.clear();
    if (!Guarded([&] { run(false, port); }))
        port += "<fault>";
}

std::string *g_calls;

// ---------------------------------------------------------------------------------------------------------------
// The copies: up to three objects, each with its emitters and systems, at fixed addresses

struct Instance {
    RVehicleParticle object;
    RParticleEmitter emitters[kSystems];
    RParticleSystem systems[kSystems];
};
Instance g_instances[3];

struct Template {
    RVehicleParticle object;
    RParticleEmitter emitters[kSystems];
    RParticleSystem systems[kSystems];
} g_template;

// The systems' recording vtable
int SystemName(const RParticleSystem *system) {
    for (int k = 0; k < 3; k++)
        for (int i = 0; i < kSystems; i++)
            if (system == &g_instances[k].systems[i])
                return k * 100 + i;
    return -1;
}
RParticleSystem *__fastcall FakeSystemDestroy(RParticleSystem *system, int, unsigned flags) {
    if (g_calls != NULL)
        Append(*g_calls, "destroy(%d,%u) ", SystemName(system), flags);
    return system;
}
void __fastcall FakeSystemUpdate(RParticleSystem *system, int, int ticks) {
    if (g_calls != NULL)
        Append(*g_calls, "update(%d,%d) ", SystemName(system), ticks);
}
const RParticleSystemVtable g_fakeSystemVtable = { &FakeSystemDestroy, &FakeSystemUpdate };

// The stand-in vehicle: the live car's bytes under a vtable whose methods answer the scenario's values
struct Scenario {
    int surface[4];
    int shredded[4];
    int ground[4];
    float slip[4];
    float speed;
    int snowmobile;
} g_scenario;
alignas(16) uint8_t g_fakeVehicle[sizeof(PVehicle)];
const void *g_fakeVehicleVtable[PVehicle::kSlotCount];
CarPhysics g_fakePhysics;
bool g_haveVehicle;

float __fastcall FakeGetCarSpeed(void *, int) {
    if (g_calls != NULL)
        *g_calls += "speed ";
    return g_scenario.speed;
}
int __fastcall FakeIsTyreShredded(void *, int, int wheel) {
    if (g_calls != NULL)
        Append(*g_calls, "shredded(%d) ", wheel & 0xff);
    return g_scenario.shredded[wheel & 3];
}
int __fastcall FakeGetWheelRoadSurface(void *, int, int wheel) {
    if (g_calls != NULL)
        Append(*g_calls, "surface(%d) ", wheel);
    return g_scenario.surface[wheel & 3];
}
int __fastcall FakeIsWheelOnGround(void *, int, int wheel) {
    if (g_calls != NULL)
        Append(*g_calls, "ground(%d) ", wheel & 0xff);
    return g_scenario.ground[wheel & 3];
}
float __fastcall FakeGetCarWheelSlip(void *, int, int wheel) {
    if (g_calls != NULL)
        Append(*g_calls, "slip(%d) ", wheel);
    return g_scenario.slip[wheel & 3];
}
CarPhysics *__fastcall FakeGetPhysics(void *, int) {
    if (g_calls != NULL)
        *g_calls += "physics ";
    g_fakePhysics.isSnowmobile = g_scenario.snowmobile;
    return &g_fakePhysics;
}

PVehicle *FakeVehicle() {
    return reinterpret_cast<PVehicle *>(g_fakeVehicle);
}

void MakeFakeVehicle(PVehicle *live) {
    memcpy(g_fakeVehicle, live, sizeof(PVehicle));
    memcpy(g_fakeVehicleVtable, live->vtable, sizeof(g_fakeVehicleVtable));
    g_fakeVehicleVtable[PVehicle::kGetCarSpeed] = (const void *)&FakeGetCarSpeed;
    g_fakeVehicleVtable[PVehicle::kIsTyreShredded] = (const void *)&FakeIsTyreShredded;
    g_fakeVehicleVtable[PVehicle::kGetPhysics] = (const void *)&FakeGetPhysics;
    g_fakeVehicleVtable[PVehicle::kGetWheelRoadSurface] = (const void *)&FakeGetWheelRoadSurface;
    g_fakeVehicleVtable[PVehicle::kIsWheelOnGround] = (const void *)&FakeIsWheelOnGround;
    g_fakeVehicleVtable[PVehicle::kGetCarWheelSlip] = (const void *)&FakeGetCarWheelSlip;
    FakeVehicle()->vtable = (void **)g_fakeVehicleVtable;
    memset(&g_fakePhysics, 0, sizeof(g_fakePhysics));
}

void RandomScenario() {
    for (int w = 0; w < 4; w++) {
        g_scenario.surface[w] = int(Random(16)) | (Random(4) == 0 ? int(Random() & 0xf0) : 0);
        g_scenario.shredded[w] = Random(3) == 0;
        g_scenario.ground[w] = Random(4) != 0;
        uint32_t r = Random(6);
        g_scenario.slip[w] = r == 0 ? 0.2f : r == 1 ? 0.3f : r == 2 ? 1.0f : Uniform(-0.5f, 2.5f);
    }
    g_scenario.speed = Random(5) == 0 ? 10.0f : Uniform(-20.0f, 40.0f);
    g_scenario.snowmobile = Random(3) == 0;
}

// The live car's body, perturbed for a case and put back
RigidBody *g_body;
RigidBody g_bodySaved, g_bodyCase;
RigidBodyInfo g_infoSaved, g_infoCase;

void PerturbBody() {
    g_bodyCase = g_bodySaved;
    g_infoCase = g_infoSaved;
    uint32_t r = Random(4);
    float scale = r == 0 ? 0.5f : r == 1 ? 2.0f : 15.0f;
    g_bodyCase.velocity.x = Uniform(-scale, scale);
    g_bodyCase.velocity.y = Uniform(-scale, scale);
    g_bodyCase.velocity.z = Uniform(-scale, scale);
    for (int w = 0; w < 4; w++) {
        g_infoCase.worldLevers[w].x += Uniform(-1.0f, 1.0f);
        g_infoCase.worldLevers[w].y += Uniform(-1.0f, 1.0f);
        g_infoCase.worldLevers[w].z += Uniform(-1.0f, 1.0f);
    }
    g_infoCase.orientation.mtx[2][0] = Uniform(-1.0f, 1.0f);
    g_infoCase.orientation.mtx[2][2] = Uniform(-1.0f, 1.0f);
}

void ApplyBody() {
    if (g_body == NULL)
        return;
    RigidBodyInfo *info = g_body->info;
    *g_body = g_bodyCase;
    g_body->info = info;
    *info = g_infoCase;
}

void ResetInstance(int k, uint32_t smokeEnd, float smokeFade) {
    Instance &instance = g_instances[k];
    instance.object = g_template.object;
    instance.object.emitters = instance.emitters;
    instance.object.vehicle = FakeVehicle();
    instance.object.smokeEndTick = smokeEnd;
    instance.object.smokeFade = smokeFade;
    for (int i = 0; i < kSystems; i++) {
        instance.emitters[i] = g_template.emitters[i];
        instance.emitters[i].system = &instance.systems[i];
        instance.systems[i] = g_template.systems[i];
        instance.systems[i].vtable = &g_fakeSystemVtable;
        instance.systems[i].emitter = &instance.emitters[i];
        instance.systems[i].positionSource = reinterpret_cast<Coord3 *>(&instance.emitters[i].position);
        instance.object.systems[i] = &instance.systems[i];
    }
}

void AppendInstance(std::string &log, int k) {
    const Instance &instance = g_instances[k];
    Append(log, "o%d %08x %08x ", k, instance.object.smokeEndTick, Bits(instance.object.smokeFade));
    AppendBytes(log, instance.emitters, sizeof(instance.emitters));
    AppendBytes(log, instance.systems, sizeof(instance.systems));
}

// ---------------------------------------------------------------------------------------------------------------
// Private sets

VehicleParticleSet *NewSet() {
    VehicleParticleSet *set = static_cast<VehicleParticleSet *>(OperatorNew(sizeof(VehicleParticleSet)));
    set->allocator = 0;
    set->head = reinterpret_cast<VehicleParticleNode *>(StringSet::BuyHead());
    set->head->isNil = 1;
    set->head->parent = set->head;
    set->head->left = set->head;
    set->head->right = set->head;
    set->size = 0;
    return set;
}

void FreeSet(VehicleParticleSet *set) {
    if (set == NULL)
        return;
    set->Destruct();
    OperatorDelete(set);
}

RVehicleParticle *RandomKey() {
    return reinterpret_cast<RVehicleParticle *>(uintptr_t(0x01000000u + (Random(64) << 4)));
}

void AddKeys(VehicleParticleSet *set, int count) {
    for (int i = 0; i < count; i++) {
        VehicleParticleSet::InsertResult result;
        set->InsertUnique(&result, RandomKey());
    }
}

void AppendKey(std::string &log, RVehicleParticle *key) {
    for (int k = 0; k < 3; k++)
        if (key == &g_instances[k].object) {
            Append(log, "o%d", k);
            return;
        }
    Append(log, "%x", unsigned(uintptr_t(key)));
}

void AppendTree(std::string &log, const VehicleParticleNode *node) {
    if (node->isNil) {
        log += '.';
        return;
    }
    log += '(';
    AppendKey(log, node->value);
    Append(log, "%c", node->color == kTreeRed ? 'r' : 'b');
    AppendTree(log, node->left);
    AppendTree(log, node->right);
    log += ')';
}

void AppendSet(std::string &log, const VehicleParticleSet *set) {
    if (set == NULL) {
        log += "set NULL ";
        return;
    }
    Append(log, "set %u ", set->size);
    if (set->head == NULL) {
        log += "headless ";
        return;
    }
    AppendTree(log, set->head->parent);
    log += " <";
    if (set->head->left != set->head)
        AppendKey(log, set->head->left->value);
    log += ',';
    if (set->head->right != set->head)
        AppendKey(log, set->head->right->value);
    log += "> ";
}

int Position(const VehicleParticleSet *set, const VehicleParticleNode *node) {
    if (node == set->head)
        return -1;
    int i = 0;
    for (VehicleParticleSet::Iterator at = {set->head->left}; at.node != set->head; i++) {
        if (at.node == node)
            return i;
        // the next node in order
        VehicleParticleNode *n = at.node;
        if (!n->right->isNil) {
            n = n->right;
            while (!n->left->isNil)
                n = n->left;
        } else {
            VehicleParticleNode *p = n->parent;
            while (!p->isNil && n == p->right) {
                n = p;
                p = p->parent;
            }
            n = p;
        }
        at.node = n;
    }
    return -2;
}

VehicleParticleNode *NodeAt(const VehicleParticleSet *set, int index) {
    VehicleParticleNode *node = set->head->left;
    for (int i = 0; i < index && node != set->head; i++) {
        if (!node->right->isNil) {
            node = node->right;
            while (!node->left->isNil)
                node = node->left;
        } else {
            VehicleParticleNode *p = node->parent;
            while (!p->isNil && node == p->right) {
                node = p;
                p = p->parent;
            }
            node = p;
        }
    }
    return node;
}

// Both copies of the owned globals
void SetGlobals(VehicleParticleSet *set, uint32_t tick, bool sparks) {
    fgVehicleParticles = set;
    GameSetCopy = set;
    VehicleParticleUpdateTick = tick;
    GameTickCopy = tick;
    VehicleSparksEnabled = sparks;
    GameSparksCopy = sparks;
}
VehicleParticleSet *SideSet(bool orig) {
    return orig ? GameSetCopy : fgVehicleParticles;
}
uint32_t SideTick(bool orig) {
    return orig ? GameTickCopy : VehicleParticleUpdateTick;
}

// ---------------------------------------------------------------------------------------------------------------
// The cases

void TestTriggers() {
    for (int i = 0; i < 300; i++) {
        uint32_t smokeEnd = GameTick + Random(100) - 50;
        float smokeFade = Uniform(0.0f, 0.2f);
        bool sparks = Random(8) != 0;
        int ticks = Random(10) == 0 ? -int(Random(20)) : int(Random(300)) + 1;
        int system = Random(4) == 0 ? int(Random(kSystems)) : RVehicleParticle::kSparks + int(Random(6));
        Coord3 position = {g_bodySaved.position.x + Uniform(-3.0f, 3.0f), g_bodySaved.position.y + Uniform(-1.0f, 2.0f),
                           g_bodySaved.position.z + Uniform(-3.0f, 3.0f)};
        if (Random(8) == 0)
            position = g_bodySaved.position;
        Coord3 velocity = {Uniform(-20.0f, 20.0f), Uniform(-20.0f, 20.0f), Uniform(-20.0f, 20.0f)};
        switch (Random(8)) {
        case 0: velocity = Coord3{0.0f, 0.0f, 0.0f}; break;
        case 1: velocity = Coord3{0.0f, Uniform(-10.0f, 10.0f), 0.0f}; break;
        case 2: velocity.x = Uniform(-0.09f, 0.09f); velocity.z = Uniform(-0.09f, 0.09f); break;
        case 3: velocity.y = 0.0f; velocity.z = 0.0f; break;
        case 4: velocity.y = velocity.x * Uniform(-3.0f, 3.0f); break;
        default: break;
        }
        float unknown = Uniform(0.0f, 2.0f);
        std::string original, port;
        g_cases++;
        Both([&] { ResetInstance(0, smokeEnd, smokeFade); ApplyBody(); },
             [&](bool orig, std::string &log) {
            SetGlobals(NULL, 0, sparks);
            RVehicleParticle *object = &g_instances[0].object;
            g_calls = &log;
            if (orig) Orig_TriggerTireSmoke(object, 0, ticks); else object->TriggerTireSmoke(ticks);
            if (orig) Orig_TriggerSparks(object, 0, &position, velocity, unknown, system);
            else object->TriggerSparks(&position, velocity, unknown, system);
            g_calls = NULL;
            AppendInstance(log, 0);
        }, original, port);
        Check(original == port, "TriggerTireSmoke/TriggerSparks", i, original, port);
    }
}

void TestUpdateCar() {
    for (int i = 0; i < 400; i++) {
        RandomScenario();
        PerturbBody();
        uint32_t smokeEnd = Random(3) == 0 ? 0 : GameTick + Random(100) - 30;
        float smokeFade = Uniform(0.0f, 0.1f);
        bool active = Random(6) != 0;
        int keys = int(Random(6));
        bool already = Random(4) == 0;
        uint32_t dirtTags[4];
        bool retag = Random(2) == 0;
        static const uint32_t kTags[] = {0x52444446, 0x52444452, 0x52444752, 0x534e4f57, 0x57415452};
        for (int w = 0; w < 4; w++)
            dirtTags[w] = kTags[Random(5)];
        std::string original, port;
        g_cases++;
        Both([&] {
            ResetInstance(0, smokeEnd, smokeFade);
            for (int w = 0; w < 4 && retag; w++)
                g_instances[0].emitters[RVehicleParticle::kWheelDirt + w].systemTag = dirtTags[w];
            ApplyBody();
        }, [&](bool orig, std::string &log) {
            uint32_t saved = g_seed;
            VehicleParticleSet *set = NewSet();
            AddKeys(set, keys);
            g_seed = saved;
            RVehicleParticle *object = &g_instances[0].object;
            if (already) {
                VehicleParticleSet::InsertResult result;
                set->InsertUnique(&result, object);
            }
            SetGlobals(set, 0, true);
            g_calls = &log;
            if (orig) Orig_UpdateCar(object, 0, active); else object->UpdateCar(active);
            g_calls = NULL;
            AppendInstance(log, 0);
            AppendSet(log, SideSet(orig));
            FreeSet(set);
            SetGlobals(NULL, 0, true);
        }, original, port);
        Check(original == port, "UpdateCar", i, original, port);
    }
}

void TestUpdateAll() {
    for (int i = 0; i < 120; i++) {
        int count = 1 + int(Random(3));
        bool same = Random(3) == 0;
        uint32_t tick = same ? SimStepCount : SimStepCount + Random(5) - 2;
        uint32_t smokeEnd[3];
        for (int k = 0; k < 3; k++)
            smokeEnd[k] = Random();
        int which = int(Random(3));   // 0 UpdateAll, 1 ResetAll, 2 Kill
        std::string original, port;
        g_cases++;
        Both([&] {
            for (int k = 0; k < 3; k++)
                ResetInstance(k, smokeEnd[k], 0.5f);
        }, [&](bool orig, std::string &log) {
            VehicleParticleSet *set = NewSet();
            for (int k = 0; k < count; k++) {
                VehicleParticleSet::InsertResult result;
                set->InsertUnique(&result, &g_instances[k].object);
            }
            SetGlobals(set, tick, true);
            g_calls = &log;
            if (which == 0) {
                if (orig) Orig_UpdateAll(); else RVehicleParticle::UpdateAll();
            } else if (which == 1) {
                if (orig) Orig_ResetAll(); else RVehicleParticle::ResetAll();
            } else {
                if (orig) Orig_Kill(); else RVehicleParticle::Kill();
            }
            g_calls = NULL;
            Append(log, "tick %08x ", SideTick(orig));
            for (int k = 0; k < count; k++)
                Append(log, "end%d %08x ", k, g_instances[k].object.smokeEndTick);
            VehicleParticleSet *after = SideSet(orig);
            if (which == 2) {
                Append(log, "killed %d ", after == NULL);
                if (after != NULL)
                    FreeSet(after);
            } else {
                AppendSet(log, after);
                FreeSet(set);
            }
            SetGlobals(NULL, 0, true);
        }, original, port);
        Check(original == port, which == 0 ? "UpdateAll" : which == 1 ? "ResetAll" : "Kill", i, original, port);
    }
}

int EmitterIndex(const RVehicleParticle *object, const void *pointer) {
    for (int i = 0; i < kSystems; i++)
        if (pointer == &object->emitters[i] || pointer == &object->emitters[i].position)
            return i;
    return pointer == NULL ? -1 : -2;
}

void TestConstruct() {
    for (int i = 0; i < 40; i++) {
        int keys = int(Random(5));
        int how = int(Random(3));
        std::string original, port;
        g_cases++;
        Both([] {}, [&](bool orig, std::string &log) {
            uint32_t saved = g_seed;
            VehicleParticleSet *set = NewSet();
            AddKeys(set, keys);
            g_seed = saved;
            SetGlobals(set, 0, true);
            RVehicleParticle *object = static_cast<RVehicleParticle *>(UMemory::FastAlloc(sizeof(RVehicleParticle),
                                                                                           "RVehicleParticle"));
            RVehicleParticle *self = orig ? Orig_Construct(object, 0, FakeVehicle()) : object->Construct(FakeVehicle());
            Append(log, "ctor %d %p %d %08x %08x ", self == object, object->vtable, object->vehicle == FakeVehicle(),
                   object->smokeEndTick, Bits(object->smokeFade));
            for (int s = 0; s < kSystems; s++) {
                const RParticleEmitter &emitter = object->emitters[s];
                const RParticleSystem *system = object->systems[s];
                Append(log, "[%08x %08x %08x %08x %08x %08x %08x %d %d | ", emitter.systemTag, Bits(emitter.side.x),
                       Bits(emitter.side.y), Bits(emitter.side.z), Bits(emitter.direction.x),
                       Bits(emitter.direction.y), Bits(emitter.direction.z), emitter.body, emitter.system == system);
                AppendBytes(log, &system->axis, sizeof(system->axis));
                Append(log, "%d %u %08x %08x %08x %p %d %d %04x %d %p %08x %08x %08x %d %d %d] ", system->keepAlive,
                       system->time, Bits(system->unknown34), Bits(system->nextSpawn), Bits(system->spawns),
                       (const void *)system->data, EmitterIndex(object, system->emitter), system->active,
                       system->systemFlags, EmitterIndex(object, system->positionSource),
                       (const void *)system->velocityPerSecond, Bits(system->rate), Bits(system->rateScale),
                       Bits(system->unknown5C), system->unknown60, system->scaledCount, system->index);
            }
            VehicleParticleSet::InsertResult result;
            set->InsertUnique(&result, object);
            if (how == 0) {
                RVehicleParticle *deleted = orig ? Orig_Delete(object, 0, 1) : object->Delete(1);
                Append(log, "delete1 %d ", deleted == object);
            } else if (how == 1) {
                RVehicleParticle *deleted = orig ? Orig_Delete(object, 0, 0) : object->Delete(0);
                Append(log, "delete0 %d %p %p ", deleted == object, object->vtable, (const void *)object->emitters);
                UMemory::FastFree(object, sizeof(RVehicleParticle));
            } else {
                if (orig) Orig_Destruct(object, 0); else object->Destruct();
                Append(log, "dtor %p %p ", object->vtable, (const void *)object->emitters);
                UMemory::FastFree(object, sizeof(RVehicleParticle));
            }
            AppendSet(log, SideSet(orig));
            FreeSet(set);
            SetGlobals(NULL, 0, true);
        }, original, port);
        Check(original == port, "construct/destruct", i, original, port);
    }
}

void TestSet() {
    for (int i = 0; i < 200; i++) {
        int count = int(Random(24));
        std::vector<RVehicleParticle *> keys;
        for (int n = 0; n < count; n++)
            keys.push_back(RandomKey());
        uint32_t opSeed = Random();
        int ops = 1 + int(Random(8));
        std::string original, port;
        g_cases++;
        Both([] {}, [&](bool orig, std::string &log) {
            uint32_t saved = g_seed;
            g_seed = opSeed;
            VehicleParticleSet *set = NewSet();
            for (RVehicleParticle *key : keys) {
                VehicleParticleSet::InsertResult result;
                if (orig) Orig_InsertUnique(set, 0, &result, &key); else set->InsertUnique(&result, key);
                Append(log, "ins %d %d ", Position(set, result.position.node), result.inserted);
            }
            AppendSet(log, set);
            for (int op = 0; op < ops; op++) {
                int size = int(set->size);
                switch (Random(4)) {
                case 0:
                    if (size > 0) {
                        VehicleParticleNode *where = NodeAt(set, int(Random(size)));
                        VehicleParticleSet::Iterator result;
                        if (orig) Orig_Erase(set, 0, &result, where);
                        else set->Erase(&result, VehicleParticleSet::Iterator{where});
                        Append(log, "erase %d ", Position(set, result.node));
                    }
                    break;
                case 1: {
                    RVehicleParticle *key = size > 0 && Random(3) != 0 ? NodeAt(set, int(Random(size)))->value
                                                                       : RandomKey();
                    VehicleParticleSet::Range range;
                    if (orig) Orig_EqualRange(set, 0, &range, &key); else set->EqualRange(&range, key);
                    Append(log, "range %d %d ", Position(set, range.first.node), Position(set, range.last.node));
                    VehicleParticleSet::Iterator result;
                    if (orig) Orig_EraseRange(set, 0, &result, range.first.node, range.last.node);
                    else set->EraseRange(&result, range.first, range.last);
                    Append(log, "-> %d ", Position(set, result.node));
                    break;
                }
                case 2: {
                    int first = int(Random(size + 1));
                    int last = first + int(Random(size - first + 1));
                    if (Random(4) == 0) {
                        first = 0;
                        last = size;
                    }
                    VehicleParticleSet::Iterator result;
                    VehicleParticleNode *a = NodeAt(set, first), *b = NodeAt(set, last);
                    if (orig) Orig_EraseRange(set, 0, &result, a, b);
                    else set->EraseRange(&result, VehicleParticleSet::Iterator{a}, VehicleParticleSet::Iterator{b});
                    Append(log, "eraserange %d %d -> %d ", first, last, Position(set, result.node));
                    break;
                }
                default:
                    if (size > 0) {
                        VehicleParticleNode *root = set->head->parent;
                        VehicleParticleNode *low = orig ? Orig_Min(root) : VehicleParticleSet::Min(root);
                        VehicleParticleNode *high = orig ? Orig_Max(root) : VehicleParticleSet::Max(root);
                        Append(log, "minmax %d %d ", Position(set, low), Position(set, high));
                    }
                    break;
                }
                AppendSet(log, set);
            }
            if (orig) Orig_SetDestruct(set, 0); else set->Destruct();
            Append(log, "destroyed %d %u", set->head == NULL, set->size);
            OperatorDelete(set);
            g_seed = saved;
        }, original, port);
        Check(original == port, "set", i, original, port);
    }
}

}  // namespace

void GameEffectsShadowB_Run(void) {
    const char *env = getenv("NIGHTFIRE_GAMEEFFECTSSHADOWB");
    if (env == NULL || atoi(env) == 0)
        return;
    FpControlGet(&g_x87, &g_sse);

    VehicleParticleSet *savedSet = fgVehicleParticles, *savedGameSet = GameSetCopy;
    uint32_t savedTick = VehicleParticleUpdateTick, savedGameTick = GameTickCopy;
    bool savedSparks = VehicleSparksEnabled, savedGameSparks = GameSparksCopy;

    PVehicle *live = SimCars.first != NULL && SimCars.last != SimCars.first ? SimCars.first[0] : NULL;
    RVehicleParticle *source = NULL;
    if (savedSet != NULL && savedSet->size != 0)
        source = savedSet->head->left->value;
    if (live != NULL)
        MakeFakeVehicle(live);

    RVehicleParticle *built = NULL;
    if (source == NULL && live != NULL) {
        SetGlobals(NULL, 0, true);
        built = static_cast<RVehicleParticle *>(UMemory::FastAlloc(sizeof(RVehicleParticle), "RVehicleParticle"));
        built->Construct(FakeVehicle());
        source = built;
    }

    if (source != NULL) {
        g_template.object = *source;
        for (int i = 0; i < kSystems; i++) {
            g_template.emitters[i] = source->emitters[i];
            g_template.systems[i] = *source->systems[i];
        }
        if (live == NULL) {
            live = source->vehicle;
            MakeFakeVehicle(live);
        }
        FakeVehicle()->rigidBodySlot = source->vehicle->rigidBodySlot;
    }

    if (source != NULL && FakeVehicle()->rigidBodySlot >= 0) {
        g_body = Simulation_GetRigidBody(Sim, 0, FakeVehicle()->rigidBodySlot);
        g_bodySaved = *g_body;
        g_infoSaved = *g_body->info;
        g_bodyCase = g_bodySaved;
        g_infoCase = g_infoSaved;
        TestTriggers();
        TestUpdateCar();
        TestUpdateAll();
        TestConstruct();
        g_bodyCase = g_bodySaved;
        g_infoCase = g_infoSaved;
        ApplyBody();
    } else {
        printf("[gameeffectsshadowb] no live vehicle particles or car: only the set's cases\n");
    }
    TestSet();

    if (built != NULL) {
        SetGlobals(NULL, 0, true);
        built->Delete(1);
    }
    fgVehicleParticles = savedSet;
    GameSetCopy = savedGameSet;
    VehicleParticleUpdateTick = savedTick;
    GameTickCopy = savedGameTick;
    VehicleSparksEnabled = savedSparks;
    GameSparksCopy = savedGameSparks;

    printf("[gameeffectsshadowb] RVehicleParticle and its set: %ld cases, %ld checks, %ld differ (%ld faults)\n",
           g_cases, g_checks, g_differ, g_faults);
    fflush(stdout);
}
