#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "SymbolTable.h"
#include "DataUntested.h"
#include "StdStreams.h"                 // GameStd::String, GameStd::LogicError
#include "../../common/xbeOverload.h"
#include "../audio/Bank.h"              // SharedTreeMax, SharedTreeIterator
#include "../engine/UGroup.h"
#include "../engine/UMemory.hpp"
#include "../world/SoundMap.h"          // MapBuyHead

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// The symbol table, its namespaces, the red-black tree under their maps, and StringToNumber. See SymbolTable.h for
// what each is and Tree.h for the tree. The order of every comparison, allocation and free is the original's;
// names compare with the game's own _stricmp.
// ---------------------------------------------------------------------------------------------------------------

// The tree helpers every map in the game shares, called at their addresses (0x000cca30 is ported as
// WSoundMap::Lrotate on that map's node type, 0x00126b70 and 0x00126cb0 as SharedTree's, audio/Bank.h).
#define Tree_Lrotate ((void (__fastcall *)(Tree *, int, TreeNode *))0x000cca30)
#define Tree_Rrotate ((void (__fastcall *)(Tree *, int, TreeNode *))0x00126b70)
#define Tree_Min ((TreeNode *(*)(TreeNode *))0x0009c310)
#define TreeIterator_Increment ((void (__fastcall *)(TreeNode **, int))0x000b2920)
#define Tree_BuyNode ((TreeNode *(__fastcall *)(Tree *, int, TreeNode *, TreeNode *, TreeNode *, const TreePair *, int))0x00093f10)
#define NameMap_Find ((TreeNode **(__fastcall *)(Tree *, int, TreeNode **, const char *const *))0x00126cb0)

// The C runtime's: case-insensitive comparison and qsort (whose order of equal elements is its own), atol.
#define Crt_stricmp ((int (*)(const char *, const char *))0x00134537)
#define Crt_qsort ((void (*)(void *, size_t, size_t, int (*)(const void *, const void *)))0x00132db0)
#define Crt_atol ((long (*)(const char *))0x00133d51)

// The STL's exception machinery.
#define CxxThrowException ((void (__stdcall *)(void *, uint32_t))0x001325ad)

constexpr uint32_t kUSymbolTableVtable = 0x001a2240;
constexpr uint32_t kUCarpNamespaceVtable = 0x001a2244;
constexpr uint32_t kUCharNamespaceVtable = 0x001a2238;

constexpr uint32_t kTagMap = 0x4d617020;      // 'Map '
constexpr uint32_t kTagShared = 0x53686172;   // 'Shar'
constexpr uint32_t kTagArticle = 0x41727469;  // 'Arti'
constexpr uint32_t kTagName = 0x4e616d65;     // 'Name'
constexpr uint32_t kTagAsPrefix = 0x61730000; // "as" in the high half: an indexed 'as' record

static void SetVtable(void *object, uint32_t vtable) {
    *static_cast<uint32_t *>(object) = vtable;
}

// A group's end of child groups, as the inlined code computes it from GetArray.
static UGroup *GroupEnd(UGroup *group) {
    return group->GetArray() + group->GroupCount();
}

// ... and its end of data records.
static UData *DataEnd(UGroup *group) {
    return group->GetArray() + (group->GroupCount() + group->count);
}

// =============================================================================================================
// The tree
// =============================================================================================================

void TreeThrow(const char *message, uint32_t vtable, uint32_t throwInfo) {
    DATA_UNTESTED("an STL container's throw");
    GameStd::String text;
    text.capacity = 15;
    text.size = 0;
    text.text.buffer[0] = 0;
    text.AssignText(message, uint32_t(strlen(message)));
    GameStd::LogicError exception;
    exception.Construct(&text);
    SetVtable(&exception, vtable);
    CxxThrowException(&exception, throwInfo);
}

TreeNode *TreeNext(TreeNode *node) {
    if (node->isNil)
        return node;
    if (!node->right->isNil) {
        node = node->right;
        while (!node->left->isNil)
            node = node->left;
        return node;
    }
    TreeNode *parent = node->parent;
    while (!parent->isNil && node == parent->right) {
        node = parent;
        parent = parent->parent;
    }
    return parent;
}

