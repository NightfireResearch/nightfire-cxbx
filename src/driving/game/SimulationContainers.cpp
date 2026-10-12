#include "SimulationContainers.h"

#include <stdio.h>

#include "VehicleParticle.h"            // VehicleParticleSet: Min, Max
#include "../audio/Bank.h"              // SharedTree::Rrotate, SharedTreeMax
#include "../data/AttributeContainers.h"   // StringSet: Buynode, Iterator::Inc
#include "../data/Tree.h"               // TreeNode, TreeThrow
#include "../engine/ActionQueue.hpp"    // FillQueuePointers
#include "../engine/UMemory.hpp"
#include "../physics/RigidBodyBasics.h" // PointerVectorDeallocate
#include "../render/TextureContext.h"   // TextureContextMap::BuyNode
#include "../world/CollisionQueries.h"  // PointerUninitializedCopy
#include "../world/CollisionTypes.h"    // PointerVector: Ucopy, Ufill
#include "../world/SoundGroup.h"        // WSoundMap::Lrotate
#include "EmpEffect.h"                  // SharedTreeMin

// ---------------------------------------------------------------------------------------------------------------
// The Simulation's containers' compiled code (0x000b2920-0x000b3f00), ported from the listing. The two trees'
// _Insert and erase(iterator) are one algorithm (Dinkumware's red-black tree, as the data layer's Tree has it);
// they are written once here as templates over the helpers each copy calls, and each address has its entry.
// ---------------------------------------------------------------------------------------------------------------

namespace {

void SimContainersUntested(const char *what) {
    printf("[missions] WARNING: %s ran - a provisional port that no shipped run is known to reach, UNTESTED. "
           "Check what it computes against the original.\n", what);
    fflush(stdout);
}

#define SIMCONTAINERS_UNTESTED(what) \
    do { \
        static bool warned_; \
        if (!warned_) { \
            warned_ = true; \
            SimContainersUntested(what); \
        } \
    } while (0)

constexpr uint32_t kMaxPointers = 0x3fffffff;           // max_size() of a vector of pointers

// ---- the helpers each tree's copies call, in one shape

// The map: the node maker, rotations, minimum and maximum every map with 0x18-byte nodes shares (ours, ported on
// other maps' types), and the iterator's ++ here.
struct PhysicsObjMapOps {
    typedef PhysicsObjMap Tree;
    typedef PhysicsObjNode Node;
    typedef SimObjectValue Value;
    static constexpr uint32_t kMaxSize = 0x1ffffffe;

    static Node* BuyNode(Tree *tree, Node *left, Node *parent, Node *right, const Value *value) {
        return reinterpret_cast<Node *>(reinterpret_cast<TextureContextMap *>(tree)->BuyNode(
            reinterpret_cast<TextureContextNode *>(left), reinterpret_cast<TextureContextNode *>(parent),
            reinterpret_cast<TextureContextNode *>(right), reinterpret_cast<const TextureContextEntry *>(value),
            kTreeRed));
    }
    static void Lrotate(Tree *tree, Node *node) {
        reinterpret_cast<WSoundMap *>(tree)->Lrotate(reinterpret_cast<SoundMapNode *>(node));
    }
    static void Rrotate(Tree *tree, Node *node) {
        reinterpret_cast<SharedTree *>(tree)->Rrotate(reinterpret_cast<TreeNode *>(node));
    }
    static Node* Min(Node *node) {
        return reinterpret_cast<Node *>(SharedTreeMin(reinterpret_cast<TreeNode *>(node)));
    }
    static Node* Max(Node *node) {
        return reinterpret_cast<Node *>(SharedTreeMax(reinterpret_cast<TreeNode *>(node)));
    }
    static void Inc(Node **node) {
        reinterpret_cast<PhysicsObjIterator *>(node)->Inc();
    }
};

// The set: the node maker and ++ of the attribute system's string set, the minimum and maximum of
// RVehicleParticle's set, the rotations here.
struct DeadObjectSetOps {
    typedef DeadObjectSet Tree;
    typedef DeadObjectNode Node;
    typedef PhysicsObjNode *Value;
    static constexpr uint32_t kMaxSize = 0x3ffffffe;

