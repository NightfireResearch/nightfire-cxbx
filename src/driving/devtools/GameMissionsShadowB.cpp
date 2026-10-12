#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "GameMissionsShadowB.h"
#include "FpControl.h"

#include <windows.h>
#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <initializer_list>
#include <map>
#include <set>
#include <vector>

#include "../camera/PlayerCamera.h"
#include "../data/AttributeContainers.h"    // StringSet::BuyHead
#include "../data/Carp.h"                   // CARP::Instance
#include "../engine/CoreContainers.h"
#include "../engine/GameLoop.h"             // LaunchPage
#include "../engine/SimRandom.h"
#include "../engine/UMemory.hpp"
#include "../game/Simulation.h"
#include "../physics/PhysicsObject.h"
#include "../physics/RigidBody.h"
#include "../render/RenderHigh.h"
#include "../world/CollisionManager.h"
#include "../world/SoundMap.h"              // MapBuyHead
#include "../../common/xbeOriginal.h"
#include "../../common/xbeOverload.h"   // XbeAddress
#include "../../helpers.h"

// ---------------------------------------------------------------------------------------------------------------
// A shadow test of game.missions' package B, run once from the first simulation tick when
// NIGHTFIRE_GAMEMISSIONSSHADOWB is set. Each case runs the ORIGINAL (the package's entries swapped back, all of them
// unless the case says) and then the PORT, each on its own deep copy of the same Simulation (the object map, the
// dead set, the vectors' storage and the random generator cloned; the bodies' pool shared or copied), and compares a
// serialisation of what each leaves (the scalars, the map and the set by shape, colour and content, every vector,
// object pointers by identity or, for objects made during the case, by order of appearance), the shared bytes they
// may write (the live body pool, the collision manager's flag, the launch page's word) and a log of every call they
// make out of the package:
//
//   - vector reserve on random vectors and counts; the dead set's insert_unique over random insert sequences;
//   - DeletePhysicsObject (and its thunk) on copies of the live Simulation, one to three live objects in a row (some
//     repeated, so some already waiting), and absent cars;
//   - SpawnPhysicsObject with synthetic objects of every type and out-of-range ones (cars on live car bodies, their
//     sleep state perturbed); CanSpawnSimpleRigidBody about the 72/96 limits with synthetic newtons (the deletes
//     faked); TrackInstance and UpdateTrackedInstanceVisibility on synthetic lists (StepCheckHitWorld faked);
//   - SimulateGame on copies of the body pool with perturbed sleep states, owners and info flags, on a map of
//     synthetic objects (their Simulate a fake), the physics faked; Reset and CleanUpObjects (DeleteDeadObjects and
//     UntrackAllInstances faked);
//   - SpawnExplosion (the event's operator new and constructor faked) and the eleven Spawn*Object (the constructors
//     and CanSpawnRigidBody faked), about the force and simple body limits;
//   - the constructor and destructor on blocks of our own.
//
// A mutation the test sees: SpawnPhysicsObject with the humans' and mines' cases swapped (the vectors differ); in
// DeletePhysicsObject, the cars' remove without its end test (the inactive cars lose their last element).
// ---------------------------------------------------------------------------------------------------------------