void Tree::Init() {
    TreeNode *node = MapBuyHead();
    head = node;
    node->isNil = 1;
    head->parent = head;
    head->left = head;
    head->right = head;
    size = 0;
}

void Tree::EraseSubtree(TreeNode *node) {
    for (TreeNode *next; !node->isNil; node = next) {
        EraseSubtree(node->right);
        next = node->left;
        if (node != NULL)
            UMemory::FastFree(node, sizeof(TreeNode));
    }
}

TreeNode **Tree::InsertAt(TreeNode **result, bool addLeft, TreeNode *where, const TreePair *value) {
    if (size >= 0x1ffffffe)
        TreeThrow("map/set<T> too long", kLengthErrorVtable, kLengthErrorThrowInfo);
    TreeNode *node = Tree_BuyNode(this, 0, head, where, head, value, kTreeRed);
    size++;
    if (where == head) {
        head->parent = node;
        head->left = node;
        head->right = node;
    } else if (addLeft) {
        where->left = node;
        if (where == head->left)
            head->left = node;
    } else {
        where->right = node;
        if (where == head->right)
            head->right = node;
    }

    for (TreeNode *x = node; x->parent->color == kTreeRed;) {
        TreeNode *parent = x->parent;
        TreeNode *grandparent = parent->parent;
        if (parent == grandparent->left) {
            TreeNode *uncle = grandparent->right;
            if (uncle->color == kTreeRed) {
                parent->color = kTreeBlack;
                uncle->color = kTreeBlack;
                x->parent->parent->color = kTreeRed;
                x = x->parent->parent;
            } else {
                if (x == parent->right) {
                    x = parent;
                    Tree_Lrotate(this, 0, x);
                }
                x->parent->color = kTreeBlack;
                x->parent->parent->color = kTreeRed;
                Tree_Rrotate(this, 0, x->parent->parent);
            }
        } else {
            TreeNode *uncle = grandparent->left;
            if (uncle->color == kTreeRed) {
                parent->color = kTreeBlack;
                uncle->color = kTreeBlack;
                x->parent->parent->color = kTreeRed;
                x = x->parent->parent;
            } else {
                if (x == parent->left) {
                    x = parent;
                    Tree_Rrotate(this, 0, x);
                }
                x->parent->color = kTreeBlack;
                x->parent->parent->color = kTreeRed;
                Tree_Lrotate(this, 0, x->parent->parent);
            }
        }
    }
    head->parent->color = kTreeBlack;
    *result = node;
    return result;
}

