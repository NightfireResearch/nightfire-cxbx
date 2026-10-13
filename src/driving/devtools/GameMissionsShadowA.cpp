#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "GameMissionsShadowA.h"
#include "OwnedGlobals.h"
#include "FpControl.h"

#include "../camera/PlayerCamera.h"
#include "../data/AttributeContainers.h"    // StringSet::BuyHead
#include "../engine/UMemory.hpp"
#include "../game/SimulationObjects.h"   // NextSignature
#include "../physics/PhysicsObject.h"
#include "../physics/RigidBody.h"
#include "../physics/SimpleRigidBody.h"
#include "../render/RenderHigh.h"
#include "../world/SoundMap.h"              // MapBuyHead
#include "../../common/xbeOriginal.h"
#include "../../helpers.h"

#include <windows.h>
#include <float.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <initializer_list>
#include <vector>

// ---------------------------------------------------------------------------------------------------------------
// NIGHTFIRE_GAMEMISSIONSSHADOWA=1, from the first simulation tick: the GM_A package (0x000b25e0-0x000b4080, the
// Simulation's smaller methods and its containers' code) against the originals, the package's range swapped back
// for each original run. Each case runs both sides from one state and compares everything the call writes:
//
//   - On a copy of the live Simulation (both sides at the same address, so pointers compare as they are), with
//     its fields perturbed: PauseSimState, UnPauseSimState, AdvanceStep, AssignRigidBodySlot (the signature
//     counter perturbed, its wrap included), the body getters, FindSignature, FindPhysicsObjectSignature,
//     GetScratchPadFreeZone, GetOrderedBody over every index, GetTrackedInstanceList, SetReflectiveObject,
//     GetPlayerObject, GetReflectiveObjectPosition; ReleaseRigidBodySlot and SetStartConditions on synthetic
//     body tables (the real callees run on them); DetectCollisionsSRB on a copy of the simple bodies;
//     DetonateRemoteMines and CanSpawnRigidBody on synthetic mines and smackables (Smackable::GoToSleep a
//     recording fake); UntrackInstance and UntrackAllInstances on synthetic tracked lists; DeleteDeadObjects on
//     a synthetic object map and dead set (the objects' deleting destructor a fake vtable's, RPlayerCamera::
//     EndMissileCamera a recording fake, the camera's anchor lent a synthetic missile).
//   - The trees, each side building its own from the same keys and compared node by node (keys, colours and
//     links as in-order indices): the map's _Insert, erase, erase(first, last), _Erase, ++, equal_range and
//     _Distance; the set's _Insert, erase, erase(first, last), _Erase, --, and both rotations.
//   - The vectors: insert_n (growth and both middle paths, values inside the vector included), erase,
//     copy_backward, remove_copy, remove and uninitialized_fill_n on arrays of made-up pointers.
//
// Mutations this catches: AssignRigidBodySlot restarting the signature counter at 0 rather than 0x100 (the
// counter and the new signature differ in the wrap cases); CanSpawnRigidBody testing numRigidBodies >= limit
// (the GoToSleep log differs whenever the count equals the limit).
// ---------------------------------------------------------------------------------------------------------------

namespace {

void OriginalWindow(bool original) {
    XbeOriginal_RestoreRange(0x000b25e0, 0x000b4080, original);
}

#define NextSignatureGlobal U32_AT(0x001c8fbc)  // the game's copy of NextSignature

// ---- results

int g_cases = 0, g_checks = 0, g_differ = 0, g_details = 0, g_faults = 0;
unsigned int g_x87 = 0, g_sse = 0;

void Differ(const char *what, int index, const char *detail) {
    g_differ++;
    if (g_details++ < 10)
        printf("[gmissionsA]   %s #%d: %s\n", what, index, detail);
}

void CheckBytes(const char *what, int index, const void *a, const void *b, size_t bytes) {
    g_checks++;
    size_t at = OwnedGlobals_FirstDifference(a, b, bytes);   // a vtable or callback: ours, or the game's
    if (at == bytes)
        return;
    const uint8_t *x = static_cast<const uint8_t *>(a), *y = static_cast<const uint8_t *>(b);
    char detail[96];
    snprintf(detail, sizeof(detail), "byte %u of %u: original %02x, port %02x", unsigned(at), unsigned(bytes), x[at],
             y[at]);
    Differ(what, index, detail);
}

void CheckU32(const char *what, int index, uint32_t a, uint32_t b) {
    g_checks++;
    if (a == b)
        return;
    char detail[64];
    snprintf(detail, sizeof(detail), "original %08x, port %08x", a, b);
    Differ(what, index, detail);
}

void CheckWords(const char *what, int index, const std::vector<uint32_t> &a, const std::vector<uint32_t> &b) {
    g_checks++;
    char detail[96];
    if (a.size() != b.size()) {
        snprintf(detail, sizeof(detail), "%u words, port %u", unsigned(a.size()), unsigned(b.size()));
        Differ(what, index, detail);
        return;
    }
    for (size_t i = 0; i < a.size(); i++) {
        if (a[i] != b[i]) {
            snprintf(detail, sizeof(detail), "word %u: original %08x, port %08x", unsigned(i), a[i], b[i]);
            Differ(what, index, detail);
            return;
        }
    }
}

void ResetFpu() {
    _fpreset();
    FpControlSetX87(g_x87);
    FpControlSetSse(g_sse);
}

typedef void (*CaseFn)(void *context, bool original);

bool Guarded(CaseFn run, void *context, bool original) {
    if (original)
        OriginalWindow(true);
    bool ok = true;
#ifdef _MSC_VER
    __try {
        run(context, original);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ResetFpu();
        g_faults++;
        ok = false;
    }
#else
    run(context, original);
#endif
    if (original)
        OriginalWindow(false);
    return ok;
}

template <class F>
void Thunk(void *context, bool original) {
    (*static_cast<F *>(context))(original);
}

// Runs f(true) (the original) then f(false) (the port); the caller sets up and compares around each
template <class F>
bool RunSide(F &f, bool original) {
    return Guarded(&Thunk<F>, &f, original);
}

// ---- random inputs (a generator of our own)

uint32_t g_seed = 0x5a17a11u;

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

bool Chance(int in) {
    return Next() % uint32_t(in) == 0;
}

// ---- jumps to the fakes

struct Hook {
    uint32_t at;
    uint8_t saved[5];
    bool on;
};

bool HookInstall(Hook *h, uint32_t at, const void *to) {
    h->at = at;
    h->on = false;
    DWORD old;
    if (!VirtualProtect((void *)uintptr_t(at), 5, PAGE_EXECUTE_READWRITE, &old))
        return false;
    memcpy(h->saved, (void *)uintptr_t(at), 5);
    uint8_t jump[5] = { 0xe9 };
    int32_t rel = int32_t(uint32_t(uintptr_t(to)) - (at + 5));
    memcpy(jump + 1, &rel, 4);
    memcpy((void *)uintptr_t(at), jump, 5);
    VirtualProtect((void *)uintptr_t(at), 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)uintptr_t(at), 5);
    h->on = true;
    return true;
}

void HookRemove(Hook *h) {
    if (!h->on)
        return;
    DWORD old;
    VirtualProtect((void *)uintptr_t(h->at), 5, PAGE_EXECUTE_READWRITE, &old);
    memcpy((void *)uintptr_t(h->at), h->saved, 5);
    VirtualProtect((void *)uintptr_t(h->at), 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)uintptr_t(h->at), 5);
    h->on = false;
}

const void *JumpTarget(uint32_t at) {
    const uint8_t *code = reinterpret_cast<const uint8_t *>(uintptr_t(at));
    if (code[0] != 0xe9)
        return NULL;
    int32_t relative;
    memcpy(&relative, code + 1, 4);
    return reinterpret_cast<const void *>(uintptr_t(at + 5 + relative));
}

