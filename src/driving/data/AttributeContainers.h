#ifndef DRIVING_DATA_ATTRIBUTECONTAINERS_H_
#define DRIVING_DATA_ATTRIBUTECONTAINERS_H_

// The attribute system's containers (engine/RbTree.h has the layout every tree shares): seven std::maps and
// std::sets, the attribute collection - one of them with bookkeeping - and the list of string store blocks with the
// std::sort the system runs over it. See AttributeContainers.cpp.
//
//   tree                 node   key -> value                                    the game's name for it
//   AttributeMap         0x1c   attribute name -> AttributeValue                (a collection's attributes)
//   AttributeFieldMap    0x2c   key -> AttributeField                           (one class's extension fields)
//   ExtensionClassMap    0x20   class name -> AttributeFieldMap                 "AttributeExtensionClassMap"
//   ExtensionTypeMap     0x28   type id -> AttributeExtension                   "AttributeExtensionTypeMap"
//   EditConfigMap        0x30   (never filled)                                  "AttributeEditConfigMap"
//   CollectionMap        0x38   (class, name) -> AttributeCollection            "AttributeCollectionMap"
//   StringSet            0x14   string                                          "AttributeStringSet"
//
// Names compare without regard to case (the game's _stricmp), the string set's strings with strcmp.

#include "../engine/RbTree.h"
#include "AttributeValue.h"
#include "AttributeParsers.h"

#include <stddef.h>
#include <stdint.h>

// ---------------------------------------------------------------------------------------------------------------
// A collection's attributes: name -> value.

struct AttributeMapValue {        // the map's value_type, 0xc
    const char *name;          // +0x00 (a string from the system's store)
    AttributeValue value;      // +0x04

    void Destruct();                                                                            // 0x000561f0
};

struct AttributeNode : RbTreeNode<AttributeNode, AttributeMapValue> {
    void DestroyValue();   // the value's destructor, called on the node                        // 0x00056230
};
static_assert(sizeof(AttributeNode) == 0x1c, "an attribute map node is 0x1c bytes");

struct AttributeMap : RbTree<AttributeNode> {
    struct Iterator {
        AttributeNode *node;
        void Inc();                                                                             // 0x00052720
        void Dec();                                                                             // 0x000526c0
    };
    struct InsertResult {
        Iterator position;
        bool inserted;
        uint8_t unknown05[3];
    };

    void Rrotate(AttributeNode *node);                                                          // 0x00052600
    void Lrotate(AttributeNode *node);                                                          // 0x00052c70
    AttributeNode *Lbound(const char *const &key);                                              // 0x00052d90
    AttributeNode *Buynode(AttributeNode *left, AttributeNode *parent, AttributeNode *right,
                           const AttributeMapValue &value, uint8_t color);                         // 0x00056320
    Iterator *Insert(Iterator *result, bool addLeft, AttributeNode *where, const AttributeMapValue &value); // 0x000565b0
    InsertResult *InsertUnique(InsertResult *result, const AttributeMapValue &value);              // 0x00056b90
    Iterator *Erase(Iterator *result, Iterator where);                                          // 0x00056c60
    void EraseSubtree(AttributeNode *node);                                                     // 0x00057070
    Iterator *EraseRange(Iterator *result, Iterator first, Iterator last);                      // 0x000573a0
    // operator[]: the value of `name`, a new one of type kAttributeNone if there was none.
    AttributeValue *Lookup(const char *const &name);                                            // 0x000572e0
    void Destruct();                                                                            // 0x00057ea0

    // Everything freed, without rebalancing (the inlined clear() a collection's release does).
    void Clear() {
        EraseSubtree(head->parent);
        head->parent = head;
        size = 0;
        head->left = head;
        head->right = head;
    }
};
static_assert(sizeof(AttributeMap) == 0xc, "a map is 12 bytes");

// ---------------------------------------------------------------------------------------------------------------
// An extension class's fields: key -> AttributeField.

struct AttributeFieldEntry {   // 0x1c
    const char *name;          // +0x00
    AttributeField field;      // +0x04
};

struct AttributeFieldNode : RbTreeNode<AttributeFieldNode, AttributeFieldEntry> {};
static_assert(sizeof(AttributeFieldNode) == 0x2c, "a field map node is 0x2c bytes");