TreeNode **Tree::EraseAt(TreeNode **result, TreeNode *where) {
    if (where->isNil)
        TreeThrow("invalid map/set<T> iterator", kOutOfRangeVtable, kOutOfRangeThrowInfo);
    TreeNode *erased = where;
    TreeNode *next = where;
    TreeIterator_Increment(&next, 0);

    TreeNode *pnode = where;   // the node that really leaves its place: where, or its successor
    TreeNode *fixnode;         // the node that takes pnode's place
    TreeNode *fixnodeParent;
    if (where->left->isNil) {
        fixnode = where->right;
    } else if (where->right->isNil) {
        fixnode = where->left;
    } else {
        pnode = next;
        fixnode = pnode->right;
    }

    if (pnode == where) {
        fixnodeParent = where->parent;
        if (!fixnode->isNil)
            fixnode->parent = fixnodeParent;
        if (head->parent == where)
            head->parent = fixnode;
        else if (fixnodeParent->left == where)
            fixnodeParent->left = fixnode;
        else
            fixnodeParent->right = fixnode;
        if (head->left == where)
            head->left = fixnode->isNil ? fixnodeParent : Tree_Min(fixnode);
        if (head->right == where)
            head->right = fixnode->isNil ? fixnodeParent : SharedTreeMax(fixnode);
    } else {
        where->left->parent = pnode;
        pnode->left = where->left;
        if (pnode == where->right) {
            fixnodeParent = pnode;
        } else {
            fixnodeParent = pnode->parent;
            if (!fixnode->isNil)
                fixnode->parent = fixnodeParent;
            fixnodeParent->left = fixnode;
            pnode->right = where->right;
            where->right->parent = pnode;
        }
        if (head->parent == where)
            head->parent = pnode;
        else if (where->parent->left == where)
            where->parent->left = pnode;
        else
            where->parent->right = pnode;
        pnode->parent = where->parent;
        uint8_t color = pnode->color;
        pnode->color = where->color;
        where->color = color;
    }

    if (erased->color == kTreeBlack) {
        for (; fixnode != head->parent && fixnode->color == kTreeBlack;
             fixnode = fixnodeParent, fixnodeParent = fixnodeParent->parent) {
            if (fixnode == fixnodeParent->left) {
                TreeNode *sibling = fixnodeParent->right;
                if (sibling->color == kTreeRed) {
                    sibling->color = kTreeBlack;
                    fixnodeParent->color = kTreeRed;
                    Tree_Lrotate(this, 0, fixnodeParent);
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
                    Tree_Rrotate(this, 0, sibling);
                    sibling = fixnodeParent->right;
                }
                sibling->color = fixnodeParent->color;
                fixnodeParent->color = kTreeBlack;
                sibling->right->color = kTreeBlack;
                Tree_Lrotate(this, 0, fixnodeParent);
                break;
            } else {
                TreeNode *sibling = fixnodeParent->left;
                if (sibling->color == kTreeRed) {
                    sibling->color = kTreeBlack;
                    fixnodeParent->color = kTreeRed;
                    Tree_Rrotate(this, 0, fixnodeParent);
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
                    Tree_Lrotate(this, 0, sibling);
                    sibling = fixnodeParent->left;
                }
                sibling->color = fixnodeParent->color;
                fixnodeParent->color = kTreeBlack;
                sibling->left->color = kTreeBlack;
                Tree_Rrotate(this, 0, fixnodeParent);
                break;
            }
        }
        fixnode->color = kTreeBlack;
    }

    UMemory::FastFree(erased, sizeof(TreeNode));
    if (size > 0)
        size--;
    *result = next;
    return result;
}

TreeNode **Tree::EraseRange(TreeNode **result, TreeNode *first, TreeNode *last) {
    if (first == head->left && last == head) {
        EraseSubtree(head->parent);
        head->parent = head;
        size = 0;
        head->left = head;
        head->right = head;
        *result = head->left;
        return result;
    }
    while (first != last) {
        TreeNode *where = first;
        first = TreeNext(first);
        TreeNode *ignored;
        EraseAt(&ignored, where);
    }
    *result = first;
    return result;
}

void Tree::Destroy() {
    TreeNode *ignored;
    EraseRange(&ignored, head->left, head);
    if (head != NULL)
        UMemory::FastFree(head, sizeof(TreeNode));
    head = NULL;
    size = 0;
}

// ---- the namespaces' multimap

// FUNC_AT(0x0011a880)
TreeNode* NamespaceMap::LowerBound(const char *const *key) {
    TreeNode *bound = head;
    for (TreeNode *node = head->parent; !node->isNil;) {
        if (Crt_stricmp(node->value.name, *key) < 0) {
            node = node->right;
        } else {
            bound = node;
            node = node->left;
        }
    }
    return bound;
}

// FUNC_AT(0x0011a8d0)
void NamespaceMap::EraseSubtree(TreeNode *node) {
    Tree::EraseSubtree(node);
}

// FUNC_AT(0x0011aba0)
TreeNode** NamespaceMap::EraseAt(TreeNode **result, TreeNode *where) {
    return Tree::EraseAt(result, where);
}

// FUNC_AT(0x0011ae70)
TreeNode** NamespaceMap::EraseRange(TreeNode **result, TreeNode *first, TreeNode *last) {
    return Tree::EraseRange(result, first, last);
}

// FUNC_AT(0x0011af30)
TreeNode** NamespaceMap::InsertAt(TreeNode **result, bool addLeft, TreeNode *where, const TreePair *value) {
    return Tree::InsertAt(result, addLeft, where, value);
}

// FUNC_AT(0x0011b250)
TreeInsertResult* NamespaceMap::InsertEqual(TreeInsertResult *result, const TreePair *value) {
    TreeNode *where = head;
    bool addLeft = true;
    for (TreeNode *node = head->parent; !node->isNil;) {
        where = node;
        addLeft = Crt_stricmp(value->name, node->value.name) < 0;
        node = addLeft ? node->left : node->right;
    }
    TreeNode *inserted;
    Tree::InsertAt(&inserted, addLeft, where, value);
    result->where = inserted;
    result->inserted = true;
    return result;
}

