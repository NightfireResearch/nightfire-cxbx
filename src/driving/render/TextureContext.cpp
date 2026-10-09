#include "TextureContext.h"

#include "RenderTree.h"
#include "../eagl/EaglGlobals.h"          // EaglMalloc, EaglFree
#include "../eagl/Realgraph.h"            // SHAPE_locatez
#include "../engine/UFileLoader.h"
#include "../engine/UMemory.hpp"
#include "../world/SoundMap.h"            // BuyMapHead

#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// RTextureContext, RTexList, RTextureContextManager and the trees under them (0x00093390..0x00095360), ported from
// the listing. The trees' code is RenderTree.h's; URefCounter<RTextureContext>'s is URefCounter.cpp's.
// ---------------------------------------------------------------------------------------------------------------

// ---- originals called by address

#define CRT_printf ((int (*)(const char *format, ...))0x00132192)

// ---- vtables

static const RTextureContextVtable *const kTextureContextVtable = (const RTextureContextVtable *)0x0019219c;
static const USingletonVtable *const kManagerVtable = (const USingletonVtable *)0x001921b4;
static const USingletonVtable *const kSingletonVtable = (const USingletonVtable *)0x0018beb0;   // USingleton's

typedef void *(__fastcall *DeletingDestructor)(void *object, int, unsigned flags);

// The shared context's stand-in for a shape not found, and the reference that is never reported
constexpr uint32_t kMissingShape = 0x7373696d;   // 'miss'
static const char kBadShapeReference[] = "BadShapeReference";

// Set when NewContext makes a context, cleared by the manager's destructor; nothing reads it (the name is ours)
// XBE_GLOBAL(0x001f2a2c, 1)
static uint8_t ContextsAdded;

// A symbol's four characters as the word they make (up to a terminator; zeros after it)
static uint32_t FourCharacters(const char *text) {
    char characters[4] = {0, 0, 0, 0};
    strncpy(characters, text, sizeof(characters));
    uint32_t word;
    memcpy(&word, characters, sizeof(word));
    return word;
}

// =============================================================================================================
// RTexList
// =============================================================================================================

static bool KeyLess(const TexListKey &a, const TexListKey &b) {
    return a.name < b.name || (a.name == b.name && a.flags < b.flags);
}

// FUNC_AT(0x000931c0)
TexListNode* RTexList::LowerBound(const TexListKey *key) {
    TexListNode *bound = head;
    for (TexListNode *node = head->parent; !node->isNil;) {
        if (KeyLess(node->value.key, *key)) {
            node = node->right;
        } else {
            bound = node;
            node = node->left;
        }
    }
    return bound;
}

// FUNC_AT(0x000932b0)
TexListNode** RTexList::Find(TexListNode **result, const TexListKey *key) {
    TexListNode *bound = LowerBound(key);
    *result = bound == head || KeyLess(*key, bound->value.key) ? head : bound;
    return result;
}

// FUNC_AT(0x00093350)
void RTexList::EraseSubtree(TexListNode *node) {
    RenderTree::EraseSubtree(this, node);
}

// FUNC_AT(0x00093420)
TexListNode** RTexList::InsertAt(TexListNode **result, bool addLeft, TexListNode *where, const TexListValue *value) {
    *result = RenderTree::InsertAt(this, addLeft, where, *value);
    return result;
}

// FUNC_AT(0x00093600)
TexListNode** RTexList::EraseAt(TexListNode **result, TexListNode *where) {
    *result = RenderTree::EraseAt(this, where);
    return result;
}

// FUNC_AT(0x000938d0)
TexListInsert* RTexList::InsertUnique(TexListInsert *result, const TexListValue *value) {
    TexListNode *where = head;
    bool addLeft = true;
    for (TexListNode *node = head->parent; !node->isNil; node = addLeft ? node->left : node->right) {
        where = node;
        addLeft = KeyLess(value->key, node->value.key);
    }
    TexListNode *before = where;
    if (addLeft) {
        if (where == head->left) {
            TexListNode *node;
            result->node = *InsertAt(&node, true, where, value);
            result->inserted = true;
            return result;
        }
        before = RenderTree::Prev(before);
    }
    if (KeyLess(before->value.key, value->key)) {
        TexListNode *node;
        result->node = *InsertAt(&node, addLeft, where, value);
        result->inserted = true;
        return result;
    }
    result->node = before;
    result->inserted = false;
    return result;
}