namespace {

// ---- the originals

const unsigned kEntries[] = {
    0x000b4080, 0x000b4190, 0x000b4250, 0x000b4740, 0x000b4750, 0x000b47a0, 0x000b4810, 0x000b4870, 0x000b49f0,
    0x000b4a60, 0x000b4cd0, 0x000b4e10, 0x000b4fe0, 0x000b5310, 0x000b5700, 0x000b57b0, 0x000b5890, 0x000b5950,
    0x000b5a70, 0x000b5b80, 0x000b5c90, 0x000b5d50, 0x000b5e20, 0x000b5ee0, 0x000b5fb0,
};

#define Orig_Reserve ((void (__fastcall *)(void *, int, uint32_t))0x000b4080)
#define Orig_InsertUnique ((DeadObjectSet::InsertResult *(__fastcall *)(void *, int, DeadObjectSet::InsertResult *, PhysicsObjNode *const *))0x000b4190)
#define Orig_Delete ((void (__fastcall *)(void *, int, PhysicsObject *))0x000b4250)
#define Orig_DeleteThunk ((void (__fastcall *)(void *, int, PhysicsObject *))0x000b4740)
#define Orig_CleanUp ((void (__fastcall *)(void *, int))0x000b4750)
#define Orig_CanSpawnSimple ((bool (__fastcall *)(void *, int, const Coord3 *, bool))0x000b47a0)
#define Orig_Track ((void (__fastcall *)(void *, int, CARP::Instance *, int))0x000b4810)
#define Orig_UpdateTracked ((void (__fastcall *)(void *, int))0x000b4870)
#define Orig_Reset ((void (__fastcall *)(void *, int))0x000b49f0)
#define Orig_SimulateGame ((void (__fastcall *)(void *, int))0x000b4a60)
#define Orig_SpawnExplosion ((void (__fastcall *)(void *, int, uint32_t, uint32_t, const Coord3 *, const Coord3 *, float, uint32_t, PhysicsObject *, uint32_t))0x000b4cd0)
#define Orig_SpawnObject ((void (__fastcall *)(void *, int, PhysicsObject *))0x000b4e10)
#define Orig_Construct ((Simulation *(__fastcall *)(void *, int))0x000b4fe0)
#define Orig_Destruct ((void (__fastcall *)(void *, int))0x000b5310)
#define Orig_SpawnCar ((PhysicsObject *(__fastcall *)(void *, int, int, const char *, uint32_t, const Coord3 *, const Coord3 *))0x000b5700)
#define Orig_SpawnSmackable ((PhysicsObject *(__fastcall *)(void *, int, RSceneObj *, uint32_t, uint32_t, uint32_t))0x000b57b0)
#define Orig_SpawnHuman ((PhysicsObject *(__fastcall *)(void *, int, int, const Coord3 *, const Coord3 *, uint32_t))0x000b5890)
#define Orig_SpawnMissile ((PhysicsObject *(__fastcall *)(void *, int, int, float, const Coord3 *, const Coord3 *, uint32_t, uint32_t, uint32_t, int))0x000b5950)
#define Orig_SpawnMine ((PhysicsObject *(__fastcall *)(void *, int, int, const Coord3 *, const Coord3 *, float, uint32_t))0x000b5a70)
#define Orig_SpawnGrenade ((PhysicsObject *(__fastcall *)(void *, int, uint32_t, uint32_t, const Coord3 *, uint32_t, uint32_t))0x000b5b80)
#define Orig_SpawnShell ((PhysicsObject *(__fastcall *)(void *, int, int, uint32_t, const Coord3 *, uint32_t, float, float, int))0x000b5c90)
#define Orig_SpawnExplosionObject ((PhysicsObject *(__fastcall *)(void *, int, uint32_t, uint32_t, uint32_t, const Coord3 *, float, uint32_t, uint32_t))0x000b5d50)
#define Orig_SpawnNewton ((PhysicsObject *(__fastcall *)(void *, int, const Coord3 *, const Coord3 *, const Coord3 *, const Coord3 *, CARP::Instance *, int, float, float))0x000b5e20)
#define Orig_SpawnHelicopter ((PhysicsObject *(__fastcall *)(void *, int, const char *, const Coord3 *, const Coord3 *, float))0x000b5ee0)
#define Orig_SpawnSentry ((PhysicsObject *(__fastcall *)(void *, int, CARP::Instance *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t))0x000b5fb0)

// The callees faked
const uint32_t kDeletePhysicsObject = 0x000b4250;
const uint32_t kDeleteDeadObjects = 0x000b3fc0;
const uint32_t kUntrackAll = 0x000b30c0;
const uint32_t kDetectCollisionsSRB = 0x000b2830;
const uint32_t kCanSpawnRigidBody = 0x000b2c40;
const uint32_t kStepCheckHitWorld = 0x000c4000;
const uint32_t kCollideWithObject = 0x000b0510;
const uint32_t kCollideWithWorld = 0x000b1420;
const uint32_t kCollideWithGround = 0x000ae4c0;
const uint32_t kAngularDamping = 0x000ad130;
const uint32_t kUpdatePosition = 0x000b0ec0;
const uint32_t kInitialForces = 0x000ade40;
const uint32_t kSimpleUpdatePosition = 0x000b1f80;
const uint32_t kEventNew = 0x0005a5a0;
const uint32_t kESpawnExplosion = 0x00044f40;
const uint32_t kBondCarConstruct = 0x0006d350;
const uint32_t kSmackableConstruct = 0x00075920;
const uint32_t kHumanConstruct = 0x0005dd90;
const uint32_t kMissileConstruct = 0x0005e8c0;
const uint32_t kMineConstruct = 0x0005e480;
const uint32_t kGrenadeConstruct = 0x0005d5c0;
const uint32_t kShellConstruct = 0x00074770;
const uint32_t kExplosionConstruct = 0x0005cd70;
const uint32_t kNewtonConstruct = 0x00061060;
const uint32_t kHelicopterConstruct = 0x0006d8f0;
const uint32_t kSentryConstruct = 0x00073a90;

#define ShadowLaunch (*(LaunchPage *)0x00243b90)

// ---- results

bool g_original;
long g_cases, g_checks, g_differ, g_faults, g_bothFaulted, g_skipped;
int g_reported;
unsigned g_x87, g_sse;

void Report(const char *what, long index, const char *detail) {
    g_differ++;
    if (g_reported++ < 10)
        printf("[gamemissionsshadowb]   %s #%ld: %s\n", what, index, detail);
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

// ---- random inputs

uint32_t g_seed = 0x5b4e10a3;

uint32_t Next() {
    g_seed = g_seed * 1664525u + 1013904223u;
    return g_seed >> 8;
}

int Range(int lo, int hi) {
    return lo + int(Next() % uint32_t(hi - lo + 1));
}

float Uniform(float lo, float hi) {
    return lo + (hi - lo) * float(Next() & 0xffff) / 65535.0f;
}

bool Chance(int percent) {
    return Range(0, 99) < percent;
}

// ---- the stream each side writes (results and the fakes' log)

std::vector<uint32_t> g_stream[2];

void Out(uint32_t word) {
    g_stream[g_original ? 0 : 1].push_back(word);
}

void OutFloat(float value) {
    uint32_t word;
    memcpy(&word, &value, 4);
    Out(word);
}

void OutXyz(const float *v) {
    if (v == NULL) {
        Out(0xdeadbeef);
        return;
    }
    OutFloat(v[0]);
    OutFloat(v[1]);
    OutFloat(v[2]);
}

void CompareStreams(const char *what, long index) {
    g_checks++;
    const std::vector<uint32_t> &a = g_stream[0], &b = g_stream[1];
    if (a == b)
        return;
    size_t at = 0;
    while (at < a.size() && at < b.size() && a[at] == b[at])
        at++;
    char detail[128];
    snprintf(detail, sizeof(detail), "stream word %u of %u/%u: original %08x, port %08x", unsigned(at),
             unsigned(a.size()), unsigned(b.size()), at < a.size() ? a[at] : 0, at < b.size() ? b[at] : 0);
    Report(what, index, detail);
}

// ---- object identity: the live and synthetic objects by address, the ones made in a case by order of appearance

std::set<const void *> g_known;
std::map<const void *, uint32_t> g_fresh;

uint32_t Canon(const void *object) {
    if (object == NULL || g_known.count(object) != 0)
        return uint32_t(uintptr_t(object));
    auto found = g_fresh.find(object);
    if (found != g_fresh.end())
        return found->second;
    uint32_t id = 0x4e000000u + uint32_t(g_fresh.size());
    g_fresh[object] = id;
    return id;
}

// ---- deep copies of a Simulation

template <class Node>
Node *CloneSubtree(const Node *node, const Node *sourceHead, Node *head, Node *parent,
                   std::map<const void *, void *> *nodes) {
    if (node == sourceHead || node->isNil)
        return head;
    Node *copy = static_cast<Node *>(UMemory::FastAlloc(sizeof(Node), "STL"));
    memcpy(copy, node, sizeof(Node));
    copy->parent = parent;
    (*nodes)[node] = copy;
    copy->left = CloneSubtree(node->left, sourceHead, head, copy, nodes);
    copy->right = CloneSubtree(node->right, sourceHead, head, copy, nodes);
    return copy;
}

template <class Tree>
Tree *CloneTree(const Tree *source, std::map<const void *, void *> *nodes) {
    typedef typename Tree::Node Node;
    Tree *tree = static_cast<Tree *>(UMemory::FastAlloc(sizeof(Tree), "STL"));
    memcpy(tree, source, sizeof(Tree));
    Node *head = static_cast<Node *>(UMemory::FastAlloc(sizeof(Node), "STL"));
    memcpy(head, source->head, sizeof(Node));
    (*nodes)[source->head] = head;
    tree->head = head;
    if (source->head->parent == source->head || source->head->parent->isNil) {
        head->parent = head->left = head->right = head;
    } else {
        head->parent = CloneSubtree(source->head->parent, source->head, head, head, nodes);
        Node *n = head->parent;
        while (!n->left->isNil)
            n = n->left;
        head->left = n;
        n = head->parent;
        while (!n->right->isNil)
            n = n->right;
        head->right = n;
    }
    return tree;
}

template <class Node>
void FreeSubtree(Node *node) {
    if (node->isNil)
        return;
    FreeSubtree(node->left);
    FreeSubtree(node->right);
    UMemory::FastFree(node, sizeof(Node));
}

template <class Tree>
void FreeTree(Tree *tree) {
    if (tree == NULL)
        return;
    if (tree->head != NULL) {
        FreeSubtree(tree->head->parent);
        UMemory::FastFree(tree->head, sizeof(typename Tree::Node));
    }
    UMemory::FastFree(tree, sizeof(Tree));
}

template <class Node>
void RemapSet(Node *node, const std::map<const void *, void *> &nodes) {
    if (node->isNil)
        return;
    auto found = nodes.find(node->value);
    if (found != nodes.end())
        node->value = static_cast<PhysicsObjNode *>(found->second);
    RemapSet(node->left, nodes);
    RemapSet(node->right, nodes);
}

template <class T>
void CloneVector(SimVector<T> *vector) {
    if (vector->first == NULL)
        return;
    uint32_t capacity = uint32_t(vector->end - vector->first), size = uint32_t(vector->last - vector->first);
    T **storage = static_cast<T **>(UMemory::FastAlloc(capacity * 4 > 0 ? capacity * 4 : 4, "STL"));
    memcpy(storage, vector->first, capacity * 4);
    vector->first = storage;
    vector->last = storage + size;
    vector->end = storage + capacity;
}

template <class T>
void FreeVector(SimVector<T> *vector) {
    if (vector->first != NULL)
        UMemory::FastFree(vector->first, uint32_t(vector->end - vector->first) * 4 > 0
                                             ? uint32_t(vector->end - vector->first) * 4 : 4);
    vector->first = vector->last = vector->end = NULL;
}

// The object vectors, each as a vector of PhysicsObject pointers (every kind derives from it first)
SimVector<PhysicsObject> *Lists(Simulation *s, int i) {
    void *lists[] = { &s->cars, &s->inactiveCars, &s->smackables, &s->humans, &s->missiles, &s->mines, &s->grenades,
                      &s->shells, &s->explosions, &s->newtons, &s->helicopters, &s->sentries };
    return static_cast<SimVector<PhysicsObject> *>(lists[i]);
}

// Our port called as the original is, with the arguments as raw words
template <class Fn, class Method>
Fn PortOf(Fn, Method method) {
    return reinterpret_cast<Fn>(XbeAddress(method));
}
const int kLists = 12;

Simulation *CloneSim(const Simulation *source) {
    Simulation *s = static_cast<Simulation *>(malloc(sizeof(Simulation)));
    memcpy(s, source, sizeof(Simulation));
    std::map<const void *, void *> nodes, unused;
    s->objects = source->objects != NULL ? CloneTree(source->objects, &nodes) : NULL;
    if (source->deadObjects != NULL) {
        s->deadObjects = CloneTree(source->deadObjects, &unused);
        RemapSet(s->deadObjects->head->parent, nodes);
    }
    for (int i = 0; i < kLists; i++)
        CloneVector(Lists(s, i));
    CloneVector(&s->trackedInstances);
    if (source->random != NULL) {
        s->random = static_cast<SimRandom *>(UMemory::FastAlloc(sizeof(SimRandom), "STL"));
        memcpy(s->random, source->random, sizeof(SimRandom));
    }
    return s;
}

void FreeSim(Simulation *s) {
    FreeTree(s->objects);
    FreeTree(s->deadObjects);
    for (int i = 0; i < kLists; i++)
        FreeVector(Lists(s, i));
    FreeVector(&s->trackedInstances);
    if (s->random != NULL)
        UMemory::FastFree(s->random, sizeof(SimRandom));
    free(s);
}

// ---- serialisation

void OutMapNodes(const PhysicsObjNode *node, std::map<const void *, uint32_t> *order) {
    if (node->isNil) {
        Out(0xffff0001);
        return;
    }
    OutMapNodes(node->left, order);
    uint32_t index = uint32_t(order->size());
    (*order)[node] = index;
    Out(node->value.key);
    Out(Canon(node->value.object));
    Out(node->color);
    OutMapNodes(node->right, order);
}

void OutSetNodes(const DeadObjectNode *node, const std::map<const void *, uint32_t> &order, const void *mapHead) {
    if (node->isNil) {
        Out(0xffff0002);
        return;
    }
    OutSetNodes(node->left, order, mapHead);
    auto found = order.find(node->value);
    Out(node->value == mapHead ? 0xeeee0000 : found != order.end() ? found->second : 0xdddd0000);
    Out(node->color);
    OutSetNodes(node->right, order, mapHead);
}

void OutSim(Simulation *s) {
    Out(uint32_t(s->numRigidBodies));
    Out(uint32_t(s->numSimpleRigidBodies));
    Out(s->useScratchPad);
    Out(uint32_t(s->savedSimState));
    Out(uint32_t(s->simState));
    Out(s->unknownE38);
    Out(uint32_t(s->stepsPerSecond));
    OutFloat(s->timeStep);
    Out(uint32_t(s->stepCount));
    Out(s->reflectiveObjectSig);
    if (s->random != NULL) {
        Out(s->random->state);
        Out(s->random->lastProduct);
        Out(s->random->seed);
        Out(s->random->counter);
    }
    std::map<const void *, uint32_t> order;
    if (s->objects != NULL) {
        Out(s->objects->size);
        OutMapNodes(s->objects->head->parent, &order);
        Out(s->objects->head->left == s->objects->head ? 0xaaaa : Canon(s->objects->head->left->value.object));
    }
    if (s->deadObjects != NULL) {
        Out(s->deadObjects->size);
        OutSetNodes(s->deadObjects->head->parent, order, s->objects != NULL ? s->objects->head : NULL);
    }
    for (int i = 0; i < kLists; i++) {
        SimVector<PhysicsObject> *list = Lists(s, i);
        Out(list->Size());
        Out(list->Capacity());
        for (uint32_t k = 0; k < list->Size(); k++)
            Out(Canon(list->first[k]));
    }
    Out(s->trackedInstances.Size());
    Out(s->trackedInstances.Capacity());
    for (uint32_t k = 0; k < s->trackedInstances.Size(); k++) {
        TrackedInstance *t = s->trackedInstances.first[k];
        if (g_known.count(t) != 0) {
            Out(uint32_t(uintptr_t(t)));
        } else {
            Out(0x7a000000);
            Out(uint32_t(t->unknown00));
            Out(uint32_t(uintptr_t(t->instance)));
        }
        Out(t->visible);
    }
}

// The tracked instances made in a case (TrackInstance's new) freed
void FreeFreshTracked(Simulation *s) {
    for (uint32_t k = 0; k < s->trackedInstances.Size(); k++)
        if (g_known.count(s->trackedInstances.first[k]) == 0)
            OperatorDelete(s->trackedInstances.first[k]);
}

// ---- hooks: a 5-byte jump at the callee's entry, or over our implementation's body when the entry is our jump
// (so that the port's direct calls reach the fake as well)

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

bool HookInstall(Hook *hook, uint32_t entry, const void *to) {
    hook->on = false;
    uint32_t at = JumpTarget(entry);
    if (at == 0)
        at = entry;
    const uint8_t *code = reinterpret_cast<const uint8_t *>(uintptr_t(at));
    // Too short to take a jump: a return and then the padding before the next function (a 0xc3 byte inside an
    // address or a jump's offset is not one)
    for (int i = 0; i < 5; i++)
        if (code[0] == 0xcc || (code[i] == 0xc3 && (code[i + 1] == 0xcc || code[i + 1] == 0x90)) ||
            (code[i] == 0xc2 && (code[i + 3] == 0xcc || code[i + 3] == 0x90)))
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

bool Install(std::initializer_list<std::pair<uint32_t, const void *>> fakes) {
    bool all = true;
    for (const auto &fake : fakes) {
        Hook hook;
        if (HookInstall(&hook, fake.first, fake.second))
            g_hooks.push_back(hook);
        else
            all = false;
    }
    return all;
}

void RemoveAll() {
    for (size_t i = g_hooks.size(); i-- > 0;)
        HookRemove(&g_hooks[i]);
    g_hooks.clear();
}

// ---- the fakes' answers (the same sequence for both sides)

std::vector<uint32_t> g_answers;
size_t g_answerAt;

uint32_t Answer() {
    return g_answerAt < g_answers.size() ? g_answers[g_answerAt++] : 0;
}

void SetAnswers(int count) {
    g_answers.clear();
    for (int i = 0; i < count; i++)
        g_answers.push_back(Next());
}

Simulation *g_copy;             // the side's copy (fakes act on it)

enum : uint32_t {
    kTagDelete = 0x44454c00,
    kTagDeleteDead,
    kTagUntrack,
    kTagDetect,
    kTagCanSpawnRigid,
    kTagStep,
    kTagCollideObject,
    kTagCollideWorld,
    kTagCollideGround,
    kTagDamping,
    kTagUpdate,
    kTagForces,
    kTagSimpleUpdate,
    kTagSimulate,
    kTagEventNew,
    kTagExplosionEvent,
    kTagConstruct,
};

void __fastcall FakeDelete(Simulation *s, int, PhysicsObject *object) {
    Out(kTagDelete);
    Out(s == &fgSimulation);
    Out(Canon(object));
    // what the delete does to the copy's newtons
    SimVector<PhysicsObject> &newtons = *Lists(g_copy, 9);
    if (newtons.Size() != 0 && newtons.first[0] == object) {
        for (PhysicsObject **p = newtons.first + 1; p != newtons.last; p++)
            p[-1] = *p;
        newtons.last--;
    }
}

void __fastcall FakeDeleteDead(Simulation *s, int) {
    Out(kTagDeleteDead);
    Out(s == g_copy);
}

void __fastcall FakeUntrack(Simulation *s, int) {
    Out(kTagUntrack);
    Out(s == g_copy);
}

void __fastcall FakeDetect(Simulation *s, int) {
    Out(kTagDetect);
    Out(s == g_copy);
}

bool __fastcall FakeCanSpawnRigid(Simulation *s, int, const Coord3 *position, bool force) {
    Out(kTagCanSpawnRigid);
    Out(s == g_copy);
    OutXyz(&position->x);
    Out(force);
    return (Answer() & 3) != 0;
}

int __fastcall FakeStep(WCollisionMgr *manager, int, Coord4 *segment, float step) {
    Out(kTagStep);
    Out(manager == fgCollisionMgr);
    OutXyz(&segment[0].x);
    OutFloat(segment[0].w);
    OutXyz(&segment[1].x);
    OutFloat(segment[1].w);
    OutFloat(step);
    return int(Answer() & 1);
}

void __fastcall FakeCollideObject(RigidBody *body, int, int index, int unknown) {
    Out(kTagCollideObject);
    Out(uint32_t(uintptr_t(body)));
    Out(uint32_t(index));
    Out(uint32_t(unknown));
}

void __fastcall FakeCollideWorld(RigidBody *body, int) {
    Out(kTagCollideWorld);
    Out(uint32_t(uintptr_t(body)));
}

void __fastcall FakeCollideGround(RigidBody *body, int) {
    Out(kTagCollideGround);
    Out(uint32_t(uintptr_t(body)));
}

void __fastcall FakeDamping(RigidBody *body, int) {
    Out(kTagDamping);
    Out(uint32_t(uintptr_t(body)));
}

void __fastcall FakeUpdate(RigidBody *body, int, float unused) {
    Out(kTagUpdate);
    Out(uint32_t(uintptr_t(body)));
    OutFloat(unused);
}

void __fastcall FakeForces(RigidBody *body, int) {
    Out(kTagForces);
    Out(uint32_t(uintptr_t(body)));
}

void __fastcall FakeSimpleUpdate(SimpleRigidBody *body, int) {
    Out(kTagSimpleUpdate);
    Out(uint32_t(uintptr_t(body)));
}

void __fastcall FakeSimulate(PhysicsObject *object, int) {
    Out(kTagSimulate);
    Out(Canon(object));
}

void __fastcall FakeTrap(PhysicsObject *object, int) {
    Out(0x54524150);
    Out(Canon(object));
}

void *g_fakeVtable[8] = {
    (void *)&FakeTrap, (void *)&FakeTrap, (void *)&FakeTrap, (void *)&FakeTrap,
    (void *)&FakeSimulate, (void *)&FakeTrap, (void *)&FakeTrap, (void *)&FakeTrap,
};

alignas(16) uint8_t g_eventBuffer[0x50];
bool g_eventFails;

void *FakeEventNew(size_t size) {
    Out(kTagEventNew);
    Out(uint32_t(size));
    return g_eventFails ? NULL : g_eventBuffer;
}

void *__fastcall FakeExplosionEvent(void *event, int, Coord4 position, Coord4 direction, int unknown9, uint32_t simple,
                                    int slot, PhysicsObject *source, uint32_t unknown1, uint32_t unknown2, float force,
                                    uint32_t unknown6, int a, int b, int c) {
    Out(kTagExplosionEvent);
    Out(event == g_eventBuffer);
    OutXyz(&position.x);
    OutFloat(position.w);
    OutXyz(&direction.x);
    OutFloat(direction.w);
    Out(uint32_t(unknown9));
    Out(simple & 0xff);                 // the byte the constructor reads
    Out(uint32_t(slot));
    Out(Canon(source));
    Out(unknown1);
    Out(unknown2);
    OutFloat(force);
    Out(unknown6);
    Out(uint32_t(a));
    Out(uint32_t(b));
    Out(uint32_t(c));
    return event;
}

// The objects the constructors make: a PhysicsObject of the case's type, cars on a live car's body
int32_t g_constructType;
int16_t g_constructSlot;
uint16_t g_constructFlags;

PhysicsObject *Made(void *memory) {
    Out(kTagConstruct);
    Out(memory != NULL);
    PhysicsObject *object = static_cast<PhysicsObject *>(memory);
    memset(object, 0, sizeof(PhysicsObject));
    object->vtable = g_fakeVtable;
    object->type = g_constructType;
    object->rigidBodySlot = g_constructSlot;
    object->flags = g_constructFlags;
    return object;
}

PhysicsObject *__fastcall FakeBondCar(void *memory, int, int carClass, const char *carType, uint32_t colour,
                                      const Coord3 *direction, const Coord3 *position) {
    Out(uint32_t(carClass));
    Out(uint32_t(uintptr_t(carType)));
    Out(colour);
    Out(uint32_t(uintptr_t(direction)));
    Out(uint32_t(uintptr_t(position)));
    return Made(memory);
}

PhysicsObject *__fastcall FakeSmackable(void *memory, int, RSceneObj *sceneObject, uint32_t a, uint32_t b,
                                        uint32_t c) {
    Out(uint32_t(uintptr_t(sceneObject)));
    Out(a);
    Out(b);
    Out(c);
    return Made(memory);
}

PhysicsObject *__fastcall FakeHuman(void *memory, int, int a, const Coord3 *b, const Coord3 *c, uint32_t d) {
    Out(uint32_t(a));
    Out(uint32_t(uintptr_t(b)));
    Out(uint32_t(uintptr_t(c)));
    Out(d);
    return Made(memory);
}

PhysicsObject *__fastcall FakeMissile(void *memory, int, int a, float b, const Coord3 *c, const Coord3 *d, uint32_t e,
                                      uint32_t f, uint32_t g, int h) {
    Out(uint32_t(a));
    OutFloat(b);
    Out(uint32_t(uintptr_t(c)));
    Out(uint32_t(uintptr_t(d)));
    Out(e);
    Out(f);
    Out(g);
    Out(uint32_t(h));
    return Made(memory);
}

PhysicsObject *__fastcall FakeMine(void *memory, int, int a, const Coord3 *b, const Coord3 *c, float d, uint32_t e) {
    Out(uint32_t(a));
    Out(uint32_t(uintptr_t(b)));
    Out(uint32_t(uintptr_t(c)));
    OutFloat(d);
    Out(e);
    return Made(memory);
}

PhysicsObject *__fastcall FakeGrenade(void *memory, int, uint32_t a, uint32_t b, const Coord3 *c, uint32_t d,
                                      uint32_t e) {
    Out(a);
    Out(b);
    Out(uint32_t(uintptr_t(c)));
    Out(d);
    Out(e);
    return Made(memory);
}

PhysicsObject *__fastcall FakeShell(void *memory, int, int a, uint32_t b, const Coord3 *c, uint32_t d, float e,
                                    float f, int g) {
    Out(uint32_t(a));
    Out(b);
    Out(uint32_t(uintptr_t(c)));
    Out(d);
    OutFloat(e);
    OutFloat(f);
    Out(uint32_t(g));
    return Made(memory);
}

PhysicsObject *__fastcall FakeExplosion(void *memory, int, uint32_t a, uint32_t b, const Coord3 *c, float d,
                                        uint32_t e, uint32_t f) {
    Out(a);
    Out(b);
    Out(uint32_t(uintptr_t(c)));
    OutFloat(d);
    Out(e);
    Out(f);
    return Made(memory);
}

PhysicsObject *__fastcall FakeNewton(void *memory, int, const Coord3 *a, const Coord3 *b, const Coord3 *c,
                                     const Coord3 *d, CARP::Instance *e, int f, float g, float h) {
    Out(uint32_t(uintptr_t(a)));
    Out(uint32_t(uintptr_t(b)));
    Out(uint32_t(uintptr_t(c)));
    Out(uint32_t(uintptr_t(d)));
    Out(uint32_t(uintptr_t(e)));
    Out(uint32_t(f));
    OutFloat(g);
    OutFloat(h);
    return Made(memory);
}

PhysicsObject *__fastcall FakeHelicopter(void *memory, int, const char *name, float speed, Coord3 direction,
                                         Coord3 position) {
    Out(uint32_t(uintptr_t(name)));
    OutFloat(speed);
    OutXyz(&direction.x);
    OutXyz(&position.x);
    return Made(memory);
}

PhysicsObject *__fastcall FakeSentry(void *memory, int, CARP::Instance *instance, uint32_t a2, uint32_t a3,
                                     uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7, uint32_t a8, uint32_t a9,
                                     uint32_t a10, uint32_t a11, uint32_t a12, uint32_t a13, uint32_t a14,
                                     uint32_t a15, uint32_t a16) {
    Out(uint32_t(uintptr_t(instance)));
    uint32_t words[] = { a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16 };
    for (uint32_t w : words)
        Out(w);
    return Made(memory);
}

// ---- running a case: each side on its own copy of `input`, with the package's entries swapped back for the
// original except `keep`

typedef void (*SideFn)(Simulation *copy);

const char *g_caseName = "";
long g_caseIndex;
std::vector<unsigned> g_keep;
bool g_freeFresh;               // free the objects the case made (their sizes by type)
void (*g_prepare)();            // puts back what a side changed outside the copies and the shared bytes

void Window(bool original) {
    for (unsigned at : kEntries) {
        bool kept = false;
        for (unsigned k : g_keep)
            kept = kept || k == at;
        if (!kept)
            XbeOriginal_Restore(at, original);
    }
}

bool RunSide(SideFn run, Simulation *copy, bool original) {
    g_original = original;
    g_copy = copy;
    g_answerAt = 0;
    long faults = g_faults;
    if (original)
        Window(true);
#ifdef _MSC_VER
    __try {
        run(copy);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (g_faults++ < 10)
            printf("[gamemissionsshadowb]   %s #%ld: the %s faulted\n", g_caseName, g_caseIndex,
                   original ? "original" : "port");
    }
#else
    run(copy);
#endif
    if (original)
        Window(false);
    ResetFpu();
    return g_faults != faults;
}

// The shared bytes a case may write: the live body pool, the collision manager's flag, the launch page's word
struct Shared {
    std::vector<uint8_t> pool;
    uint8_t collisionFlag;
    int32_t launchWord;
};

void CaptureShared(Shared *s) {
    uint8_t *pool = static_cast<uint8_t *>(fgSimulation.bodyPool);
    s->pool.assign(pool, pool + fgSimulation.bodyPoolSize);
    s->collisionFlag = fgCollisionMgr != NULL ? fgCollisionMgr->unknown30 : 0;
    s->launchWord = ShadowLaunch.unknown978;
}

void RestoreShared(const Shared &s) {
    memcpy(fgSimulation.bodyPool, s.pool.data(), s.pool.size());
    if (fgCollisionMgr != NULL)
        fgCollisionMgr->unknown30 = s.collisionFlag;
    ShadowLaunch.unknown978 = s.launchWord;
}

void FreeFreshObjects(Simulation *s);

void RunCase(const char *what, long index, SideFn run, const Simulation *input) {
    g_caseName = what;
    g_caseIndex = index;
    g_cases++;
    Shared before, afterOriginal, afterPort;
    CaptureShared(&before);
    g_stream[0].clear();
    g_stream[1].clear();

    Simulation *copy = CloneSim(input);
    g_fresh.clear();
    if (g_prepare != NULL)
        g_prepare();
    bool originalFaulted = RunSide(run, copy, true);
    g_original = true;
    if (!originalFaulted)
        OutSim(copy);
    CaptureShared(&afterOriginal);
    if (g_freeFresh)
        FreeFreshObjects(copy);
    FreeFreshTracked(copy);
    FreeSim(copy);
    RestoreShared(before);

    copy = CloneSim(input);
    g_fresh.clear();
    if (g_prepare != NULL)
        g_prepare();
    bool portFaulted = RunSide(run, copy, false);
    g_original = false;
    if (!portFaulted)
        OutSim(copy);
    CaptureShared(&afterPort);
    if (g_freeFresh)
        FreeFreshObjects(copy);
    FreeFreshTracked(copy);
    FreeSim(copy);
    RestoreShared(before);

    if (originalFaulted && portFaulted) {
        g_bothFaulted++;
        return;
    }
    if (originalFaulted != portFaulted) {
        Report(what, index, originalFaulted ? "only the original faulted" : "only the port faulted");
        return;
    }
    CompareStreams(what, index);
    CompareBytes(what, index, afterOriginal.pool.data(), afterPort.pool.data(), afterOriginal.pool.size());
    CompareBytes(what, index, &afterOriginal.collisionFlag, &afterPort.collisionFlag, 1);
    CompareBytes(what, index, &afterOriginal.launchWord, &afterPort.launchWord, 4);
}

// ---- inputs

std::vector<PhysicsObject *> g_liveObjects;
std::vector<PhysicsObject *> g_liveCars;        // rigid-bodied cars (for synthetic cars' bodies)

void CollectLive() {
    g_liveObjects.clear();
    g_liveCars.clear();
    PhysicsObjMap *map = fgSimulation.objects;
    for (PhysicsObjIterator it = { map->head->left }; it.node != map->head;) {
        PhysicsObject *object = static_cast<PhysicsObject *>(it.node->value.object);
        g_liveObjects.push_back(object);
        g_known.insert(object);
        if (object->type == kObjectVehicle && !(object->flags & PhysicsObject::kSimpleBody))
            g_liveCars.push_back(object);
        // the successor
        PhysicsObjNode *n = it.node;
        if (!n->right->isNil) {
            n = n->right;
            while (!n->left->isNil)
                n = n->left;
        } else {
            PhysicsObjNode *p = n->parent;
            while (!p->isNil && n == p->right) {
                n = p;
                p = p->parent;
            }
            n = p;
        }
        it.node = n;
    }
    for (uint32_t k = 0; k < fgSimulation.trackedInstances.Size(); k++)
        g_known.insert(fgSimulation.trackedInstances.first[k]);
}

// Synthetic objects (stable for the run)
const int kSynthetic = 64;
PhysicsObject g_synthetic[kSynthetic];
TrackedInstance g_syntheticTracked[32];
CARP::Instance g_syntheticInstances[32];

void MakeSynthetic() {
    for (int i = 0; i < kSynthetic; i++) {
        memset(&g_synthetic[i], 0, sizeof(PhysicsObject));
        g_synthetic[i].vtable = g_fakeVtable;
        g_synthetic[i].type = (i % 13);
        g_synthetic[i].flags = PhysicsObject::kSimpleBody;
        g_known.insert(&g_synthetic[i]);
    }
    for (int i = 0; i < 32; i++) {
        memset(&g_syntheticInstances[i], 0, sizeof(CARP::Instance));
        g_syntheticTracked[i].unknown00 = i;
        g_syntheticTracked[i].instance = &g_syntheticInstances[i];
        g_syntheticTracked[i].visible = false;
        g_known.insert(&g_syntheticTracked[i]);
    }
}

// A synthetic object as a car on a live car's body
void MakeCar(PhysicsObject *object) {
    object->type = kObjectVehicle;
    if (!g_liveCars.empty()) {
        PhysicsObject *car = g_liveCars[Range(0, int(g_liveCars.size()) - 1)];
        object->flags = car->flags;
        object->rigidBodySlot = car->rigidBodySlot;
    }
}

// The objects a case made (not live, not synthetic) freed, by the sizes of their types
void FreeFreshObjects(Simulation *s) {
    std::set<PhysicsObject *> fresh;
    for (int i = 0; i < kLists; i++)
        for (uint32_t k = 0; k < Lists(s, i)->Size(); k++)
            if (g_known.count(Lists(s, i)->first[k]) == 0)
                fresh.insert(Lists(s, i)->first[k]);
    for (PhysicsObject *object : fresh) {
        uint32_t size = 0;
        switch (object->type) {
        case kObjectVehicle: size = 0x420; break;
        case kObjectSmackable: size = 0x88; break;
        case kObjectExplosion: size = 0x98; break;
        case kObjectHuman: size = 0x90; break;
        case kObjectMine: size = 0x88; break;
        case kObjectMissile: size = 0x730; break;
        case kObjectShell: size = 0x90; break;
        case kObjectGrenade: size = 0x6e0; break;
        case kObjectHelicopter: size = 0x140; break;
        case kObjectSentry: size = 0xc8; break;
        case kObjectNewton: UMemory::Free(object); continue;
        }
        if (size != 0)
            UMemory::FastFree(object, size);
    }
}

// ---- A: vector reserve

struct ReserveInput {
    uint32_t size, capacity, count;
    std::vector<void *> contents;
} g_reserve;

void SideReserve(Simulation *) {
    SimPointerVector vector = {};
    vector.allocator = 0x5a;
    if (g_reserve.capacity != 0) {
        vector.first = static_cast<void **>(UMemory::FastAlloc(g_reserve.capacity * 4, "STL"));
        memcpy(vector.first, g_reserve.contents.data(), g_reserve.capacity * 4);
        vector.last = vector.first + g_reserve.size;
        vector.end = vector.first + g_reserve.capacity;
    }
    if (g_original)
        Orig_Reserve(&vector, 0, g_reserve.count);
    else
        vector.Reserve(g_reserve.count);
    Out(vector.allocator);
    Out(vector.first == NULL);
    uint32_t size = vector.first == NULL ? 0 : uint32_t(vector.last - vector.first);
    uint32_t capacity = vector.first == NULL ? 0 : uint32_t(vector.end - vector.first);
    Out(size);
    Out(capacity);
    for (uint32_t k = 0; k < size; k++)
        Out(uint32_t(uintptr_t(vector.first[k])));
    if (vector.first != NULL)
        UMemory::FastFree(vector.first, capacity * 4);
}

void TestReserve() {
    for (long i = 0; i < 200; i++) {
        g_reserve.capacity = Chance(15) ? 0 : uint32_t(Range(1, 120));
        g_reserve.size = g_reserve.capacity == 0 ? 0 : uint32_t(Range(0, int(g_reserve.capacity)));
        g_reserve.count = uint32_t(Chance(10) ? 0 : Range(0, 160));
        g_reserve.contents.assign(g_reserve.capacity + 1, NULL);
        for (auto &word : g_reserve.contents)
            word = reinterpret_cast<void *>(uintptr_t(Next()));
        RunCase("reserve", i, SideReserve, &fgSimulation);
    }
}

// ---- B: the dead set's insert_unique

PhysicsObjNode g_fakeEntries[48];
std::vector<int> g_insertSequence;

void SideInsertUnique(Simulation *) {
    DeadObjectSet set = {};
    set.head = reinterpret_cast<DeadObjectNode *>(StringSet::BuyHead());
    set.head->isNil = 1;
    set.head->parent = set.head->left = set.head->right = set.head;
    for (int index : g_insertSequence) {
        PhysicsObjNode *value = &g_fakeEntries[index];
        DeadObjectSet::InsertResult result = {};
        if (g_original)
            Orig_InsertUnique(&set, 0, &result, &value);
        else
            set.InsertUnique(&result, value);
        Out(result.inserted);
        Out(uint32_t(result.position.node->value - g_fakeEntries));
        Out(set.size);
    }
    std::map<const void *, uint32_t> order;
    for (int i = 0; i < 48; i++)
        order[&g_fakeEntries[i]] = uint32_t(i);
    OutSetNodes(set.head->parent, order, NULL);
    FreeSubtree(set.head->parent);
    UMemory::FastFree(set.head, sizeof(DeadObjectNode));
}

void TestInsertUnique() {
    for (long i = 0; i < 60; i++) {
        for (int k = 0; k < 48; k++) {
            memset(&g_fakeEntries[k], 0, sizeof(PhysicsObjNode));
            g_fakeEntries[k].value.object = reinterpret_cast<void *>(uintptr_t(Chance(20) ? (Next() & 7) * 16
                                                                                         : Next() << 4));
        }
        g_insertSequence.clear();
        int count = Range(1, 40);
        for (int k = 0; k < count; k++)
            g_insertSequence.push_back(Range(0, 47));
        RunCase("insert_unique", i, SideInsertUnique, &fgSimulation);
    }
}

// ---- C: deleting

std::vector<PhysicsObject *> g_deletes;
bool g_useThunk;

void SideDelete(Simulation *s) {
    for (PhysicsObject *object : g_deletes) {
        if (g_original)
            (g_useThunk ? Orig_DeleteThunk : Orig_Delete)(s, 0, object);
        else if (g_useThunk)
            s->DeletePhysicsObjectThunk(object);
        else
            s->DeletePhysicsObject(object);
    }
}

void TestDelete() {
    if (g_liveObjects.empty()) {
        g_skipped++;
        return;
    }
    long index = 0;
    for (int i = 0; i < int(g_liveObjects.size()) + 40; i++) {
        g_deletes.clear();
        int count = Range(1, 3);
        for (int k = 0; k < count; k++) {
            PhysicsObject *object = i < int(g_liveObjects.size()) && k == 0
                                        ? g_liveObjects[i]
                                        : g_liveObjects[Range(0, int(g_liveObjects.size()) - 1)];
            if (k > 0 && Chance(30))
                object = g_deletes[0];
            g_deletes.push_back(object);
        }
        if (Chance(10)) {       // an absent car (the cars' removal tests the end)
            PhysicsObject *car = &g_synthetic[Range(0, kSynthetic - 1)];
            MakeCar(car);
            g_deletes.push_back(car);
        }
        g_useThunk = Chance(30);
        RunCase("DeletePhysicsObject", index++, SideDelete, &fgSimulation);
    }
}

// ---- D: SpawnPhysicsObject

std::vector<PhysicsObject *> g_spawns;
std::vector<uint8_t> g_sleepStates;

void SideSpawnObject(Simulation *s) {
    for (size_t k = 0; k < g_spawns.size(); k++) {
        PhysicsObject *object = g_spawns[k];
        if (object->type == kObjectVehicle)
            fgSimulation.rigidBodies[object->rigidBodySlot].sleepState = g_sleepStates[k];
        if (g_original)
            Orig_SpawnObject(s, 0, object);
        else
            s->SpawnPhysicsObject(object);
    }
}

void TestSpawnObject() {
    for (long i = 0; i < 100; i++) {
        g_spawns.clear();
        g_sleepStates.clear();
        int count = Range(1, 4);
        for (int k = 0; k < count; k++) {
            PhysicsObject *object = &g_synthetic[Range(0, kSynthetic - 1)];
            object->type = Range(0, 12);
            object->flags = PhysicsObject::kSimpleBody;
            if (object->type == kObjectVehicle) {
                if (g_liveCars.empty())
                    object->type = kObjectSmackable;
                else
                    MakeCar(object);
            }
            g_spawns.push_back(object);
            g_sleepStates.push_back(uint8_t(Range(0, 3)));
        }
        RunCase("SpawnPhysicsObject", i, SideSpawnObject, &fgSimulation);
    }
}

// ---- E: CanSpawnSimpleRigidBody (the deletes faked: the original's call reaches our body through its jump)

struct CanSpawnInput {
    int32_t count;
    bool force;
    Coord3 position;
} g_canSpawn;

void SideCanSpawn(Simulation *s) {
    s->numSimpleRigidBodies = g_canSpawn.count;
    bool answer = g_original ? Orig_CanSpawnSimple(s, 0, &g_canSpawn.position, g_canSpawn.force)
                             : s->CanSpawnSimpleRigidBody(&g_canSpawn.position, g_canSpawn.force);
    Out(answer);
}

void TestCanSpawn() {
    if (!Install({ { kDeletePhysicsObject, (const void *)&FakeDelete } })) {
        RemoveAll();
        g_skipped++;
        return;
    }
    g_keep = { 0x000b4250, 0x000b4740 };
    Simulation *input = CloneSim(&fgSimulation);
    for (long i = 0; i < 100; i++) {
        static const int32_t counts[] = { 0, 50, 71, 72, 73, 80, 95, 96, 97, 120, -1, -100 };
        g_canSpawn.count = Chance(70) ? counts[Range(0, 11)] : Range(-5, 130);
        g_canSpawn.force = Chance(50);
        g_canSpawn.position = { Uniform(-100, 100), Uniform(-100, 100), Uniform(-100, 100) };
        FreeVector(&input->newtons);
        int newtons = Range(0, 6);
        if (newtons != 0 || Chance(50)) {
            SimVector<PhysicsObject> &list = *Lists(input, 9);
            list.first = static_cast<PhysicsObject **>(UMemory::FastAlloc(8 * 4, "STL"));
            list.last = list.first;
            list.end = list.first + 8;
            for (int k = 0; k < newtons; k++)
                *list.last++ = &g_synthetic[Range(0, kSynthetic - 1)];
        }
        RunCase("CanSpawnSimpleRigidBody", i, SideCanSpawn, input);
    }
    FreeSim(input);
    g_keep.clear();
    RemoveAll();
}

// ---- F, G: the tracked instances

CARP::Instance *g_trackWhat;
int g_trackUnknown;

void SideTrack(Simulation *s) {
    if (g_original)
        Orig_Track(s, 0, g_trackWhat, g_trackUnknown);
    else
        s->TrackInstance(g_trackWhat, g_trackUnknown);
}

void FillTracked(Simulation *input, int count) {
    FreeVector(&input->trackedInstances);
    if (count == 0 && Chance(50))
        return;
    int capacity = count + Range(0, 3);
    input->trackedInstances.first = static_cast<TrackedInstance **>(UMemory::FastAlloc(capacity * 4 > 0 ? capacity * 4 : 4, "STL"));
    input->trackedInstances.last = input->trackedInstances.first;
    input->trackedInstances.end = input->trackedInstances.first + capacity;
    for (int k = 0; k < count; k++)
        *input->trackedInstances.last++ = &g_syntheticTracked[Range(0, 31)];
}

void TestTrack() {
    Simulation *input = CloneSim(&fgSimulation);
    for (long i = 0; i < 60; i++) {
        FillTracked(input, Range(0, 8));
        g_trackWhat = Chance(50) && input->trackedInstances.Size() != 0
                          ? input->trackedInstances.first[Range(0, int(input->trackedInstances.Size()) - 1)]->instance
                          : &g_syntheticInstances[Range(0, 31)];
        g_trackUnknown = int(Next());
        RunCase("TrackInstance", i, SideTrack, input);
    }
    FreeSim(input);
}

void SideUpdateTracked(Simulation *s) {
    for (int k = 0; k < 3; k++) {
        if (g_original)
            Orig_UpdateTracked(s, 0);
        else
            s->UpdateTrackedInstanceVisibility();
    }
    for (int k = 0; k < 32; k++)
        Out(g_syntheticTracked[k].visible);
}

TrackedInstance g_savedTracked[32];

void PutTrackedBack() {
    memcpy(g_syntheticTracked, g_savedTracked, sizeof(g_savedTracked));
}

void TestUpdateTracked() {
    if (fgRenderHigh == NULL || fgRenderHigh->views[0].camera == NULL || fgCollisionMgr == NULL ||
        !Install({ { kStepCheckHitWorld, (const void *)&FakeStep } })) {
        RemoveAll();
        g_skipped++;
        return;
    }
    const float *eye = fgRenderHigh->views[0].camera->matrix.mtx[3];
    Simulation *input = CloneSim(&fgSimulation);
    for (long i = 0; i < 60; i++) {
        for (int k = 0; k < 32; k++) {
            float reach = Chance(20) ? 100.0f : Uniform(0.0f, 160.0f);
            g_syntheticInstances[k].position[0] = eye[0] + reach * Uniform(-1.0f, 1.0f);
            g_syntheticInstances[k].position[1] = eye[1] + Uniform(-5.0f, 5.0f);
            g_syntheticInstances[k].position[2] = eye[2] + reach * Uniform(-1.0f, 1.0f);
            g_syntheticTracked[k].visible = Chance(50);
        }
        FillTracked(input, Range(0, 6));
        SetAnswers(8);
        memcpy(g_savedTracked, g_syntheticTracked, sizeof(g_savedTracked));
        g_prepare = PutTrackedBack;
        RunCase("UpdateTrackedInstanceVisibility", i, SideUpdateTracked, input);
        g_prepare = NULL;
        PutTrackedBack();
    }
    FreeSim(input);
    RemoveAll();
}

// ---- H: SimulateGame

void SideSimulateGame(Simulation *s) {
    if (g_original)
        Orig_SimulateGame(s, 0);
    else
        s->SimulateGame();
}

PhysicsObjMap *MakeSyntheticMap(int count) {
    PhysicsObjMap *map = static_cast<PhysicsObjMap *>(UMemory::FastAlloc(sizeof(PhysicsObjMap), "STL"));
    memset(map, 0, sizeof(PhysicsObjMap));
    map->head = reinterpret_cast<PhysicsObjNode *>(MapBuyHead());
    map->head->isNil = 1;
    map->head->parent = map->head->left = map->head->right = map->head;
    for (int k = 0; k < count; k++) {
        PhysicsObject *object = &g_synthetic[Range(0, kSynthetic - 1)];
        SimObjectValue value = { uint32_t(Range(1, 11)), object };
        SimObjectInsert inserted;
        map->InsertMulti(&inserted, &value);
    }
    return map;
}

void TestSimulateGame() {
    if (fgCollisionMgr == NULL ||
        !Install({ { kCollideWithObject, (const void *)&FakeCollideObject },
                   { kCollideWithWorld, (const void *)&FakeCollideWorld },
                   { kCollideWithGround, (const void *)&FakeCollideGround },
                   { kAngularDamping, (const void *)&FakeDamping },
                   { kUpdatePosition, (const void *)&FakeUpdate },
                   { kInitialForces, (const void *)&FakeForces },
                   { kSimpleUpdatePosition, (const void *)&FakeSimpleUpdate },
                   { kDetectCollisionsSRB, (const void *)&FakeDetect } })) {
        RemoveAll();
        g_skipped++;
        return;
    }
    uint32_t poolSize = fgSimulation.bodyPoolSize;
    uint8_t *pools[2] = { static_cast<uint8_t *>(malloc(poolSize)), static_cast<uint8_t *>(malloc(poolSize)) };
    Shared live;
    CaptureShared(&live);
    for (long i = 0; i < 40; i++) {
        Simulation *input = CloneSim(&fgSimulation);
        for (uint8_t *pool : pools) {
            memcpy(pool, fgSimulation.bodyPool, poolSize);
            for (int b = 0; b < Simulation::kMaxRigidBodies; b++) {
                RigidBody *body = reinterpret_cast<RigidBody *>(pool) + b;
                if (Chance(40))
                    body->sleepState = uint8_t(Range(0, 3));
                if (body->info == NULL || Chance(10))
                    body->info = fgSimulation.rigidBodyInfo + (b % Simulation::kMaxRigidBodies);
            }
        }
        input->rigidBodies = reinterpret_cast<RigidBody *>(pools[0]);
        input->simpleRigidBodies = reinterpret_cast<SimpleRigidBody *>(pools[0] + 0x2000);
        input->rigidBodiesScratch = reinterpret_cast<RigidBody *>(pools[1]);
        input->simpleRigidBodiesScratch = reinterpret_cast<SimpleRigidBody *>(pools[1] + 0x2000);
        input->useScratchPad = Chance(30);
        input->simState = Chance(15) ? kSimPaused : Range(0, 2);
        for (int b = 0; b < Simulation::kMaxRigidBodies; b++)
            if (Chance(30))
                input->rigidBodyOwners[b] = Chance(50) ? NULL : &g_synthetic[b % kSynthetic];
        for (int b = 0; b < Simulation::kMaxSimpleBodies; b++)
            if (Chance(30))
                input->simpleRigidBodyOwners[b] = Chance(50) ? NULL : &g_synthetic[b % kSynthetic];
        // the live infos' flags (restored after the case)
        for (int b = 0; b < Simulation::kMaxRigidBodies; b++) {
            fgSimulation.rigidBodyInfo[b].unknown4fc = uint8_t(Chance(50) ? Range(0, 2) : 0);
            fgSimulation.rigidBodyInfo[b].unknown4ff = uint8_t(Chance(50) ? 1 : 0);
        }
        FreeTree(input->objects);
        input->objects = MakeSyntheticMap(Range(0, 12));
        FreeVector(&input->trackedInstances);     // UpdateTrackedInstanceVisibility returns at once
        RunCase("SimulateGame", i, SideSimulateGame, input);
        FreeSim(input);
        RestoreShared(live);
    }
    free(pools[0]);
    free(pools[1]);
    RemoveAll();
}

// ---- I, J: Reset and CleanUpObjects

void SideReset(Simulation *s) {
    if (g_original)
        Orig_Reset(s, 0);
    else
        s->Reset();
}

void SideCleanUp(Simulation *s) {
    if (g_original)
        Orig_CleanUp(s, 0);
    else
        s->CleanUpObjects();
}

void TestResetAndCleanUp() {
    if (!Install({ { kDeleteDeadObjects, (const void *)&FakeDeleteDead },
                   { kUntrackAll, (const void *)&FakeUntrack } })) {
        RemoveAll();
        g_skipped++;
        return;
    }
    for (long i = 0; i < 20; i++) {
        Simulation *input = CloneSim(&fgSimulation);
        FreeTree(input->objects);
        input->objects = MakeSyntheticMap(0);
        input->simState = Range(0, 3);
        input->savedSimState = Range(0, 3);
        input->stepCount = int32_t(Next());
        input->numRigidBodies = Range(0, 64);
        input->numSimpleRigidBodies = Range(0, 96);
        input->useScratchPad = Chance(50);
        input->random->state = Next();
        input->random->counter = Next();
        ShadowLaunch.unknown978 = Range(0, 4);
        RunCase("Reset", i, SideReset, input);
        FreeSim(input);
    }
    for (long i = 0; i < 10; i++)
        RunCase("CleanUpObjects", i, SideCleanUp, &fgSimulation);
    RemoveAll();
}

// ---- K: SpawnExplosion

struct ExplosionInput {
    uint32_t unknown1, unknown2, unknown6, unused8;
    Coord3 direction, position;
    float force;
    PhysicsObject *source;
    int32_t count;
} g_explosion;

void SideSpawnExplosion(Simulation *s) {
    s->numSimpleRigidBodies = g_explosion.count;
    const ExplosionInput &e = g_explosion;
    if (g_original)
        Orig_SpawnExplosion(s, 0, e.unknown1, e.unknown2, &e.direction, &e.position, e.force, e.unknown6, e.source,
                            e.unused8);
    else
        PortOf(Orig_SpawnExplosion, &Simulation::SpawnExplosion)(s, 0, e.unknown1, e.unknown2, &e.direction,
                                                                 &e.position, e.force, e.unknown6, e.source,
                                                                 e.unused8);
}

void TestSpawnExplosion() {
    if (!Install({ { kEventNew, (const void *)&FakeEventNew },
                   { kESpawnExplosion, (const void *)&FakeExplosionEvent } })) {
        RemoveAll();
        g_skipped++;
        return;
    }
    Simulation *input = CloneSim(&fgSimulation);
    FreeVector(&input->newtons);
    for (long i = 0; i < 60; i++) {
        static const float forces[] = { 0.0f, 499.0f, 500.0f, 500.001f, 2000.0f, -1.0f };
        static const int32_t counts[] = { 10, 72, 73, 95, 96, 97 };
        g_explosion = { Next(), Next(), Next(), Next(),
                        { Uniform(-1, 1), Uniform(-1, 1), Uniform(-1, 1) },
                        { Uniform(-500, 500), Uniform(-50, 50), Uniform(-500, 500) },
                        Chance(70) ? forces[Range(0, 5)] : Uniform(0, 1000),
                        NULL,
                        counts[Range(0, 5)] };
        if (Chance(60)) {
            PhysicsObject *source = Chance(50) && !g_liveObjects.empty()
                                        ? g_liveObjects[Range(0, int(g_liveObjects.size()) - 1)]
                                        : &g_synthetic[Range(0, kSynthetic - 1)];
            if (g_known.count(source) && source >= g_synthetic && source < g_synthetic + kSynthetic) {
                source->flags = uint16_t(Next());
                source->rigidBodySlot = int16_t(Next());
            }
            g_explosion.source = source;
        }
        if (Chance(5))
            g_explosion.force = -NAN;
        g_eventFails = Chance(10);
        RunCase("SpawnExplosion", i, SideSpawnExplosion, input);
    }
    FreeSim(input);
    RemoveAll();
}

// ---- L: the Spawn*Object

int g_spawnKind;
uint32_t g_args[16];
float g_floats[4];
Coord3 g_vectors[4];
CARP::Instance g_sentryInstance;
RSceneObj *g_sceneObject;
const char g_name[] = "shadow";
int32_t g_simpleCount;

void SideSpawn(Simulation *s) {
    s->numSimpleRigidBodies = g_simpleCount;
    PhysicsObject *made = NULL;
    const uint32_t *a = g_args;
    const Coord3 *v = g_vectors;
    bool o = g_original;
    switch (g_spawnKind) {
    case 0:
        made = o ? Orig_SpawnCar(s, 0, int(a[0]), g_name, a[2], &v[0], &v[1])
                 : s->SpawnCarObject(int(a[0]), g_name, a[2], &v[0], &v[1]);
        break;
    case 1:
        made = o ? Orig_SpawnSmackable(s, 0, g_sceneObject, a[1], a[2], a[3])
                 : PortOf(Orig_SpawnSmackable, &Simulation::SpawnSmackableObject)(s, 0, g_sceneObject, a[1], a[2],
                                                                                  a[3]);
        break;
    case 2:
        made = o ? Orig_SpawnHuman(s, 0, int(a[0]), &v[0], &v[1], a[3]) : s->SpawnHumanObject(int(a[0]), &v[0], &v[1], a[3]);
        break;
    case 3:
        made = o ? Orig_SpawnMissile(s, 0, int(a[0]), g_floats[0], &v[0], &v[1], a[4], a[5], a[6], int(a[7]))
                 : PortOf(Orig_SpawnMissile, &Simulation::SpawnMissileObject)(s, 0, int(a[0]), g_floats[0], &v[0],
                                                                              &v[1], a[4], a[5], a[6], int(a[7]));
        break;
    case 4:
        made = o ? Orig_SpawnMine(s, 0, int(a[0]), &v[0], &v[1], g_floats[0], a[4])
                 : s->SpawnMineObject(int(a[0]), &v[0], &v[1], g_floats[0], a[4]);
        break;
    case 5:
        made = o ? Orig_SpawnGrenade(s, 0, a[0], a[1], &v[1], a[3], a[4])
                 : PortOf(Orig_SpawnGrenade, &Simulation::SpawnGrenadeObject)(s, 0, a[0], a[1], &v[1], a[3], a[4]);
        break;
    case 6:
        made = o ? Orig_SpawnShell(s, 0, int(a[0]), a[1], &v[1], a[3], g_floats[0], g_floats[1], int(a[6]))
                 : PortOf(Orig_SpawnShell, &Simulation::SpawnShellObject)(s, 0, int(a[0]), a[1], &v[1], a[3],
                                                                          g_floats[0], g_floats[1], int(a[6]));
        break;
    case 7:
        made = o ? Orig_SpawnExplosionObject(s, 0, a[0], a[1], a[2], &v[1], g_floats[0], a[5], a[6])
                 : PortOf(Orig_SpawnExplosionObject, &Simulation::SpawnExplosionObject)(s, 0, a[0], a[1], a[2], &v[1],
                                                                                        g_floats[0], a[5], a[6]);
        break;
    case 8:
        made = o ? Orig_SpawnNewton(s, 0, &v[0], &v[1], &v[2], &v[3], &g_sentryInstance, int(a[5]), g_floats[0], g_floats[1])
                 : s->SpawnNewtonObject(&v[0], &v[1], &v[2], &v[3], &g_sentryInstance, int(a[5]), g_floats[0], g_floats[1]);
        break;
    case 9:
        made = o ? Orig_SpawnHelicopter(s, 0, g_name, &v[0], &v[1], g_floats[0])
                 : s->SpawnHelicopterObject(g_name, &v[0], &v[1], g_floats[0]);
        break;
    case 10:
        made = o ? Orig_SpawnSentry(s, 0, &g_sentryInstance, a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9],
                                    a[10], a[11], a[12], a[13], a[14], a[15])
                 : PortOf(Orig_SpawnSentry, &Simulation::SpawnSentryObject)(s, 0, &g_sentryInstance, a[1], a[2], a[3],
                                                                            a[4], a[5], a[6], a[7], a[8], a[9], a[10],
                                                                            a[11], a[12], a[13], a[14], a[15]);
        break;
    }
    Out(Canon(made));
}

void TestSpawns() {
    if (!Install({ { kCanSpawnRigidBody, (const void *)&FakeCanSpawnRigid },
                   { kBondCarConstruct, (const void *)&FakeBondCar },
                   { kSmackableConstruct, (const void *)&FakeSmackable },
                   { kHumanConstruct, (const void *)&FakeHuman },
                   { kMissileConstruct, (const void *)&FakeMissile },
                   { kMineConstruct, (const void *)&FakeMine },
                   { kGrenadeConstruct, (const void *)&FakeGrenade },
                   { kShellConstruct, (const void *)&FakeShell },
                   { kExplosionConstruct, (const void *)&FakeExplosion },
                   { kNewtonConstruct, (const void *)&FakeNewton },
                   { kHelicopterConstruct, (const void *)&FakeHelicopter },
                   { kSentryConstruct, (const void *)&FakeSentry } })) {
        RemoveAll();
        g_skipped++;
        return;
    }
    static const int32_t kTypes[] = { kObjectVehicle, kObjectSmackable, kObjectHuman, kObjectMissile, kObjectMine,
                                      kObjectGrenade, kObjectShell, kObjectExplosion, kObjectNewton,
                                      kObjectHelicopter, kObjectSentry };
    g_sceneObject = !g_liveCars.empty() ? g_liveCars[0]->renderObject : NULL;
    Simulation *input = CloneSim(&fgSimulation);
    FreeVector(&input->newtons);
    g_freeFresh = true;
    long index = 0;
    for (int kind = 0; kind < 11; kind++) {
        if ((kind == 0 && g_liveCars.empty()) || (kind == 1 && g_sceneObject == NULL)) {
            g_skipped++;
            continue;
        }
        for (int i = 0; i < 20; i++) {
            g_spawnKind = kind;
            for (uint32_t &word : g_args)
                word = Next();
            if (Chance(50))
                g_args[0] = uint32_t(Range(0, 2));
            // owner signatures: the player's car's, another live body's, a stale one, none
            uint32_t sig = 0;
            if (!g_liveObjects.empty() && Chance(70)) {
                PhysicsObject *owner = Chance(50) ? fgSimulation.cars.first[0]
                                                  : g_liveObjects[Range(0, int(g_liveObjects.size()) - 1)];
                bool simple = (owner->flags & PhysicsObject::kSimpleBody) != 0;
                sig = simple ? fgSimulation.simpleRigidBodySigs[owner->rigidBodySlot & 0x7f]
                             : fgSimulation.rigidBodySigs[owner->rigidBodySlot & 0x3f];
                if (Chance(15))
                    sig ^= 0x10;
            }
            g_args[4] = sig;
            g_floats[0] = Chance(30) ? 500.0f : Uniform(0, 1000);
            g_floats[1] = Uniform(0, 10);
            for (Coord3 &v : g_vectors)
                v = { Uniform(-300, 300), Uniform(-30, 30), Uniform(-300, 300) };
            for (int k = 0; k < 3; k++)
                g_sentryInstance.position[k] = Uniform(-300, 300);
            static const int32_t counts[] = { 10, 72, 73, 95, 96, 97 };
            g_simpleCount = counts[Range(0, 5)];
            g_constructType = kTypes[kind];
            g_constructFlags = PhysicsObject::kSimpleBody;
            g_constructSlot = 0;
            if (kind == 0) {
                PhysicsObject *car = g_liveCars[Range(0, int(g_liveCars.size()) - 1)];
                g_constructFlags = car->flags;
                g_constructSlot = car->rigidBodySlot;
            }
            SetAnswers(4);
            RunCase("Spawn*Object", index++, SideSpawn, input);
        }
    }
    g_freeFresh = false;
    FreeSim(input);
    RemoveAll();
}

// ---- M: the constructor and destructor on blocks of our own

void OutBuilt(Simulation *s) {
    OutSim(s);
    Out(s->bodyPoolSize);
    Out(s->scratchPadSize);
    uint8_t *pool = static_cast<uint8_t *>(s->bodyPool);
    Out(uint32_t(reinterpret_cast<uint8_t *>(s->rigidBodies) - pool));
    Out(uint32_t(reinterpret_cast<uint8_t *>(s->simpleRigidBodies) - pool));
    Out(uint32_t(reinterpret_cast<uint8_t *>(s->rigidBodyInfo) - pool));
    Out(uint32_t(uintptr_t(s->rigidBodiesScratch)));
    Out(uint32_t(uintptr_t(s->simpleRigidBodiesScratch)));
    uint32_t sum = 0;
    for (uint32_t k = 0; k < s->bodyPoolSize; k++)
        sum = sum * 31 + pool[k];
    Out(sum);
    const uint32_t *words = reinterpret_cast<const uint32_t *>(s->rigidBodySigs);
    for (size_t k = 0; k < (0xe30 - 0x30) / 4; k++)
        Out(words[k]);
    Out(s->objects->head->isNil);
    Out(s->objects->head->color);
    Out(s->objects->head->parent == s->objects->head);
    Out(s->objects->head->left == s->objects->head);
    Out(s->objects->head->right == s->objects->head);
    Out(s->deadObjects->head->isNil);
    Out(s->deadObjects->head->color);
    Out(s->deadObjects->head->parent == s->deadObjects->head);
    Out(s->deadObjects->head->left == s->deadObjects->head);
    Out(s->deadObjects->head->right == s->deadObjects->head);
}

void OutDestroyed(Simulation *s) {
    for (int i = 0; i < kLists; i++) {
        Out(uint32_t(uintptr_t(Lists(s, i)->first)));
        Out(uint32_t(uintptr_t(Lists(s, i)->last)));
        Out(uint32_t(uintptr_t(Lists(s, i)->end)));
        Out(Lists(s, i)->allocator);
    }
    Out(uint32_t(uintptr_t(s->trackedInstances.first)));
}

void TestConstruct() {
    for (long i = 0; i < 3; i++) {
        g_caseName = "Construct/Destruct";
        g_caseIndex = i;
        g_cases++;
        g_stream[0].clear();
        g_stream[1].clear();
        uint8_t fill = uint8_t(Next());
        bool faulted[2] = {};
        for (int side = 0; side < 2; side++) {
            Simulation *s = static_cast<Simulation *>(malloc(sizeof(Simulation)));
            memset(s, fill, sizeof(Simulation));
            s->trackedInstances.first = s->trackedInstances.last = s->trackedInstances.end = NULL;
            g_original = side == 0;
            static Simulation *block;
            block = s;
            struct Local {
                static void Build(Simulation *) {
                    if (g_original)
                        Orig_Construct(block, 0);
                    else
                        block->Construct();
                    OutBuilt(block);
                    // the destructor's UntrackAllInstances walks the tracked instances: none
                    block->trackedInstances.first = block->trackedInstances.last = block->trackedInstances.end = NULL;
                    if (g_original)
                        Orig_Destruct(block, 0);
                    else
                        block->Destruct();
                    OutDestroyed(block);
                }
            };
            faulted[side] = RunSide(Local::Build, s, side == 0);
            free(s);
        }
        if (faulted[0] || faulted[1]) {
            if (faulted[0] != faulted[1])
                Report(g_caseName, i, faulted[0] ? "only the original faulted" : "only the port faulted");
            else
                g_bothFaulted++;
            continue;
        }
        CompareStreams(g_caseName, i);
    }
}

}  // namespace

void GameMissionsShadowB_Run(void) {
    char value[16] = "";
    DWORD length = GetEnvironmentVariableA("NIGHTFIRE_GAMEMISSIONSSHADOWB", value, sizeof(value));
    if (length == 0 || length >= sizeof(value) || atoi(value) == 0)
        return;
    if (fgSimulation.objects == NULL || fgSimulation.deadObjects == NULL || fgSimulation.bodyPool == NULL) {
        printf("[gamemissionsshadowb] no Simulation yet - skipped\n");
        fflush(stdout);
        return;
    }
    FpControlGet(&g_x87, &g_sse);
    Shared live;
    CaptureShared(&live);
    MakeSynthetic();
    CollectLive();

    TestReserve();
    TestInsertUnique();
    TestDelete();
    TestSpawnObject();
    TestCanSpawn();
    TestTrack();
    TestUpdateTracked();
    TestSimulateGame();
    TestResetAndCleanUp();
    TestSpawnExplosion();
    TestSpawns();
    TestConstruct();

    RestoreShared(live);
    RemoveAll();
    ResetFpu();
    printf("[gamemissionsshadowb] Simulation part 2 (reserve, dead set insert, delete, spawns, tracked instances, "
           "SimulateGame, Reset, construction) vs originals: %ld cases, %ld checks, %ld differ%s\n",
           g_cases, g_checks, g_differ, g_faults != 0 ? " (with faults)" : "");
    if (g_faults != 0 || g_skipped != 0)
        printf("[gamemissionsshadowb]   %ld calls faulted (%ld cases on both sides, not compared); %ld groups or "
               "kinds skipped (fakes not installable or no live data)\n", g_faults, g_bothFaulted, g_skipped);
    fflush(stdout);
}
