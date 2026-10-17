#ifndef DRIVING_RENDER_RENDERTREE_H_
#define DRIVING_RENDER_RENDERTREE_H_

// ---------------------------------------------------------------------------------------------------------------
// The red-black tree algorithms (MSVC 7's _Tree, Dinkumware's) as the renderer's sets and maps compiled them, written
// once as function templates over a tree type (engine/RbTree.h has the layout): the rotations, the minimum and
// maximum, stepping an iterator, _Erase, _Insert, erase at an iterator and over a range. Each compiled copy the game
// has gets a FUNC_AT'd member of its tree that calls these (StateManager.h, TextureContext.h). Where a copy calls a
// helper the linker shared with another tree of the same node size, these call their own: the same code, so the
// same result.
//
// Nodes come from UMemory::FastAlloc ("STL") and go back with FastFree at their size; no value here owns anything.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "../data/Tree.h"                 // TreeThrow and the STL's exception descriptions
#include "../engine/RbTree.h"
#include "../engine/UMemory.hpp"

namespace RenderTree {

template <class Node>
Node *Min(Node *node) {
    while (!node->left->isNil)
        node = node->left;
    return node;
}

template <class Node>
Node *Max(Node *node) {
    while (!node->right->isNil)
        node = node->right;
    return node;
}

// ++iterator: the leftmost of the right subtree, or up to the first ancestor this subtree is on the left of. The
// head (end) stays where it is.
template <class Node>
Node *Next(Node *node) {
    if (node->isNil)
        return node;
    if (!node->right->isNil)
        return Min(node->right);
    Node *parent = node->parent;
    while (!parent->isNil && node == parent->right) {
        node = parent;
        parent = parent->parent;
    }
    return parent;
}

// --iterator: end goes to the rightmost node; otherwise the rightmost of the left subtree, or up to the first
// ancestor this subtree is on the right of (staying put at the leftmost node).
template <class Node>
Node *Prev(Node *node) {
    if (node->isNil)
        return node->right;
    if (!node->left->isNil)
        return Max(node->left);
    Node *parent = node->parent;
    while (!parent->isNil && node == parent->left) {
        node = parent;
        parent = parent->parent;
    }
    if (!parent->isNil)
        node = parent;
    return node;
}

template <class Tree>
void Lrotate(Tree *tree, typename Tree::Node *where) {
    typename Tree::Node *node = where->right;
    where->right = node->left;
    if (!node->left->isNil)
        node->left->parent = where;
    node->parent = where->parent;
    if (where == tree->head->parent)
        tree->head->parent = node;
    else if (where == where->parent->left)
        where->parent->left = node;
    else
        where->parent->right = node;
    node->left = where;
    where->parent = node;
}

template <class Tree>
void Rrotate(Tree *tree, typename Tree::Node *where) {
    typename Tree::Node *node = where->left;
    where->left = node->right;
    if (!node->right->isNil)
        node->right->parent = where;
    node->parent = where->parent;
    if (where == tree->head->parent)
        tree->head->parent = node;
    else if (where == where->parent->right)
        where->parent->right = node;
    else
        where->parent->left = node;
    node->right = where;
    where->parent = node;
}

// _Buynode(left, parent, right, value, colour)
template <class Node, class Value>
Node *BuyNode(Node *left, Node *parent, Node *right, const Value &value, uint8_t color) {
    Node *node = static_cast<Node *>(UMemory::FastAlloc(sizeof(Node), "STL"));
    if (node != NULL) {
        node->left = left;
        node->parent = parent;
        node->right = right;
        node->value = value;
        node->color = color;
        node->isNil = 0;
    }
    return node;
}

// The constructor, inlined wherever the game makes one of these trees, given its new head (world/SoundMap.h's
// BuyMapHead). The empty allocator's byte is zero here; the original copies an uninitialised stack byte.
template <class Tree>
void Construct(Tree *tree, typename Tree::Node *head) {
    tree->allocator = 0;
    tree->head = head;
    head->isNil = 1;
    tree->head->parent = tree->head;
    tree->head->left = tree->head;
    tree->head->right = tree->head;
    tree->size = 0;
}

// _Erase: frees a subtree without rebalancing, right branches by recursion, left ones by iteration.
template <class Tree>
void EraseSubtree(Tree *tree, typename Tree::Node *node) {
    while (!node->isNil) {
        EraseSubtree(tree, node->right);
        typename Tree::Node *left = node->left;
        if (node != NULL)
            UMemory::FastFree(node, sizeof(*node));
        node = left;
    }
}

// _Insert: a new red node under `where` (on its left if `addLeft`), then rebalances.
template <class Tree, class Value>
typename Tree::Node *InsertAt(Tree *tree, bool addLeft, typename Tree::Node *where, const Value &value) {
    typedef typename Tree::Node Node;
    if (tree->size >= 0xffffffffu / sizeof(Value) - 1)
        TreeThrow("map/set<T> too long", kLengthErrorVtable, kLengthErrorThrowInfo);
    Node *node = BuyNode(tree->head, where, tree->head, value, kTreeRed);
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
    for (Node *at = node; at->parent->color == kTreeRed;) {
        if (at->parent == at->parent->parent->left) {
            Node *uncle = at->parent->parent->right;
            if (uncle->color == kTreeRed) {
                at->parent->color = kTreeBlack;
                uncle->color = kTreeBlack;
                at->parent->parent->color = kTreeRed;
                at = at->parent->parent;
            } else {
                if (at == at->parent->right) {
                    at = at->parent;
                    Lrotate(tree, at);
                }
                at->parent->color = kTreeBlack;
                at->parent->parent->color = kTreeRed;
                Rrotate(tree, at->parent->parent);
            }
        } else {
            Node *uncle = at->parent->parent->left;
            if (uncle->color == kTreeRed) {
                at->parent->color = kTreeBlack;
                uncle->color = kTreeBlack;
                at->parent->parent->color = kTreeRed;
                at = at->parent->parent;
            } else {
                if (at == at->parent->left) {
                    at = at->parent;
                    Rrotate(tree, at);
                }
                at->parent->color = kTreeBlack;
                at->parent->parent->color = kTreeRed;
                Lrotate(tree, at->parent->parent);
            }
        }
    }
    tree->head->parent->color = kTreeBlack;
    return node;
}

// erase(iterator): unlinks and frees the node, rebalances, answers the next node.
template <class Tree>
typename Tree::Node *EraseAt(Tree *tree, typename Tree::Node *erased) {
    typedef typename Tree::Node Node;
    Node *head = tree->head;
    if (erased->isNil)
        TreeThrow("invalid map/set<T> iterator", kOutOfRangeVtable, kOutOfRangeThrowInfo);
    Node *next = Next(erased);

    Node *node = erased;    // the node that leaves its place: the erased one or its successor
    Node *fix;              // the subtree that takes that node's place
    if (node->left->isNil) {
        fix = node->right;
    } else if (node->right->isNil) {
        fix = node->left;
    } else {
        node = next;
        fix = node->right;
    }
    Node *fixParent;
    if (node == erased) {
        fixParent = erased->parent;
        if (!fix->isNil)
            fix->parent = fixParent;
        if (head->parent == erased)
            head->parent = fix;
        else if (fixParent->left == erased)
            fixParent->left = fix;
        else
            fixParent->right = fix;
        if (head->left == erased)
            head->left = fix->isNil ? fixParent : Min(fix);
        if (head->right == erased)
            head->right = fix->isNil ? fixParent : Max(fix);
    } else {
        erased->left->parent = node;
        node->left = erased->left;
        if (node == erased->right) {
            fixParent = node;
        } else {
            fixParent = node->parent;
            if (!fix->isNil)
                fix->parent = fixParent;
            fixParent->left = fix;
            node->right = erased->right;
            erased->right->parent = node;
        }
        if (head->parent == erased)
            head->parent = node;
        else if (erased->parent->left == erased)
            erased->parent->left = node;
        else
            erased->parent->right = node;
        node->parent = erased->parent;
        uint8_t color = node->color;
        node->color = erased->color;
        erased->color = color;
    }

    if (erased->color == kTreeBlack) {
        while (fix != head->parent && fix->color == kTreeBlack) {
            if (fix == fixParent->left) {
                Node *sibling = fixParent->right;
                if (sibling->color == kTreeRed) {
                    sibling->color = kTreeBlack;
                    fixParent->color = kTreeRed;
                    Lrotate(tree, fixParent);
                    sibling = fixParent->right;
                }
                if (!sibling->isNil) {
                    if (sibling->left->color == kTreeBlack && sibling->right->color == kTreeBlack) {
                        sibling->color = kTreeRed;
                    } else {
                        if (sibling->right->color == kTreeBlack) {
                            sibling->left->color = kTreeBlack;
                            sibling->color = kTreeRed;
                            Rrotate(tree, sibling);
                            sibling = fixParent->right;
                        }
                        sibling->color = fixParent->color;
                        fixParent->color = kTreeBlack;
                        sibling->right->color = kTreeBlack;
                        Lrotate(tree, fixParent);
                        break;
                    }
                }
            } else {
                Node *sibling = fixParent->left;
                if (sibling->color == kTreeRed) {
                    sibling->color = kTreeBlack;
                    fixParent->color = kTreeRed;
                    Rrotate(tree, fixParent);
                    sibling = fixParent->left;
                }
                if (!sibling->isNil) {
                    if (sibling->right->color == kTreeBlack && sibling->left->color == kTreeBlack) {
                        sibling->color = kTreeRed;
                    } else {
                        if (sibling->left->color == kTreeBlack) {
                            sibling->right->color = kTreeBlack;
                            sibling->color = kTreeRed;
                            Lrotate(tree, sibling);
                            sibling = fixParent->left;
                        }
                        sibling->color = fixParent->color;
                        fixParent->color = kTreeBlack;
                        sibling->left->color = kTreeBlack;
                        Rrotate(tree, fixParent);
                        break;
                    }
                }
            }
            fix = fixParent;
            fixParent = fix->parent;
        }
        fix->color = kTreeBlack;
    }

    UMemory::FastFree(erased, sizeof(Node));
    if (tree->size > 0)
        tree->size--;
    return next;
}

// erase(first, last): everything is cleared at once, otherwise one node at a time.
template <class Tree>
typename Tree::Node *EraseRange(Tree *tree, typename Tree::Node *first, typename Tree::Node *last) {
    if (first == tree->head->left && last == tree->head) {
        EraseSubtree(tree, tree->head->parent);
        tree->head->parent = tree->head;
        tree->size = 0;
        tree->head->left = tree->head;
        tree->head->right = tree->head;
        return tree->head->left;
    }
    while (first != last) {
        typename Tree::Node *erased = first;
        first = Next(first);
        EraseAt(tree, erased);
    }
    return first;
}

// The destructor: everything erased, the head freed.
template <class Tree>
void Destroy(Tree *tree) {
    EraseRange(tree, tree->head->left, tree->head);
    if (tree->head != NULL)
        UMemory::FastFree(tree->head, sizeof(*tree->head));
    tree->head = NULL;
    tree->size = 0;
}

}  // namespace RenderTree

#endif // DRIVING_RENDER_RENDERTREE_H_
