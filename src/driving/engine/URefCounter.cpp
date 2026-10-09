#include "URefCounter.h"

#include "UMemory.hpp"
#include "../../helpers.h"
#include "../audio/Bank.h"              // BankRefTree
#include "../audio/Fader.h"             // FaderRefTree
#include "../audio/Mix.h"               // MixRefTree
#include "../audio/Stream.h"            // StreamRefTree
#include "../anim/Model.h"              // ModelInfoRefTree, TextureInfoRefTree
#include "../anim/Weapon.h"             // WeaponInfoRefTree
#include "../data/Tree.h"               // TreeThrow
#include "../render/RSceneObj.hpp"
#include "../render/TextureContext.h"
#include "../world/SoundMap.h"          // RefCounterMapBuyHead

// ---------------------------------------------------------------------------------------------------------------
// URefCounter<T>, ported from the listings of its 29 compiled copies (see URefCounter.h). Every AddReference copy
// is the same code (0x00018030 is the model), as is every RemoveReference (0x00090130), destructor (0x000182b0)
// and Get (0x00018340); they differ only in the tree helpers they call and, for Get, the static they make.
//
// Each instantiation's own tree code is reached through the shapes URefCounter's helpers take (the adaptors below),
// or by address where it is not ours yet. The name comparisons are the C runtime's _stricmp, called at its own
// address: the tree's order depends on them. RefCounterTree's code, every instantiation's, is at the end.
// ---------------------------------------------------------------------------------------------------------------

// ---- originals called by address

#define RefCounterValue_Construct ((RefCounterValue *(__fastcall *)(RefCounterValue *, int, const char *name, const RefCounterEntry *entry))0x0012ef50)
#define CRT_stricmp ((int (*)(const char *, const char *))0x00134537)
#define CRT_atexit ((int (*)(void (*)(void)))0x00132a7b)

// Each instantiation's own insert_unique, erase(where), _Erase(subtree) and erase(first, last)
#define EngineMap_Erase ((RefCounterNode **(__fastcall *)(URefCounterMap *, int, RefCounterNode **, RefCounterNode *))0x0012efd0)
#define EngineMap_Insert ((RefCounterInsertResult *(__fastcall *)(URefCounterMap *, int, RefCounterInsertResult *, const RefCounterValue *))0x0012f620)

// ---- the statics: each instance, the guard of its construction (bit 0), and the destructor its Get registers
// with atexit (the original thunks, StaticInit.cpp)

#define ModelInfoRefs (*(ModelInfoRefCounter *)0x001dd9e8)
#define ModelInfoRefsGuard U32_AT(0x001dd9f4)
#define TextureInfoRefs (*(TextureInfoRefCounter *)0x001dda04)
#define TextureInfoRefsGuard U32_AT(0x001dda10)
#define WeaponInfoRefs (*(WeaponInfoRefCounter *)0x001dda14)
#define WeaponInfoRefsGuard U32_AT(0x001dda20)
#define CarpFileRefs (*(CarpFileRefCounter *)0x001f25f4)
#define CarpFileRefsGuard U32_AT(0x001f2600)
#define TextureContextRefs (*(TextureContextRefCounter *)0x001f2a34)
#define TextureContextRefsGuard U32_AT(0x001f2a40)
#define MixRefs (*(MixRefCounter *)0x00243944)
#define MixRefsGuard U32_AT(0x00243950)
#define StreamRefs (*(StreamRefCounter *)0x00243a88)
#define StreamRefsGuard U32_AT(0x00243a94)
#define FaderRefs (*(FaderRefCounter *)0x00243af8)
#define FaderRefsGuard U32_AT(0x00243b04)
#define BankRefs (*(BankRefCounter *)0x00243b08)
#define BankRefsGuard U32_AT(0x00243b14)
#define EngineRefs (*(EngineRefCounter *)0x00243b74)
#define EngineRefsGuard U32_AT(0x00243b80)