// A callee faked for both sides: the jump at its address, and at its port's entry where it has one
struct Fake {
    uint32_t address;
    const void *fake;
    Hook entry, port;
};

void FakeInstall(Fake *f) {
    const void *port = JumpTarget(f->address);
    HookInstall(&f->entry, f->address, f->fake);
    f->port.on = false;
    if (port != NULL)
        HookInstall(&f->port, uint32_t(uintptr_t(port)), f->fake);
}

void FakeRemove(Fake *f) {
    HookRemove(&f->port);
    HookRemove(&f->entry);
}

// ---- the call log

std::vector<uint32_t> *g_log = NULL;

void Log(std::initializer_list<uint32_t> words) {
    if (g_log != NULL)
        g_log->insert(g_log->end(), words.begin(), words.end());
}

uint32_t Word(const void *p) {
    return uint32_t(uintptr_t(p));
}

void __fastcall FakeGoToSleep(PhysicsObject *self, int) {
    Log({ 1, Word(self) });
}

void __fastcall FakeEndMissileCamera(void *self, int) {
    Log({ 2, Word(self) });
}

// ---- the originals

typedef void (__fastcall *SimVoidFn)(Simulation *, int);
typedef int (__fastcall *AssignFn)(Simulation *, int, PhysicsObject *, int);
typedef void *(__fastcall *GetBodyFn)(Simulation *, int, int);
typedef int (__fastcall *FindSigFn)(Simulation *, int, uint32_t, bool *);
typedef PhysicsObject *(__fastcall *FindObjFn)(Simulation *, int, uint32_t);
typedef void *(*ScratchFn)(void);
typedef void *(__fastcall *SimPtrFn)(Simulation *, int);
typedef void (__fastcall *SimU32Fn)(Simulation *, int, uint32_t);
typedef void (__fastcall *ReleaseFn)(Simulation *, int, int, int);
typedef void (__fastcall *StartFn)(Simulation *, int, const Coord3 *, const Coord3 *);
typedef uint32_t (__fastcall *CanSpawnFn)(Simulation *, int, const Coord3 *, int);
typedef void (__fastcall *UntrackFn)(Simulation *, int, CARP::Instance *);

typedef void (__fastcall *MapIncFn)(PhysicsObjNode **, int);
typedef void (__fastcall *MapSubtreeFn)(PhysicsObjMap *, int, PhysicsObjNode *);
typedef void (*DistanceFn)(PhysicsObjNode *, PhysicsObjNode *, uint32_t *);
typedef void *(__fastcall *EqualRangeFn)(PhysicsObjMap *, int, PhysicsObjMap::Range *, const uint32_t *);
typedef PhysicsObjNode **(__fastcall *MapInsertFn)(PhysicsObjMap *, int, PhysicsObjNode **, int, PhysicsObjNode *,
                                                    const SimObjectValue *);
typedef PhysicsObjNode **(__fastcall *MapEraseFn)(PhysicsObjMap *, int, PhysicsObjNode **, PhysicsObjNode *);
typedef PhysicsObjNode **(__fastcall *MapRangeFn)(PhysicsObjMap *, int, PhysicsObjNode **, PhysicsObjNode *,
                                                   PhysicsObjNode *);
typedef void (__fastcall *SetRotateFn)(DeadObjectSet *, int, DeadObjectNode *);
typedef void (__fastcall *SetDecFn)(DeadObjectNode **, int);
typedef void (__fastcall *SetSubtreeFn)(DeadObjectSet *, int, DeadObjectNode *);
typedef DeadObjectNode **(__fastcall *SetEraseFn)(DeadObjectSet *, int, DeadObjectNode **, DeadObjectNode *);
typedef DeadObjectNode **(__fastcall *SetInsertFn)(DeadObjectSet *, int, DeadObjectNode **, int, DeadObjectNode *,
                                                    PhysicsObjNode *const *);
typedef DeadObjectNode **(__fastcall *SetRangeFn)(DeadObjectSet *, int, DeadObjectNode **, DeadObjectNode *,
                                                   DeadObjectNode *);

typedef void ***(*CopyBackFn)(void ***, void **, void **, void **);
typedef void ***(*RemoveCopyFn)(void ***, void **, void **, void **, void *const *);
typedef void (*FillNFn)(void **, uint32_t, void *const *);
typedef void ***(*RemoveFn)(void ***, void **, void **, void *const *);
typedef void ***(__fastcall *VecEraseFn)(SimPointerVector *, int, void ***, void **);
typedef void (__fastcall *InsertNFn)(SimPointerVector *, int, void **, uint32_t, void *const *);

// =============================================================================================================
// The trees
// =============================================================================================================

// A tree as words: the size, the head's links, then each node in order - its value's words, colour, isNil and
// links - every link as an in-order index (the head 0xffffffff)
template <class Node, class ValueWords>
std::vector<uint32_t> Serialize(const RbTree<Node> *tree, ValueWords valueWords) {
    std::vector<Node *> order;
    Node *head = tree->head;
    // in order, by an explicit walk (no tree code of the package's)
    std::vector<Node *> stack;
    Node *node = head->parent;
    size_t guard = 0;
    while ((!node->isNil || !stack.empty()) && guard++ < 100000) {
        while (!node->isNil) {
            stack.push_back(node);
            node = node->left;
        }
        node = stack.back();
        stack.pop_back();
        order.push_back(node);
        node = node->right;
    }
    auto index = [&](Node *n) -> uint32_t {
        if (n == head)
            return 0xffffffffu;
        for (size_t i = 0; i < order.size(); i++)
            if (order[i] == n)
                return uint32_t(i);
        return 0xeeeeeeeeu;
    };
    std::vector<uint32_t> out;
    out.push_back(tree->size);
    out.push_back(uint32_t(order.size()));
    out.push_back(index(head->parent));
    out.push_back(index(head->left));
    out.push_back(index(head->right));
    out.push_back(head->isNil);
    for (Node *n : order) {
        valueWords(n, out);
        out.push_back(n->color);
        out.push_back(n->isNil);
        out.push_back(index(n->left));
        out.push_back(index(n->parent));
        out.push_back(index(n->right));
    }
    return out;
}

template <class Node>
uint32_t InOrderIndex(const RbTree<Node> *tree, Node *target) {
    if (target == tree->head)
        return 0xffffffffu;
    std::vector<Node *> stack;
    Node *node = tree->head->parent;
    uint32_t i = 0;
    while (!node->isNil || !stack.empty()) {
        while (!node->isNil) {
            stack.push_back(node);
            node = node->left;
        }
        node = stack.back();
        stack.pop_back();
        if (node == target)
            return i;
        i++;
        node = node->right;
    }
    return 0xeeeeeeeeu;
}

template <class Node>
Node *NodeAt(const RbTree<Node> *tree, uint32_t at) {
    if (at == 0xffffffffu)
        return tree->head;
    std::vector<Node *> stack;
    Node *node = tree->head->parent;
    uint32_t i = 0;
    while (!node->isNil || !stack.empty()) {
        while (!node->isNil) {
            stack.push_back(node);
            node = node->left;
        }
        node = stack.back();
        stack.pop_back();
        if (i++ == at)
            return node;
        node = node->right;
    }
    return tree->head;
}

void MapWords(PhysicsObjNode *n, std::vector<uint32_t> &out) {
    out.push_back(n->value.key);
    out.push_back(Word(n->value.object));
}

void SetWords(DeadObjectNode *n, std::vector<uint32_t> &out) {
    out.push_back(Word(n->value));
}

