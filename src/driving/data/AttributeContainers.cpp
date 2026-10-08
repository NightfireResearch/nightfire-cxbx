#include "AttributeContainers.h"
#include "AttributeSystem.h"
#include "AttributeUntested.h"
#include "StdStreams.h"
#include "../engine/UMemory.hpp"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// The attribute system's containers (0x00052600..0x00055c60, 0x00055f60..0x000573a0, 0x00057ea0, 0x00058470..
// 0x00058fd0): MSVC 7's red-black tree, std::vector and std::sort, as the game compiled them for its own types.
//
// The tree algorithms are written once below, as function templates over a tree type, from the listings of the
// game's copies (which are Dinkumware's _Tree for VC 7.0, line for line): the rotations, stepping an iterator,
// lower_bound, _Buynode, _Erase, _Insert, erase at an iterator and over a range, insert, find and _Copy. Each
// address the game has gets a FUNC_AT'd member of its tree calling them. Where the game calls a helper of another
// tree with the same node size (the linker folded identical copies; the attribute map's minimum and maximum are
// at 0x0009c350 and 0x0009c330, for one), the templates call their own: the same code, so the same result.
//
// What differs between the trees is in TreeTraits: the key, its comparison (the game's _stricmp, or strcmp for
// the string set), and how a node's value is copied in and destroyed - an AttributeValue that owns its data
// allocates a copy, a collection or a field map is constructed and torn down. Every node and head comes from
// UMemory::FastAlloc ("STL") and goes back with FastFree at its size.
//
// The "too long" and "invalid iterator" exceptions the STL throws are ported (the game's std::string, logic_error
// and _CxxThrowException), but nothing reaches them.
// ---------------------------------------------------------------------------------------------------------------

#define CRT_stricmp ((int (*)(const char *, const char *))0x00134537)

// The pieces of the game's exception machinery the STL's throws use.
#define CxxThrowException ((void (__stdcall *)(void *, const void *))0x001325ad)

// std::length_error's and std::out_of_range's vtables and throw descriptions.
#define LengthErrorVtable ((void *)0x00189eec)
#define LengthErrorThrowInfo ((const void *)0x001a89bc)
#define OutOfRangeVtable ((void *)0x0018a29c)
#define OutOfRangeThrowInfo ((const void *)0x001a8bd8)

namespace {

// _THROW(error, message): a std::string of the message, the exception constructed from it (logic_error's
// constructor, then the derived class's vtable), thrown. Does not return.
void ThrowStl(const char *message, void *vtable, const void *throwInfo) {
    GameStd::String text;
    text.capacity = 15;
    text.size = 0;
    text.text.buffer[0] = '\0';
    text.AssignText(message, static_cast<uint32_t>(strlen(message)));
    GameStd::LogicError error;
    error.Construct(&text);
    error.vtable = vtable;
    CxxThrowException(&error, throwInfo);
}

// ---------------------------------------------------------------------------------------------------------------
// What each tree is made of.

template <class Tree>
struct TreeTraits;

template <>
struct TreeTraits<AttributeMap> {
    typedef const char *Key;
    static Key KeyOf(const AttributeMapValue &value) { return value.name; }
    static bool Less(Key a, Key b) { return CRT_stricmp(a, b) < 0; }
    static void ConstructValue(AttributeMapValue *at, const AttributeMapValue &value) {
        at->name = value.name;
        at->value.ConstructCopy(value.value);
    }
    static void DestroyValue(AttributeNode *node) { node->DestroyValue(); }
};

template <>
struct TreeTraits<AttributeFieldMap> {
    typedef const char *Key;
    static Key KeyOf(const AttributeFieldEntry &value) { return value.name; }
    static bool Less(Key a, Key b) { return CRT_stricmp(a, b) < 0; }
    static void ConstructValue(AttributeFieldEntry *at, const AttributeFieldEntry &value) { *at = value; }
    static void DestroyValue(AttributeFieldNode *) {}
};

template <>
struct TreeTraits<ExtensionClassMap> {
    typedef const char *Key;
    static Key KeyOf(const ExtensionClassEntry &value) { return value.className; }
    static bool Less(Key a, Key b) { return CRT_stricmp(a, b) < 0; }
    static void ConstructValue(ExtensionClassEntry *at, const ExtensionClassEntry &value) {
        at->className = value.className;
        at->fields.ConstructCopy(value.fields);
    }
    static void DestroyValue(ExtensionClassNode *node) { node->DestroyValue(); }
};

template <>
struct TreeTraits<ExtensionTypeMap> {
    typedef uint32_t Key;
    static Key KeyOf(const ExtensionTypeEntry &value) { return value.type; }
    static bool Less(Key a, Key b) { return a < b; }
    static void ConstructValue(ExtensionTypeEntry *at, const ExtensionTypeEntry &value) { *at = value; }
    static void DestroyValue(ExtensionTypeNode *) {}
};

template <>
struct TreeTraits<EditConfigMap> {
    static void DestroyValue(EditConfigNode *) {}
};

template <>
struct TreeTraits<CollectionMap> {
    typedef CollectionKey Key;
    static const CollectionKey &KeyOf(const CollectionEntry &value) { return value.key; }
    static bool Less(const CollectionKey &a, const CollectionKey &b) {
        int order = CRT_stricmp(a.className, b.className);
        if (order == 0)
            order = CRT_stricmp(a.name, b.name);
        return order < 0;
    }
    static void ConstructValue(CollectionEntry *at, const CollectionEntry &value) {
        at->key = value.key;
        at->collection.ConstructCopy(value.collection);
    }
    static void DestroyValue(CollectionNode *node) { node->DestroyValue(); }
};

template <>
struct TreeTraits<StringSet> {
    typedef const char *Key;
    static Key KeyOf(const char *value) { return value; }
    static bool Less(Key a, Key b) { return strcmp(a, b) < 0; }
    static void ConstructValue(const char **at, const char *value) { *at = value; }
    static void DestroyValue(StringNode *) {}
};

// ---------------------------------------------------------------------------------------------------------------
// The algorithms.

template <class Node>
Node *TreeMin(Node *node) {
    while (!node->left->isNil)
        node = node->left;
    return node;
}

template <class Node>
Node *TreeMax(Node *node) {
    while (!node->right->isNil)
        node = node->right;
    return node;
}

// ++iterator: the leftmost of the right subtree, or up to the first ancestor this subtree is on the left of. The
// head (end) stays where it is.
template <class Node>
Node *TreeNext(Node *node) {
    if (node->isNil)
        return node;
    if (!node->right->isNil)
        return TreeMin(node->right);
    Node *parent;
    while (!(parent = node->parent)->isNil && node == parent->right)
        node = parent;
    return parent;
}

// --iterator: end goes to the rightmost node; otherwise the rightmost of the left subtree, or up to the first
// ancestor this subtree is on the right of (staying put at the leftmost node).
template <class Node>
Node *TreePrev(Node *node) {
    if (node->isNil)
        return node->right;
    if (!node->left->isNil)
        return TreeMax(node->left);
    Node *parent;
    while (!(parent = node->parent)->isNil && node == parent->left)
        node = parent;
    if (!parent->isNil)
        node = parent;
    return node;
}

template <class Tree>
void TreeLrotate(Tree *tree, typename Tree::Node *where) {
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
void TreeRrotate(Tree *tree, typename Tree::Node *where) {
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

template <class Tree>
typename Tree::Node *TreeLbound(Tree *tree, const typename TreeTraits<Tree>::Key &key) {
    typedef TreeTraits<Tree> Traits;
    typename Tree::Node *node = tree->head->parent;
    typename Tree::Node *where = tree->head;
    while (!node->isNil) {
        if (Traits::Less(Traits::KeyOf(node->value), key)) {
            node = node->right;
        } else {
            where = node;
            node = node->left;
        }
    }
    return where;
}

template <class Tree, class Value>
typename Tree::Node *TreeBuynode(typename Tree::Node *left, typename Tree::Node *parent, typename Tree::Node *right,
                                 const Value &value, uint8_t color) {
    typedef typename Tree::Node Node;
    Node *node = static_cast<Node *>(UMemory::FastAlloc(sizeof(Node), "STL"));
    if (node != NULL) {
        node->left = left;
        node->parent = parent;
        node->right = right;
        TreeTraits<Tree>::ConstructValue(&node->value, value);
        node->color = color;
        node->isNil = 0;
    }
    return node;
}

// The head: unlinked and black (the constructor then marks it nil and links it to itself). The allocator's
// construct() guards each link's store with a test of the link's address, which is never null, so a failed
// allocation writes through NULL as the original does.
template <class Node>
Node *TreeBuyHead() {
    Node *node = static_cast<Node *>(UMemory::FastAlloc(sizeof(Node), "STL"));
    if (node != NULL)
        node->left = NULL;
    node->parent = NULL;
    node->right = NULL;
    node->color = kTreeBlack;
    node->isNil = 0;
    return node;
}

// The tree's constructor, inlined wherever the game makes one. `allocator` is the byte the empty allocator
// object's copy takes (whatever its temporary's stack slot held).
template <class Tree>
void TreeConstruct(Tree *tree, uint8_t allocator) {
    tree->allocator = allocator;
    tree->head = TreeBuyHead<typename Tree::Node>();
    tree->head->isNil = 1;
    tree->head->parent = tree->head;
    tree->head->left = tree->head;
    tree->head->right = tree->head;
    tree->size = 0;
}

template <class Tree>
void TreeEraseSubtree(Tree *tree, typename Tree::Node *root) {
    for (typename Tree::Node *node = root; !node->isNil; root = node) {
        TreeEraseSubtree(tree, node->right);
        node = node->left;
        TreeTraits<Tree>::DestroyValue(root);
        if (root != NULL)
            UMemory::FastFree(root, sizeof(*root));
    }
}

template <class Tree, class Value>
typename Tree::Node *TreeInsert(Tree *tree, bool addLeft, typename Tree::Node *where, const Value &value) {
    typedef typename Tree::Node Node;
    if (tree->size >= 0xffffffffu / sizeof(Value) - 1) {
        ATTRIBUTE_UNTESTED("an attribute tree's insert past max_size");
        ThrowStl("map/set<T> too long", LengthErrorVtable, LengthErrorThrowInfo);
    }
    Node *node = TreeBuynode<Tree>(tree->head, where, tree->head, value, kTreeRed);
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
                    TreeLrotate(tree, at);
                }
                at->parent->color = kTreeBlack;
                at->parent->parent->color = kTreeRed;
                TreeRrotate(tree, at->parent->parent);
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
                    TreeRrotate(tree, at);
                }
                at->parent->color = kTreeBlack;
                at->parent->parent->color = kTreeRed;
                TreeLrotate(tree, at->parent->parent);
            }
        }
    }
    tree->head->parent->color = kTreeBlack;
    return node;
}