static const RefCounterDestroyFn kDestroyModelInfoRefs = (RefCounterDestroyFn)0x0015cd20;
static const RefCounterDestroyFn kDestroyTextureInfoRefs = (RefCounterDestroyFn)0x0015cd30;
static const RefCounterDestroyFn kDestroyWeaponInfoRefs = (RefCounterDestroyFn)0x0015cd40;
static const RefCounterDestroyFn kDestroyCarpFileRefs = (RefCounterDestroyFn)0x0015ced0;
static const RefCounterDestroyFn kDestroyTextureContextRefs = (RefCounterDestroyFn)0x0015cef0;
static const RefCounterDestroyFn kDestroyMixRefs = (RefCounterDestroyFn)0x0015d250;
static const RefCounterDestroyFn kDestroyStreamRefs = (RefCounterDestroyFn)0x0015d260;
static const RefCounterDestroyFn kDestroyFaderRefs = (RefCounterDestroyFn)0x0015d2d0;
static const RefCounterDestroyFn kDestroyBankRefs = (RefCounterDestroyFn)0x0015d2f0;
static const RefCounterDestroyFn kDestroyEngineRefs = (RefCounterDestroyFn)0x0015d300;

// The game's inline strcpy into a 128-byte key (unbounded, as the original's)
static void CopyName(char *key, const char *name) {
    for (int i = 0;; i++) {
        key[i] = name[i];
        if (name[i] == '\0')
            break;
    }
}

// ---- the code every instantiation shares

RefCounterNode* URefCounterMap::Find(const char *name) {
    char key[0x80];
    CopyName(key, name);
    RefCounterNode *node = LowerBound(key);
    if (node == head || CRT_stricmp(key, node->value.name) < 0)
        return head;
    return node;
}

// FUNC_AT(0x00125270)
void* URefCounterMap::GetReference(const char *name) {
    RefCounterNode *node = Find(name);
    return node != head ? node->value.entry.object : NULL;
}

void URefCounterMap::AddReference(const char *name, void *object, RefCounterInsertFn insert) {
    RefCounterNode *node = Find(name);
    if (node == head) {
        char key[0x80];
        CopyName(key, name);
        RefCounterEntry entry;
        entry.references = 0;
        entry.object = NULL;   // the original leaves it uninitialised; the node's is set just below
        RefCounterValue value;
        RefCounterValue_Construct(&value, 0, key, &entry);   // copies all 128 bytes of the key
        RefCounterInsertResult result;
        node = insert(this, 0, &result, &value)->node;
        node->value.entry.object = object;
    }
    node->value.entry.references++;
}

bool URefCounterMap::RemoveReference(void *object, RefCounterEraseFn erase) {
    for (RefCounterIterator it = {head->left}; it.node != head; it.Increment()) {
        if (it.node->value.entry.object == object && --it.node->value.entry.references == 0) {
            RefCounterNode *after;
            erase(this, 0, &after, it.node);
            return true;
        }
    }
    return false;
}

void URefCounterMap::Destruct(RefCounterEraseTreeFn eraseTree, RefCounterEraseRangeFn eraseRange) {
    eraseTree(this, 0, head->parent);
    head->parent = head;
    size = 0;
    head->left = head;
    head->right = head;
    RefCounterNode *after;
    eraseRange(this, 0, &after, head->left, head);   // nothing left to erase
    if (head != NULL)
        UMemory::FastFree(head, sizeof(RefCounterNode));
    head = NULL;
    size = 0;
}

void URefCounterMap::ConstructOnce(uint32_t *guard, RefCounterDestroyFn destroy) {
    if ((*guard & 1) != 0)
        return;
    *guard |= 1;
    allocator = 0;   // the original copies an uninitialised byte of its stack: the empty comparator
    head = RefCounterMapBuyHead();
    head->isNil = 1;
    head->parent = head;
    head->left = head;
    head->right = head;
    size = 0;
    CRT_atexit(destroy);
}

// ---- ActModelDatabase::ModelInfo

// The instantiation's tree code (anim/Model.h), in the shapes URefCounter's helpers take; likewise for TextureInfo
// and WeaponInfo below.
static RefCounterInsertResult *__fastcall ModelInfoMap_Insert(URefCounterMap *map, int, RefCounterInsertResult *result,
                                                              const RefCounterValue *value) {
    return static_cast<ModelInfoRefTree *>(map)->InsertUnique(result, value);
}

