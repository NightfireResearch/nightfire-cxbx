#ifndef DRIVING_GAME_SIMULATIONCONTAINERS_H_
#define DRIVING_GAME_SIMULATIONCONTAINERS_H_

// ---------------------------------------------------------------------------------------------------------------
// The C++ library code compiled for the Simulation's containers (engine/RbTree.h has the tree layout): the
// multimap of every spawned object by type, the set of objects waiting to be deleted, and the vectors of pointers.
// Several of these copies serve other containers too (the linker folded identical code together): those are said
// at each. See SimulationContainers.cpp.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "../engine/CoreContainers.h"   // GameVector, GameTreeNode, SimObjectValue, SimObjectMultimap, SimObjectVector
#include "../engine/RbTree.h"

// ---- std::multimap<int, PhysicsObject *>: the Simulation's objects by type (SimObjectValue's key the type,
// its object the object)

typedef GameTreeNode<SimObjectValue> PhysicsObjNode;
static_assert(sizeof(PhysicsObjNode) == 0x18, "a map node is 0x18 bytes");

struct PhysicsObjIterator {
    PhysicsObjNode *node;

    // ++, for every map with 0x18-byte nodes (data/Tree.h's TreeNode among them)
    void Inc();                                                                                 // 0x000b2920
};

struct PhysicsObjMap : SimObjectMultimap {
    struct Range {
        PhysicsObjIterator first;
        PhysicsObjIterator last;
    };

    // _Erase: a subtree freed, without rebalancing
    void EraseSubtree(PhysicsObjNode *node);                                                    // 0x000b2b10
    Range* EqualRange(Range *result, const uint32_t *key);                                      // 0x000b2ef0
    // _Insert: a new red node under `where`, on its left if `addLeft`, then rebalanced (InsertMulti's)
    PhysicsObjNode** InsertAt(PhysicsObjNode **result, bool addLeft, PhysicsObjNode *where,
                              const SimObjectValue *value);                                     // 0x000b31b0
    // erase(iterator): the node unlinked and freed, the tree rebalanced; answers the next one
    PhysicsObjIterator* Erase(PhysicsObjIterator *result, PhysicsObjIterator where);            // 0x000b3660
    PhysicsObjIterator* EraseRange(PhysicsObjIterator *result, PhysicsObjIterator first,
                                   PhysicsObjIterator last);                                    // 0x000b3e40
};
static_assert(sizeof(PhysicsObjMap) == 0xc, "a map is 12 bytes");

// std::_Distance over the map's iterators: adds the number of nodes from first to last to *count
void PhysicsObjDistance(PhysicsObjIterator first, PhysicsObjIterator last, uint32_t *count);   // 0x000b2e00

// ---- std::set<PhysicsObjMap::iterator>: the objects to delete at the next DeleteDeadObjects, ordered by the
// object each node holds (unsigned)

struct DeadObjectNode : RbTreeNode<DeadObjectNode, PhysicsObjNode *> {};
static_assert(sizeof(DeadObjectNode) == 0x14, "a set node is 0x14 bytes");

struct DeadObjectSet : RbTree<DeadObjectNode> {
    struct Iterator {
        DeadObjectNode *node;

        // --, for every set with 0x14-byte nodes
        void Dec();                                                                             // 0x000b2ab0
    };
    struct InsertResult {
        Iterator position;
        bool inserted;
        uint8_t unknown05[3];
    };

    // The rotations, for every set with 0x14-byte nodes (the attribute system's string set, RVehicleParticle's set)
    void Lrotate(DeadObjectNode *node);                                                         // 0x000b29f0
    void Rrotate(DeadObjectNode *node);                                                         // 0x000b2a50
    // _Erase: a subtree freed, without rebalancing
    void EraseSubtree(DeadObjectNode *node);                                                    // 0x000b2dc0
    Iterator* Erase(Iterator *result, Iterator where);                                          // 0x000b3390
    // _Insert: a new red node under `where`, on its left if `addLeft`, then rebalanced
    Iterator* Insert(Iterator *result, bool addLeft, DeadObjectNode *where,
                     PhysicsObjNode *const &value);                                             // 0x000b39a0
    Iterator* EraseRange(Iterator *result, Iterator first, Iterator last);                      // 0x000b3f00
    // SimulationSpawn.cpp's
    InsertResult* InsertUnique(InsertResult *result, PhysicsObjNode *const &value);             // 0x000b4190
};
static_assert(sizeof(DeadObjectSet) == 0xc, "a set is 12 bytes");

// ---- std::vector<T *>: the Simulation's vectors. One compiled copy of the code serves them all; the free
// functions below serve every vector of pointers in the game.

template <class T>
struct SimVector : GameVector<T *> {
    uint32_t Size() const { return this->first == NULL ? 0 : uint32_t(this->last - this->first); }
    uint32_t Capacity() const { return this->first == NULL ? 0 : uint32_t(this->end - this->first); }
};

struct SimPointerVector : SimObjectVector {
    // reserve (SimulationSpawn.cpp's)
    void Reserve(uint32_t count);                                                               // 0x000b4080
    // _Insert_n: `count` copies of *value before `where`
    void InsertN(void **where, uint32_t count, void *const *value);                             // 0x000b3b80
    // _Xlen: throws std::length_error("vector<T> too long")
    void Xlen();                                                                                // 0x000b3130
    // erase(iterator): answers `where`
    void*** Erase(void ***result, void **where);                                                // 0x000b2fc0
};
static_assert(sizeof(SimPointerVector) == 0x10, "a vector is 16 bytes");

// std::copy_backward: [first, last) to the range ending at destEnd; answers its start
void*** PointerCopyBackwardTo(void ***result, void **first, void **last, void **destEnd);       // 0x000b2e40
// std::remove_copy: the elements other than *value, to dest; answers dest's new end
void*** PointerRemoveCopy(void ***result, void **first, void **last, void **dest,
                          void *const *value);                                                  // 0x000b2e70
// std::uninitialized_fill_n (callers pass the allocator and one more word after these, unread)
void PointerUninitializedFillN(void **dest, uint32_t count, void *const *value);               // 0x000b2ec0
// std::remove: answers the new end
void*** PointerRemove(void ***result, void **first, void **last, void *const *value);           // 0x000b2f60

// The same two on a vector of any pointer type
template <class T>
void UninitializedFillPointers(T **dest, uint32_t count, T *const *value) {
    PointerUninitializedFillN((void **)dest, count, (void *const *)value);
}
template <class T>
T*** CopyBackwardPointers(T ***result, T **first, T **last, T **destEnd) {
    return (T ***)PointerCopyBackwardTo((void ***)result, (void **)first, (void **)last, (void **)destEnd);
}

#endif // DRIVING_GAME_SIMULATIONCONTAINERS_H_