// erase(iterator): unlinks and frees the node, rebalances, answers the next node.
template <class Tree>
typename Tree::Node *TreeErase(Tree *tree, typename Tree::Node *erased) {
    typedef typename Tree::Node Node;
    if (erased->isNil) {
        ATTRIBUTE_UNTESTED("an attribute tree's erase at end()");
        ThrowStl("invalid map/set<T> iterator", OutOfRangeVtable, OutOfRangeThrowInfo);
    }
    Node *next = TreeNext(erased);
    Node *fix;          // the node to recolour as needed
    Node *fixParent;    // its parent (fix may be the head)
    Node *node = erased;
    if (node->left->isNil) {
        fix = node->right;
    } else if (node->right->isNil) {
        fix = node->left;
    } else {            // two subtrees: the successor takes the erased node's place
        node = next;
        fix = node->right;
    }
    if (node == erased) {
        fixParent = erased->parent;
        if (!fix->isNil)
            fix->parent = fixParent;
        if (tree->head->parent == erased)
            tree->head->parent = fix;
        else if (fixParent->left == erased)
            fixParent->left = fix;
        else
            fixParent->right = fix;
        if (tree->head->left == erased)
            tree->head->left = fix->isNil ? fixParent : TreeMin(fix);
        if (tree->head->right == erased)
            tree->head->right = fix->isNil ? fixParent : TreeMax(fix);
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
        if (tree->head->parent == erased)
            tree->head->parent = node;
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
        for (; fix != tree->head->parent && fix->color == kTreeBlack;
             fix = fixParent, fixParent = fix->parent) {
            if (fix == fixParent->left) {
                node = fixParent->right;
                if (node->color == kTreeRed) {
                    node->color = kTreeBlack;
                    fixParent->color = kTreeRed;
                    TreeLrotate(tree, fixParent);
                    node = fixParent->right;
                }
                if (node->isNil) {
                    fix = fixParent;   // (shouldn't happen)
                } else if (node->left->color == kTreeBlack && node->right->color == kTreeBlack) {
                    node->color = kTreeRed;
                    fix = fixParent;
                } else {
                    if (node->right->color == kTreeBlack) {
                        node->left->color = kTreeBlack;
                        node->color = kTreeRed;
                        TreeRrotate(tree, node);
                        node = fixParent->right;
                    }
                    node->color = fixParent->color;
                    fixParent->color = kTreeBlack;
                    node->right->color = kTreeBlack;
                    TreeLrotate(tree, fixParent);
                    break;
                }
            } else {
                node = fixParent->left;
                if (node->color == kTreeRed) {
                    node->color = kTreeBlack;
                    fixParent->color = kTreeRed;
                    TreeRrotate(tree, fixParent);
                    node = fixParent->left;
                }
                if (node->isNil) {
                    fix = fixParent;
                } else if (node->right->color == kTreeBlack && node->left->color == kTreeBlack) {
                    node->color = kTreeRed;
                    fix = fixParent;
                } else {
                    if (node->left->color == kTreeBlack) {
                        node->right->color = kTreeBlack;
                        node->color = kTreeRed;
                        TreeLrotate(tree, node);
                        node = fixParent->left;
                    }
                    node->color = fixParent->color;
                    fixParent->color = kTreeBlack;
                    node->left->color = kTreeBlack;
                    TreeRrotate(tree, fixParent);
                    break;
                }
            }
        }
        fix->color = kTreeBlack;
    }
    TreeTraits<Tree>::DestroyValue(erased);
    UMemory::FastFree(erased, sizeof(Node));
    if (tree->size > 0)
        tree->size--;
    return next;
}