static RefCounterNode **__fastcall ModelInfoMap_Erase(URefCounterMap *map, int, RefCounterNode **result,
                                                      RefCounterNode *where) {
    return static_cast<ModelInfoRefTree *>(map)->EraseAt(result, where);
}

static void __fastcall ModelInfoMap_EraseTree(URefCounterMap *map, int, RefCounterNode *subtree) {
    static_cast<ModelInfoRefTree *>(map)->EraseSubtree(subtree);
}

static RefCounterNode **__fastcall ModelInfoMap_EraseRange(URefCounterMap *map, int, RefCounterNode **result,
                                                           RefCounterNode *first, RefCounterNode *last) {
    return static_cast<ModelInfoRefTree *>(map)->EraseRange(result, first, last);
}

// FUNC_AT(0x00017ee0)
bool ModelInfoRefCounter::RemoveReference(ActModelInfo *info) {
    return URefCounter::RemoveReference(info, ModelInfoMap_Erase);
}

// FUNC_AT(0x00018030)
void ModelInfoRefCounter::AddReference(const char *name, ActModelInfo *info) {
    URefCounter::AddReference(name, info, ModelInfoMap_Insert);
}

// FUNC_AT(0x000182b0)
void ModelInfoRefCounter::Destruct() {
    URefCounterMap::Destruct(ModelInfoMap_EraseTree, ModelInfoMap_EraseRange);
}

// FUNC_AT(0x00018340)
ModelInfoRefCounter* ModelInfoRefCounter::Get() {
    ModelInfoRefs.ConstructOnce(&ModelInfoRefsGuard, kDestroyModelInfoRefs);
    return &ModelInfoRefs;
}

// ---- ActTextureDatabase::TextureInfo

static RefCounterInsertResult *__fastcall TextureInfoMap_Insert(URefCounterMap *map, int, RefCounterInsertResult *result,
                                                                const RefCounterValue *value) {
    return static_cast<TextureInfoRefTree *>(map)->InsertUnique(result, value);
}

static RefCounterNode **__fastcall TextureInfoMap_Erase(URefCounterMap *map, int, RefCounterNode **result,
                                                        RefCounterNode *where) {
    return static_cast<TextureInfoRefTree *>(map)->EraseAt(result, where);
}

static void __fastcall TextureInfoMap_EraseTree(URefCounterMap *map, int, RefCounterNode *subtree) {
    static_cast<TextureInfoRefTree *>(map)->EraseSubtree(subtree);
}

static RefCounterNode **__fastcall TextureInfoMap_EraseRange(URefCounterMap *map, int, RefCounterNode **result,
                                                             RefCounterNode *first, RefCounterNode *last) {
    return static_cast<TextureInfoRefTree *>(map)->EraseRange(result, first, last);
}

// FUNC_AT(0x0001a490)
bool TextureInfoRefCounter::RemoveReference(ActTextureInfo *info) {
    return URefCounter::RemoveReference(info, TextureInfoMap_Erase);
}

// FUNC_AT(0x0001a5e0)
void TextureInfoRefCounter::AddReference(const char *name, ActTextureInfo *info) {
    URefCounter::AddReference(name, info, TextureInfoMap_Insert);
}

// FUNC_AT(0x0001a880)
void TextureInfoRefCounter::Destruct() {
    URefCounterMap::Destruct(TextureInfoMap_EraseTree, TextureInfoMap_EraseRange);
}

// FUNC_AT(0x0001a910)
TextureInfoRefCounter* TextureInfoRefCounter::Get() {
    TextureInfoRefs.ConstructOnce(&TextureInfoRefsGuard, kDestroyTextureInfoRefs);
    return &TextureInfoRefs;
}

// ---- ActWeaponDatabase::WeaponInfo

static RefCounterInsertResult *__fastcall WeaponInfoMap_Insert(URefCounterMap *map, int, RefCounterInsertResult *result,
                                                               const RefCounterValue *value) {
    return static_cast<WeaponInfoRefTree *>(map)->InsertUnique(result, value);
}

static RefCounterNode **__fastcall WeaponInfoMap_Erase(URefCounterMap *map, int, RefCounterNode **result,
                                                       RefCounterNode *where) {
    return static_cast<WeaponInfoRefTree *>(map)->EraseAt(result, where);
}

