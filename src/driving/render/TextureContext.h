#ifndef DRIVING_RENDER_TEXTURECONTEXT_H_
#define DRIVING_RENDER_TEXTURECONTEXT_H_

// ---------------------------------------------------------------------------------------------------------------
// Texture contexts: a context is one loaded shape file (SHPX, realgraph's images) and the textures made from it,
// found by a four-character name and a second word of flags. RTexList maps each (name, flags) asked for to the
// file's shape and, once something wants one, the EAGL::TAR made from it. RTextureContextManager keeps the contexts
// in a multimap by an int key (RCARPFile uses 4 and 5; NameLookup's stand-in comes from 0) and registers
// each by its path with URefCounter<RTextureContext>, so a path loads once. A context is also a symbol namespace:
// "NAME" or "NAME::FLGS" references resolve to shapes, with context 0's 'miss' standing in for one not
// found. The manager is a USingleton (its pointer at 0x001f2a30). See TextureContext.cpp.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "../eagl/Tar.h"                  // EAGL::TAR
#include "../engine/RbTree.h"
#include "../engine/URefCounter.h"        // RefCounterTree, TextureContextRefCounter
#include "../engine/USingleton.h"         // USingletonVtable

class RTextureContext;

// ---- RTexList: (name, flags) -> the shape, and its texture once made

struct TexListKey {                       // ordered as a std::pair, both words unsigned
    uint32_t name;                        // four characters, as the symbol spells them
    uint32_t flags;
};

struct TexListValue {
    TexListKey key;                       // +0x00
    uint8_t *shape;                       // +0x08 in the context's file
    EAGL::TAR *tar;                       // +0x0c made by FindOrCreateTexture, NULL until then
    uint8_t used;                         // +0x10 set by Find
    uint8_t pad11[3];
};
static_assert(sizeof(TexListValue) == 0x14, "a texture list entry is 0x14 bytes");

struct TexListNode : RbTreeNode<TexListNode, TexListValue> {};
static_assert(sizeof(TexListNode) == 0x24, "a texture list node is 0x24 bytes");

struct TexListInsert {
    TexListNode *node;
    bool inserted;
};

// The map (std::map<pair, ...>, 0xc bytes) and its compiled copies. Its rotations, minimum and maximum, ++, -- and
// node maker are the state set's (StateManager.h): the same node size.
class RTexList : public RbTree<TexListNode> {
public:
    TexListNode* LowerBound(const TexListKey *key);                                                 // 0x000931c0
    TexListNode** Find(TexListNode **result, const TexListKey *key);                                // 0x000932b0
    void EraseSubtree(TexListNode *node);                                                           // 0x00093350
    TexListNode** InsertAt(TexListNode **result, bool addLeft, TexListNode *where,
                           const TexListValue *value);                                              // 0x00093420
    TexListNode** EraseAt(TexListNode **result, TexListNode *where);                                // 0x00093600
    TexListInsert* InsertUnique(TexListInsert *result, const TexListValue *value);                  // 0x000938d0
    TexListNode** EraseRange(TexListNode **result, TexListNode *first, TexListNode *last);          // 0x000939d0
    // The entry for (name, flags), its shape looked up in `shapes` by the name's four characters; the end when
    // there is no file or no such shape.
    TexListNode** CreateNewTextureElement(TexListNode **result, uint32_t name, uint32_t flags,
                                          uint8_t *shapes);                                         // 0x00093a90
    // The destructor: everything erased, the head freed.
    void Destruct();                                                                                // 0x00093d90
};
static_assert(sizeof(RTexList) == 0xc, "RTexList is a map");

// ---- RTextureContext

struct RTextureContextVtable {            // 0x0019219c
    void *nameLookup;
    RTextureContext *(__fastcall *deleteObject)(RTextureContext *context, int, unsigned flags);
};

class RTextureContext {
public:
    const RTextureContextVtable *vtable;  // +0x00
    uint32_t flags;                       // +0x04 the constructor's second argument
    uint8_t *shapes;                      // +0x08 the file (UFileLoader::FileLoadz); Unload frees it, keeping the pointer
    RTexList *textures;                   // +0x0c

    // Loads `path` (unless the file is somehow there already) and makes the empty list.
    RTextureContext* Construct(const char *path, uint32_t contextFlags);                            // 0x00093dd0
    RTextureContext* Delete(unsigned deleteFlags);                                                  // 0x00093e80
    // Frees the textures made and the file, and empties the list. Neither pointer is cleared.
    void Unload();                                                                                  // 0x00093390