// ---- the map

PhysicsObjMap *NewMap() {
    PhysicsObjMap *map = static_cast<PhysicsObjMap *>(calloc(1, sizeof(PhysicsObjMap)));
    PhysicsObjNode *head = reinterpret_cast<PhysicsObjNode *>(MapBuyHead());
    head->isNil = 1;
    head->parent = head;
    head->left = head;
    head->right = head;
    map->head = head;
    map->size = 0;
    return map;
}

void FreeMap(PhysicsObjMap *map) {
    PhysicsObjIterator ignored;
    map->EraseRange(&ignored, PhysicsObjIterator{ map->head->left }, PhysicsObjIterator{ map->head });
    UMemory::FastFree(map->head, sizeof(PhysicsObjNode));
    free(map);
}

// insert_multi's descent, then the side's _Insert
void MapInsert(PhysicsObjMap *map, const SimObjectValue &value, bool original) {
    PhysicsObjNode *where = map->head;
    bool addLeft = true;
    for (PhysicsObjNode *node = map->head->parent; !node->isNil; node = addLeft ? node->left : node->right) {
        where = node;
        addLeft = value.key < node->value.key;
    }
    PhysicsObjNode *inserted;
    if (original)
        ((MapInsertFn)0x000b31b0)(map, 0, &inserted, addLeft, where, &value);
    else
        map->InsertAt(&inserted, addLeft, where, &value);
}

std::vector<SimObjectValue> RandomMapValues(int count) {
    std::vector<SimObjectValue> values;
    for (int i = 0; i < count; i++) {
        SimObjectValue v;
        v.key = uint32_t(Range(1, 11));
        v.object = reinterpret_cast<void *>(uintptr_t(0x10000000u + uint32_t(i) * 0x10u));
        values.push_back(v);
    }
    return values;
}

void TestMap() {
    for (int round = 0; round < 40; round++) {
        int count = Range(0, 60);
        std::vector<SimObjectValue> values = RandomMapValues(count);
        PhysicsObjMap *maps[2] = { NewMap(), NewMap() };
        for (int side = 0; side < 2; side++) {
            auto build = [&](bool original) {
                for (const SimObjectValue &v : values)
                    MapInsert(maps[side], v, original);
            };
            RunSide(build, side == 0);
        }
        g_cases++;
        CheckWords("map _Insert", round, Serialize(maps[0], MapWords), Serialize(maps[1], MapWords));

        // ++ from every node and the head
        for (uint32_t at = 0; at <= uint32_t(count); at++) {
            uint32_t i = at == uint32_t(count) ? 0xffffffffu : at;
            PhysicsObjNode *a = NodeAt(maps[0], i), *b = NodeAt(maps[0], i);
            OriginalWindow(true);
            ((MapIncFn)0x000b2920)(&a, 0);
            OriginalWindow(false);
            PhysicsObjIterator it = { b };
            it.Inc();
            CheckU32("map ++", round, InOrderIndex(maps[0], a), InOrderIndex(maps[0], it.node));
        }

        // equal_range and _Distance
        for (uint32_t key = 0; key <= 12; key++) {
            PhysicsObjMap::Range ra = {}, rb = {};
            OriginalWindow(true);
            ((EqualRangeFn)0x000b2ef0)(maps[0], 0, &ra, &key);
            OriginalWindow(false);
            maps[0]->EqualRange(&rb, &key);
            CheckU32("map equal_range first", round, InOrderIndex(maps[0], ra.first.node),
                     InOrderIndex(maps[0], rb.first.node));
            CheckU32("map equal_range last", round, InOrderIndex(maps[0], ra.last.node),
                     InOrderIndex(maps[0], rb.last.node));
            uint32_t ca = key, cb = key;
            OriginalWindow(true);
            ((DistanceFn)0x000b2e00)(ra.first.node, ra.last.node, &ca);
            OriginalWindow(false);
            PhysicsObjDistance(rb.first, rb.last, &cb);
            CheckU32("map _Distance", round, ca, cb);
        }

        // erase(iterator) and erase(first, last), the same places on both
        int erasures = count == 0 ? 0 : Range(1, 6);
        for (int e = 0; e < erasures && maps[0]->size > 0; e++) {
            uint32_t at = uint32_t(Range(0, int(maps[0]->size) - 1));
            uint32_t next[2];
            for (int side = 0; side < 2; side++) {
                PhysicsObjMap *map = maps[side];
                PhysicsObjNode *where = NodeAt(map, at), *result = NULL;
                auto erase = [&](bool original) {
                    if (original) {
                        ((MapEraseFn)0x000b3660)(map, 0, &result, where);
                    } else {
                        PhysicsObjIterator r;
                        map->Erase(&r, PhysicsObjIterator{ where });
                        result = r.node;
                    }
                };
                RunSide(erase, side == 0);
                next[side] = InOrderIndex(map, result);
            }
            g_cases++;
            CheckU32("map erase's next", round, next[0], next[1]);
            CheckWords("map erase", round, Serialize(maps[0], MapWords), Serialize(maps[1], MapWords));
        }
        if (maps[0]->size > 0) {
            bool whole = Chance(3);
            uint32_t lo = whole ? 0 : uint32_t(Range(0, int(maps[0]->size) - 1));
            uint32_t hi = whole ? 0xffffffffu : uint32_t(Range(int(lo), int(maps[0]->size)));
            if (hi == maps[0]->size)
                hi = 0xffffffffu;
            uint32_t next[2];
            for (int side = 0; side < 2; side++) {
                PhysicsObjMap *map = maps[side];
                PhysicsObjNode *first = NodeAt(map, lo), *last = NodeAt(map, hi), *result = NULL;
                auto range = [&](bool original) {
                    if (original) {
                        ((MapRangeFn)0x000b3e40)(map, 0, &result, first, last);
                    } else {
                        PhysicsObjIterator r;
                        map->EraseRange(&r, PhysicsObjIterator{ first }, PhysicsObjIterator{ last });
                        result = r.node;
                    }
                };
                RunSide(range, side == 0);
                next[side] = InOrderIndex(map, result);
            }
            g_cases++;
            CheckU32("map erase(first, last)'s end", round, next[0], next[1]);
            CheckWords("map erase(first, last)", round, Serialize(maps[0], MapWords), Serialize(maps[1], MapWords));
        }
        // _Erase of the root's right subtree (the tree is left broken, then the head reset by hand)
        for (int side = 0; side < 2; side++) {
            PhysicsObjMap *map = maps[side];
            auto subtree = [&](bool original) {
                PhysicsObjNode *root = map->head->parent;
                if (root->isNil)
                    return;
                if (original)
                    ((MapSubtreeFn)0x000b2b10)(map, 0, root->right);
                else
                    map->EraseSubtree(root->right);
                root->right = map->head;
                map->head->right = root;
            };
            RunSide(subtree, side == 0);
        }
        // what is left is the root and its left subtree (the sizes stale on both)
        g_cases++;
        CheckWords("map _Erase", round, Serialize(maps[0], MapWords), Serialize(maps[1], MapWords));
        for (PhysicsObjMap *map : maps) {
            // count what is left so the free walks a consistent tree
            uint32_t left = 0;
            for (PhysicsObjNode *n = map->head->left; n != map->head;) {
                left++;
                PhysicsObjIterator it = { n };
                it.Inc();
                n = it.node;
            }
            map->size = left;
            FreeMap(map);
        }
    }
}

// ---- the set

DeadObjectSet *NewSet() {
    DeadObjectSet *set = static_cast<DeadObjectSet *>(calloc(1, sizeof(DeadObjectSet)));
    DeadObjectNode *head = reinterpret_cast<DeadObjectNode *>(StringSet::BuyHead());
    head->isNil = 1;
    head->parent = head;
    head->left = head;
    head->right = head;
    set->head = head;
    set->size = 0;
    return set;
}