// erase(first, last): all of it is clear() - the nodes freed without rebalancing - else one at a time.
template <class Tree>
typename Tree::Node *TreeEraseRange(Tree *tree, typename Tree::Node *first, typename Tree::Node *last) {
    if (first == tree->head->left && last == tree->head) {
        TreeEraseSubtree(tree, tree->head->parent);
        tree->head->parent = tree->head;
        tree->size = 0;
        tree->head->left = tree->head;
        tree->head->right = tree->head;
        return tree->head->left;
    }
    while (first != last) {
        typename Tree::Node *erased = first;
        first = TreeNext(first);
        TreeErase(tree, erased);
    }
    return first;
}

// The map's destructor: everything erased, the head freed.
template <class Tree>
void TreeDestruct(Tree *tree) {
    TreeEraseRange(tree, tree->head->left, tree->head);
    if (tree->head != NULL)
        UMemory::FastFree(tree->head, sizeof(*tree->head));
    tree->head = NULL;
    tree->size = 0;
}

// insert(value) of a map or set (unique keys): where the key goes, and whether it was new.
template <class Tree, class Value>
typename Tree::Node *TreeInsertUnique(Tree *tree, const Value &value, bool *inserted) {
    typedef TreeTraits<Tree> Traits;
    typedef typename Tree::Node Node;
    Node *tryNode = tree->head->parent;
    Node *where = tree->head;
    bool addLeft = true;   // to the head's left if the tree is empty
    while (!tryNode->isNil) {
        where = tryNode;
        addLeft = Traits::Less(Traits::KeyOf(value), Traits::KeyOf(tryNode->value));
        tryNode = addLeft ? tryNode->left : tryNode->right;
    }
    Node *at = where;
    if (addLeft) {
        if (at == tree->head->left) {
            *inserted = true;
            return TreeInsert(tree, true, where, value);
        }
        at = TreePrev(at);
    }
    if (Traits::Less(Traits::KeyOf(at->value), Traits::KeyOf(value))) {
        *inserted = true;
        return TreeInsert(tree, addLeft, where, value);
    }
    *inserted = false;
    return at;
}

template <class Tree>
typename Tree::Node *TreeFind(Tree *tree, const typename TreeTraits<Tree>::Key &key) {
    typedef TreeTraits<Tree> Traits;
    typename Tree::Node *where = TreeLbound(tree, key);
    if (where == tree->head || Traits::Less(key, Traits::KeyOf(where->value)))
        return tree->head;
    return where;
}

// _Copy(root, where): a copy of the subtree under `where`, colours and all.
template <class Tree>
typename Tree::Node *TreeCopy(Tree *tree, typename Tree::Node *root, typename Tree::Node *where) {
    typename Tree::Node *newRoot = tree->head;
    if (!root->isNil) {
        typename Tree::Node *node = TreeBuynode<Tree>(tree->head, where, tree->head, root->value, root->color);
        if (newRoot->isNil)
            newRoot = node;
        node->left = TreeCopy(tree, root->left, node);
        node->right = TreeCopy(tree, root->right, node);
    }
    return newRoot;
}

template <class Tree>
void TreeCopyFrom(Tree *tree, const Tree &other) {
    tree->head->parent = TreeCopy(tree, other.head->parent, tree->head);
    tree->size = other.size;
    if (!tree->head->parent->isNil) {
        tree->head->left = TreeMin(tree->head->parent);
        tree->head->right = TreeMax(tree->head->parent);
    } else {
        tree->head->left = tree->head;
        tree->head->right = tree->head;
    }
}