// FUNC_AT(0x000939d0)
TexListNode** RTexList::EraseRange(TexListNode **result, TexListNode *first, TexListNode *last) {
    *result = RenderTree::EraseRange(this, first, last);
    return result;
}

// FUNC_AT(0x00093a90)
TexListNode** RTexList::CreateNewTextureElement(TexListNode **result, uint32_t name, uint32_t flags, uint8_t *shapes) {
    TexListNode *end = head;
    if (shapes != NULL) {
        char shapeName[5];
        memcpy(shapeName, &name, 4);
        shapeName[4] = '\0';
        if (uint8_t *shape = SHAPE_locatez(shapes, shapeName)) {
            TexListValue value;
            value.key.name = name;
            value.key.flags = flags;
            value.shape = shape;
            value.tar = NULL;
            value.used = 0;
            memset(value.pad11, 0, sizeof(value.pad11));
            TexListInsert inserted;
            *result = InsertUnique(&inserted, &value)->node;
            return result;
        }
    }
    *result = end;
    return result;
}

// FUNC_AT(0x00093d90)
void RTexList::Destruct() {
    RenderTree::Destroy(this);
}

// =============================================================================================================
// RTextureContext
// =============================================================================================================

// FUNC_AT(0x00093dd0)
RTextureContext* RTextureContext::Construct(const char *path, uint32_t contextFlags) {
    vtable = kTextureContextVtable;
    flags = contextFlags;
    shapes = NULL;
    textures = NULL;
    RTexList *list = static_cast<RTexList *>(UMemory::FastAlloc(sizeof(RTexList), "RTexList"));
    if (list != NULL)
        RenderTree::Construct(list, BuyMapHead<TexListNode>());
    textures = list;
    if (shapes == NULL)
        shapes = static_cast<uint8_t *>(UFileLoader::FileLoadz(path, 0));
    return this;
}

// FUNC_AT(0x00093e80)
RTextureContext* RTextureContext::Delete(unsigned deleteFlags) {
    vtable = kTextureContextVtable;
    Unload();
    if (RTexList *list = textures) {
        list->Destruct();
        UMemory::FastFree(list, sizeof(RTexList));
    }
    if (deleteFlags & 1)
        UMemory::FastFree(this, sizeof(RTextureContext));
    return this;
}

// FUNC_AT(0x00093390)
void RTextureContext::Unload() {
    for (TexListNode *node = textures->head->left; node != textures->head; node = RenderTree::Next(node)) {
        if (EAGL::TAR *tar = node->value.tar) {
            tar->Destruct();
            EaglFree(tar, sizeof(EAGL::TAR));
        }
    }
    if (shapes != NULL)
        UMemory::Free(shapes);
    RTexList *list = textures;
    list->EraseSubtree(list->head->parent);
    list->head->parent = list->head;
    list->size = 0;
    list->head->left = list->head;
    list->head->right = list->head;
}

// FUNC_AT(0x00093b20)
uint8_t* RTextureContext::FindWithoutOwning(uint32_t name, uint32_t flags) {
    RTexList *list = textures;
    TexListKey key = {name, flags};
    TexListNode *node;
    list->Find(&node, &key);
    if (node == list->head)
        list->CreateNewTextureElement(&node, name, flags, shapes);
    return node != textures->head ? node->value.shape : NULL;
}

// FUNC_AT(0x00093ba0)
EAGL::TAR* RTextureContext::FindOrCreateTexture(uint32_t name, uint32_t flags) {
    RTexList *list = textures;
    TexListKey key = {name, flags};
    TexListNode *node;
    list->Find(&node, &key);
    if (node == list->head)
        list->CreateNewTextureElement(&node, name, flags, shapes);
    if (node == textures->head)
        return NULL;
    if (node->value.tar == NULL) {
        uint8_t *shape = node->value.shape;
        EAGL::TAR *tar = static_cast<EAGL::TAR *>(EaglMalloc(sizeof(EAGL::TAR), "EAGL::TAR new"));
        node->value.tar = tar != NULL ? tar->Construct(shape) : NULL;
    }
    return node->value.tar;
}