struct AttributeFieldMap : RbTree<AttributeFieldNode> {
    struct Iterator {
        AttributeFieldNode *node;
        void Inc();                                                                             // 0x00052b40
        void Dec();                                                                             // 0x00052ef0
    };
    struct InsertResult {
        Iterator position;
        bool inserted;
        uint8_t unknown05[3];
    };

    static AttributeFieldNode *Max(AttributeFieldNode *node);                                   // 0x000527c0
    static AttributeFieldNode *Min(AttributeFieldNode *node);                                   // 0x00052980
    AttributeFieldNode *Lbound(const char *const &key);                                         // 0x00052de0
    void Lrotate(AttributeFieldNode *node);                                                     // 0x00052e30
    void Rrotate(AttributeFieldNode *node);                                                     // 0x00052e90
    AttributeFieldNode *Buynode(AttributeFieldNode *left, AttributeFieldNode *parent, AttributeFieldNode *right,
                                const AttributeFieldEntry &value, uint8_t color);               // 0x00053430
    static AttributeFieldNode *BuyHead();                                                       // 0x000536c0
    void EraseSubtree(AttributeFieldNode *node);                                                // 0x00053780
    AttributeFieldNode *Copy(AttributeFieldNode *root, AttributeFieldNode *where);              // 0x00053ba0
    void CopyFrom(const AttributeFieldMap &other);                                              // 0x00053df0
    Iterator *Insert(Iterator *result, bool addLeft, AttributeFieldNode *where,
                     const AttributeFieldEntry &value);                                         // 0x000540a0
    Iterator *Erase(Iterator *result, Iterator where);                                          // 0x00054f30
    InsertResult *InsertUnique(InsertResult *result, const AttributeFieldEntry &value);         // 0x00055200
    Iterator *EraseRange(Iterator *result, Iterator first, Iterator last);                      // 0x000559d0
    AttributeFieldMap *ConstructCopy(const AttributeFieldMap &other);                           // 0x00055c60
    void Destruct();                                                                            // 0x00055af0
};
static_assert(sizeof(AttributeFieldMap) == 0xc, "a map is 12 bytes");

// ---------------------------------------------------------------------------------------------------------------
// Extension classes: class name -> its fields.

struct ExtensionClassEntry {   // 0x10
    const char *className;     // +0x00
    AttributeFieldMap fields;  // +0x04

    void Destruct();                                                                            // 0x00055f60
};

struct ExtensionClassNode : RbTreeNode<ExtensionClassNode, ExtensionClassEntry> {
    void DestroyValue();                                                                        // 0x00055fa0
};
static_assert(sizeof(ExtensionClassNode) == 0x20, "an extension class map node is 0x20 bytes");

struct ExtensionClassMap : RbTree<ExtensionClassNode> {
    struct Iterator {
        ExtensionClassNode *node;
        void Inc();                                                                             // 0x00052a20
    };
    struct InsertResult {
        Iterator position;
        bool inserted;
        uint8_t unknown05[3];
    };

    void Rrotate(ExtensionClassNode *node);                                                     // 0x000527e0
    static ExtensionClassNode *BuyHead();                                                       // 0x00053580
    ExtensionClassNode *Buynode(ExtensionClassNode *left, ExtensionClassNode *parent, ExtensionClassNode *right,
                                const ExtensionClassEntry &value, uint8_t color);               // 0x00056270
    Iterator *Insert(Iterator *result, bool addLeft, ExtensionClassNode *where,
                     const ExtensionClassEntry &value);                                         // 0x000563d0
    InsertResult *InsertUnique(InsertResult *result, const ExtensionClassEntry &value);         // 0x00056790
    Iterator *Erase(Iterator *result, Iterator where);                                          // 0x00056860
    void EraseSubtree(ExtensionClassNode *node);                                                // 0x00056b40
    Iterator *EraseRange(Iterator *result, Iterator first, Iterator last);                      // 0x00057460
    // operator[]: the fields of `className`, a new empty map if it had none.
    AttributeFieldMap *Lookup(const char *const &className);                                    // 0x00056f70
};
static_assert(sizeof(ExtensionClassMap) == 0xc, "a map is 12 bytes");

// ---------------------------------------------------------------------------------------------------------------
// Extension types: id -> AttributeExtension.

typedef void (*AttributeExtensionInit)(const char *className, const char *collectionName,
                                       const char *attributeName, uint32_t type, void *data);