    static Node* BuyNode(Tree *tree, Node *left, Node *parent, Node *right, const Value *value) {
        return reinterpret_cast<Node *>(reinterpret_cast<StringSet *>(tree)->Buynode(
            reinterpret_cast<StringNode *>(left), reinterpret_cast<StringNode *>(parent),
            reinterpret_cast<StringNode *>(right), *reinterpret_cast<const char *const *>(value), kTreeRed));
    }
    static void Lrotate(Tree *tree, Node *node) { tree->Lrotate(node); }
    static void Rrotate(Tree *tree, Node *node) { tree->Rrotate(node); }
    static Node* Min(Node *node) {
        return reinterpret_cast<Node *>(VehicleParticleSet::Min(reinterpret_cast<VehicleParticleNode *>(node)));
    }
    static Node* Max(Node *node) {
        return reinterpret_cast<Node *>(VehicleParticleSet::Max(reinterpret_cast<VehicleParticleNode *>(node)));
    }
    static void Inc(Node **node) {
        reinterpret_cast<StringSet::Iterator *>(node)->Inc();
    }
};

// _Insert
template <class Ops>
typename Ops::Node* TreeInsertAt(typename Ops::Tree *tree, bool addLeft, typename Ops::Node *where,
                                 const typename Ops::Value *value) {
    typedef typename Ops::Node Node;
    if (tree->size >= Ops::kMaxSize) {
        SIMCONTAINERS_UNTESTED("a Simulation tree's length error");
        TreeThrow("map/set<T> too long", kLengthErrorVtable, kLengthErrorThrowInfo);
    }
    Node *head = tree->head;
    Node *node = Ops::BuyNode(tree, head, where, head, value);
    tree->size++;
    if (where == tree->head) {
        tree->head->parent = node;
        tree->head->left = node;
        tree->head->right = node;
    } else if (addLeft) {
        where->left = node;
        if (where == tree->head->left)
            tree->head->left = node;
    } else {
        where->right = node;
        if (where == tree->head->right)
            tree->head->right = node;
    }

    for (Node *x = node; x->parent->color == kTreeRed;) {
        Node *parent = x->parent;
        Node *grandparent = parent->parent;
        if (parent == grandparent->left) {
            Node *uncle = grandparent->right;
            if (uncle->color == kTreeRed) {
                parent->color = kTreeBlack;
                uncle->color = kTreeBlack;
                x->parent->parent->color = kTreeRed;
                x = x->parent->parent;
            } else {
                if (x == parent->right) {
                    x = parent;
                    Ops::Lrotate(tree, x);
                }
                x->parent->color = kTreeBlack;
                x->parent->parent->color = kTreeRed;
                Ops::Rrotate(tree, x->parent->parent);
            }
        } else {
            Node *uncle = grandparent->left;
            if (uncle->color == kTreeRed) {
                parent->color = kTreeBlack;
                uncle->color = kTreeBlack;
                x->parent->parent->color = kTreeRed;
                x = x->parent->parent;
            } else {
                if (x == parent->left) {
                    x = parent;
                    Ops::Rrotate(tree, x);
                }
                x->parent->color = kTreeBlack;
                x->parent->parent->color = kTreeRed;
                Ops::Lrotate(tree, x->parent->parent);
            }
        }
    }
    tree->head->parent->color = kTreeBlack;
    return node;
}

// erase(iterator): answers the node after `where`
template <class Ops>
typename Ops::Node* TreeErase(typename Ops::Tree *tree, typename Ops::Node *where) {
    typedef typename Ops::Node Node;
    if (where->isNil) {
        SIMCONTAINERS_UNTESTED("a Simulation tree's invalid iterator");
        TreeThrow("invalid map/set<T> iterator", kOutOfRangeVtable, kOutOfRangeThrowInfo);
    }
    Node *erased = where;
    Node *next = where;
    Ops::Inc(&next);

    Node *pnode = erased;       // the node that really leaves its place: erased, or its successor
    Node *fixnode;              // the node that takes pnode's place
    Node *fixnodeParent;
    if (erased->left->isNil) {
        fixnode = erased->right;
    } else if (erased->right->isNil) {
        fixnode = erased->left;
    } else {
        pnode = next;
        fixnode = pnode->right;
    }

    Node *head = tree->head;
    if (pnode == erased) {
        fixnodeParent = erased->parent;
        if (!fixnode->isNil)
            fixnode->parent = fixnodeParent;
        if (head->parent == erased)
            head->parent = fixnode;
        else if (fixnodeParent->left == erased)
            fixnodeParent->left = fixnode;
        else
            fixnodeParent->right = fixnode;
        if (tree->head->left == erased)
            tree->head->left = fixnode->isNil ? fixnodeParent : Ops::Min(fixnode);
        if (tree->head->right == erased)
            tree->head->right = fixnode->isNil ? fixnodeParent : Ops::Max(fixnode);
    } else {
        erased->left->parent = pnode;
        pnode->left = erased->left;
        if (pnode == erased->right) {
            fixnodeParent = pnode;
        } else {
            fixnodeParent = pnode->parent;
            if (!fixnode->isNil)
                fixnode->parent = fixnodeParent;
            fixnodeParent->left = fixnode;
            pnode->right = erased->right;
            erased->right->parent = pnode;
        }
        if (head->parent == erased)
            head->parent = pnode;
        else if (erased->parent->left == erased)
            erased->parent->left = pnode;
        else
            erased->parent->right = pnode;
        pnode->parent = erased->parent;
        uint8_t color = pnode->color;
        pnode->color = erased->color;
        erased->color = color;
    }

    if (erased->color == kTreeBlack) {
        for (; fixnode != tree->head->parent && fixnode->color == kTreeBlack;
             fixnode = fixnodeParent, fixnodeParent = fixnodeParent->parent) {
            if (fixnode == fixnodeParent->left) {
                Node *sibling = fixnodeParent->right;
                if (sibling->color == kTreeRed) {
                    sibling->color = kTreeBlack;
                    fixnodeParent->color = kTreeRed;
                    Ops::Lrotate(tree, fixnodeParent);
                    sibling = fixnodeParent->right;
                }
                if (sibling->isNil)
                    continue;
                if (sibling->left->color == kTreeBlack && sibling->right->color == kTreeBlack) {
                    sibling->color = kTreeRed;
                    continue;
                }
                if (sibling->right->color == kTreeBlack) {
                    sibling->left->color = kTreeBlack;
                    sibling->color = kTreeRed;
                    Ops::Rrotate(tree, sibling);
                    sibling = fixnodeParent->right;
                }
                sibling->color = fixnodeParent->color;
                fixnodeParent->color = kTreeBlack;
                sibling->right->color = kTreeBlack;
                Ops::Lrotate(tree, fixnodeParent);
                break;
            } else {
                Node *sibling = fixnodeParent->left;
                if (sibling->color == kTreeRed) {
                    sibling->color = kTreeBlack;
                    fixnodeParent->color = kTreeRed;
                    Ops::Rrotate(tree, fixnodeParent);
                    sibling = fixnodeParent->left;
                }
                if (sibling->isNil)
                    continue;
                if (sibling->right->color == kTreeBlack && sibling->left->color == kTreeBlack) {
                    sibling->color = kTreeRed;
                    continue;
                }
                if (sibling->left->color == kTreeBlack) {
                    sibling->right->color = kTreeBlack;
                    sibling->color = kTreeRed;
                    Ops::Lrotate(tree, sibling);
                    sibling = fixnodeParent->left;
                }
                sibling->color = fixnodeParent->color;
                fixnodeParent->color = kTreeBlack;
                sibling->left->color = kTreeBlack;
                Ops::Rrotate(tree, fixnodeParent);
                break;
            }
        }
        fixnode->color = kTreeBlack;
    }

    UMemory::FastFree(erased, sizeof(Node));
    if (tree->size > 0)
        tree->size--;
    return next;
}

// _Erase
template <class Tree, class Node>
void TreeEraseSubtree(Tree *tree, Node *node) {
    for (Node *next; !node->isNil; node = next) {
        tree->EraseSubtree(node->right);
        next = node->left;
        if (node != NULL)
            UMemory::FastFree(node, sizeof(Node));
    }
}

// erase(first, last): the whole tree at once, else node by node (the ++ inlined in the original)
template <class Ops>
typename Ops::Node* TreeEraseRange(typename Ops::Tree *tree, typename Ops::Node *first, typename Ops::Node *last) {
    typedef typename Ops::Node Node;
    Node *head = tree->head;
    if (first == head->left && last == head) {
        tree->EraseSubtree(head->parent);
        tree->head->parent = tree->head;
        tree->size = 0;
        tree->head->left = tree->head;
        tree->head->right = tree->head;
        return tree->head->left;
    }
    while (first != last) {
        Node *where = first;
        Ops::Inc(&first);
        TreeErase<Ops>(tree, where);
    }
    return first;
}

// FillQueuePointers is the game's one std::fill for vectors of pointers (ActionQueue.cpp has it on its type)
void FillPointers(void **first, void **last, void *const *value) {
    FillQueuePointers(reinterpret_cast<ActionQueue **>(first), reinterpret_cast<ActionQueue **>(last),
                      reinterpret_cast<ActionQueue *const *>(value));
}

}  // namespace