// FUNC_AT(0x0011b4d0)
void NamespaceMap::Destroy() {
    Tree::Destroy();
}

// ---- UCarpNamespace's map

// FUNC_AT(0x0011a910)
void CarpGroupMap::EraseSubtree(TreeNode *node) {
    Tree::EraseSubtree(node);
}

// FUNC_AT(0x0011b180)
TreeInsertResult* CarpGroupMap::InsertUnique(TreeInsertResult *result, const TreePair *value) {
    TreeNode *where = head;
    bool addLeft = true;
    for (TreeNode *node = head->parent; !node->isNil;) {
        where = node;
        addLeft = Crt_stricmp(value->name, node->value.name) < 0;
        node = addLeft ? node->left : node->right;
    }
    SharedTreeIterator before = {where};
    if (addLeft) {
        if (where == head->left) {
            TreeNode *inserted;
            Tree::InsertAt(&inserted, true, where, value);
            result->where = inserted;
            result->inserted = true;
            return result;
        }
        before.Dec();
    }
    if (Crt_stricmp(before.node->value.name, value->name) < 0) {
        TreeNode *inserted;
        Tree::InsertAt(&inserted, addLeft, where, value);
        result->where = inserted;
        result->inserted = true;
        return result;
    }
    result->where = before.node;
    result->inserted = false;
    return result;
}

// FUNC_AT(0x0011b2d0)
TreeNode** CarpGroupMap::EraseRange(TreeNode **result, TreeNode *first, TreeNode *last) {
    return Tree::EraseRange(result, first, last);
}

// FUNC_AT(0x0011b540)
void CarpGroupMap::Destroy() {
    Tree::Destroy();
}

// =============================================================================================================
// The symbol table and its namespaces
// =============================================================================================================

void *SymbolNamespace::Lookup(const char *name, int *size) {
    return (this->*XbeVirtual<decltype(&SymbolNamespace::NameLookupSlot)>(this, 0))(name, size);
}

// The maps' constructor, inlined in both namespaces' constructors. The allocator's byte is copied from an
// uninitialised local in the original; 0 stands in for it (nothing reads it).
template <class Map> static Map *NewMap(const char *name) {
    Map *map = static_cast<Map *>(UMemory::FastAlloc(sizeof(Map), name));
    if (map != NULL) {
        map->allocator = 0;
        map->Init();
    }
    return map;
}

// FUNC_AT(0x0011b580)
USymbolTable* USymbolTable::Construct() {
    SetVtable(this, kUSymbolTableVtable);
    namespaces = NewMap<NamespaceMap>("Namespacelist");
    return this;
}

// FUNC_AT(0x0011b620)
void USymbolTable::Destruct() {
    NamespaceMap *map = namespaces;
    SetVtable(this, kUSymbolTableVtable);
    if (map != NULL) {
        map->Destroy();
        UMemory::FastFree(map, sizeof(NamespaceMap));
    }
}

// FUNC_AT(0x0011b650)
USymbolTable* USymbolTable::Delete(unsigned flags) {
    Destruct();
    if (flags & 1)
        UMemory::FastFree(this, sizeof(USymbolTable));
    return this;
}

// FUNC_AT(0x0011a950)
void* USymbolTable::NameLookup(const char *name, int *size) {
    char prefix[32];
    const char *separator = strstr(name, "::");
    if (separator != NULL) {
        size_t length = size_t(separator - name);
        strncpy(prefix, name, length);
        prefix[length] = 0;
    } else {
        prefix[0] = 0;
    }

    NamespaceMap *map = namespaces;
    const char *key = prefix;
    TreeNode *node = map->LowerBound(&key);
    if (node == map->head || Crt_stricmp(prefix, node->value.name) < 0)
        node = map->head;
    // Without a separator the original hands the namespaces (char *)2 - null plus the separator's length; only
    // a namespace called "" would see it.
    const char *rest = reinterpret_cast<const char *>(uintptr_t(separator) + 2);
    for (; node != namespaces->head; node = TreeNext(node)) {
        if (Crt_stricmp(node->value.name, prefix) != 0)
            return NULL;
        void *found = node->value.ns->Lookup(rest, size);
        if (found != NULL)
            return found;
    }
    return NULL;
}