static void __fastcall WeaponInfoMap_EraseTree(URefCounterMap *map, int, RefCounterNode *subtree) {
    static_cast<WeaponInfoRefTree *>(map)->EraseSubtree(subtree);
}

static RefCounterNode **__fastcall WeaponInfoMap_EraseRange(URefCounterMap *map, int, RefCounterNode **result,
                                                            RefCounterNode *first, RefCounterNode *last) {
    return static_cast<WeaponInfoRefTree *>(map)->EraseRange(result, first, last);
}

// FUNC_AT(0x0001ba80)
bool WeaponInfoRefCounter::RemoveReference(ActWeaponInfo *info) {
    return URefCounter::RemoveReference(info, WeaponInfoMap_Erase);
}

// FUNC_AT(0x0001bbd0)
void WeaponInfoRefCounter::AddReference(const char *name, ActWeaponInfo *info) {
    URefCounter::AddReference(name, info, WeaponInfoMap_Insert);
}

// FUNC_AT(0x0001be60)
void WeaponInfoRefCounter::Destruct() {
    URefCounterMap::Destruct(WeaponInfoMap_EraseTree, WeaponInfoMap_EraseRange);
}

// FUNC_AT(0x0001bef0)
WeaponInfoRefCounter* WeaponInfoRefCounter::Get() {
    WeaponInfoRefs.ConstructOnce(&WeaponInfoRefsGuard, kDestroyWeaponInfoRefs);
    return &WeaponInfoRefs;
}

// ---- RCARPFile

// The instantiations' tree code (render/RSceneObj.hpp, render/TextureContext.h), in the shapes URefCounter's
// helpers take
static RefCounterNode **__fastcall CarpFileMap_Erase(URefCounterMap *map, int, RefCounterNode **result,
                                                    RefCounterNode *where) {
    return static_cast<CarpFileRefTree *>(map)->EraseAt(result, where);
}

static RefCounterInsertResult *__fastcall CarpFileMap_Insert(URefCounterMap *map, int, RefCounterInsertResult *result,
                                                            const RefCounterValue *value) {
    return static_cast<CarpFileRefTree *>(map)->InsertUnique(result, value);
}

static RefCounterNode **__fastcall TextureContextMap_Erase(URefCounterMap *map, int, RefCounterNode **result,
                                                          RefCounterNode *where) {
    return static_cast<TextureContextRefTree *>(map)->EraseAt(result, where);
}

static RefCounterInsertResult *__fastcall TextureContextMap_Insert(URefCounterMap *map, int,
                                                                  RefCounterInsertResult *result,
                                                                  const RefCounterValue *value) {
    return static_cast<TextureContextRefTree *>(map)->InsertUnique(result, value);
}

// FUNC_AT(0x00090130)
bool CarpFileRefCounter::RemoveReference(RCARPFile *file) {
    return URefCounter::RemoveReference(file, CarpFileMap_Erase);
}

// FUNC_AT(0x00090280)
void CarpFileRefCounter::AddReference(const char *name, RCARPFile *file) {
    URefCounter::AddReference(name, file, CarpFileMap_Insert);
}

// FUNC_AT(0x00090430)
CarpFileRefCounter* CarpFileRefCounter::Get() {
    CarpFileRefs.ConstructOnce(&CarpFileRefsGuard, kDestroyCarpFileRefs);
    return &CarpFileRefs;
}

// ---- RTextureContext

// FUNC_AT(0x00094d80)
bool TextureContextRefCounter::RemoveReference(RTextureContext *context) {
    return URefCounter::RemoveReference(context, TextureContextMap_Erase);
}

// FUNC_AT(0x00094ed0)
void TextureContextRefCounter::AddReference(const char *name, RTextureContext *context) {
    URefCounter::AddReference(name, context, TextureContextMap_Insert);
}

// FUNC_AT(0x00095150)
TextureContextRefCounter* TextureContextRefCounter::Get() {
    TextureContextRefs.ConstructOnce(&TextureContextRefsGuard, kDestroyTextureContextRefs);
    return &TextureContextRefs;
}

// ---- AMix

