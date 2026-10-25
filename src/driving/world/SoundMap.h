#ifndef DRIVING_WORLD_SOUNDMAP_H_
#define DRIVING_WORLD_SOUNDMAP_H_

// ---------------------------------------------------------------------------------------------------------------
// Two std::map head allocators the linker shared out among many maps (Ghidra files the first under WSoundMap, whose
// map WSoundGroup's constructor makes with it). WSoundGroup and WSound are SoundGroup.h's.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "../data/Tree.h"                 // TreeNode: the 0x18-byte map node
#include "../engine/RbTree.h"
#include "../engine/UMemory.hpp"
#include "../engine/URefCounter.h"        // RefCounterNode: the 0x98-byte one

// ---- the heads of two families of maps (each a copy of _Tree::_Buynode() the linker folded together)

// _Tree::_Buynode() for a map's head, as each map compiles it: unlinked and black (the constructor then marks it
// nil and links it to itself). The allocator's construct() guards each link's store with a test of the link's
// address, which is never null, so a failed allocation writes through NULL as the original does.
template <class Node>
Node *BuyMapHead() {
    Node *node = static_cast<Node *>(UMemory::FastAlloc(sizeof(Node), "STL"));
    if (node != NULL)
        node->left = NULL;
    node->parent = NULL;
    node->right = NULL;
    node->color = kTreeBlack;
    node->isNil = 0;
    return node;
}

// Every map whose node is 0x18 bytes: the data layer's maps, WSoundGroup's, the collision manager's article map,
// the texture context and particle managers', the simulation's and others.
TreeNode* MapBuyHead();                                                     // 0x00094030
// Every URefCounter<T>'s map (0x98-byte nodes).
RefCounterNode* RefCounterMapBuyHead();                                     // 0x00094070

#endif // DRIVING_WORLD_SOUNDMAP_H_
