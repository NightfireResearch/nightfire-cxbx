#ifndef DRIVING_DATA_TREE_H_
#define DRIVING_DATA_TREE_H_

// The red-black tree under the data layer's three std::maps (MSVC 7's _Tree), as the game compiled it:
//
//   - USymbolTable's namespaces: a multimap from a name to a SymbolNamespace, names compared with _stricmp
//     (NamespaceMap, SymbolTable.h);
//   - UCarpNamespace's groups: a map from a name to a UGroup, the same comparison (CarpGroupMap, SymbolTable.h);
//   - CARP's resolvers: a map from a record tag to the function that resolves it (ResolverMap, Carp.h).
//
// The three share a node layout and, in the game, most of their code: erasing at an iterator and inserting at a
// known place are one function each for both name maps (identical code folded by the linker), and the resolver
// map's copies are the same instructions again. So the bodies are written once here, as Tree's methods, and each
// address the game has gets a FUNC_AT'd member of its map that calls them. The helpers the game shares with
// every other map in the program (rotations, the minimum and maximum, stepping an iterator, making a node) are
// called at their addresses.
//
// A map object is 12 bytes - the allocator's byte, the head node and the size. The head is the end() node:
// isNil set, its parent the root, its left the leftmost node and its right the rightmost. Red is 0, black 1.

#include <stdint.h>

class SymbolNamespace;
class UGroup;
typedef void (*CarpResolverFn)(UGroup *record, UGroup *shared, UGroup *parent);

enum TreeColor : uint8_t {
    kTreeRed = 0,
    kTreeBlack = 1,
};

struct TreeNode {
    TreeNode *left;
    TreeNode *parent;
    TreeNode *right;
    union {   // the key: a name in the name maps, a tag in the resolver map
        const char *name;
        uint32_t tag;
    };
    union {   // the mapped value
        SymbolNamespace *ns;
        UGroup *group;
        CarpResolverFn resolver;
    };
    uint8_t color;
    uint8_t isNil;
    uint8_t unknown16[2];
};
static_assert(sizeof(TreeNode) == 0x18, "the game's map node is 24 bytes");

// The map's value_type, as insert takes it.
struct TreePair {
    union {
        const char *name;
        uint32_t tag;
    };
    union {
        SymbolNamespace *ns;
        UGroup *group;
        CarpResolverFn resolver;
    };
};

// What insert answers (std::pair<iterator, bool>, returned through a hidden pointer).
struct TreeInsertResult {
    TreeNode *where;
    bool inserted;
    uint8_t unknown5[3];
};

class Tree {
public:
    uint8_t allocator;
    uint8_t unknown1[3];
    TreeNode *head;
    uint32_t size;

    TreeNode *Root() const { return head->parent; }
    TreeNode *Begin() const { return head->left; }

    // erase(iterator) (0x0011aba0, 0x00119620): unlinks and frees the node, rebalances, answers the next one.
    TreeNode **EraseAt(TreeNode **result, TreeNode *where);
    // _Insert (0x0011af30, 0x00119440): a new red node under `where`, on its left if `addLeft`, then rebalances.
    TreeNode **InsertAt(TreeNode **result, bool addLeft, TreeNode *where, const TreePair *value);
    // _Erase (0x0011a8d0, 0x0011a910, 0x00118e90): frees a subtree without rebalancing.
    void EraseSubtree(TreeNode *node);
    // erase(first, last) (0x0011ae70, 0x0011b2d0, 0x001199b0).
    TreeNode **EraseRange(TreeNode **result, TreeNode *first, TreeNode *last);
    // The map's destructor (0x0011b4d0, 0x0011b540, 0x00119e80): everything erased, the head freed.
    void Destroy();
    // The map's constructor, inlined in the namespaces' and called at 0x00119e40 for the resolvers: an empty
    // tree, the head linked to itself.
    void Init();
};
static_assert(sizeof(Tree) == 12, "a map is 12 bytes");

// The next node in order (the inlined ++iterator): the leftmost of the right subtree, or the first ancestor
// this subtree is on the left of. The head stays where it is.
TreeNode *TreeNext(TreeNode *node);

// Throws a standard exception the way the game's STL does (std::string then the exception object, its vtable
// set, _CxxThrowException): the "too long" and "invalid iterator" errors no shipped data reaches.
void TreeThrow(const char *message, uint32_t vtable, uint32_t throwInfo);

// The game's std::length_error / std::out_of_range vtables and throw descriptions.
constexpr uint32_t kLengthErrorVtable = 0x00189eec;
constexpr uint32_t kLengthErrorThrowInfo = 0x001a89bc;
constexpr uint32_t kOutOfRangeVtable = 0x0018a29c;
constexpr uint32_t kOutOfRangeThrowInfo = 0x001a8bd8;

#endif // DRIVING_DATA_TREE_H_