// The instantiation's tree code (audio/Mix.h), in the shape URefCounter's helpers take; likewise for AStream,
// AFader and ABank below.
static RefCounterNode **__fastcall MixMap_Erase(URefCounterMap *map, int, RefCounterNode **result,
                                                RefCounterNode *where) {
    return static_cast<MixRefTree *>(map)->EraseAt(result, where);
}

static RefCounterInsertResult *__fastcall MixMap_Insert(URefCounterMap *map, int, RefCounterInsertResult *result,
                                                        const RefCounterValue *value) {
    return static_cast<MixRefTree *>(map)->InsertUnique(result, value);
}

// FUNC_AT(0x0011d310)
bool MixRefCounter::RemoveReference(AMix *mix) {
    return URefCounter::RemoveReference(mix, MixMap_Erase);
}

// FUNC_AT(0x0011d460)
void MixRefCounter::AddReference(const char *name, AMix *mix) {
    URefCounter::AddReference(name, mix, MixMap_Insert);
}

// FUNC_AT(0x0011d610)
MixRefCounter* MixRefCounter::Get() {
    MixRefs.ConstructOnce(&MixRefsGuard, kDestroyMixRefs);
    return &MixRefs;
}

// ---- AStream

static RefCounterNode **__fastcall StreamMap_Erase(URefCounterMap *map, int, RefCounterNode **result,
                                                   RefCounterNode *where) {
    return static_cast<StreamRefTree *>(map)->EraseAt(result, where);
}

static RefCounterInsertResult *__fastcall StreamMap_Insert(URefCounterMap *map, int, RefCounterInsertResult *result,
                                                           const RefCounterValue *value) {
    return static_cast<StreamRefTree *>(map)->InsertUnique(result, value);
}

// FUNC_AT(0x00123230)
bool StreamRefCounter::RemoveReference(AStream *stream) {
    return URefCounter::RemoveReference(stream, StreamMap_Erase);
}

// FUNC_AT(0x001233f0)
void StreamRefCounter::AddReference(const char *name, AStream *stream) {
    URefCounter::AddReference(name, stream, StreamMap_Insert);
}

// FUNC_AT(0x00123630)
StreamRefCounter* StreamRefCounter::Get() {
    StreamRefs.ConstructOnce(&StreamRefsGuard, kDestroyStreamRefs);
    return &StreamRefs;
}

// ---- AFader

static RefCounterNode **__fastcall FaderMap_Erase(URefCounterMap *map, int, RefCounterNode **result,
                                                  RefCounterNode *where) {
    return static_cast<FaderRefTree *>(map)->EraseAt(result, where);
}

static RefCounterInsertResult *__fastcall FaderMap_Insert(URefCounterMap *map, int, RefCounterInsertResult *result,
                                                          const RefCounterValue *value) {
    return static_cast<FaderRefTree *>(map)->InsertUnique(result, value);
}

// FUNC_AT(0x00125920)
bool FaderRefCounter::RemoveReference(AFader *fader) {
    return URefCounter::RemoveReference(fader, FaderMap_Erase);
}

// FUNC_AT(0x00125a70)
void FaderRefCounter::AddReference(const char *name, AFader *fader) {
    URefCounter::AddReference(name, fader, FaderMap_Insert);
}

// FUNC_AT(0x00125c20)
FaderRefCounter* FaderRefCounter::Get() {
    FaderRefs.ConstructOnce(&FaderRefsGuard, kDestroyFaderRefs);
    return &FaderRefs;
}

// ---- ABank

// The tree's _Erase: frees a subtree, right branches by recursion, left ones by iteration.
// FUNC_AT(0x00125f50)
void BankRefCounter::EraseSubtree(RefCounterNode *node) {
    while (!node->isNil) {
        EraseSubtree(node->right);
        RefCounterNode *left = node->left;
        if (node != NULL)
            UMemory::FastFree(node, sizeof(RefCounterNode));
        node = left;
    }
}

static void __fastcall BankMap_EraseTree(URefCounterMap *map, int, RefCounterNode *subtree) {
    static_cast<BankRefCounter *>(map)->EraseSubtree(subtree);
}