void FreeSet(DeadObjectSet *set) {
    DeadObjectSet::Iterator ignored;
    set->EraseRange(&ignored, DeadObjectSet::Iterator{ set->head->left }, DeadObjectSet::Iterator{ set->head });
    UMemory::FastFree(set->head, sizeof(DeadObjectNode));
    free(set);
}

// insert_unique's descent (the values are unique), then the side's _Insert
void SetInsert(DeadObjectSet *set, PhysicsObjNode *value, bool original) {
    DeadObjectNode *where = set->head;
    bool addLeft = true;
    for (DeadObjectNode *node = set->head->parent; !node->isNil; node = addLeft ? node->left : node->right) {
        where = node;
        addLeft = Word(value) < Word(node->value);
    }
    DeadObjectNode *inserted;
    if (original) {
        ((SetInsertFn)0x000b39a0)(set, 0, &inserted, addLeft, where, &value);
    } else {
        DeadObjectSet::Iterator r;
        set->Insert(&r, addLeft, where, value);
    }
}

std::vector<PhysicsObjNode *> RandomSetValues(int count) {
    std::vector<PhysicsObjNode *> values;
    while (int(values.size()) < count) {
        PhysicsObjNode *v = reinterpret_cast<PhysicsObjNode *>(uintptr_t(0x20000000u + (Next() & 0xfff) * 0x18u));
        bool present = false;
        for (PhysicsObjNode *w : values)
            present = present || w == v;
        if (!present)
            values.push_back(v);
    }
    return values;
}

void TestSet() {
    for (int round = 0; round < 40; round++) {
        int count = Range(0, 60);
        std::vector<PhysicsObjNode *> values = RandomSetValues(count);
        DeadObjectSet *sets[2] = { NewSet(), NewSet() };
        for (int side = 0; side < 2; side++) {
            auto build = [&](bool original) {
                for (PhysicsObjNode *v : values)
                    SetInsert(sets[side], v, original);
            };
            RunSide(build, side == 0);
        }
        g_cases++;
        CheckWords("set _Insert", round, Serialize(sets[0], SetWords), Serialize(sets[1], SetWords));

        // -- from every node and the head
        for (uint32_t at = 0; at <= uint32_t(count); at++) {
            uint32_t i = at == uint32_t(count) ? 0xffffffffu : at;
            DeadObjectNode *a = NodeAt(sets[0], i);
            DeadObjectSet::Iterator it = { a };
            OriginalWindow(true);
            ((SetDecFn)0x000b2ab0)(&a, 0);
            OriginalWindow(false);
            it.Dec();
            CheckU32("set --", round, InOrderIndex(sets[0], a), InOrderIndex(sets[0], it.node));
        }

        int erasures = count == 0 ? 0 : Range(1, 6);
        for (int e = 0; e < erasures && sets[0]->size > 0; e++) {
            uint32_t at = uint32_t(Range(0, int(sets[0]->size) - 1));
            uint32_t next[2];
            for (int side = 0; side < 2; side++) {
                DeadObjectSet *set = sets[side];
                DeadObjectNode *where = NodeAt(set, at), *result = NULL;
                auto erase = [&](bool original) {
                    if (original) {
                        ((SetEraseFn)0x000b3390)(set, 0, &result, where);
                    } else {
                        DeadObjectSet::Iterator r;
                        set->Erase(&r, DeadObjectSet::Iterator{ where });
                        result = r.node;
                    }
                };
                RunSide(erase, side == 0);
                next[side] = InOrderIndex(set, result);
            }
            g_cases++;
            CheckU32("set erase's next", round, next[0], next[1]);
            CheckWords("set erase", round, Serialize(sets[0], SetWords), Serialize(sets[1], SetWords));
        }
        if (sets[0]->size > 0) {
            bool whole = Chance(3);
            uint32_t lo = whole ? 0 : uint32_t(Range(0, int(sets[0]->size) - 1));
            uint32_t hi = whole ? 0xffffffffu : uint32_t(Range(int(lo), int(sets[0]->size)));
            if (hi == sets[0]->size)
                hi = 0xffffffffu;
            uint32_t next[2];
            for (int side = 0; side < 2; side++) {
                DeadObjectSet *set = sets[side];
                DeadObjectNode *first = NodeAt(set, lo), *last = NodeAt(set, hi), *result = NULL;
                auto range = [&](bool original) {
                    if (original) {
                        ((SetRangeFn)0x000b3f00)(set, 0, &result, first, last);
                    } else {
                        DeadObjectSet::Iterator r;
                        set->EraseRange(&r, DeadObjectSet::Iterator{ first }, DeadObjectSet::Iterator{ last });
                        result = r.node;
                    }
                };
                RunSide(range, side == 0);
                next[side] = InOrderIndex(set, result);
            }
            g_cases++;
            CheckU32("set erase(first, last)'s end", round, next[0], next[1]);
            CheckWords("set erase(first, last)", round, Serialize(sets[0], SetWords), Serialize(sets[1], SetWords));
        }
        // the rotations at one node (the order is kept; only the shape changes)
        if (sets[0]->size > 0) {
            uint32_t at = uint32_t(Range(0, int(sets[0]->size) - 1));
            bool left = Chance(2);
            for (int side = 0; side < 2; side++) {
                DeadObjectSet *set = sets[side];
                DeadObjectNode *node = NodeAt(set, at);
                if ((left ? node->right : node->left)->isNil)
                    continue;
                auto rotate = [&](bool original) {
                    if (original)
                        ((SetRotateFn)(left ? 0x000b29f0 : 0x000b2a50))(set, 0, node);
                    else if (left)
                        set->Lrotate(node);
                    else
                        set->Rrotate(node);
                };
                RunSide(rotate, side == 0);
            }
            g_cases++;
            CheckWords(left ? "set Lrotate" : "set Rrotate", round, Serialize(sets[0], SetWords),
                       Serialize(sets[1], SetWords));
        }
        // _Erase of the whole tree
        for (int side = 0; side < 2; side++) {
            DeadObjectSet *set = sets[side];
            auto subtree = [&](bool original) {
                if (original)
                    ((SetSubtreeFn)0x000b2dc0)(set, 0, set->head->parent);
                else
                    set->EraseSubtree(set->head->parent);
                set->head->parent = set->head;
                set->head->left = set->head;
                set->head->right = set->head;
                set->size = 0;
            };
            RunSide(subtree, side == 0);
        }
        g_cases++;
        CheckWords("set _Erase", round, Serialize(sets[0], SetWords), Serialize(sets[1], SetWords));
        FreeSet(sets[0]);
        FreeSet(sets[1]);
    }
}

// =============================================================================================================
// The vectors
// =============================================================================================================

void *FakePointer(uint32_t i) {
    return reinterpret_cast<void *>(uintptr_t(0x30000000u + i * 4u));
}

std::vector<uint32_t> VectorWords(const SimPointerVector *v) {
    std::vector<uint32_t> out;
    uint32_t size = v->first == NULL ? 0 : uint32_t(v->last - v->first);
    uint32_t capacity = v->first == NULL ? 0 : uint32_t(v->end - v->first);
    out.push_back(size);
    out.push_back(capacity);
    for (uint32_t i = 0; i < size; i++)
        out.push_back(Word(v->first[i]));
    return out;
}

