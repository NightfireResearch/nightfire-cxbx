#include "CoreContainers.h"

#include "UMemory.hpp"
#include "../../common/xbeOverload.h"
#include "../render/Lightning.h"
#include "../render/StateManager.h"

#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// The compiled container copies of CoreContainers.h, ported from their listings. The trees' insert_unique and
// insert_multi are Dinkumware's: walk down from the root remembering the last node and the last direction, then
// either insert there or (insert_unique) find the key already present one step back. Each copy's _Insert (which
// makes the node and rebalances) and iterator -- are its own compiled helpers, not ours yet, called by address.
// The attribute extension map's insert (0x000552d0) and the CARP resolver map's (0x001198f0) are with the rest of
// their maps, in data/AttributeContainers.cpp and data/Carp.cpp.
// ---------------------------------------------------------------------------------------------------------------

// ---- originals called by address

#define SimObjectMultimap_Insert ((GameTreeNode<SimObjectValue> **(__fastcall *)(GameTree<SimObjectValue> *, int, GameTreeNode<SimObjectValue> **, bool, GameTreeNode<SimObjectValue> *, const SimObjectValue *))0x000b31b0)
#define SimObjectVector_Construct ((void (*)(void **where, int count, void *const *value, GameVector<void *> *vector, void *const *same))0x000b2ec0)   // uninitialized_fill_n with the allocator
#define SimObjectVector_InsertN ((void (__fastcall *)(GameVector<void *> *, int, void **where, int count, void *const *value))0x000b3b80)
#define CRT_VectorDestructorIterator ((void (__stdcall *)(void *array, uint32_t size, int32_t count, void *destructor))0x0013332e)   // ??_M

static void *const kLightningSegmentDestructor = (void *)XbeAddress(&RLightningSegment::Destruct);
static const uint32_t kLightningSegmentBytes = 0x18;

template <class Value>
using InsertFn = GameTreeNode<Value> **(__fastcall *)(GameTree<Value> *, int, GameTreeNode<Value> **, bool,
                                                     GameTreeNode<Value> *, const Value *);
template <class Value>
using DecrementFn = void (__fastcall *)(GameTreeNode<Value> **, int);

// The set's compiled helpers (render/StateManager.h), in the shapes InsertUnique takes
static StateRefNode **__fastcall StateRefSet_Insert(GameTree<StateRefValue> *tree, int, StateRefNode **result,
                                                    bool addLeft, StateRefNode *where, const StateRefValue *value) {
    return static_cast<StateRefTree *>(tree)->InsertAt(result, addLeft, where, value);
}

static void __fastcall StateRefNode_Decrement(StateRefNode **node, int) {
    reinterpret_cast<StateRefIterator *>(node)->Decrement();
}

// StateRef's order: the bytes of the state
static bool StateLess(const StateRefValue &a, const StateRefValue &b) {
    return memcmp(a.state, b.state, 0x4c) < 0;
}

template <class Value>
static GameTreeInsertResult<Value> *InsertUnique(GameTree<Value> *tree, GameTreeInsertResult<Value> *result,
                                                const Value *value, bool (*less)(const Value &, const Value &),
                                                InsertFn<Value> insert, DecrementFn<Value> decrement) {
    GameTreeNode<Value> *head = tree->head;
    GameTreeNode<Value> *where = head;
    bool addLeft = true;
    for (GameTreeNode<Value> *node = head->parent; !node->isNil; node = addLeft ? node->left : node->right) {
        where = node;
        addLeft = less(*value, node->value);
    }
    GameTreeNode<Value> *previous = where;
    if (addLeft) {
        if (where == head->left) {
            GameTreeNode<Value> *inserted;
            result->node = *insert(tree, 0, &inserted, true, where, value);
            result->inserted = true;
            return result;
        }
        decrement(&previous, 0);
    }
    if (less(previous->value, *value)) {
        GameTreeNode<Value> *inserted;
        result->node = *insert(tree, 0, &inserted, addLeft, where, value);
        result->inserted = true;
        return result;
    }
    result->node = previous;
    result->inserted = false;
    return result;
}

// FUNC_AT(0x00092b40)
StateRefInsert* StateRefSet::InsertUnique(StateRefInsert *result, const StateRefValue *value) {
    return ::InsertUnique(this, result, value, StateLess, StateRefSet_Insert, StateRefNode_Decrement);
}

// FUNC_AT(0x000b3930)
SimObjectInsert* SimObjectMultimap::InsertMulti(SimObjectInsert *result, const SimObjectValue *value) {
    GameTreeNode<SimObjectValue> *where = head;
    bool addLeft = true;
    for (GameTreeNode<SimObjectValue> *node = head->parent; !node->isNil; node = addLeft ? node->left : node->right) {
        where = node;
        addLeft = value->key < node->value.key;
    }
    GameTreeNode<SimObjectValue> *inserted;
    result->node = *SimObjectMultimap_Insert(this, 0, &inserted, addLeft, where, value);
    result->inserted = true;
    return result;
}

// lower_bound, then the end unless the key is there
// FUNC_AT(0x000a8ae0)
CollisionInstanceNode** CollisionInstanceMap::Find(CollisionInstanceNode **result, const uint32_t *key) {
    CollisionInstanceNode *where = head;
    for (CollisionInstanceNode *node = head->parent; !node->isNil;) {
        if (node->value.key < *key) {
            node = node->right;
        } else {
            where = node;
            node = node->left;
        }
    }
    *result = where == head || *key < where->value.key ? head : where;
    return result;
}

// FUNC_AT(0x000b4980)
void SimObjectVector::PushBack(void *const *value) {
    size_t size = first == NULL ? 0 : last - first;
    if (first != NULL && size < size_t(end - first)) {
        SimObjectVector_Construct(last, 1, value, this, value);
        last++;
    } else {
        SimObjectVector_InsertN(this, 0, last, 1, value);
    }
}

// FUNC_AT(0x0009f870)
void LightningSegmentVec::Destruct() {
    if (data != NULL) {
        int32_t *cookie = (int32_t *)data - 1;   // new[]'s count of elements
        CRT_VectorDestructorIterator(data, kLightningSegmentBytes, *cookie, kLightningSegmentDestructor);
        OperatorDelete(cookie);
    }
    data = NULL;
    count = 0;
}