// A whole value-construct-insert-destroy sequence as the game's operator[] does it, for the two maps whose
// operator[] is a function of its own.
template <class Tree, class Entry>
typename Tree::Node *TreeInsertEntry(Tree *tree, const Entry &entry) {
    bool inserted;
    return TreeInsertUnique(tree, entry, &inserted);
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// The entries. Erasing at an iterator is data-dead in every tree: the game only ever erases whole trees, which
// erase(first, last) does as clear(); so are the minimum and maximum entries and the iterator increments only
// those erases call (the templates use their own). They are marked provisional.
//
// AttributeMap (0x1c nodes).

// FUNC_AT(0x00056230)
void AttributeNode::DestroyValue() {
    value.value.Destruct();
}

// FUNC_AT(0x000561f0)
void AttributeMapValue::Destruct() {
    value.Destruct();
}

// FUNC_AT(0x00052720)
void AttributeMap::Iterator::Inc() {
    node = TreeNext(node);
}

// FUNC_AT(0x000526c0)
void AttributeMap::Iterator::Dec() {
    node = TreePrev(node);
}

// FUNC_AT(0x00052600)
void AttributeMap::Rrotate(AttributeNode *node) {
    TreeRrotate(this, node);
}

// FUNC_AT(0x00052c70)
void AttributeMap::Lrotate(AttributeNode *node) {
    TreeLrotate(this, node);
}

// FUNC_AT(0x00052d90)
AttributeNode* AttributeMap::Lbound(const char *const &key) {
    return TreeLbound(this, key);
}

// FUNC_AT(0x00056320)
AttributeNode* AttributeMap::Buynode(AttributeNode *left, AttributeNode *parent, AttributeNode *right,
                                     const AttributeMapValue &value, uint8_t color) {
    return TreeBuynode<AttributeMap>(left, parent, right, value, color);
}

// FUNC_AT(0x000565b0)
AttributeMap::Iterator* AttributeMap::Insert(Iterator *result, bool addLeft, AttributeNode *where,
                                             const AttributeMapValue &value) {
    result->node = TreeInsert(this, addLeft, where, value);
    return result;
}

// FUNC_AT(0x00056b90)
AttributeMap::InsertResult* AttributeMap::InsertUnique(InsertResult *result, const AttributeMapValue &value) {
    result->position.node = TreeInsertUnique(this, value, &result->inserted);
    return result;
}

// FUNC_AT(0x00056c60)
AttributeMap::Iterator* AttributeMap::Erase(Iterator *result, Iterator where) {
    ATTRIBUTE_UNTESTED("AttributeMap::Erase");
    result->node = TreeErase(this, where.node);
    return result;
}

// FUNC_AT(0x00057070)
void AttributeMap::EraseSubtree(AttributeNode *node) {
    TreeEraseSubtree(this, node);
}

// FUNC_AT(0x000573a0)
AttributeMap::Iterator* AttributeMap::EraseRange(Iterator *result, Iterator first, Iterator last) {
    result->node = TreeEraseRange(this, first.node, last.node);
    return result;
}

// A new value starts as kAttributeNone with no data (the game leaves its data word unset; nothing reads it before
// the caller assigns the real value).
// FUNC_AT(0x000572e0)
AttributeValue* AttributeMap::Lookup(const char *const &name) {
    AttributeValue none;
    none.count = 0;
    none.flags = 0;
    none.type = kAttributeNone;
    none.data = NULL;
    AttributeMapValue entry;
    entry.name = name;
    entry.value.ConstructCopy(none);
    AttributeNode *node = TreeInsertEntry(this, entry);
    entry.value.Destruct();
    return &node->value.value;
}

// FUNC_AT(0x00057ea0)
void AttributeMap::Destruct() {
    TreeDestruct(this);
}

// ---------------------------------------------------------------------------------------------------------------
// AttributeFieldMap (0x2c nodes).

// FUNC_AT(0x00052b40)
void AttributeFieldMap::Iterator::Inc() {
    ATTRIBUTE_UNTESTED("AttributeFieldMap::Iterator::Inc");
    node = TreeNext(node);
}

// FUNC_AT(0x00052ef0)
void AttributeFieldMap::Iterator::Dec() {
    node = TreePrev(node);
}

// FUNC_AT(0x000527c0)
AttributeFieldNode* AttributeFieldMap::Max(AttributeFieldNode *node) {
    ATTRIBUTE_UNTESTED("AttributeFieldMap::Max");
    return TreeMax(node);
}

// FUNC_AT(0x00052980)
AttributeFieldNode* AttributeFieldMap::Min(AttributeFieldNode *node) {
    ATTRIBUTE_UNTESTED("AttributeFieldMap::Min");
    return TreeMin(node);
}

// FUNC_AT(0x00052de0)
AttributeFieldNode* AttributeFieldMap::Lbound(const char *const &key) {
    return TreeLbound(this, key);
}

// FUNC_AT(0x00052e30)
void AttributeFieldMap::Lrotate(AttributeFieldNode *node) {
    TreeLrotate(this, node);
}

// FUNC_AT(0x00052e90)
void AttributeFieldMap::Rrotate(AttributeFieldNode *node) {
    TreeRrotate(this, node);
}

// FUNC_AT(0x00053430)
AttributeFieldNode* AttributeFieldMap::Buynode(AttributeFieldNode *left, AttributeFieldNode *parent,
                                               AttributeFieldNode *right, const AttributeFieldEntry &value,
                                               uint8_t color) {
    return TreeBuynode<AttributeFieldMap>(left, parent, right, value, color);
}

// FUNC_AT(0x000536c0)
AttributeFieldNode* AttributeFieldMap::BuyHead() {
    return TreeBuyHead<AttributeFieldNode>();
}

// FUNC_AT(0x00053780)
void AttributeFieldMap::EraseSubtree(AttributeFieldNode *node) {
    TreeEraseSubtree(this, node);
}

// FUNC_AT(0x00053ba0)
AttributeFieldNode* AttributeFieldMap::Copy(AttributeFieldNode *root, AttributeFieldNode *where) {
    return TreeCopy(this, root, where);
}

// FUNC_AT(0x00053df0)
void AttributeFieldMap::CopyFrom(const AttributeFieldMap &other) {
    TreeCopyFrom(this, other);
}

// FUNC_AT(0x000540a0)
AttributeFieldMap::Iterator* AttributeFieldMap::Insert(Iterator *result, bool addLeft, AttributeFieldNode *where,
                                                       const AttributeFieldEntry &value) {
    result->node = TreeInsert(this, addLeft, where, value);
    return result;
}

// FUNC_AT(0x00054f30)
AttributeFieldMap::Iterator* AttributeFieldMap::Erase(Iterator *result, Iterator where) {
    ATTRIBUTE_UNTESTED("AttributeFieldMap::Erase");
    result->node = TreeErase(this, where.node);
    return result;
}

// FUNC_AT(0x00055200)
AttributeFieldMap::InsertResult* AttributeFieldMap::InsertUnique(InsertResult *result,
                                                                 const AttributeFieldEntry &value) {
    result->position.node = TreeInsertUnique(this, value, &result->inserted);
    return result;
}

// FUNC_AT(0x000559d0)
AttributeFieldMap::Iterator* AttributeFieldMap::EraseRange(Iterator *result, Iterator first, Iterator last) {
    result->node = TreeEraseRange(this, first.node, last.node);
    return result;
}

// FUNC_AT(0x00055c60)
AttributeFieldMap* AttributeFieldMap::ConstructCopy(const AttributeFieldMap &other) {
    TreeConstruct(this, other.allocator);
    CopyFrom(other);
    return this;
}

// FUNC_AT(0x00055af0)
void AttributeFieldMap::Destruct() {
    TreeDestruct(this);
}

// ---------------------------------------------------------------------------------------------------------------
// ExtensionClassMap (0x20 nodes).

// FUNC_AT(0x00055f60)
void ExtensionClassEntry::Destruct() {
    fields.Destruct();
}

// FUNC_AT(0x00055fa0)
void ExtensionClassNode::DestroyValue() {
    value.fields.Destruct();
}

// FUNC_AT(0x00052a20)
void ExtensionClassMap::Iterator::Inc() {
    ATTRIBUTE_UNTESTED("ExtensionClassMap::Iterator::Inc");
    node = TreeNext(node);
}

// FUNC_AT(0x000527e0)
void ExtensionClassMap::Rrotate(ExtensionClassNode *node) {
    TreeRrotate(this, node);
}

// FUNC_AT(0x00053580)
ExtensionClassNode* ExtensionClassMap::BuyHead() {
    return TreeBuyHead<ExtensionClassNode>();
}

// FUNC_AT(0x00056270)
ExtensionClassNode* ExtensionClassMap::Buynode(ExtensionClassNode *left, ExtensionClassNode *parent,
                                               ExtensionClassNode *right, const ExtensionClassEntry &value,
                                               uint8_t color) {
    return TreeBuynode<ExtensionClassMap>(left, parent, right, value, color);
}

// FUNC_AT(0x000563d0)
ExtensionClassMap::Iterator* ExtensionClassMap::Insert(Iterator *result, bool addLeft, ExtensionClassNode *where,
                                                       const ExtensionClassEntry &value) {
    result->node = TreeInsert(this, addLeft, where, value);
    return result;
}

// FUNC_AT(0x00056790)
ExtensionClassMap::InsertResult* ExtensionClassMap::InsertUnique(InsertResult *result,
                                                                 const ExtensionClassEntry &value) {
    result->position.node = TreeInsertUnique(this, value, &result->inserted);
    return result;
}

// FUNC_AT(0x00056860)
ExtensionClassMap::Iterator* ExtensionClassMap::Erase(Iterator *result, Iterator where) {
    ATTRIBUTE_UNTESTED("ExtensionClassMap::Erase");
    result->node = TreeErase(this, where.node);
    return result;
}

// FUNC_AT(0x00056b40)
void ExtensionClassMap::EraseSubtree(ExtensionClassNode *node) {
    TreeEraseSubtree(this, node);
}

// FUNC_AT(0x00057460)
ExtensionClassMap::Iterator* ExtensionClassMap::EraseRange(Iterator *result, Iterator first, Iterator last) {
    result->node = TreeEraseRange(this, first.node, last.node);
    return result;
}

// operator[]: an empty field map is made, copied into the entry, the entry inserted (copying the map again into
// the node), and both temporaries torn down. The empty map's allocator byte comes from the stack slot of the
// argument - the low byte of the key's address.
// FUNC_AT(0x00056f70)
AttributeFieldMap* ExtensionClassMap::Lookup(const char *const &className) {
    AttributeFieldMap empty;
    TreeConstruct(&empty, static_cast<uint8_t>(reinterpret_cast<uintptr_t>(&className)));
    ExtensionClassEntry entry;
    entry.className = className;
    entry.fields.ConstructCopy(empty);
    ExtensionClassNode *node = TreeInsertEntry(this, entry);
    entry.fields.Destruct();
    empty.Destruct();
    return &node->value.fields;
}

// ---------------------------------------------------------------------------------------------------------------
// ExtensionTypeMap (0x28 nodes).

// FUNC_AT(0x00052a80)
void ExtensionTypeMap::Iterator::Inc() {
    ATTRIBUTE_UNTESTED("ExtensionTypeMap::Iterator::Inc");
    node = TreeNext(node);
}

// FUNC_AT(0x00053010)
void ExtensionTypeMap::Iterator::Dec() {
    node = TreePrev(node);
}

// FUNC_AT(0x00052840)
ExtensionTypeNode* ExtensionTypeMap::Max(ExtensionTypeNode *node) {
    ATTRIBUTE_UNTESTED("ExtensionTypeMap::Max");
    return TreeMax(node);
}

// FUNC_AT(0x000529a0)
ExtensionTypeNode* ExtensionTypeMap::Min(ExtensionTypeNode *node) {
    ATTRIBUTE_UNTESTED("ExtensionTypeMap::Min");
    return TreeMin(node);
}

// FUNC_AT(0x00052f50)
void ExtensionTypeMap::Lrotate(ExtensionTypeNode *node) {
    TreeLrotate(this, node);
}

// FUNC_AT(0x00052fb0)
void ExtensionTypeMap::Rrotate(ExtensionTypeNode *node) {
    TreeRrotate(this, node);
}

// FUNC_AT(0x000534a0)
ExtensionTypeNode* ExtensionTypeMap::Buynode(ExtensionTypeNode *left, ExtensionTypeNode *parent,
                                             ExtensionTypeNode *right, const ExtensionTypeEntry &value,
                                             uint8_t color) {
    return TreeBuynode<ExtensionTypeMap>(left, parent, right, value, color);
}

// FUNC_AT(0x000535c0)
ExtensionTypeNode* ExtensionTypeMap::BuyHead() {
    return TreeBuyHead<ExtensionTypeNode>();
}

// FUNC_AT(0x00053700)
void ExtensionTypeMap::EraseSubtree(ExtensionTypeNode *node) {
    TreeEraseSubtree(this, node);
}

// FUNC_AT(0x00054280)
ExtensionTypeMap::Iterator* ExtensionTypeMap::Insert(Iterator *result, bool addLeft, ExtensionTypeNode *where,
                                                     const ExtensionTypeEntry &value) {
    result->node = TreeInsert(this, addLeft, where, value);
    return result;
}

// FUNC_AT(0x000552d0)
ExtensionTypeMap::InsertResult* ExtensionTypeMap::InsertUnique(InsertResult *result, const ExtensionTypeEntry &value) {
    result->position.node = TreeInsertUnique(this, value, &result->inserted);
    return result;
}

// FUNC_AT(0x00054990)
ExtensionTypeMap::Iterator* ExtensionTypeMap::Erase(Iterator *result, Iterator where) {
    ATTRIBUTE_UNTESTED("ExtensionTypeMap::Erase");
    result->node = TreeErase(this, where.node);
    return result;
}

// FUNC_AT(0x00055560)
ExtensionTypeMap::Iterator* ExtensionTypeMap::EraseRange(Iterator *result, Iterator first, Iterator last) {
    result->node = TreeEraseRange(this, first.node, last.node);
    return result;
}

// ---------------------------------------------------------------------------------------------------------------
// EditConfigMap (0x30 nodes): always empty in the retail game, so only the head's construction and the clear of
// an empty tree run; the rest is provisional.

// FUNC_AT(0x000529c0)
void EditConfigMap::Iterator::Inc() {
    ATTRIBUTE_UNTESTED("EditConfigMap::Iterator::Inc");
    node = TreeNext(node);
}

// FUNC_AT(0x00052880)
void EditConfigMap::Lrotate(EditConfigNode *node) {
    ATTRIBUTE_UNTESTED("EditConfigMap::Lrotate");
    TreeLrotate(this, node);
}

// FUNC_AT(0x000528e0)
EditConfigNode* EditConfigMap::Max(EditConfigNode *node) {
    ATTRIBUTE_UNTESTED("EditConfigMap::Max");
    return TreeMax(node);
}

// FUNC_AT(0x00052900)
EditConfigNode* EditConfigMap::Min(EditConfigNode *node) {
    ATTRIBUTE_UNTESTED("EditConfigMap::Min");
    return TreeMin(node);
}

// FUNC_AT(0x00052920)
void EditConfigMap::Rrotate(EditConfigNode *node) {
    ATTRIBUTE_UNTESTED("EditConfigMap::Rrotate");
    TreeRrotate(this, node);
}

// FUNC_AT(0x00053540)
EditConfigNode* EditConfigMap::BuyHead() {
    return TreeBuyHead<EditConfigNode>();
}

// FUNC_AT(0x00053680)
void EditConfigMap::EraseSubtree(EditConfigNode *node) {
    TreeEraseSubtree(this, node);
}

// FUNC_AT(0x000546c0)
EditConfigMap::Iterator* EditConfigMap::Erase(Iterator *result, Iterator where) {
    ATTRIBUTE_UNTESTED("EditConfigMap::Erase");
    result->node = TreeErase(this, where.node);
    return result;
}

// FUNC_AT(0x000554a0)
EditConfigMap::Iterator* EditConfigMap::EraseRange(Iterator *result, Iterator first, Iterator last) {
    result->node = TreeEraseRange(this, first.node, last.node);
    return result;
}

// ---------------------------------------------------------------------------------------------------------------
// AttributeCollection and CollectionMap (0x38 nodes).

// A collection made by GetCollection. Its map's allocator byte comes from the stack slot of `named`. It counts as
// loaded at once unless it is made while the database is being read (PrepareDatabase), whose collections load
// their files when first used.
// FUNC_AT(0x00058470)
AttributeCollection* AttributeCollection::Construct(const char *ofClass, const char *named) {
    TreeConstruct(&attributes, static_cast<uint8_t>(reinterpret_cast<uintptr_t>(named)));
    refCount = 0;
    loaded = AttributeSystemInstance->loadingDatabase == 0;
    name = AttributeSystemInstance->MakeString(named);
    className = AttributeSystemInstance->MakeString(ofClass);
    parent = NULL;
    return this;
}

// The copy the collection map's node gets: the names, count and parent, but a new empty attribute map, and
// `loaded` worked out again rather than copied.
// FUNC_AT(0x00058510)
AttributeCollection* AttributeCollection::ConstructCopy(const AttributeCollection &other) {
    TreeConstruct(&attributes, static_cast<uint8_t>(reinterpret_cast<uintptr_t>(&other)));
    refCount = other.refCount;
    loaded = AttributeSystemInstance->loadingDatabase == 0;
    name = other.name;
    className = other.className;
    parent = other.parent;
    return this;
}

// FUNC_AT(0x000579e0)
void AttributeCollection::SetParent(AttributeCollection *newParent) {
    if (newParent != NULL)
        newParent->refCount++;
    if (parent != NULL)
        parent->Release();
    parent = newParent;
}

// FUNC_AT(0x000585b0)
void CollectionNode::DestroyValue() {
    value.collection.attributes.Destruct();
}

// FUNC_AT(0x00052780)
CollectionNode* CollectionMap::Min(CollectionNode *node) {
    ATTRIBUTE_UNTESTED("CollectionMap::Min");
    return TreeMin(node);
}

// FUNC_AT(0x00052860)
CollectionNode* CollectionMap::Max(CollectionNode *node) {
    ATTRIBUTE_UNTESTED("CollectionMap::Max");
    return TreeMax(node);
}

// FUNC_AT(0x00052cd0)
CollectionNode* CollectionMap::Lbound(const CollectionKey &key) {
    return TreeLbound(this, key);
}

// FUNC_AT(0x00052d30)
void CollectionMap::Iterator::Inc() {
    node = TreeNext(node);
}

// FUNC_AT(0x00053130)
void CollectionMap::Iterator::Dec() {
    node = TreePrev(node);
}

// FUNC_AT(0x00053070)
void CollectionMap::Lrotate(CollectionNode *node) {
    TreeLrotate(this, node);
}

// FUNC_AT(0x000530d0)
void CollectionMap::Rrotate(CollectionNode *node) {
    TreeRrotate(this, node);
}

// FUNC_AT(0x00053600)
CollectionNode* CollectionMap::BuyHead() {
    return TreeBuyHead<CollectionNode>();
}

// FUNC_AT(0x00053ad0)
CollectionMap::Iterator* CollectionMap::Find(Iterator *result, const CollectionKey &key) {
    result->node = TreeFind(this, key);
    return result;
}

// FUNC_AT(0x000585f0)
CollectionNode* CollectionMap::Buynode(CollectionNode *left, CollectionNode *parent, CollectionNode *right,
                                       const CollectionEntry &value, uint8_t color) {
    return TreeBuynode<CollectionMap>(left, parent, right, value, color);
}

// FUNC_AT(0x000586a0)
CollectionMap::Iterator* CollectionMap::Insert(Iterator *result, bool addLeft, CollectionNode *where,
                                               const CollectionEntry &value) {
    result->node = TreeInsert(this, addLeft, where, value);
    return result;
}

// FUNC_AT(0x00058880)
CollectionMap::InsertResult* CollectionMap::InsertUnique(InsertResult *result, const CollectionEntry &value) {
    result->position.node = TreeInsertUnique(this, value, &result->inserted);
    return result;
}

// FUNC_AT(0x00058990)
CollectionMap::Iterator* CollectionMap::Erase(Iterator *result, Iterator where) {
    ATTRIBUTE_UNTESTED("CollectionMap::Erase");
    result->node = TreeErase(this, where.node);
    return result;
}

// FUNC_AT(0x00058c70)
void CollectionMap::EraseSubtree(CollectionNode *node) {
    TreeEraseSubtree(this, node);
}

// FUNC_AT(0x00058fd0)
CollectionMap::Iterator* CollectionMap::EraseRange(Iterator *result, Iterator first, Iterator last) {
    result->node = TreeEraseRange(this, first.node, last.node);
    return result;
}

// ---------------------------------------------------------------------------------------------------------------
// StringSet (0x14 nodes).

// FUNC_AT(0x00052ae0)
void StringSet::Iterator::Inc() {
    node = TreeNext(node);
}

// FUNC_AT(0x00052660)
bool StringSet::KeyLess(const char *const &a, const char *const &b) {
    return strcmp(a, b) < 0;
}

// FUNC_AT(0x00053190)
StringNode* StringSet::Lbound(const char *const &key) {
    return TreeLbound(this, key);
}

// FUNC_AT(0x00053500)
StringNode* StringSet::Buynode(StringNode *left, StringNode *parent, StringNode *right, const char *const &value,
                               uint8_t color) {
    return TreeBuynode<StringSet>(left, parent, right, value, color);
}

// FUNC_AT(0x00053640)
StringNode* StringSet::BuyHead() {
    return TreeBuyHead<StringNode>();
}

// FUNC_AT(0x00053740)
void StringSet::EraseSubtree(StringNode *node) {
    TreeEraseSubtree(this, node);
}

// FUNC_AT(0x00053b40)
StringSet::Iterator* StringSet::Find(Iterator *result, const char *const &key) {
    result->node = TreeFind(this, key);
    return result;
}

// FUNC_AT(0x00054460)
StringSet::Iterator* StringSet::Insert(Iterator *result, bool addLeft, StringNode *where, const char *const &value) {
    result->node = TreeInsert(this, addLeft, where, value);
    return result;
}

// FUNC_AT(0x00054c60)
StringSet::Iterator* StringSet::Erase(Iterator *result, Iterator where) {
    ATTRIBUTE_UNTESTED("StringSet::Erase");
    result->node = TreeErase(this, where.node);
    return result;
}

// FUNC_AT(0x00055390)
StringSet::InsertResult* StringSet::InsertUnique(InsertResult *result, const char *const &value) {
    result->position.node = TreeInsertUnique(this, value, &result->inserted);
    return result;
}

// FUNC_AT(0x00055620)
StringSet::Iterator* StringSet::EraseRange(Iterator *result, Iterator first, Iterator last) {
    result->node = TreeEraseRange(this, first.node, last.node);
    return result;
}

// ---------------------------------------------------------------------------------------------------------------
// StoreBlockList: std::vector<AttributeStoreBlock>, and the helpers of its insert and of std::sort. The list only
// grows by push_back at the end, one block at a time; the sort runs after every string stored.

namespace {

const uint32_t kStoreBlockListMaxSize = 0x1fffffff;   // max_size(): 0xffffffff / 8

// _Uninit_fill_n (the game's copy at 0x000c0dd0, which other vectors of 8-byte elements share).
AttributeStoreBlock *StoreBlockUninitializedFill(AttributeStoreBlock *first, uint32_t count,
                                                 const AttributeStoreBlock &value) {
    for (; count != 0; count--, first++)
        if (first != NULL)
            *first = value;
    return first;
}

}  // namespace

// FUNC_AT(0x00054640)
void StoreBlockList::Xlen() {
    ATTRIBUTE_UNTESTED("StoreBlockList::Xlen");
    ThrowStl("vector<T> too long", LengthErrorVtable, LengthErrorThrowInfo);
}

// _Insert_n: room made by moving the tail, or a new array half as large again (or as large as needed).
// FUNC_AT(0x000556e0)
void StoreBlockList::InsertN(AttributeStoreBlock *where, uint32_t count, const AttributeStoreBlock &value) {
    AttributeStoreBlock copy = value;   // in case value is in the list
    uint32_t capacity = Capacity();
    if (count == 0)
        return;
    if (kStoreBlockListMaxSize - Size() < count) {
        Xlen();
        return;
    }
    if (capacity < Size() + count) {
        capacity = kStoreBlockListMaxSize - capacity / 2 < capacity ? 0 : capacity + capacity / 2;
        if (capacity < Size() + count)
            capacity = Size() + count;
        AttributeStoreBlock *blocks =
            static_cast<AttributeStoreBlock *>(UMemory::FastAlloc(capacity * sizeof(AttributeStoreBlock), "STL"));
        AttributeStoreBlock *at = StoreBlockUninitializedCopy(first, where, blocks);
        StoreBlockUninitializedFill(at, count, copy);
        StoreBlockUninitializedCopy(where, last, at + count);
        count += Size();
        if (first != NULL)   // (the elements' destructor is trivial)
            UMemory::FastFree(first, static_cast<uint32_t>(end - first) * sizeof(AttributeStoreBlock));
        end = blocks + capacity;
        last = blocks + count;
        first = blocks;
    } else if (static_cast<uint32_t>(last - where) < count) {
        StoreBlockUninitializedCopy(where, last, where + count);
        StoreBlockUninitializedFill(last, count - static_cast<uint32_t>(last - where), copy);
        last += count;
        StoreBlockFill(where, last - count, copy);
    } else {
        AttributeStoreBlock *oldLast = last;
        last = StoreBlockUninitializedCopy(oldLast - count, oldLast, last);
        AttributeStoreBlock *copiedTo;
        StoreBlockCopyBackward(&copiedTo, where, oldLast - count, oldLast);
        StoreBlockFill(where, where + count, copy);
    }
}

// FUNC_AT(0x00055bf0)
void StoreBlockList::PushBack(const AttributeStoreBlock &value) {
    if (first != NULL && Size() < Capacity()) {
        AttributeStoreBlock *at = last;
        StoreBlockUninitializedFill(at, 1, value);
        last = at + 1;
        return;
    }
    InsertN(last, 1, value);
}

// FUNC_AT(0x00052ba0)
void StoreBlockFill(AttributeStoreBlock *first, AttributeStoreBlock *last, const AttributeStoreBlock &value) {
    for (; first != last; first++)
        *first = value;
}

// FUNC_AT(0x00052bd0)
void StoreBlockIterSwap(AttributeStoreBlock *a, AttributeStoreBlock *b) {
    AttributeStoreBlock held = *a;
    *a = *b;
    *b = held;
}

// FUNC_AT(0x00052c00)
void StoreBlockPushHeap(AttributeStoreBlock *first, int hole, int top, AttributeStoreBlock value) {
    for (int at = (hole - 1) / 2; top < hole && first[at] < value; at = (hole - 1) / 2) {
        first[hole] = first[at];
        hole = at;
    }
    first[hole] = value;
}

// FUNC_AT(0x00053200)
void StoreBlockMed3(AttributeStoreBlock *first, AttributeStoreBlock *mid, AttributeStoreBlock *last) {
    if (*mid < *first)
        StoreBlockIterSwap(mid, first);
    if (*last < *mid)
        StoreBlockIterSwap(last, mid);
    if (*mid < *first)
        StoreBlockIterSwap(mid, first);
}

// FUNC_AT(0x00053280)
void StoreBlockAdjustHeap(AttributeStoreBlock *first, int hole, int bottom, AttributeStoreBlock value) {
    ATTRIBUTE_UNTESTED("StoreBlockAdjustHeap");
    int top = hole;
    int at = 2 * hole + 2;
    for (; at < bottom; at = 2 * at + 2) {
        if (first[at] < first[at - 1])
            at--;
        first[hole] = first[at];
        hole = at;
    }
    if (at == bottom) {
        first[hole] = first[bottom - 1];
        hole = bottom - 1;
    }
    StoreBlockPushHeap(first, hole, top, value);
}

// FUNC_AT(0x00053310)
void StoreBlockPopHeap(AttributeStoreBlock *first, AttributeStoreBlock *last, AttributeStoreBlock *dest,
                       AttributeStoreBlock value) {
    *dest = *first;
    StoreBlockAdjustHeap(first, 0, static_cast<int>(last - first), value);
}

// FUNC_AT(0x00053350)
void StoreBlockRotate(AttributeStoreBlock *first, AttributeStoreBlock *mid, AttributeStoreBlock *last) {
    int shift = static_cast<int>(mid - first);
    int count = static_cast<int>(last - first);
    for (int factor = shift; factor != 0;) {   // the gcd of shift and count
        int remainder = count % factor;
        count = factor;
        factor = remainder;
    }
    if (count < last - first) {
        for (; 0 < count; count--) {
            AttributeStoreBlock *hole = first + count;
            AttributeStoreBlock *next = hole;
            AttributeStoreBlock held = *hole;
            AttributeStoreBlock *next1 = next + shift == last ? first : next + shift;
            while (next1 != hole) {
                *next = *next1;
                next = next1;
                next1 = shift < last - next1 ? next1 + shift : first + (shift - (last - next1));
            }
            *next = held;
        }
    }
}

// FUNC_AT(0x000537c0)
AttributeStoreBlock** StoreBlockCopyBackward(AttributeStoreBlock **result, AttributeStoreBlock *first,
                                             AttributeStoreBlock *last, AttributeStoreBlock *dest) {
    while (first != last)
        *--dest = *--last;
    *result = dest;
    return result;
}

// FUNC_AT(0x000537f0)
AttributeStoreBlock* StoreBlockUninitializedCopy(AttributeStoreBlock *first, AttributeStoreBlock *last,
                                                 AttributeStoreBlock *dest) {
    for (; first != last; first++, dest++)
        if (dest != NULL)
            *dest = *first;
    return dest;
}

// FUNC_AT(0x00053820)
void StoreBlockMedian(AttributeStoreBlock *first, AttributeStoreBlock *mid, AttributeStoreBlock *last) {
    ATTRIBUTE_UNTESTED("StoreBlockMedian");
    if (40 < last - first) {   // Tukey's ninther
        int step = static_cast<int>(last - first + 1) / 8;
        StoreBlockMed3(first, first + step, first + 2 * step);
        StoreBlockMed3(mid - step, mid, mid + step);
        StoreBlockMed3(last - 2 * step, last - step, last);
        StoreBlockMed3(first + step, mid, last - step);
    } else {
        StoreBlockMed3(first, mid, last);
    }
}

// FUNC_AT(0x00053900)
void StoreBlockMakeHeap(AttributeStoreBlock *first, AttributeStoreBlock *last) {
    ATTRIBUTE_UNTESTED("StoreBlockMakeHeap");
    int bottom = static_cast<int>(last - first);
    for (int hole = bottom / 2; 0 < hole;) {
        hole--;
        StoreBlockAdjustHeap(first, hole, bottom, first[hole]);
    }
}

// FUNC_AT(0x00053950)
void StoreBlockPopHeapLast(AttributeStoreBlock *first, AttributeStoreBlock *last) {
    StoreBlockPopHeap(first, last - 1, last - 1, last[-1]);
}

// FUNC_AT(0x00053c50)
StoreBlockRange* StoreBlockUnguardedPartition(StoreBlockRange *result, AttributeStoreBlock *first,
                                              AttributeStoreBlock *last) {
    ATTRIBUTE_UNTESTED("StoreBlockUnguardedPartition");
    AttributeStoreBlock *mid = first + (last - first) / 2;
    StoreBlockMedian(first, mid, last - 1);
    AttributeStoreBlock *pfirst = mid;
    AttributeStoreBlock *plast = pfirst + 1;
    while (first < pfirst && !(pfirst[-1] < *pfirst) && !(*pfirst < pfirst[-1]))
        pfirst--;
    while (plast < last && !(*plast < *pfirst) && !(*pfirst < *plast))
        plast++;
    AttributeStoreBlock *gfirst = plast;
    AttributeStoreBlock *glast = pfirst;
    for (;;) {
        for (; gfirst < last; gfirst++) {
            if (*pfirst < *gfirst)
                ;
            else if (*gfirst < *pfirst)
                break;
            else
                StoreBlockIterSwap(plast++, gfirst);
        }
        for (; first < glast; glast--) {
            if (glast[-1] < *pfirst)
                ;
            else if (*pfirst < glast[-1])
                break;
            else
                StoreBlockIterSwap(--pfirst, glast - 1);
        }
        if (glast == first && gfirst == last) {
            result->first = pfirst;
            result->last = plast;
            return result;
        }
        if (glast == first) {   // no room at the bottom, rotate the pivot upward
            if (plast != gfirst)
                StoreBlockIterSwap(pfirst, plast);
            plast++;
            StoreBlockIterSwap(pfirst++, gfirst++);
        } else if (gfirst == last) {   // no room at the top, rotate the pivot downward
            if (--glast != --pfirst)
                StoreBlockIterSwap(glast, pfirst);
            StoreBlockIterSwap(pfirst, --plast);
        } else {
            StoreBlockIterSwap(gfirst++, --glast);
        }
    }
}

// FUNC_AT(0x00053e80)
void StoreBlockSortHeap(AttributeStoreBlock *first, AttributeStoreBlock *last) {
    ATTRIBUTE_UNTESTED("StoreBlockSortHeap");
    for (; 1 < last - first; last--)
        StoreBlockPopHeapLast(first, last);
}

// FUNC_AT(0x00053ed0)
void StoreBlockInsertionSort(AttributeStoreBlock *first, AttributeStoreBlock *last) {
    if (first == last)
        return;
    for (AttributeStoreBlock *next = first; ++next != last;) {
        if (*next < *first) {   // a new earliest element: rotate it to the front
            AttributeStoreBlock *next1 = next + 1;
            if (first != next && next != next1)
                StoreBlockRotate(first, next, next1);
        } else {                // look for its place after the first
            AttributeStoreBlock *dest = next;
            for (AttributeStoreBlock *dest0 = dest; *next < *--dest0;)
                dest = dest0;
            AttributeStoreBlock *next1 = next + 1;
            if (dest != next && next != next1)
                StoreBlockRotate(dest, next, next1);
        }
    }
}

// std::sort's _Sort: quicksort while the pieces are larger than 32 and the depth allowance lasts, a heap sort
// when it runs out, insertion sort for the small pieces. The store-block lists the game builds stay far below 32.
// FUNC_AT(0x00053f90)
void StoreBlockSort(AttributeStoreBlock *first, AttributeStoreBlock *last, int ideal) {
    int count;
    for (; 32 < (count = static_cast<int>(last - first)) && 0 < ideal;) {
        StoreBlockRange mid;
        StoreBlockUnguardedPartition(&mid, first, last);
        ideal /= 2;
        ideal += ideal / 2;   // allow 1.5 log2(N) divisions
        if (mid.first - first < last - mid.last) {   // loop on the larger half
            StoreBlockSort(first, mid.first, ideal);
            first = mid.last;
        } else {
            StoreBlockSort(mid.last, last, ideal);
            last = mid.first;
        }
    }
    if (32 < count) {
        if (2 <= last - first)
            StoreBlockMakeHeap(first, last);
        StoreBlockSortHeap(first, last);
    } else if (1 < count) {
        StoreBlockInsertionSort(first, last);
    }
}