void NewVector(SimPointerVector *v, uint32_t size, uint32_t capacity) {
    memset(v, 0, sizeof(*v));
    if (capacity == 0)
        return;
    v->first = static_cast<void **>(UMemory::FastAlloc(capacity * 4, "STL"));
    for (uint32_t i = 0; i < size; i++)
        v->first[i] = FakePointer(i);
    v->last = v->first + size;
    v->end = v->first + capacity;
}

void FreeVector(SimPointerVector *v) {
    if (v->first != NULL)
        UMemory::FastFree(v->first, uint32_t(v->end - v->first) * 4);
    memset(v, 0, sizeof(*v));
}

void TestVectors() {
    for (int round = 0; round < 200; round++) {
        uint32_t capacity = Chance(6) ? 0 : uint32_t(Range(1, 40));
        uint32_t size = capacity == 0 ? 0 : uint32_t(Range(0, int(capacity)));
        uint32_t where = uint32_t(Range(0, int(size)));
        uint32_t count = uint32_t(Range(0, 12));
        bool inside = size > 0 && Chance(3);
        uint32_t insideAt = inside ? uint32_t(Range(0, int(size) - 1)) : 0;
        void *outside = FakePointer(0x777);
        SimPointerVector vectors[2];
        for (int side = 0; side < 2; side++) {
            SimPointerVector *v = &vectors[side];
            NewVector(v, size, capacity);
            auto insert = [&](bool original) {
                void **at = v->first == NULL ? NULL : v->first + where;
                void *const *value = inside ? v->first + insideAt : &outside;
                if (original)
                    ((InsertNFn)0x000b3b80)(v, 0, at, count, value);
                else
                    v->InsertN(at, count, value);
            };
            RunSide(insert, side == 0);
        }
        g_cases++;
        CheckWords("vector insert_n", round, VectorWords(&vectors[0]), VectorWords(&vectors[1]));

        // erase(iterator)
        uint32_t newSize = vectors[0].first == NULL ? 0 : uint32_t(vectors[0].last - vectors[0].first);
        if (newSize > 0) {
            uint32_t at = uint32_t(Range(0, int(newSize) - 1));
            uint32_t results[2];
            for (int side = 0; side < 2; side++) {
                SimPointerVector *v = &vectors[side];
                void **result = NULL;
                auto erase = [&](bool original) {
                    if (original)
                        ((VecEraseFn)0x000b2fc0)(v, 0, &result, v->first + at);
                    else
                        v->Erase(&result, v->first + at);
                };
                RunSide(erase, side == 0);
                results[side] = uint32_t(result - v->first);
            }
            g_cases++;
            CheckU32("vector erase's result", round, results[0], results[1]);
            CheckWords("vector erase", round, VectorWords(&vectors[0]), VectorWords(&vectors[1]));
        }
        FreeVector(&vectors[0]);
        FreeVector(&vectors[1]);
    }

    // the algorithms on plain arrays, values repeated so remove has work
    for (int round = 0; round < 200; round++) {
        uint32_t n = uint32_t(Range(0, 24));
        void *source[32], *arrays[2][40];
        for (uint32_t i = 0; i < n; i++)
            source[i] = FakePointer(uint32_t(Range(0, 4)));
        void *value = FakePointer(uint32_t(Range(0, 5)));
        uint32_t lo = uint32_t(Range(0, int(n))), hi = uint32_t(Range(int(lo), int(n)));
        uint32_t fillCount = uint32_t(Range(0, 8));
        uint32_t results[2];
        const char *names[4] = { "copy_backward", "remove_copy", "remove", "uninitialized_fill_n" };
        int which = round % 4;
        for (int side = 0; side < 2; side++) {
            void **a = arrays[side];
            for (int i = 0; i < 40; i++)
                a[i] = FakePointer(0x100 + uint32_t(i));
            memcpy(a + 8, source, n * sizeof(void *));
            void **result = NULL;
            auto run = [&](bool original) {
                switch (which) {
                case 0:
                    if (original)
                        ((CopyBackFn)0x000b2e40)(&result, a + 8 + lo, a + 8 + hi, a + 40);
                    else
                        PointerCopyBackwardTo(&result, a + 8 + lo, a + 8 + hi, a + 40);
                    break;
                case 1:
                    if (original)
                        ((RemoveCopyFn)0x000b2e70)(&result, a + 8 + lo, a + 8 + hi, a, &value);
                    else
                        PointerRemoveCopy(&result, a + 8 + lo, a + 8 + hi, a, &value);
                    break;
                case 2:
                    if (original)
                        ((RemoveFn)0x000b2f60)(&result, a + 8 + lo, a + 8 + hi, &value);
                    else
                        PointerRemove(&result, a + 8 + lo, a + 8 + hi, &value);
                    break;
                default:
                    if (original)
                        ((FillNFn)0x000b2ec0)(a + 2, fillCount, &value);
                    else
                        PointerUninitializedFillN(a + 2, fillCount, &value);
                    result = a;
                    break;
                }
            };
            RunSide(run, side == 0);
            results[side] = uint32_t(result - a);
        }
        g_cases++;
        CheckU32(names[which], round, results[0], results[1]);
        CheckBytes(names[which], round, arrays[0], arrays[1], sizeof(arrays[0]));
    }
}

// =============================================================================================================
// The Simulation's methods
// =============================================================================================================

Simulation *g_copy = NULL;
uint8_t g_state[sizeof(Simulation)];

void SaveCopy() {
    memcpy(g_state, g_copy, sizeof(Simulation));
}

void LoadCopy() {
    memcpy(g_copy, g_state, sizeof(Simulation));
}

void FreshCopy() {
    memcpy(g_copy, &fgSimulation, sizeof(Simulation));
}

void NoSetup() {
}

void NoCapture(std::vector<uint32_t> &) {
}

// Runs `run` on both sides from the saved copy (and what `setup` prepares); compares the copy, the signature
// counter, the call log and what `capture` adds
template <class Setup, class Run, class Capture>
void SimCase(const char *what, int index, Setup setup, Run run, Capture capture, uint32_t nextSignature) {
    std::vector<uint32_t> results[2];
    std::vector<uint8_t> copies[2];
    uint32_t savedSignature = NextSignatureGlobal, savedPortSignature = NextSignature;
    for (int side = 0; side < 2; side++) {
        LoadCopy();
        setup();
        NextSignatureGlobal = NextSignature = nextSignature;
        std::vector<uint32_t> log;
        g_log = &log;
        auto body = [&](bool original) { run(original); };
        RunSide(body, side == 0);
        g_log = NULL;
        results[side] = log;
        capture(results[side]);
        results[side].push_back(side == 0 ? NextSignatureGlobal : NextSignature);
        copies[side].assign(reinterpret_cast<uint8_t *>(g_copy), reinterpret_cast<uint8_t *>(g_copy) + sizeof(Simulation));
    }
    NextSignatureGlobal = savedSignature;
    NextSignature = savedPortSignature;
    g_cases++;
    CheckWords(what, index, results[0], results[1]);
    CheckBytes(what, index, copies[0].data(), copies[1].data(), sizeof(Simulation));
}

void AppendBytes(std::vector<uint32_t> &out, const void *data, size_t bytes) {
    const uint8_t *p = static_cast<const uint8_t *>(data);
    for (size_t i = 0; i + 4 <= bytes; i += 4) {
        uint32_t w;
        memcpy(&w, p + i, 4);
        out.push_back(w);
    }
}