    // The shape of (name, flags), entering it in the list the first time; NULL if the file has none.
    uint8_t* FindWithoutOwning(uint32_t name, uint32_t flags);                                      // 0x00093b20
    // The texture of (name, flags), made the first time; NULL if the file has no such shape.
    EAGL::TAR* FindOrCreateTexture(uint32_t name, uint32_t flags);                                  // 0x00093ba0
    // FindWithoutOwning, marking the entry used.
    uint8_t* Find(uint32_t name, uint32_t flags);                                                   // 0x00093c70
    // Slot 0: the shape a symbol names, "NAME" or "NAME::FLGS"; one not found is reported and replaced by
    // context 0's 'miss' (both reported when that fails too). `size` is not touched.
    uint8_t* NameLookup(const char *symbol, int *size);                                             // 0x00093cd0
};
static_assert(sizeof(RTextureContext) == 0x10, "RTextureContext is 0x10 bytes");

// ---- the manager's multimap, int key -> context

struct TextureContextEntry {
    int32_t key;                          // compared signed
    RTextureContext *context;
};

struct TextureContextNode : RbTreeNode<TextureContextNode, TextureContextEntry> {};
static_assert(sizeof(TextureContextNode) == 0x18, "a context map node is 0x18 bytes");

struct TextureContextInsert {
    TextureContextNode *node;
    bool inserted;
};

// Its compiled copies: the data layer's maps' code (data/Tree.h) instruction for instruction. The node maker is the
// one every map with 0x18-byte nodes shares.
class TextureContextMap : public RbTree<TextureContextNode> {
public:
    void EraseSubtree(TextureContextNode *node);                                                    // 0x00093ed0
    TextureContextNode* BuyNode(TextureContextNode *left, TextureContextNode *parent, TextureContextNode *right,
                                const TextureContextEntry *value, uint8_t color);                  // 0x00093f10
    TextureContextNode** EraseAt(TextureContextNode **result, TextureContextNode *where);           // 0x00094140
    TextureContextNode** EraseRange(TextureContextNode **result, TextureContextNode *first,
                                    TextureContextNode *last);                                      // 0x00094410
    TextureContextNode** InsertAt(TextureContextNode **result, bool addLeft, TextureContextNode *where,
                                  const TextureContextEntry *value);                                // 0x000944d0
    // insert for a multimap: always inserts, after any node of the same key.
    TextureContextInsert* InsertEqual(TextureContextInsert *result, const TextureContextEntry *value);   // 0x00094d10
};
static_assert(sizeof(TextureContextMap) == 0xc, "a map is 12 bytes");

// ---- URefCounter<RTextureContext>'s tree (TextureContextRefCounter's map, at 0x001f2a34): its compiled copies

class TextureContextRefTree : public RefCounterTree {
public:
    void EraseSubtree(RefCounterNode *node);                                                        // 0x000940f0
    RefCounterNode** EraseAt(RefCounterNode **result, RefCounterNode *where);                       // 0x000946b0
    RefCounterNode** InsertAt(RefCounterNode **result, bool addLeft, RefCounterNode *where,
                              const RefCounterValue *value);                                        // 0x00094a20
    RefCounterNode** EraseRange(RefCounterNode **result, RefCounterNode *first, RefCounterNode *last);   // 0x00094c10
    RefCounterInsertResult* InsertUnique(RefCounterInsertResult *result, const RefCounterValue *value);  // 0x00094df0
    // The copy an exception unwind calls: the range erased, the head freed.
    void DestroyRange();                                                                            // 0x00094fb0
    // The static's destructor (atexit): everything erased, the head freed.
    void Destruct();                                                                                // 0x00094ff0
};

// ---- the manager

class RTextureContextManager {
public:
    const USingletonVtable *vtable;       // +0x00 a USingleton's (0x001921b4)
    TextureContextMap *contexts;          // +0x04

    RTextureContextManager* Construct();                                                            // 0x00095080
    void Destruct();                                                                                // 0x00095290
    RTextureContextManager* Delete(unsigned flags);                                                 // 0x00095360
    // The vtable's kill slot: deletes the manager the singleton pointer holds (without clearing it).
    void Kill();                                                                                    // 0x00095130

    // The texture of `name` from the first context under `key` that has it; NULL if none does.
    EAGL::TAR* FindOrCreateTexture(uint32_t name, int32_t key);                                     // 0x00093fb0
    // The context under `key` (the first, if several). A key not there answers whatever the map's head holds.
    static RTextureContext* GetContext(int32_t key);                                                // 0x000940c0
    // Takes every entry of `context` out of the map (the context itself is left alone).
    void KillContext(RTextureContext *context);                                                     // 0x00094c90
    // The context of `path`: one already registered under the path is answered as it is (no reference added, not
    // entered under `key`); otherwise a new one, registered and entered under `key`.
    RTextureContext* NewContext(const char *path, int32_t key);                                     // 0x000951e0
};
static_assert(sizeof(RTextureContextManager) == 8, "RTextureContextManager is 8 bytes");

// The singleton (made by its USingleton Init, 0x0008b5b0..0x0008bf30)
#define TheTextureContextManager (*(RTextureContextManager **)0x001f2a30)

#endif // DRIVING_RENDER_TEXTURECONTEXT_H_
