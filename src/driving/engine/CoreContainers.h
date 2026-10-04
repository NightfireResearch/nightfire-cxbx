#ifndef DRIVING_ENGINE_CORECONTAINERS_H_
#define DRIVING_ENGINE_CORECONTAINERS_H_

#include <stddef.h>
#include <stdint.h>

#include "RbTree.h"

// ---------------------------------------------------------------------------------------------------------------
// Compiled copies of the C++ library's containers that the engine's core owns: a few red-black tree methods
// (std::map, std::set, std::multimap as the game's compiler had them - Dinkumware's), a vector's push_back, and
// the game's USimpleVec. Written once as templates in CoreContainers.cpp; each compiled copy has an entry here.
// The reference counters' trees are in URefCounter.h.
// ---------------------------------------------------------------------------------------------------------------

// A tree node (RbTree.h has the layout): the links, the value (its first field the key), the colour and the nil
// mark (the head node's)
template <class Value>
struct GameTreeNode : RbTreeNode<GameTreeNode<Value>, Value> {};

// A tree (0xc bytes): the comparator and allocator (empty), the head node (the end; its left the first node, its
// right the last), the number of nodes
template <class Value>
using GameTree = RbTree<GameTreeNode<Value>>;

// What insert answers, through a pointer the caller supplies: the node, and whether it is new
template <class Value>
struct GameTreeInsertResult {
    GameTreeNode<Value> *node;
    bool inserted;
};

// A std::vector as laid out (0x10 bytes): the allocator's word, then begin, end and the end of the storage
template <class T>
struct GameVector {
    uint32_t allocator;
    T *first;
    T *last;
    T *end;
};

// ---- the values

struct StateRefValue {              // StateRef: ordered by the 0x4c bytes of the state it points at
    const void *state;
    uint8_t unknown04[0x10];
};

struct CollisionInstanceValue {     // keyed by a WCollisionInstance pointer (compared unsigned)
    uint32_t key;
    uint8_t unknown04[0xc];
};

struct SimObjectValue {             // Simulation::SpawnPhysicsObject's multimap
    uint32_t key;
    void *object;
};

static_assert(sizeof(GameTreeNode<StateRefValue>) == 0x24, "the StateRef set's node");
static_assert(sizeof(GameTreeNode<CollisionInstanceValue>) == 0x20, "the WCollisionInstance map's node");
static_assert(sizeof(GameTree<StateRefValue>) == 0xc, "a tree is 0xc bytes");
static_assert(sizeof(GameVector<void *>) == 0x10, "a vector is 0x10 bytes");

typedef GameTreeInsertResult<StateRefValue> StateRefInsert;
typedef GameTreeInsertResult<SimObjectValue> SimObjectInsert;
typedef GameTreeNode<CollisionInstanceValue> CollisionInstanceNode;

// ---- the compiled copies

// _Rb_tree<StateRef, StateRef>::insert_unique (0x00092b40)
struct StateRefSet : GameTree<StateRefValue> {
    StateRefInsert* InsertUnique(StateRefInsert *result, const StateRefValue *value);
};

// _Rb_tree<WCollisionInstance *, WCollisionInstance>::find (0x000a8ae0)
struct CollisionInstanceMap : GameTree<CollisionInstanceValue> {
    CollisionInstanceNode** Find(CollisionInstanceNode **result, const uint32_t *key);
};

// std::__tree<>::insert_multi (0x000b3930)
struct SimObjectMultimap : GameTree<SimObjectValue> {
    SimObjectInsert* InsertMulti(SimObjectInsert *result, const SimObjectValue *value);
};

// std::vector<>::push_back (0x000b4980), a vector of pointers of Simulation's
struct SimObjectVector : GameVector<void *> {
    void PushBack(void *const *value);
};

// USimpleVec<T>: an array from new[] (its count in the word before it) and a count
template <class T>
struct USimpleVec {
    T *data;
    uint32_t count;
};

struct RLightningSegment;   // RLightning::Segment, 0x18 bytes

// USimpleVec<RLightning::Segment>::~USimpleVec (0x0009f870)
struct LightningSegmentVec : USimpleVec<RLightningSegment> {
    void Destruct();
};

#endif // DRIVING_ENGINE_CORECONTAINERS_H_