static RefCounterNode **__fastcall BankMap_Erase(URefCounterMap *map, int, RefCounterNode **result,
                                                 RefCounterNode *where) {
    return static_cast<BankRefTree *>(map)->EraseAt(result, where);
}

static RefCounterInsertResult *__fastcall BankMap_Insert(URefCounterMap *map, int, RefCounterInsertResult *result,
                                                         const RefCounterValue *value) {
    return static_cast<BankRefTree *>(map)->InsertUnique(result, value);
}

static RefCounterNode **__fastcall BankMap_EraseRange(URefCounterMap *map, int, RefCounterNode **result,
                                                      RefCounterNode *first, RefCounterNode *last) {
    return static_cast<BankRefTree *>(map)->EraseRange(result, first, last);
}

// FUNC_AT(0x00126580)
bool BankRefCounter::RemoveReference(ABank *bank) {
    return URefCounter::RemoveReference(bank, BankMap_Erase);
}

// FUNC_AT(0x001266d0)
void BankRefCounter::AddReference(const char *name, ABank *bank) {
    URefCounter::AddReference(name, bank, BankMap_Insert);
}

// FUNC_AT(0x001267f0)
void BankRefCounter::Destruct() {
    URefCounterMap::Destruct(BankMap_EraseTree, BankMap_EraseRange);
}

// FUNC_AT(0x00126880)
BankRefCounter* BankRefCounter::Get() {
    BankRefs.ConstructOnce(&BankRefsGuard, kDestroyBankRefs);
    return &BankRefs;
}

// ---- AEngine

// FUNC_AT(0x0012f5b0)
bool EngineRefCounter::RemoveReference(AEngine *engine) {
    return URefCounter::RemoveReference(engine, EngineMap_Erase);
}

// FUNC_AT(0x0012f700)
void EngineRefCounter::AddReference(const char *name, AEngine *engine) {
    URefCounter::AddReference(name, engine, EngineMap_Insert);
}

// FUNC_AT(0x0012f8b0)
EngineRefCounter* EngineRefCounter::Get() {
    EngineRefs.ConstructOnce(&EngineRefsGuard, kDestroyEngineRefs);
    return &EngineRefs;
}

// ---------------------------------------------------------------------------------------------------------------
// The trees' code (RefCounterTree): the helpers every URefCounter's tree calls, and the bodies of each
// instantiation's compiled copies
// ---------------------------------------------------------------------------------------------------------------

// FUNC_AT(0x00019d20)
void RefCounterTree::Rrotate(RefCounterNode *where) {
    RefCounterNode *node = where->left;
    where->left = node->right;
    if (!node->right->isNil)
        node->right->parent = where;
    node->parent = where->parent;
    if (where == head->parent)
        head->parent = node;
    else if (where == where->parent->right)
        where->parent->right = node;
    else
        where->parent->left = node;
    node->right = where;
    where->parent = node;
}

// FUNC_AT(0x00019de0)
RefCounterNode* URefCounterMap::LowerBound(const char *key) {
    RefCounterNode *bound = head;
    RefCounterNode *node = head->parent;
    while (!node->isNil) {
        if (CRT_stricmp(node->value.name, key) < 0) {
            node = node->right;
        } else {
            bound = node;
            node = node->left;
        }
    }
    return bound;
}

// FUNC_AT(0x00019d80)
void RefCounterIterator::Increment() {
    if (node->isNil)
        return;
    if (!node->right->isNil) {
        RefCounterNode *leftmost = node->right;
        while (!leftmost->left->isNil)
            leftmost = leftmost->left;
        node = leftmost;
        return;
    }
    RefCounterNode *parent = node->parent;
    while (!parent->isNil && node == parent->right) {
        node = parent;
        parent = parent->parent;
    }
    node = parent;
}

// FUNC_AT(0x00093f60)
RefCounterNode* RefCounterTree::BuyNode(RefCounterNode *left, RefCounterNode *parent, RefCounterNode *right,
                                        const RefCounterValue *value, uint8_t color) {
    RefCounterNode *node = static_cast<RefCounterNode *>(UMemory::FastAlloc(sizeof(RefCounterNode), "STL"));
    if (node != NULL) {
        node->left = left;
        node->parent = parent;
        node->right = right;
        node->value = *value;
        node->color = color;
        node->isNil = 0;
    }
    return node;
}