// FUNC_AT(0x0011b510)
void USymbolTable::AddNamespace(const char *name, SymbolNamespace *ns) {
    TreePair pair;
    pair.name = name;
    pair.ns = ns;
    TreeInsertResult result;
    namespaces->InsertEqual(&result, &pair);
}

// FUNC_AT(0x0011b110)
SymbolNamespace* USymbolTable::RemoveNamespace(const char *name) {
    NamespaceMap *map = namespaces;
    TreeNode *node = map->LowerBound(&name);
    if (node == map->head || Crt_stricmp(name, node->value.name) < 0)
        node = map->head;
    SymbolNamespace *ns = node->value.ns;
    TreeNode *ignored;
    namespaces->EraseAt(&ignored, node);
    return ns;
}

// ---- "CHAR" and "DATA"

// FUNC_AT(0x0011a800)
UCharNamespace* UCharNamespace::Construct() {
    SetVtable(this, kUCharNamespaceVtable);
    return this;
}

// FUNC_AT(0x0011a810)
void UCharNamespace::Destruct() {
    SetVtable(this, kUCharNamespaceVtable);
}

// FUNC_AT(0x0011a820)
void* UCharNamespace::NameLookup(const char *name, int *size) {
    *size = int(strlen(name)) + 1;
    return const_cast<char *>(name);
}

// FUNC_AT(0x0011a850)
UCharNamespace* UCharNamespace::Delete(unsigned flags) {
    SetVtable(this, kUCharNamespaceVtable);
    if (flags & 1)
        UMemory::FastFree(this, sizeof(UCharNamespace));
    return this;
}

// ---- "CARP"

// FUNC_AT(0x0011b690)
UCarpNamespace* UCarpNamespace::Construct() {
    SetVtable(this, kUCarpNamespaceVtable);
    groups = NewMap<CarpGroupMap>("UCarpNamespace");
    return this;
}

// FUNC_AT(0x0011b730)
void UCarpNamespace::Destruct() {
    CarpGroupMap *map = groups;
    SetVtable(this, kUCarpNamespaceVtable);
    if (map != NULL) {
        map->Destroy();
        UMemory::FastFree(map, sizeof(CarpGroupMap));
    }
}

// FUNC_AT(0x0011b760)
UCarpNamespace* UCarpNamespace::Delete(unsigned flags) {
    Destruct();
    if (flags & 1)
        UMemory::FastFree(this, sizeof(UCarpNamespace));
    return this;
}

// FUNC_AT(0x0011a750)
bool UCarpNamespace::ParseNameAndTag(const char *name, char *group, uint32_t *tag, int *offset) {
    const char *tagText;
    if (*name == '{') {
        *tag = UDataGroupDecodeTag(name);
        *group = 0;
        tagText = name;
    } else {
        strcpy(group, name);
        char *separator = strstr(group, "::");
        if (separator == NULL) {
            *tag = 0;
            *offset = 0;
            return true;
        }
        *separator = 0;
        tagText = separator + 2;
        *tag = UDataGroupDecodeTag(tagText);
    }
    const char *offsetText = strstr(tagText, "}::");
    if (offsetText != NULL) {
        *offset = int(Crt_atol(offsetText + 3));
        return true;
    }
    *offset = 0;
    return true;
}

// FUNC_AT(0x0011aa80)
void* UCarpNamespace::NameLookup(const char *name, int *size) {
    uint32_t tag;
    int offset;
    char groupName[84];
    if (!ParseNameAndTag(name, groupName, &tag, &offset))
        return NULL;

    const char *key = groupName;
    TreeNode *node;
    NameMap_Find(groups, 0, &node, &key);   // find, shared by both name maps
    if (node == groups->head)
        return NULL;
    UGroup *group = node->value.group;
    if (tag == 0) {
        *size = 0;
        return group;
    }
    UData *record = group->DataLocateTag(tag);
    if (record == DataEnd(group) && (tag & 0xffff0000) == kTagAsPrefix)
        record = group->DataLocateTag(tag & 0xffff00ff);   // 'as' and an index: try the number's low byte
    if (record == DataEnd(group))
        return NULL;
    *size = int(record->Size());
    return record->Data() + offset;
}