// FUNC_AT(0x00093c70)
uint8_t* RTextureContext::Find(uint32_t name, uint32_t flags) {
    uint8_t *shape = FindWithoutOwning(name, flags);
    if (shape != NULL) {
        TexListKey key = {name, flags};
        TexListNode *node;
        textures->Find(&node, &key);
        node->value.used = 1;
    }
    return shape;
}

// FUNC_AT(0x00093cd0)
uint8_t* RTextureContext::NameLookup(const char *symbol, int *) {
    uint32_t name = FourCharacters(symbol);
    uint32_t flags = 0;
    if (symbol[4] == ':')
        flags = FourCharacters(symbol + 6);
    if (uint8_t *shape = FindWithoutOwning(name, flags))
        return shape;
    if (strcmp(symbol, kBadShapeReference) != 0)
        CRT_printf("COULDN'T FIND TEXTURE [%s]\n", symbol);
    uint8_t *shape = RTextureContextManager::GetContext(0)->Find(kMissingShape, 0);
    if (shape == NULL)
        CRT_printf("COULDN'T GET REPLACEMENT TEXTURE\n");
    return shape;
}

// =============================================================================================================
// The manager's map
// =============================================================================================================

// FUNC_AT(0x00093ed0)
void TextureContextMap::EraseSubtree(TextureContextNode *node) {
    RenderTree::EraseSubtree(this, node);
}

// FUNC_AT(0x00093f10)
TextureContextNode* TextureContextMap::BuyNode(TextureContextNode *left, TextureContextNode *parent, TextureContextNode *right, const TextureContextEntry *value, uint8_t color) {
    return RenderTree::BuyNode(left, parent, right, *value, color);
}

// FUNC_AT(0x00094140)
TextureContextNode** TextureContextMap::EraseAt(TextureContextNode **result, TextureContextNode *where) {
    *result = RenderTree::EraseAt(this, where);
    return result;
}

// FUNC_AT(0x00094410)
TextureContextNode** TextureContextMap::EraseRange(TextureContextNode **result, TextureContextNode *first, TextureContextNode *last) {
    *result = RenderTree::EraseRange(this, first, last);
    return result;
}

// FUNC_AT(0x000944d0)
TextureContextNode** TextureContextMap::InsertAt(TextureContextNode **result, bool addLeft, TextureContextNode *where, const TextureContextEntry *value) {
    *result = RenderTree::InsertAt(this, addLeft, where, *value);
    return result;
}

// FUNC_AT(0x00094d10)
TextureContextInsert* TextureContextMap::InsertEqual(TextureContextInsert *result, const TextureContextEntry *value) {
    TextureContextNode *where = head;
    bool addLeft = true;
    for (TextureContextNode *node = head->parent; !node->isNil; node = addLeft ? node->left : node->right) {
        where = node;
        addLeft = value->key < node->value.key;
    }
    TextureContextNode *node;
    result->node = *InsertAt(&node, addLeft, where, value);
    result->inserted = true;
    return result;
}

// The first node of `key`, or the end (the game calls the copy every map keyed by int shares, WSoundMap::Find)
static TextureContextNode *FindContext(TextureContextMap *map, int32_t key) {
    TextureContextNode *bound = map->head;
    for (TextureContextNode *node = map->head->parent; !node->isNil;) {
        if (node->value.key < key) {
            node = node->right;
        } else {
            bound = node;
            node = node->left;
        }
    }
    return bound == map->head || key < bound->value.key ? map->head : bound;
}

// =============================================================================================================
// URefCounter<RTextureContext>'s tree
// =============================================================================================================

// FUNC_AT(0x000940f0)
void TextureContextRefTree::EraseSubtree(RefCounterNode *node) {
    RefCounterTree::EraseSubtree(node);
}

// FUNC_AT(0x000946b0)
RefCounterNode** TextureContextRefTree::EraseAt(RefCounterNode **result, RefCounterNode *where) {
    return RefCounterTree::EraseAt(result, where);
}

// FUNC_AT(0x00094a20)
RefCounterNode** TextureContextRefTree::InsertAt(RefCounterNode **result, bool addLeft, RefCounterNode *where, const RefCounterValue *value) {
    return RefCounterTree::InsertAt(result, addLeft, where, value);
}

// FUNC_AT(0x00094c10)
RefCounterNode** TextureContextRefTree::EraseRange(RefCounterNode **result, RefCounterNode *first, RefCounterNode *last) {
    return RefCounterTree::EraseRange(result, first, last);
}