// FUNC_AT(0x0008eea0)
RefCounterNode* RefCounterTree::Min(RefCounterNode *node) {
    while (!node->left->isNil)
        node = node->left;
    return node;
}

// FUNC_AT(0x00017560)
RefCounterNode* RefCounterTree::Max(RefCounterNode *node) {
    while (!node->right->isNil)
        node = node->right;
    return node;
}

// FUNC_AT(0x0011caa0)
void RefCounterIterator::Decrement() {
    if (node->isNil) {
        node = node->right;
        return;
    }
    if (!node->left->isNil) {
        RefCounterNode *rightmost = node->left;
        while (!rightmost->right->isNil)
            rightmost = rightmost->right;
        node = rightmost;
        return;
    }
    RefCounterNode *parent = node->parent;
    while (!parent->isNil && node == parent->left) {
        node = parent;
        parent = parent->parent;
    }
    if (!parent->isNil)
        node = parent;
}

// FUNC_AT(0x0011cb20)
void RefCounterTree::Lrotate(RefCounterNode *where) {
    RefCounterNode *node = where->right;
    where->right = node->left;
    if (!node->left->isNil)
        node->left->parent = where;
    node->parent = where->parent;
    if (where == head->parent)
        head->parent = node;
    else if (where == where->parent->left)
        where->parent->left = node;
    else
        where->parent->right = node;
    node->left = where;
    where->parent = node;
}

void RefCounterTree::EraseSubtree(RefCounterNode *node) {
    while (!node->isNil) {
        EraseSubtree(node->right);
        RefCounterNode *left = node->left;
        if (node != NULL)
            UMemory::FastFree(node, sizeof(RefCounterNode));
        node = left;
    }
}

RefCounterNode **RefCounterTree::EraseAt(RefCounterNode **result, RefCounterNode *where) {
    if (where->isNil)
        TreeThrow("invalid map/set<T> iterator", kOutOfRangeVtable, kOutOfRangeThrowInfo);
    RefCounterNode *erased = where;
    RefCounterIterator next = { where };
    next.Increment();
    where = next.node;                      // where is the next node from here on

    RefCounterNode *node = erased;          // the node that leaves its place: the erased one or its successor
    RefCounterNode *fix;                    // the subtree that takes that node's place
    if (node->left->isNil) {
        fix = node->right;
    } else if (node->right->isNil) {
        fix = node->left;
    } else {
        node = where;
        fix = node->right;
    }
    RefCounterNode *fixParent;
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
                RefCounterNode *sibling = fixParent->right;
                if (sibling->color == kTreeRed) {
                    sibling->color = kTreeBlack;
                    fixParent->color = kTreeRed;
                    Lrotate(fixParent);
                    sibling = fixParent->right;
                }
                if (!sibling->isNil) {
                    if (sibling->left->color == kTreeBlack && sibling->right->color == kTreeBlack) {
                        sibling->color = kTreeRed;
                    } else {
                        if (sibling->right->color == kTreeBlack) {
                            sibling->left->color = kTreeBlack;
                            sibling->color = kTreeRed;
                            Rrotate(sibling);
                            sibling = fixParent->right;
                        }
                        sibling->color = fixParent->color;
                        fixParent->color = kTreeBlack;
                        sibling->right->color = kTreeBlack;
                        Lrotate(fixParent);
                        break;
                    }
                }
            } else {
                RefCounterNode *sibling = fixParent->left;
                if (sibling->color == kTreeRed) {
                    sibling->color = kTreeBlack;
                    fixParent->color = kTreeRed;
                    Rrotate(fixParent);
                    sibling = fixParent->left;
                }
                if (!sibling->isNil) {
                    if (sibling->right->color == kTreeBlack && sibling->left->color == kTreeBlack) {
                        sibling->color = kTreeRed;
                    } else {
                        if (sibling->left->color == kTreeBlack) {
                            sibling->right->color = kTreeBlack;
                            sibling->color = kTreeRed;
                            Lrotate(sibling);
                            sibling = fixParent->left;
                        }
                        sibling->color = fixParent->color;
                        fixParent->color = kTreeBlack;
                        sibling->left->color = kTreeBlack;
                        Rrotate(fixParent);
                        break;
                    }
                }
            }
            fix = fixParent;
            fixParent = fix->parent;
        }
        fix->color = kTreeBlack;
    }

    UMemory::FastFree(erased, sizeof(RefCounterNode));
    if (size > 0)
        size--;
    *result = where;
    return result;
}