// FUNC_AT(0x0011b390)
void UCarpNamespace::AddCarpFile(UGroup *root) {
    TreePair pair;
    TreeInsertResult result;
    pair.name = "<<Root>>";
    pair.group = root;
    groups->InsertUnique(&result, &pair);

    UGroup *map = root->GroupLocateTag(kTagMap);
    if (map != GroupEnd(root)) {
        pair.name = "<<Map>>";
        pair.group = map;
        groups->InsertUnique(&result, &pair);
    }
    UGroup *shared = root->GroupLocateTag(kTagShared);
    if (shared != GroupEnd(root)) {
        pair.name = "<<Shared>>";
        pair.group = shared;
        groups->InsertUnique(&result, &pair);
    }

    for (UGroup *child = root->GetArray(); child != GroupEnd(root); child++) {
        if (child->MatchTag() == kTagArticle) {
            UData *nameRecord = child->DataLocateTag(kTagName);   // every article has one
            pair.name = reinterpret_cast<const char *>(nameRecord->Data());
            pair.group = child;
            groups->InsertUnique(&result, &pair);
        }
    }
}

// =============================================================================================================
// StringToNumber
// =============================================================================================================

// FUNC_AT(0x00119ef0)
int CompareByString(const void *a, const void *b) {
    int c = strcmp(static_cast<const StringToNumberEntry *>(a)->string,
                   static_cast<const StringToNumberEntry *>(b)->string);
    return (c > 0) - (c < 0);   // the inlined strcmp answers -1, 0 or 1
}

// FUNC_AT(0x00119f40)
int CompareByNumber(const void *a, const void *b) {
    return static_cast<const StringToNumberEntry *>(a)->number - static_cast<const StringToNumberEntry *>(b)->number;
}

// FUNC_AT(0x00119f50)
StringToNumber* StringToNumber::Construct(StringToNumberEntry *entries) {
    int n = 0;
    while (entries[n].string != NULL)
        n++;
    count = n;
    table = entries;
    if (n <= 30) {
        sortedByString = NULL;
        sortedByNumber = NULL;
        return this;
    }
    StringToNumberEntry *block =
        static_cast<StringToNumberEntry *>(UMemory::FastAlloc(n * 2 * sizeof(StringToNumberEntry), "StringToNumber"));
    sortedByNumber = block + n;
    sortedByString = block;
    for (int i = 0; i < count; i++) {
        sortedByString[i] = entries[i];
        sortedByNumber[i] = entries[i];
    }
    Crt_qsort(sortedByString, count, sizeof(StringToNumberEntry), CompareByString);
    Crt_qsort(sortedByNumber, count, sizeof(StringToNumberEntry), CompareByNumber);
    return this;
}

// FUNC_AT(0x0011a020)
void StringToNumber::Destruct() {
    if (sortedByString != NULL)
        UMemory::FastFree(sortedByString, count * 2 * sizeof(StringToNumberEntry));
}

// FUNC_AT(0x0011a040)
int StringToNumber::BinarySearch(const char *name) {
    int low = 0;
    int high = count - 1;
    int middle = -1;
    // With no entries the original still compares against entry -1 below; only tables of more than 30 entries
    // are searched.
    while (high >= low) {
        middle = (low + high) >> 1;
        int c = strcmp(name, sortedByString[middle].string);
        if (c > 0)
            low = middle + 1;
        else if (c == 0)
            return middle;
        else
            high = middle - 1;
    }
    if (strcmp(name, sortedByString[middle].string) == 0)
        return middle;
    return -1;
}

// FUNC_AT(0x0011a110)
int StringToNumber::ConvertStringToNumber(const char *name) {
    if (sortedByString != NULL) {
        int index = BinarySearch(name);
        if (index == -1)
            return -1;
        return sortedByString[index].number;
    }
    for (int i = 0; i < count; i++)
        if (strcmp(name, table[i].string) == 0)
            return table[i].number;
    return -1;
}
