#ifndef DRIVING_ENGINE_RBTREE_H_
#define DRIVING_ENGINE_RBTREE_H_

// The layout of the game's std::map, std::multimap and std::set: MSVC 7's _Tree, Dinkumware's red-black tree, as
// the game compiled it. Every tree in the engine has this shape; what differs between them is the value in the
// node, and which compiled helpers each copy of the algorithm calls:
//
//   - the core's compiled copies (CoreContainers.h): insert_unique, insert_multi and find of a few maps and sets;
//   - the data layer's name maps and the CARP resolver map (data/Tree.h), whose bodies call the rotations and
//     iterator steps every map in the game shares, at their addresses;
//   - the attribute system's seven trees (data/AttributeContainers.h), each with its own compiled helpers.
//
// The bodies stay with their owners, each a port of the copies it replaces; only the layout is shared.
//
// A tree object is 12 bytes: the comparator and the allocator (empty classes, one byte between them - a byte
// nothing reads), the head node and the size. The head is the end() node: isNil set, its parent the root, its
// left the leftmost node and its right the rightmost. A node is the three links, the value (key first), the
// colour (red 0, black 1) and the isNil byte, padded to four bytes; the nodes come from UMemory::FastAlloc under
// the name "STL".

#include <stdint.h>

enum TreeColor : uint8_t {
    kTreeRed = 0,
    kTreeBlack = 1,
};

// A node: Node is the node type itself (so that its links have the type of the class built on this), Value the
// map's value_type.
template <class Node, class Value>
struct RbTreeNode {
    Node *left;          // +0x00
    Node *parent;        // +0x04 (the head's parent is the root)
    Node *right;         // +0x08
    Value value;         // +0x0c the key, then the mapped value
    uint8_t color;       // TreeColor
    uint8_t isNil;       // 1 only in the head
};

template <class NodeType>
struct RbTree {
    typedef NodeType Node;

    uint8_t allocator;   // +0x00 the empty comparator and allocator
    uint8_t unknown01[3];
    Node *head;          // +0x04
    uint32_t size;       // +0x08

    Node *Root() const { return head->parent; }
    Node *Begin() const { return head->left; }
    Node *End() const { return head; }
};

#endif // DRIVING_ENGINE_RBTREE_H_