// FUNC_AT(0x00094df0)
RefCounterInsertResult* TextureContextRefTree::InsertUnique(RefCounterInsertResult *result, const RefCounterValue *value) {
    return RefCounterTree::InsertUnique(result, value);
}

// FUNC_AT(0x00094fb0)
void TextureContextRefTree::DestroyRange() {
    RefCounterTree::DestroyRange();
}

// FUNC_AT(0x00094ff0)
void TextureContextRefTree::Destruct() {
    Destroy();
}

// =============================================================================================================
// RTextureContextManager
// =============================================================================================================

// FUNC_AT(0x00095080)
RTextureContextManager* RTextureContextManager::Construct() {
    vtable = kManagerVtable;
    TextureContextMap *map = static_cast<TextureContextMap *>(OperatorNew(sizeof(TextureContextMap)));
    if (map != NULL)
        RenderTree::Construct(map, BuyMapHead<TextureContextNode>());
    contexts = map;
    return this;
}

// Each context's reference is given back, and a context with none left deleted; then the map goes.
// FUNC_AT(0x00095290)
void RTextureContextManager::Destruct() {
    vtable = kManagerVtable;
    TextureContextMap *map = contexts;
    ContextsAdded = 0;
    for (TextureContextNode *node = map->head->left; node != map->head; node = RenderTree::Next(node)) {
        if (TextureContextRefCounter::Get()->RemoveReference(node->value.context) && node->value.context != NULL)
            node->value.context->vtable->deleteObject(node->value.context, 0, 1);
    }
    if (map != NULL) {
        TextureContextNode *after;
        map->EraseRange(&after, map->head->left, map->head);
        if (map->head != NULL)
            UMemory::FastFree(map->head, sizeof(TextureContextNode));
        map->head = NULL;
        map->size = 0;
        OperatorDelete(map);
    }
    vtable = kSingletonVtable;
}

// FUNC_AT(0x00095360)
RTextureContextManager* RTextureContextManager::Delete(unsigned flags) {
    Destruct();
    if (flags & 1)
        OperatorDelete(this);
    return this;
}

// FUNC_AT(0x00095130)
void RTextureContextManager::Kill() {
    RTextureContextManager *manager = TheTextureContextManager;
    if (manager != NULL)
        reinterpret_cast<DeletingDestructor>(manager->vtable->slot0)(manager, 0, 1);
}

// FUNC_AT(0x00093fb0)
EAGL::TAR* RTextureContextManager::FindOrCreateTexture(uint32_t name, int32_t key) {
    TextureContextMap *map = contexts;
    TextureContextNode *node = map->head;
    for (TextureContextNode *at = map->head->parent; !at->isNil;) {
        if (at->value.key < key) {
            at = at->right;
        } else {
            node = at;
            at = at->left;
        }
    }
    for (; node != map->head && node->value.key == key; node = RenderTree::Next(node)) {
        if (EAGL::TAR *tar = node->value.context->FindOrCreateTexture(name, 0))
            return tar;
    }
    return NULL;
}

// FUNC_AT(0x000940c0)
RTextureContext* RTextureContextManager::GetContext(int32_t key) {
    return FindContext(TheTextureContextManager->contexts, key)->value.context;
}

// FUNC_AT(0x00094c90)
void RTextureContextManager::KillContext(RTextureContext *context) {
    TextureContextMap *map = contexts;
    for (TextureContextNode *node = map->head->left; node != map->head;) {
        TextureContextNode *at = node;
        node = RenderTree::Next(node);
        if (at->value.context == context) {
            TextureContextNode *after;
            map->EraseAt(&after, at);
        }
    }
}

// FUNC_AT(0x000951e0)
RTextureContext* RTextureContextManager::NewContext(const char *path, int32_t key) {
    if (RTextureContext *known = TextureContextRefCounter::Get()->GetReference(path))
        return known;
    RTextureContext *made = static_cast<RTextureContext *>(UMemory::FastAlloc(sizeof(RTextureContext), "RTextureContext"));
    RTextureContext *context = made != NULL ? made->Construct(path, 0) : NULL;
    TextureContextRefCounter::Get()->AddReference(path, context);
    TextureContextEntry entry = {key, context};
    TextureContextInsert inserted;
    contexts->InsertEqual(&inserted, &entry);
    ContextsAdded = 1;
    return context;
}
