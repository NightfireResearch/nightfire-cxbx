#ifndef DRIVING_DATA_ATTRIBUTETREE_H_
#define DRIVING_DATA_ATTRIBUTETREE_H_

// The layout of the attribute system's std::maps and std::set (MSVC 7's _Tree, Dinkumware's red-black tree), as
// the game compiled them. Seven trees, seven node sizes (AttributeContainers.h), one algorithm: the bodies are
// written once, as function templates in AttributeContainers.cpp, and each address the game has for a tree's
// method gets a FUNC_AT'd member of that tree which calls them.
//
// A tree object is 12 bytes: the comparator and the allocator (empty classes, one byte between them - a byte
// nothing reads), the head node and the size. The head is the end() node: isNil set, its parent the root, its
// left the leftmost node and its right the rightmost. A node is the three links, the value (key first), the
// colour (red 0, black 1) and the isNil byte; the nodes are padded to four bytes and come from
// UMemory::FastAlloc under the name "STL".

#include <stdint.h>

enum AttributeTreeColor : uint8_t {
    kAttributeTreeRed = 0,
    kAttributeTreeBlack = 1,
};

template <class Node, class Value>
struct AttributeTreeNode {
    Node *left;          // +0x00
    Node *parent;        // +0x04 (the head's parent is the root)
    Node *right;         // +0x08
    Value value;         // +0x0c the key, then the mapped value
    uint8_t color;       // AttributeTreeColor
    uint8_t isNil;       // 1 only in the head
};

template <class NodeType>
struct AttributeTree {
    typedef NodeType Node;

    uint8_t allocator;   // +0x00 the empty comparator and allocator
    uint8_t unknown01[3];
    Node *head;          // +0x04
    uint32_t size;       // +0x08

    Node *Root() const { return head->parent; }
    Node *Begin() const { return head->left; }
    Node *End() const { return head; }
};

#endif // DRIVING_DATA_ATTRIBUTETREE_H_