void TestStateAndLookups() {
    for (int round = 0; round < 60; round++) {
        FreshCopy();
        g_copy->simState = Range(0, 4);
        g_copy->savedSimState = Range(0, 4);
        g_copy->stepCount = int32_t(Next());
        g_copy->useScratchPad = Chance(2);
        SaveCopy();
        uint32_t signature = NextSignatureGlobal;
        SimCase("PauseSimState", round, NoSetup,
                [](bool original) { original ? ((SimVoidFn)0x000b25e0)(g_copy, 0) : g_copy->PauseSimState(); },
                NoCapture, signature);
        SimCase("UnPauseSimState", round, NoSetup,
                [](bool original) { original ? ((SimVoidFn)0x000b2600)(g_copy, 0) : g_copy->UnPauseSimState(); },
                NoCapture, signature);
        SimCase("AdvanceStep", round, NoSetup,
                [](bool original) { original ? ((SimVoidFn)0x000b2620)(g_copy, 0) : g_copy->AdvanceStep(); },
                NoCapture, signature);

        // the getters
        int slot = Range(0, 95);
        uint32_t r[2][3];
        for (int side = 0; side < 2; side++) {
            auto get = [&](bool original) {
                if (original) {
                    r[side][0] = Word(((GetBodyFn)0x000b2700)(g_copy, 0, slot));
                    r[side][1] = Word(((GetBodyFn)0x000b2730)(g_copy, 0, slot));
                    r[side][2] = Word(((GetBodyFn)0x000b2760)(g_copy, 0, slot));
                } else {
                    r[side][0] = Word(g_copy->GetRigidBody(slot));
                    r[side][1] = Word(g_copy->GetSimpleRigidBody(slot));
                    r[side][2] = Word(g_copy->GetRigidBodyInfo(slot));
                }
            };
            RunSide(get, side == 0);
        }
        g_cases++;
        CheckBytes("body getters", round, r[0], r[1], sizeof(r[0]));

        // signatures: live ones, perturbed ones, zero
        for (int k = 0; k < 8; k++) {
            uint32_t sig;
            int pick = Range(0, 3);
            int s = Range(0, 95);
            if (pick == 0)
                sig = g_copy->rigidBodySigs[s % 64];
            else if (pick == 1)
                sig = g_copy->simpleRigidBodySigs[s];
            else if (pick == 2)
                sig = Next() ^ (Next() << 8);
            else
                sig = 0;
            if (Chance(4))
                sig ^= 1u << Range(0, 31);
            uint32_t f[2][4];
            for (int side = 0; side < 2; side++) {
                auto find = [&](bool original) {
                    bool simple;
                    uint8_t *raw = reinterpret_cast<uint8_t *>(&simple);
                    *raw = 0xcc;    // a value the call must overwrite
                    if (original) {
                        f[side][0] = uint32_t(((FindSigFn)0x000b2780)(g_copy, 0, sig, &simple));
                        f[side][2] = Word(((FindObjFn)0x000b27d0)(g_copy, 0, sig));
                    } else {
                        f[side][0] = uint32_t(g_copy->FindSignature(sig, &simple));
                        f[side][2] = Word(g_copy->FindPhysicsObjectSignature(sig));
                    }
                    f[side][1] = *raw;
                    f[side][3] = 0;
                };
                RunSide(find, side == 0);
            }
            g_cases++;
            CheckBytes("FindSignature", round * 8 + k, f[0], f[1], sizeof(f[0]));
        }

        // the rest of the plain lookups
        std::vector<uint32_t> l[2];
        for (int side = 0; side < 2; side++) {
            auto look = [&](bool original) {
                for (uint32_t i = 0; i < 160; i++)
                    l[side].push_back(Word(original ? ((FindObjFn)0x000b28d0)(g_copy, 0, i) : g_copy->GetOrderedBody(i)));
                l[side].push_back(Word(original ? ((ScratchFn)0x000b2820)() : Simulation::GetScratchPadFreeZone()));
                l[side].push_back(Word(original ? ((SimPtrFn)0x000b2900)(g_copy, 0) : g_copy->GetTrackedInstanceList()));
                l[side].push_back(Word(original ? ((SimPtrFn)0x000b2d30)(g_copy, 0) : g_copy->GetPlayerObject()));
            };
            RunSide(look, side == 0);
        }
        g_cases++;
        CheckWords("GetOrderedBody, GetScratchPadFreeZone, GetTrackedInstanceList, GetPlayerObject", round, l[0], l[1]);

        // the reflective object: none (the player's), a live signature, a made-up one
        uint32_t reflective = Chance(3) ? 0
                              : Chance(2) ? g_copy->rigidBodySigs[Range(0, 63)]
                                          : (Next() & 0xff7fffffu) | (Chance(2) ? kSignatureSimple : 0);
        uint32_t set = Next();
        SimCase("SetReflectiveObject", round, NoSetup, [set](bool original) {
            original ? ((SimU32Fn)0x000b2910)(g_copy, 0, set) : g_copy->SetReflectiveObject(set);
        }, NoCapture, signature);
        g_copy->reflectiveObjectSig = reflective;
        SaveCopy();
        SimCase("GetReflectiveObjectPosition", round, NoSetup, [](bool original) {
            Log({ Word(original ? ((SimPtrFn)0x000b2d40)(g_copy, 0) : g_copy->GetReflectiveObjectPosition()) });
        }, NoCapture, signature);
    }
}

void TestSlots() {
    for (int round = 0; round < 80; round++) {
        FreshCopy();
        // owners: the live ones, some cleared, some filled
        for (int i = 0; i < Simulation::kMaxRigidBodies; i++)
            if (Chance(3))
                g_copy->rigidBodyOwners[i] = Chance(2) ? NULL : reinterpret_cast<PhysicsObject *>(FakePointer(i));
        for (int i = 0; i < Simulation::kMaxSimpleBodies; i++)
            if (Chance(3))
                g_copy->simpleRigidBodyOwners[i] = Chance(2) ? NULL : reinterpret_cast<PhysicsObject *>(FakePointer(i));
        if (Chance(5))
            for (int i = 0; i < Simulation::kMaxSimpleBodies; i++)
                g_copy->simpleRigidBodyOwners[i] = reinterpret_cast<PhysicsObject *>(FakePointer(i));
        SaveCopy();
        uint32_t counter = Chance(3) ? 0xffffff - uint32_t(Range(0, 2)) : Next() & 0xffffff;
        PhysicsObject *owner = reinterpret_cast<PhysicsObject *>(FakePointer(0x999));
        bool simple = Chance(2);
        SimCase("AssignRigidBodySlot", round, NoSetup, [owner, simple](bool original) {
            Log({ uint32_t(original ? ((AssignFn)0x000b2630)(g_copy, 0, owner, simple)
                                    : g_copy->AssignRigidBodySlot(owner, simple)) });
        }, NoCapture, counter);
    }

    // ReleaseRigidBodySlot on synthetic tables: the bodies' destructors run on them
    RigidBody *rigid = static_cast<RigidBody *>(calloc(Simulation::kMaxRigidBodies, sizeof(RigidBody)));
    SimpleRigidBody *simpleBodies =
        static_cast<SimpleRigidBody *>(calloc(Simulation::kMaxSimpleBodies, sizeof(SimpleRigidBody)));
    RigidBodyInfo *info = static_cast<RigidBodyInfo *>(calloc(1, sizeof(RigidBodyInfo)));
    for (int round = 0; round < 40; round++) {
        FreshCopy();
        g_copy->rigidBodies = rigid;
        g_copy->rigidBodiesScratch = rigid;
        g_copy->simpleRigidBodies = simpleBodies;
        g_copy->simpleRigidBodiesScratch = simpleBodies;
        g_copy->useScratchPad = Chance(2);
        SaveCopy();
        int slot = Range(0, 63);
        bool simple = Chance(2);
        SimCase("ReleaseRigidBodySlot", round,
                [=] {
                    for (int i = 0; i < Simulation::kMaxRigidBodies; i++) {
                        memset(&rigid[i], 0x5a, sizeof(RigidBody));
                        rigid[i].info = info;
                    }
                    memset(simpleBodies, 0xa5, Simulation::kMaxSimpleBodies * sizeof(SimpleRigidBody));
                },
                [slot, simple](bool original) {
                    original ? ((ReleaseFn)0x000b2980)(g_copy, 0, slot, simple)
                             : g_copy->ReleaseRigidBodySlot(slot, simple);
                },
                [=](std::vector<uint32_t> &out) {
                    AppendBytes(out, rigid, Simulation::kMaxRigidBodies * sizeof(RigidBody));
                    AppendBytes(out, simpleBodies, Simulation::kMaxSimpleBodies * sizeof(SimpleRigidBody));
                },
                NextSignatureGlobal);
    }
    free(rigid);
    free(simpleBodies);
    free(info);
}