struct AttributeExtension {         // 0x14
    AttributeExtensionInit init;    // +0x00 fills in a new structure's defaults; may be NULL
    uint32_t size;                  // +0x04 the structure's size
    uint32_t type;                  // +0x08 its id; -1 in an entry operator[] made for an unknown id
    const char *attributeName;      // +0x0c the attribute a collection keeps the structure under ("CarPhysics")
    const char *className;          // +0x10 the class whose collections have it ("pvehicle")
};

struct ExtensionTypeEntry {         // 0x18
    uint32_t type;                  // +0x00
    AttributeExtension extension;   // +0x04
};

struct ExtensionTypeNode : RbTreeNode<ExtensionTypeNode, ExtensionTypeEntry> {};
static_assert(sizeof(ExtensionTypeNode) == 0x28, "an extension type map node is 0x28 bytes");

struct ExtensionTypeMap : RbTree<ExtensionTypeNode> {
    struct Iterator {
        ExtensionTypeNode *node;
        void Inc();                                                                             // 0x00052a80
        void Dec();                                                                             // 0x00053010
    };
    struct InsertResult {
        Iterator position;
        bool inserted;
        uint8_t unknown05[3];
    };

    static ExtensionTypeNode *Max(ExtensionTypeNode *node);                                     // 0x00052840
    static ExtensionTypeNode *Min(ExtensionTypeNode *node);                                     // 0x000529a0
    void Lrotate(ExtensionTypeNode *node);                                                      // 0x00052f50
    void Rrotate(ExtensionTypeNode *node);                                                      // 0x00052fb0
    ExtensionTypeNode *Buynode(ExtensionTypeNode *left, ExtensionTypeNode *parent, ExtensionTypeNode *right,
                               const ExtensionTypeEntry &value, uint8_t color);                 // 0x000534a0
    static ExtensionTypeNode *BuyHead();                                                        // 0x000535c0
    void EraseSubtree(ExtensionTypeNode *node);                                                 // 0x00053700
    Iterator *Insert(Iterator *result, bool addLeft, ExtensionTypeNode *where,
                     const ExtensionTypeEntry &value);                                          // 0x00054280
    Iterator *Erase(Iterator *result, Iterator where);                                          // 0x00054990
    InsertResult *InsertUnique(InsertResult *result, const ExtensionTypeEntry &value);          // 0x000552d0
    Iterator *EraseRange(Iterator *result, Iterator first, Iterator last);                      // 0x00055560
};
static_assert(sizeof(ExtensionTypeMap) == 0xc, "a map is 12 bytes");

// ---------------------------------------------------------------------------------------------------------------
// The edit configuration: the retail game registers nothing in it (ConfigEditParameters is empty), so only its
// construction and destruction run. Its node's value layout is unknown beyond its size.

struct EditConfigEntry {            // 0x20
    uint32_t key;                   // +0x00
    uint8_t unknown04[0x1c];
};

struct EditConfigNode : RbTreeNode<EditConfigNode, EditConfigEntry> {};
static_assert(sizeof(EditConfigNode) == 0x30, "an edit configuration map node is 0x30 bytes");

struct EditConfigMap : RbTree<EditConfigNode> {
    struct Iterator {
        EditConfigNode *node;
        void Inc();                                                                             // 0x000529c0
    };

    void Lrotate(EditConfigNode *node);                                                         // 0x00052880
    static EditConfigNode *Max(EditConfigNode *node);                                           // 0x000528e0
    static EditConfigNode *Min(EditConfigNode *node);                                           // 0x00052900
    void Rrotate(EditConfigNode *node);                                                         // 0x00052920
    static EditConfigNode *BuyHead();                                                           // 0x00053540
    void EraseSubtree(EditConfigNode *node);                                                    // 0x00053680
    Iterator *Erase(Iterator *result, Iterator where);                                          // 0x000546c0
    Iterator *EraseRange(Iterator *result, Iterator first, Iterator last);                      // 0x000554a0
};
static_assert(sizeof(EditConfigMap) == 0xc, "a map is 12 bytes");

// ---------------------------------------------------------------------------------------------------------------
// A collection: the attributes of one name of one class, with the class's "default" collection behind it.

struct AttributeCollection {         // 0x20
    AttributeMap attributes;         // +0x00
    uint32_t refCount;               // +0x0c AttributeSets on it, and collections it is the parent of
    uint8_t loaded;                  // +0x10 its file has been read (set at once for one made after the database)
    uint8_t unknown11[3];
    const char *name;                // +0x14
    const char *className;           // +0x18
    AttributeCollection *parent;     // +0x1c searched after this one (the class's "default")

