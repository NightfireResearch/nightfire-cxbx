#ifndef DRIVING_DATA_SYMBOLTABLE_H_
#define DRIVING_DATA_SYMBOLTABLE_H_

// Names to data: the symbol table CARP references resolve against, and the string-to-number tables.
//
// USymbolTable is a multimap from a namespace's name to SymbolNamespace objects. A lookup takes "NS::rest",
// finds the namespaces called NS (case-insensitively, several may share a name) and asks each in turn to look up
// "rest" until one answers. There is one table, GlobalSymbolTable's (RCARPFile.cpp), holding:
//   - "CARP": the RCARPFileLoader, a UCarpNamespace whose groups map holds each loaded CARP file's groups by name
//     ("<<Root>>", "<<Map>>", "<<Shared>>" and every article by its 'Name' record). It answers
//     "Group::{tag}" or "Group::{tag}::offset" with that group's record of the tag (and its size);
//   - "CHAR" and "DATA": a UCharNamespace, which answers a name with the name itself - a literal string;
//   - "EAGL", "TEX0".."TEX9": what RCARPFile::Resolve adds while it resolves a file (its model loader and
//     texture files), and what the renderer adds (outside this layer).
// Every namespace object starts with a vtable whose slot 0 is NameLookup(name, int *size) and slot 1 the
// deleting destructor.
//
// StringToNumber maps names to numbers through a table of {number, name} pairs ending in a null name; a table of
// more than 30 entries gets a sorted copy for binary search. The input layer uses it for action and control
// names.

#include "Tree.h"
#include "UData.h"

#include <stdint.h>

// ---- string-to-number tables

struct StringToNumberEntry {
    int number;
    char *string;
};

struct StringToNumber {
    int count;
    StringToNumberEntry *table;            // the caller's table, ending in a null string
    StringToNumberEntry *sortedByString;   // count entries sorted by name, or null for 30 or fewer
    StringToNumberEntry *sortedByNumber;   // the second half of the same block, sorted by number

    // The constructor (0x00119f50): counts the table and, above 30 entries, sorts two copies of it.
    StringToNumber *Construct(StringToNumberEntry *entries);
    // The destructor (0x0011a020).
    void Destruct();
    // The index of `name` in sortedByString, or -1 (0x0011a040).
    int BinarySearch(const char *name);
    // The number for `name`, -1 for an unknown one (0x0011a110).
    int ConvertStringToNumber(const char *name);
};
static_assert(sizeof(StringToNumber) == 16, "StringToNumber is 16 bytes");

// qsort's comparisons for the sorted copies (0x00119ef0, 0x00119f40).
int CompareByString(const void *a, const void *b);
int CompareByNumber(const void *a, const void *b);

// ---- namespaces

// Any namespace the table holds. Its lookup is the game's virtual: Lookup calls slot 0 of the object's vtable.
class SymbolNamespace {
public:
    void *vtable;

    void *Lookup(const char *name, int *size);

    // The vtable's slots, declared for their types only (XbeVirtual calls them; the objects' classes define them).
    void *NameLookupSlot(const char *name, int *size);
    void *DeleteSlot(unsigned flags);
};

// USymbolTable's multimap of namespaces by name.
class NamespaceMap : public Tree {
public:
    // lower_bound (0x0011a880): the first node whose name is not below *key.
    TreeNode* LowerBound(const char *const *key);
    void EraseSubtree(TreeNode *node);                                        // 0x0011a8d0
    TreeNode** EraseAt(TreeNode **result, TreeNode *where);                   // 0x0011aba0 (both name maps)
    TreeNode** EraseRange(TreeNode **result, TreeNode *first, TreeNode *last);   // 0x0011ae70
    TreeNode** InsertAt(TreeNode **result, bool addLeft, TreeNode *where, const TreePair *value);  // 0x0011af30
    // insert for a multimap (0x0011b250): always inserts, after any node of the same name.
    TreeInsertResult* InsertEqual(TreeInsertResult *result, const TreePair *value);
    void Destroy();                                                           // 0x0011b4d0
};

// UCarpNamespace's map of groups by name.
class CarpGroupMap : public Tree {
public:
    void EraseSubtree(TreeNode *node);                                        // 0x0011a910
    // insert for a map (0x0011b180): a name already there keeps its group.
    TreeInsertResult* InsertUnique(TreeInsertResult *result, const TreePair *value);
    TreeNode** EraseRange(TreeNode **result, TreeNode *first, TreeNode *last);   // 0x0011b2d0
    void Destroy();                                                           // 0x0011b540
};

class USymbolTable {
public:
    void *vtable;
    NamespaceMap *namespaces;

    // The constructor (0x0011b580) and destructor (0x0011b620): the map, made and freed through the game's
    // pools. Delete is the deleting destructor (0x0011b650), the vtable's only slot.
    USymbolTable *Construct();
    void Destruct();
    USymbolTable *Delete(unsigned flags);

    // "NS::rest": asks each namespace called NS for "rest", first answer wins; null if none (0x0011a950).
    void *NameLookup(const char *name, int *size);
    void AddNamespace(const char *name, SymbolNamespace *ns);   // 0x0011b510
    // Takes out the first namespace of that name and answers it (0x0011b110). The game never removes one it
    // did not add; a missing name would throw from erase.
    SymbolNamespace* RemoveNamespace(const char *name);
};
static_assert(sizeof(USymbolTable) == 8, "USymbolTable is 8 bytes");

// "CHAR" and "DATA": a name is its own value, its size its length with the terminator.
class UCharNamespace : public SymbolNamespace {
public:
    UCharNamespace *Construct();                       // 0x0011a800
    void Destruct();                                   // 0x0011a810
    void *NameLookup(const char *name, int *size);     // 0x0011a820, slot 0
    UCharNamespace *Delete(unsigned flags);            // 0x0011a850, slot 1
};
static_assert(sizeof(UCharNamespace) == 4, "UCharNamespace is 4 bytes");

// "CARP": loaded files' groups by name.
class UCarpNamespace : public SymbolNamespace {
public:
    CarpGroupMap *groups;

    UCarpNamespace *Construct();                       // 0x0011b690
    void Destruct();                                   // 0x0011b730
    UCarpNamespace *Delete(unsigned flags);            // 0x0011b760, slot 1
    // "Group::{tag}" or "Group::{tag}}::offset" -> the group's record of that tag plus the offset, its size in
    // *size; "Group" alone -> the group itself, size 0 (0x0011aa80, slot 0).
    void *NameLookup(const char *name, int *size);
    // Registers a file's root group, its 'Map ' and 'Shar' groups and every 'Arti' group by name (0x0011b390).
    void AddCarpFile(UGroup *root);

    // Splits "Group::{tag}::offset" (0x0011a750): the group's name into `group`, the decoded tag and the offset.
    // A name starting with '{' is a tag alone, with an empty group name. Always true.
    static bool ParseNameAndTag(const char *name, char *group, uint32_t *tag, int *offset);
};
static_assert(sizeof(UCarpNamespace) == 8, "UCarpNamespace is 8 bytes");

#endif // DRIVING_DATA_SYMBOLTABLE_H_