// SetStartConditions on a copy of the player's body and its info
void TestStartConditions() {
    PhysicsObject *player = fgSimulation.GetPlayerObject();
    int slot = player->rigidBodySlot;
    RigidBody *bodies = static_cast<RigidBody *>(calloc(Simulation::kMaxRigidBodies, sizeof(RigidBody)));
    RigidBodyInfo *info = static_cast<RigidBodyInfo *>(calloc(1, sizeof(RigidBodyInfo)));
    RigidBody *live = fgSimulation.GetRigidBody(slot);
    for (int round = 0; round < 30; round++) {
        Coord3 position = { live->position.x + Uniform(-50.0f, 50.0f), live->position.y + Uniform(-20.0f, 20.0f),
                            live->position.z + Uniform(-50.0f, 50.0f) };
        Coord3 direction = { Uniform(-1.0f, 1.0f), Uniform(-0.3f, 0.3f), Uniform(-1.0f, 1.0f) };
        FreshCopy();
        g_copy->rigidBodies = bodies;
        g_copy->rigidBodiesScratch = bodies;
        SaveCopy();
        SimCase("SetStartConditions", round,
                [=] {
                    memcpy(&bodies[slot], live, sizeof(RigidBody));
                    memcpy(info, live->info, sizeof(RigidBodyInfo));
                    bodies[slot].info = info;
                },
                [&](bool original) {
                    original ? ((StartFn)0x000b2b50)(g_copy, 0, &position, &direction)
                             : g_copy->SetStartConditions(&position, &direction);
                },
                [=](std::vector<uint32_t> &out) {
                    AppendBytes(out, &bodies[slot], sizeof(RigidBody));
                    AppendBytes(out, info, sizeof(RigidBodyInfo));
                },
                NextSignatureGlobal);
    }
    free(bodies);
    free(info);
}

// DetectCollisionsSRB on a copy of the simple bodies, moved a little
void TestDetectCollisions() {
    const size_t bytes = Simulation::kMaxSimpleBodies * sizeof(SimpleRigidBody);
    SimpleRigidBody *bodies = static_cast<SimpleRigidBody *>(calloc(1, bytes));
    SimpleRigidBody *start = static_cast<SimpleRigidBody *>(calloc(1, bytes));
    for (int round = 0; round < 10; round++) {
        memcpy(start, fgSimulation.GetSimpleRigidBody(0), bytes);
        for (int i = 0; i < Simulation::kMaxSimpleBodies && round > 0; i++)
            start[i].position.y += Uniform(-0.5f, 0.5f) * float(round);
        FreshCopy();
        memset(g_copy->simpleBodyHits, 0x33 + round, sizeof(g_copy->simpleBodyHits));
        g_copy->simpleRigidBodies = bodies;
        g_copy->simpleRigidBodiesScratch = bodies;
        SaveCopy();
        SimCase("DetectCollisionsSRB", round, [=] { memcpy(bodies, start, bytes); },
                [](bool original) { original ? ((SimVoidFn)0x000b2830)(g_copy, 0) : g_copy->DetectCollisionsSRB(); },
                [=](std::vector<uint32_t> &out) { AppendBytes(out, bodies, bytes); }, NextSignatureGlobal);
    }
    free(bodies);
    free(start);
}

// A mine or smackable as the calls read it
struct FakeObject {
    PhysicsObject base;
    uint8_t rest[0x40];
};

void TestMinesAndSmackables() {
    const int kObjects = 24;
    FakeObject *objects = static_cast<FakeObject *>(calloc(kObjects, sizeof(FakeObject)));
    PhysicsObject *pointers[kObjects];
    Fake sleep = { 0x00075370, (const void *)&FakeGoToSleep, {}, {} };
    FakeInstall(&sleep);
    for (int round = 0; round < 60; round++) {
        int count = Range(0, kObjects);
        for (int i = 0; i < kObjects; i++) {
            pointers[i] = &objects[i].base;
            objects[i].base.rigidBodySlot = int16_t(Range(0, 63));
            *reinterpret_cast<int32_t *>(reinterpret_cast<uint8_t *>(&objects[i]) + 0x70) = Range(4, 8);
        }
        FreshCopy();
        g_copy->mines.first = reinterpret_cast<Mine **>(pointers);
        g_copy->mines.last = reinterpret_cast<Mine **>(pointers + count);
        g_copy->smackables.first = reinterpret_cast<Smackable **>(pointers);
        g_copy->smackables.last = reinterpret_cast<Smackable **>(pointers + count);
        g_copy->numRigidBodies = Chance(2) ? Range(50, 66) : Range(0, 64);
        SaveCopy();
        SimCase("DetonateRemoteMines", round,
                [=] {
                    for (int i = 0; i < kObjects; i++)
                        reinterpret_cast<uint8_t *>(&objects[i])[0x7c] = 0;
                },
                [](bool original) { original ? ((SimVoidFn)0x000b2c10)(g_copy, 0) : g_copy->DetonateRemoteMines(); },
                [=](std::vector<uint32_t> &out) { AppendBytes(out, objects, kObjects * sizeof(FakeObject)); },
                NextSignatureGlobal);

        Coord3 position = fgSimulation.GetRigidBody(Range(0, 63))->position;
        position.x += Uniform(-30.0f, 30.0f);
        position.z += Uniform(-30.0f, 30.0f);
        bool force = Chance(2);
        SimCase("CanSpawnRigidBody", round, NoSetup, [position, force](bool original) {
            uint32_t answer = original ? ((CanSpawnFn)0x000b2c40)(g_copy, 0, &position, force)
                                       : g_copy->CanSpawnRigidBody(&position, force);
            Log({ answer & 0xff });
        }, NoCapture, NextSignatureGlobal);
    }
    FakeRemove(&sleep);
    free(objects);
}