// =============================================================================================================
// The objects' multimap
// =============================================================================================================

// FUNC_AT(0x000b2920)
void PhysicsObjIterator::Inc() {
    if (node->isNil)
        return;
    if (!node->right->isNil) {
        PhysicsObjNode *next = node->right;
        while (!next->left->isNil)
            next = next->left;
        node = next;
        return;
    }
    PhysicsObjNode *parent = node->parent;
    while (!parent->isNil && node == parent->right) {
        node = parent;
        parent = parent->parent;
    }
    node = parent;
}

// FUNC_AT(0x000b2b10)
void PhysicsObjMap::EraseSubtree(PhysicsObjNode *node) {
    TreeEraseSubtree(this, node);
}

// FUNC_AT(0x000b2e00)
void PhysicsObjDistance(PhysicsObjIterator first, PhysicsObjIterator last, uint32_t *count) {
    for (; first.node != last.node; first.Inc())
        ++*count;
}

// FUNC_AT(0x000b2ef0)
PhysicsObjMap::Range* PhysicsObjMap::EqualRange(Range *result, const uint32_t *key) {
    PhysicsObjNode *upper = head;
    for (PhysicsObjNode *node = Root(); !node->isNil;) {
        if (*key < node->value.key) {
            upper = node;
            node = node->left;
        } else {
            node = node->right;
        }
    }
    PhysicsObjNode *lower = head;
    for (PhysicsObjNode *node = Root(); !node->isNil;) {
        if (node->value.key < *key) {
            node = node->right;
        } else {
            lower = node;
            node = node->left;
        }
    }
    result->last.node = upper;
    result->first.node = lower;
    return result;
}