    AttributeCollection *Construct(const char *ofClass, const char *named);                     // 0x00058470
    AttributeCollection *ConstructCopy(const AttributeCollection &other);                       // 0x00058510
    void SetParent(AttributeCollection *newParent);                                             // 0x000579e0

    // One user fewer; with none left the attributes are freed - but `loaded` stays set, so nothing reloads them
    // (inlined in ~AttributeSet, SetName and SetParent).
    void Release() {
        if (refCount != 0)
            refCount--;
        if (refCount == 0)
            attributes.Clear();
    }
};
static_assert(sizeof(AttributeCollection) == 0x20, "an attribute collection is 0x20 bytes");

struct CollectionKey {               // 8
    const char *className;
    const char *name;
};

struct CollectionEntry {             // 0x28
    CollectionKey key;               // +0x00
    AttributeCollection collection;  // +0x08
};

struct CollectionNode : RbTreeNode<CollectionNode, CollectionEntry> {
    void DestroyValue();                                                                        // 0x000585b0
};
static_assert(sizeof(CollectionNode) == 0x38, "a collection map node is 0x38 bytes");

struct CollectionMap : RbTree<CollectionNode> {
    struct Iterator {
        CollectionNode *node;
        void Inc();                                                                             // 0x00052d30
        void Dec();                                                                             // 0x00053130
    };
    struct InsertResult {
        Iterator position;
        bool inserted;
        uint8_t unknown05[3];
    };

    static CollectionNode *Min(CollectionNode *node);                                           // 0x00052780
    static CollectionNode *Max(CollectionNode *node);                                           // 0x00052860
    CollectionNode *Lbound(const CollectionKey &key);                                           // 0x00052cd0
    void Lrotate(CollectionNode *node);                                                         // 0x00053070
    void Rrotate(CollectionNode *node);                                                         // 0x000530d0
    static CollectionNode *BuyHead();                                                           // 0x00053600
    Iterator *Find(Iterator *result, const CollectionKey &key);                                 // 0x00053ad0
    CollectionNode *Buynode(CollectionNode *left, CollectionNode *parent, CollectionNode *right,
                            const CollectionEntry &value, uint8_t color);                       // 0x000585f0
    Iterator *Insert(Iterator *result, bool addLeft, CollectionNode *where, const CollectionEntry &value); // 0x000586a0
    InsertResult *InsertUnique(InsertResult *result, const CollectionEntry &value);             // 0x00058880
    Iterator *Erase(Iterator *result, Iterator where);                                          // 0x00058990
    void EraseSubtree(CollectionNode *node);                                                    // 0x00058c70
    Iterator *EraseRange(Iterator *result, Iterator first, Iterator last);                      // 0x00058fd0
};
static_assert(sizeof(CollectionMap) == 0xc, "a map is 12 bytes");

// ---------------------------------------------------------------------------------------------------------------
// The string set: every string the system has stored, compared with strcmp.

struct StringNode : RbTreeNode<StringNode, const char *> {};
static_assert(sizeof(StringNode) == 0x14, "a string set node is 0x14 bytes");

struct StringSet : RbTree<StringNode> {
    struct Iterator {
        StringNode *node;
        void Inc();                                                                             // 0x00052ae0
    };
    struct InsertResult {
        Iterator position;
        bool inserted;
        uint8_t unknown05[3];
    };

    // The comparator (std::less on the strings).
    bool KeyLess(const char *const &a, const char *const &b);                                   // 0x00052660
    StringNode *Lbound(const char *const &key);                                                 // 0x00053190
    StringNode *Buynode(StringNode *left, StringNode *parent, StringNode *right, const char *const &value,
                        uint8_t color);                                                         // 0x00053500
    static StringNode *BuyHead();                                                               // 0x00053640
    void EraseSubtree(StringNode *node);                                                        // 0x00053740
    Iterator *Find(Iterator *result, const char *const &key);                                   // 0x00053b40
    Iterator *Insert(Iterator *result, bool addLeft, StringNode *where, const char *const &value); // 0x00054460
    Iterator *Erase(Iterator *result, Iterator where);                                          // 0x00054c60
    InsertResult *InsertUnique(InsertResult *result, const char *const &value);                 // 0x00055390
    Iterator *EraseRange(Iterator *result, Iterator first, Iterator last);                      // 0x00055620
};
static_assert(sizeof(StringSet) == 0xc, "a set is 12 bytes");