// The tracked instances: both sides on lists of their own (operator new's), compared by the instances left
void TestTracked() {
    for (int round = 0; round < 40; round++) {
        int count = Range(0, 12);
        std::vector<uint32_t> instances;
        for (int i = 0; i < count; i++)
            instances.push_back(0x40000000u + uint32_t(Range(0, 15)) * 0x40u);
        uint32_t target = Chance(4) ? 0x4ffff000u : (count > 0 ? instances[Range(0, count - 1)] : 0x40000000u);
        bool all = Chance(4);
        std::vector<uint32_t> left[2];
        for (int side = 0; side < 2; side++) {
            FreshCopy();
            SimVector<TrackedInstance> &list = g_copy->trackedInstances;
            memset(&list, 0, sizeof(list));
            if (count > 0) {
                list.first = static_cast<TrackedInstance **>(UMemory::FastAlloc(uint32_t(count + 2) * 4, "STL"));
                list.last = list.first + count;
                list.end = list.first + count + 2;
                for (int i = 0; i < count; i++) {
                    TrackedInstance *t = static_cast<TrackedInstance *>(OperatorNew(sizeof(TrackedInstance)));
                    t->unknown00 = i;
                    t->instance = reinterpret_cast<CARP::Instance *>(uintptr_t(instances[i]));
                    t->visible = false;
                    list.first[i] = t;
                }
            }
            CARP::Instance *instance = reinterpret_cast<CARP::Instance *>(uintptr_t(target));
            auto run = [&](bool original) {
                if (all)
                    original ? ((SimVoidFn)0x000b30c0)(g_copy, 0) : g_copy->UntrackAllInstances();
                else
                    original ? ((UntrackFn)0x000b3000)(g_copy, 0, instance) : g_copy->UntrackInstance(instance);
            };
            RunSide(run, side == 0);
            left[side].push_back(list.first == NULL ? 0 : uint32_t(list.last - list.first));
            left[side].push_back(list.first == NULL ? 0 : uint32_t(list.end - list.first));
            if (list.first != NULL) {
                for (TrackedInstance **it = list.first; it != list.last; it++) {
                    left[side].push_back(uint32_t((*it)->unknown00));
                    left[side].push_back(Word((*it)->instance));
                }
                for (TrackedInstance **it = list.first; it != list.last; it++)
                    OperatorDelete(*it);
                UMemory::FastFree(list.first, uint32_t(list.end - list.first) * 4);
            }
            memset(&list, 0, sizeof(list));
        }
        g_cases++;
        CheckWords(all ? "UntrackAllInstances" : "UntrackInstance", round, left[0], left[1]);
    }
}

// ---- DeleteDeadObjects

const int kDeadObjects = 16;
FakeObject *g_deadObjects = NULL;
void *g_deadVtable[8];

PhysicsObject *__fastcall FakeDelete(PhysicsObject *self, int, unsigned flags) {
    Log({ 3, uint32_t((reinterpret_cast<FakeObject *>(self) - g_deadObjects)), flags });
    return self;
}

void TestDeleteDeadObjects() {
    g_deadObjects = static_cast<FakeObject *>(calloc(kDeadObjects, sizeof(FakeObject)));
    for (void *&slot : g_deadVtable)
        slot = (void *)&FakeDelete;
    RPlayerCamera *camera = fgRenderHigh != NULL ? fgRenderHigh->views[0].camera : NULL;
    Fake end = { 0x000895e0, (const void *)&FakeEndMissileCamera, {}, {} };
    FakeInstall(&end);
    for (int round = 0; round < 30; round++) {
        int count = Range(0, kDeadObjects);
        int dead = count == 0 ? 0 : Range(0, count);
        int anchor = Chance(2) && count > 0 ? Range(0, count - 1) : -1;
        std::vector<int> types;
        for (int i = 0; i < kDeadObjects; i++) {
            memset(&g_deadObjects[i], 0, sizeof(FakeObject));
            g_deadObjects[i].base.vtable = g_deadVtable;
            g_deadObjects[i].base.type = Range(1, 11);
        }
        std::vector<std::vector<uint32_t>> words(2);
        for (int side = 0; side < 2; side++) {
            FreshCopy();
            PhysicsObjMap *map = NewMap();
            DeadObjectSet *set = NewSet();
            std::vector<PhysicsObjNode *> nodes;
            for (int i = 0; i < count; i++) {
                SimObjectValue v = { uint32_t(g_deadObjects[i].base.type), &g_deadObjects[i] };
                MapInsert(map, v, false);
            }
            for (PhysicsObjNode *n = map->head->left; n != map->head;) {
                nodes.push_back(n);
                PhysicsObjIterator it = { n };
                it.Inc();
                n = it.node;
            }
            for (int i = 0; i < dead; i++)
                SetInsert(set, nodes[size_t(i)], false);
            // the set is ordered by node address, which differs by side: the deletions are compared by their place
            // in the set, and which objects went as a mask
            std::vector<uint32_t> order;
            std::vector<DeadObjectNode *> stack;
            for (DeadObjectNode *n = set->head->parent; !n->isNil || !stack.empty(); n = n->right) {
                for (; !n->isNil; n = n->left)
                    stack.push_back(n);
                n = stack.back();
                stack.pop_back();
                order.push_back(uint32_t(static_cast<FakeObject *>(n->value->value.object) - g_deadObjects));
            }
            g_copy->objects = map;
            g_copy->deadObjects = set;
            PhysicsObject *savedAnchor = camera != NULL ? camera->anchor : NULL;
            if (camera != NULL && anchor >= 0)
                camera->anchor = &g_deadObjects[anchor].base;
            bool savedFlag = SimDeleteInProgress;
            std::vector<uint32_t> log;
            g_log = &log;
            auto run = [](bool original) {
                original ? ((SimVoidFn)0x000b3fc0)(g_copy, 0) : g_copy->DeleteDeadObjects();
            };
            RunSide(run, side == 0);
            g_log = NULL;
            if (camera != NULL)
                camera->anchor = savedAnchor;
            // the camera's end by the object deleted after it
            std::vector<uint32_t> deletions, cameras;
            uint32_t deleted = 0;
            for (size_t i = 0; i < log.size(); i += log[i] == 3 ? 3 : 2) {
                if (log[i] == 2) {
                    uint32_t next = i + 2 < log.size() && log[i + 2] == 3 ? log[i + 3] : 0xffffffffu;
                    cameras.insert(cameras.end(), { 2, log[i + 1], next });
                    continue;
                }
                deleted |= 1u << log[i + 1];
                uint32_t place = 0xffffffffu;
                for (size_t p = 0; p < order.size(); p++)
                    if (order[p] == log[i + 1])
                        place = uint32_t(p);
                deletions.insert(deletions.end(), { 3, place, log[i + 2] });
            }
            words[side] = deletions;
            words[side].insert(words[side].end(), cameras.begin(), cameras.end());
            words[side].push_back(deleted);
            words[side].push_back(SimDeleteInProgress);
            SimDeleteInProgress = savedFlag;
            std::vector<uint32_t> m = Serialize(map, [](PhysicsObjNode *n, std::vector<uint32_t> &out) {
                out.push_back(n->value.key);
                out.push_back(uint32_t(static_cast<FakeObject *>(n->value.object) - g_deadObjects));
            });
            std::vector<uint32_t> s = Serialize(set, SetWords);
            words[side].insert(words[side].end(), m.begin(), m.end());
            words[side].insert(words[side].end(), s.begin(), s.end());
            FreeSet(set);
            FreeMap(map);
        }
        g_cases++;
        CheckWords("DeleteDeadObjects", round, words[0], words[1]);
    }
    FakeRemove(&end);
    free(g_deadObjects);
    g_deadObjects = NULL;
}

}  // namespace

void GameMissionsShadowA_Run(void) {
    const char *flag = getenv("NIGHTFIRE_GAMEMISSIONSSHADOWA");
    if (flag == NULL || atoi(flag) == 0)
        return;
    FpControlGet(&g_x87, &g_sse);
    g_copy = static_cast<Simulation *>(calloc(1, sizeof(Simulation)));

    TestMap();
    TestSet();
    TestVectors();
    TestStateAndLookups();
    TestSlots();
    if (fgSimulation.cars.first != NULL && fgSimulation.cars.first != fgSimulation.cars.last)
        TestStartConditions();
    TestDetectCollisions();
    TestMinesAndSmackables();
    TestTracked();
    TestDeleteDeadObjects();

    free(g_copy);
    g_copy = NULL;
    printf("[gmissionsA] Simulation part 1 (0x000b25e0-0x000b4080): %d cases, %d checks, %d differ, %d faults\n",
           g_cases, g_checks, g_differ, g_faults);
    fflush(stdout);
}