// FUNC_AT(0x000b31b0)
PhysicsObjNode** PhysicsObjMap::InsertAt(PhysicsObjNode **result, bool addLeft, PhysicsObjNode *where,
                                         const SimObjectValue *value) {
    *result = TreeInsertAt<PhysicsObjMapOps>(this, addLeft, where, value);
    return result;
}

// FUNC_AT(0x000b3660)
PhysicsObjIterator* PhysicsObjMap::Erase(PhysicsObjIterator *result, PhysicsObjIterator where) {
    result->node = TreeErase<PhysicsObjMapOps>(this, where.node);
    return result;
}

// FUNC_AT(0x000b3e40)
PhysicsObjIterator* PhysicsObjMap::EraseRange(PhysicsObjIterator *result, PhysicsObjIterator first,
                                              PhysicsObjIterator last) {
    result->node = TreeEraseRange<PhysicsObjMapOps>(this, first.node, last.node);
    return result;
}

// =============================================================================================================
// The dead objects' set
// =============================================================================================================

// FUNC_AT(0x000b29f0)
void DeadObjectSet::Lrotate(DeadObjectNode *node) {
    DeadObjectNode *pivot = node->right;
    node->right = pivot->left;
    if (!pivot->left->isNil)
        pivot->left->parent = node;
    pivot->parent = node->parent;
    if (node == head->parent)
        head->parent = pivot;
    else if (node == node->parent->left)
        node->parent->left = pivot;
    else
        node->parent->right = pivot;
    pivot->left = node;
    node->parent = pivot;
}

// FUNC_AT(0x000b2a50)
void DeadObjectSet::Rrotate(DeadObjectNode *node) {
    DeadObjectNode *pivot = node->left;
    node->left = pivot->right;
    if (!pivot->right->isNil)
        pivot->right->parent = node;
    pivot->parent = node->parent;
    if (node == head->parent)
        head->parent = pivot;
    else if (node == node->parent->right)
        node->parent->right = pivot;
    else
        node->parent->left = pivot;
    pivot->right = node;
    node->parent = pivot;
}

// From end() to the rightmost node
// FUNC_AT(0x000b2ab0)
void DeadObjectSet::Iterator::Dec() {
    if (node->isNil) {
        node = node->right;
        return;
    }
    if (!node->left->isNil) {
        DeadObjectNode *previous = node->left;
        while (!previous->right->isNil)
            previous = previous->right;
        node = previous;
        return;
    }
    DeadObjectNode *parent = node->parent;
    while (!parent->isNil && node == parent->left) {
        node = parent;
        parent = parent->parent;
    }
    if (!parent->isNil)
        node = parent;
}

// FUNC_AT(0x000b2dc0)
void DeadObjectSet::EraseSubtree(DeadObjectNode *node) {
    TreeEraseSubtree(this, node);
}

// FUNC_AT(0x000b3390)
DeadObjectSet::Iterator* DeadObjectSet::Erase(Iterator *result, Iterator where) {
    result->node = TreeErase<DeadObjectSetOps>(this, where.node);
    return result;
}