// ---------------------------------------------------------------------------------------------------------------
// The string store: 4 KB blocks (new[]), each filled from the front. The list is kept sorted by free space,
// fullest first, so that a string goes into the fullest block it fits in.

struct AttributeStoreBlock {         // 8
    char *block;                     // +0x00 NULL until the first string needs it
    uint32_t used;                   // +0x04 bytes taken

    static const uint32_t kBlockSize = 0x1000;
    uint32_t Free() const { return kBlockSize - used; }
    bool operator<(const AttributeStoreBlock &other) const { return Free() < other.Free(); }
};

// A std::vector<AttributeStoreBlock> (0x10: the allocator's byte, first, last, end of storage).
struct StoreBlockList {
    uint8_t allocator;
    uint8_t unknown01[3];
    AttributeStoreBlock *first;      // +0x04
    AttributeStoreBlock *last;       // +0x08
    AttributeStoreBlock *end;        // +0x0c

    uint32_t Size() const { return first == NULL ? 0 : static_cast<uint32_t>(last - first); }
    uint32_t Capacity() const { return first == NULL ? 0 : static_cast<uint32_t>(end - first); }

    void Xlen();   // throws std::length_error("vector<T> too long")                            // 0x00054640
    void InsertN(AttributeStoreBlock *where, uint32_t count, const AttributeStoreBlock &value);  // 0x000556e0
    void PushBack(const AttributeStoreBlock &value);                                            // 0x00055bf0
};
static_assert(sizeof(StoreBlockList) == 0x10, "a vector is 16 bytes");

// The vector's helpers and std::sort's, at their addresses. The iterators are plain pointers; the trailing
// arguments some callers push (the iterator category, a distance type) are not read.
struct StoreBlockRange {
    AttributeStoreBlock *first;
    AttributeStoreBlock *last;
};

// 0x00052ba0
void StoreBlockFill(AttributeStoreBlock *first, AttributeStoreBlock *last, const AttributeStoreBlock &value);
// 0x00052bd0
void StoreBlockIterSwap(AttributeStoreBlock *a, AttributeStoreBlock *b);
// 0x00052c00
void StoreBlockPushHeap(AttributeStoreBlock *first, int hole, int top, AttributeStoreBlock value);
// 0x00053200
void StoreBlockMed3(AttributeStoreBlock *first, AttributeStoreBlock *mid, AttributeStoreBlock *last);
// 0x00053280
void StoreBlockAdjustHeap(AttributeStoreBlock *first, int hole, int bottom, AttributeStoreBlock value);
// 0x00053310
void StoreBlockPopHeap(AttributeStoreBlock *first, AttributeStoreBlock *last, AttributeStoreBlock *dest,
                       AttributeStoreBlock value);
// 0x00053350
void StoreBlockRotate(AttributeStoreBlock *first, AttributeStoreBlock *mid, AttributeStoreBlock *last);
// 0x000537c0
AttributeStoreBlock **StoreBlockCopyBackward(AttributeStoreBlock **result, AttributeStoreBlock *first,
                                             AttributeStoreBlock *last, AttributeStoreBlock *dest);
// 0x000537f0: answers the end of the copy
AttributeStoreBlock *StoreBlockUninitializedCopy(AttributeStoreBlock *first, AttributeStoreBlock *last,
                                                 AttributeStoreBlock *dest);
// 0x00053820
void StoreBlockMedian(AttributeStoreBlock *first, AttributeStoreBlock *mid, AttributeStoreBlock *last);
// 0x00053900
void StoreBlockMakeHeap(AttributeStoreBlock *first, AttributeStoreBlock *last);
// 0x00053950
void StoreBlockPopHeapLast(AttributeStoreBlock *first, AttributeStoreBlock *last);
// 0x00053c50
StoreBlockRange *StoreBlockUnguardedPartition(StoreBlockRange *result, AttributeStoreBlock *first,
                                              AttributeStoreBlock *last);
// 0x00053e80
void StoreBlockSortHeap(AttributeStoreBlock *first, AttributeStoreBlock *last);
// 0x00053ed0
void StoreBlockInsertionSort(AttributeStoreBlock *first, AttributeStoreBlock *last);
// 0x00053f90: std::sort(first, last), `ideal` the depth allowance (the element count)
void StoreBlockSort(AttributeStoreBlock *first, AttributeStoreBlock *last, int ideal);

#endif // DRIVING_DATA_ATTRIBUTECONTAINERS_H_