RefCounterNode **RefCounterTree::InsertAt(RefCounterNode **result, bool addLeft, RefCounterNode *where,
                                          const RefCounterValue *value) {
    if (size >= 0xffffffffu / sizeof(RefCounterValue) - 1)
        TreeThrow("map/set<T> too long", kLengthErrorVtable, kLengthErrorThrowInfo);
    RefCounterNode *node = BuyNode(head, where, head, value, kTreeRed);
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
    for (RefCounterNode *at = node; at->parent->color == kTreeRed;) {
        if (at->parent == at->parent->parent->left) {
            RefCounterNode *uncle = at->parent->parent->right;
            if (uncle->color == kTreeRed) {
                at->parent->color = kTreeBlack;
                uncle->color = kTreeBlack;
                at->parent->parent->color = kTreeRed;
                at = at->parent->parent;
            } else {
                if (at == at->parent->right) {
                    at = at->parent;
                    Lrotate(at);
                }
                at->parent->color = kTreeBlack;
                at->parent->parent->color = kTreeRed;
                Rrotate(at->parent->parent);
            }
        } else {
            RefCounterNode *uncle = at->parent->parent->left;
            if (uncle->color == kTreeRed) {
                at->parent->color = kTreeBlack;
                uncle->color = kTreeBlack;
                at->parent->parent->color = kTreeRed;
                at = at->parent->parent;
            } else {
                if (at == at->parent->left) {
                    at = at->parent;
                    Rrotate(at);
                }
                at->parent->color = kTreeBlack;
                at->parent->parent->color = kTreeRed;
                Lrotate(at->parent->parent);
            }
        }
    }
    head->parent->color = kTreeBlack;
    *result = node;
    return result;
}

RefCounterNode **RefCounterTree::EraseRange(RefCounterNode **result, RefCounterNode *first, RefCounterNode *last) {
    if (first == head->left && last == head) {
        EraseSubtree(head->parent);
        head->parent = head;
        size = 0;
        head->left = head;
        head->right = head;
        *result = head->left;
        return result;
    }
    RefCounterIterator it = { first };
    while (it.node != last) {
        RefCounterNode *erased = it.node;
        it.Increment();
        RefCounterNode *next;
        EraseAt(&next, erased);
    }
    *result = it.node;
    return result;
}

RefCounterInsertResult *RefCounterTree::InsertUnique(RefCounterInsertResult *result, const RefCounterValue *value) {
    RefCounterNode *tryNode = head->parent;
    RefCounterNode *where = head;
    bool addLeft = true;
    while (!tryNode->isNil) {
        where = tryNode;
        addLeft = CRT_stricmp(value->name, tryNode->value.name) < 0;
        tryNode = addLeft ? tryNode->left : tryNode->right;
    }
    RefCounterIterator before = { where };
    if (addLeft) {
        if (where == head->left) {
            RefCounterNode *node;
            result->node = *InsertAt(&node, true, where, value);
            result->inserted = true;
            return result;
        }
        before.Decrement();
    }
    if (CRT_stricmp(before.node->value.name, value->name) < 0) {
        RefCounterNode *node;
        result->node = *InsertAt(&node, addLeft, where, value);
        result->inserted = true;
        return result;
    }
    result->node = before.node;
    result->inserted = false;
    return result;
}

void RefCounterTree::Destroy() {
    EraseSubtree(head->parent);
    head->parent = head;
    size = 0;
    head->left = head;
    head->right = head;
    DestroyRange();
}

void RefCounterTree::DestroyRange() {
    RefCounterNode *after;
    EraseRange(&after, head->left, head);
    if (head != NULL)
        UMemory::FastFree(head, sizeof(RefCounterNode));
    head = NULL;
    size = 0;
}