// FUNC_AT(0x000b39a0)
DeadObjectSet::Iterator* DeadObjectSet::Insert(Iterator *result, bool addLeft, DeadObjectNode *where,
                                               PhysicsObjNode *const &value) {
    result->node = TreeInsertAt<DeadObjectSetOps>(this, addLeft, where, &value);
    return result;
}

// FUNC_AT(0x000b3f00)
DeadObjectSet::Iterator* DeadObjectSet::EraseRange(Iterator *result, Iterator first, Iterator last) {
    result->node = TreeEraseRange<DeadObjectSetOps>(this, first.node, last.node);
    return result;
}

// =============================================================================================================
// The vectors of pointers
// =============================================================================================================

// FUNC_AT(0x000b2e40)
void*** PointerCopyBackwardTo(void ***result, void **first, void **last, void **destEnd) {
    while (last != first)
        *--destEnd = *--last;
    *result = destEnd;
    return result;
}

// FUNC_AT(0x000b2e70)
void*** PointerRemoveCopy(void ***result, void **first, void **last, void **dest, void *const *value) {
    for (; first != last; first++) {
        if (*first != *value)
            *dest++ = *first;
    }
    *result = dest;
    return result;
}

// FUNC_AT(0x000b2ec0)
void PointerUninitializedFillN(void **dest, uint32_t count, void *const *value) {
    for (; count > 0; count--, dest++) {
        if (dest != NULL)
            *dest = *value;
    }
}

// FUNC_AT(0x000b2f60)
void*** PointerRemove(void ***result, void **first, void **last, void *const *value) {
    while (first != last && *first != *value)
        first++;
    if (first == last) {
        *result = first;
        return result;
    }
    return PointerRemoveCopy(result, first + 1, last, first, value);
}

// FUNC_AT(0x000b2fc0)
void*** SimPointerVector::Erase(void ***result, void **where) {
    for (void **next = where + 1; next != last; next++)
        next[-1] = *next;
    last--;
    *result = where;
    return result;
}

// FUNC_AT(0x000b3130)
void SimPointerVector::Xlen() {
    SIMCONTAINERS_UNTESTED("SimPointerVector::Xlen");
    TreeThrow("vector<T> too long", kLengthErrorVtable, kLengthErrorThrowInfo);
}

// The original's catch block (free the new storage, rethrow) has nothing to catch here: copying pointers cannot
// throw.
// FUNC_AT(0x000b3b80)
void SimPointerVector::InsertN(void **where, uint32_t count, void *const *value) {
    void *copy = *value;        // copied first: it may live in the vector
    uint32_t capacity = first != NULL ? uint32_t(end - first) : 0;
    if (count == 0)
        return;
    uint32_t size = first != NULL ? uint32_t(last - first) : 0;
    if (kMaxPointers - size < count) {
        Xlen();
        return;
    }
    PointerVector *pointers = reinterpret_cast<PointerVector *>(this);
    if (capacity < size + count) {
        uint32_t grown = kMaxPointers - capacity / 2 < capacity ? 0 : capacity + capacity / 2;
        if (grown < size + count)
            grown = size + count;
        uint32_t bytes = grown * sizeof(void *);
        void **storage = static_cast<void **>(UMemory::FastAlloc(bytes, "STL"));
        void **at = PointerUninitializedCopy(first, where, storage);
        PointerUninitializedFillN(at, count, &copy);
        PointerUninitializedCopy(where, last, at + count);
        uint32_t newSize = count + (first != NULL ? uint32_t(last - first) : 0);
        if (first != NULL)
            PointerVectorDeallocate(first, uint32_t(end - first));
        end = reinterpret_cast<void **>(reinterpret_cast<uint8_t *>(storage) + bytes);
        last = storage + newSize;
        first = storage;
    } else if (uint32_t(last - where) < count) {
        // push_back, the one caller, only inserts at the end of a full vector
        SIMCONTAINERS_UNTESTED("SimPointerVector::InsertN in the middle");
        pointers->Ucopy(where, last, where + count);
        pointers->Ufill(last, count - uint32_t(last - where), &copy);
        last += count;
        FillPointers(where, last - count, &copy);
    } else {
        SIMCONTAINERS_UNTESTED("SimPointerVector::InsertN in the middle");
        void **oldLast = last;
        last = pointers->Ucopy(oldLast - count, oldLast, oldLast);
        void **ignored;
        PointerCopyBackwardTo(&ignored, where, oldLast - count, oldLast);
        FillPointers(where, where + count, &copy);
    }
}
