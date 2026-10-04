#include "URefCounter.h"

#include "UMemory.hpp"
#include "../../helpers.h"

// ---------------------------------------------------------------------------------------------------------------
// URefCounter<T>, ported from the listings of its 29 compiled copies (see URefCounter.h). Every AddReference copy
// is the same code (0x00018030 is the model), as is every RemoveReference (0x00090130), destructor (0x000182b0)
// and Get (0x00018340); they differ only in the tree helpers they call and, for Get, the static they make.
//
// The tree's helpers (lower_bound, the iterator's ++, the value's constructor, the head node's allocation, and each
// instantiation's insert and erase) are not ours yet and are called by address. The name comparisons are the C
// runtime's _stricmp, called at its own address too: the tree's order depends on them.
// ---------------------------------------------------------------------------------------------------------------

// ---- originals called by address

#define RefCounterMap_LowerBound ((RefCounterNode *(__fastcall *)(URefCounterMap *, int, const char *key))0x00019de0)
#define RefCounterNode_Increment ((void (__fastcall *)(RefCounterNode **iterator, int))0x00019d80)
#define RefCounterValue_Construct ((RefCounterValue *(__fastcall *)(RefCounterValue *, int, const char *name, const RefCounterEntry *entry))0x0012ef50)
#define RefCounterMap_BuyHeadNode ((RefCounterNode *(__fastcall *)(URefCounterMap *, int))0x00094070)
#define CRT_stricmp ((int (*)(const char *, const char *))0x00134537)
#define CRT_atexit ((int (*)(void (*)(void)))0x00132a7b)

// Each instantiation's own insert_unique, erase(where), _Erase(subtree) and erase(first, last)
#define ModelInfoMap_Insert ((RefCounterInsertResult *(__fastcall *)(URefCounterMap *, int, RefCounterInsertResult *, const RefCounterValue *))0x00017f50)
#define ModelInfoMap_EraseTree ((void (__fastcall *)(URefCounterMap *, int, RefCounterNode *))0x00017860)
#define ModelInfoMap_EraseRange ((RefCounterNode **(__fastcall *)(URefCounterMap *, int, RefCounterNode **, RefCounterNode *, RefCounterNode *))0x00017e60)
#define TextureInfoMap_Insert ((RefCounterInsertResult *(__fastcall *)(URefCounterMap *, int, RefCounterInsertResult *, const RefCounterValue *))0x0001a500)
#define TextureInfoMap_EraseTree ((void (__fastcall *)(URefCounterMap *, int, RefCounterNode *))0x00019e60)
#define TextureInfoMap_EraseRange ((RefCounterNode **(__fastcall *)(URefCounterMap *, int, RefCounterNode **, RefCounterNode *, RefCounterNode *))0x0001a410)
#define WeaponInfoMap_Insert ((RefCounterInsertResult *(__fastcall *)(URefCounterMap *, int, RefCounterInsertResult *, const RefCounterValue *))0x0001baf0)
#define CarpFileMap_Erase ((RefCounterNode **(__fastcall *)(URefCounterMap *, int, RefCounterNode **, RefCounterNode *))0x0008fb50)
#define CarpFileMap_Insert ((RefCounterInsertResult *(__fastcall *)(URefCounterMap *, int, RefCounterInsertResult *, const RefCounterValue *))0x000901a0)
#define TextureContextMap_Erase ((RefCounterNode **(__fastcall *)(URefCounterMap *, int, RefCounterNode **, RefCounterNode *))0x000946b0)
#define TextureContextMap_Insert ((RefCounterInsertResult *(__fastcall *)(URefCounterMap *, int, RefCounterInsertResult *, const RefCounterValue *))0x00094df0)
#define MixMap_Erase ((RefCounterNode **(__fastcall *)(URefCounterMap *, int, RefCounterNode **, RefCounterNode *))0x0011cd30)
#define MixMap_Insert ((RefCounterInsertResult *(__fastcall *)(URefCounterMap *, int, RefCounterInsertResult *, const RefCounterValue *))0x0011d380)
#define StreamMap_Erase ((RefCounterNode **(__fastcall *)(URefCounterMap *, int, RefCounterNode **, RefCounterNode *))0x00122b10)
#define StreamMap_Insert ((RefCounterInsertResult *(__fastcall *)(URefCounterMap *, int, RefCounterInsertResult *, const RefCounterValue *))0x001232a0)
#define FaderMap_Erase ((RefCounterNode **(__fastcall *)(URefCounterMap *, int, RefCounterNode **, RefCounterNode *))0x00125340)
#define FaderMap_Insert ((RefCounterInsertResult *(__fastcall *)(URefCounterMap *, int, RefCounterInsertResult *, const RefCounterValue *))0x00125990)
#define BankMap_Erase ((RefCounterNode **(__fastcall *)(URefCounterMap *, int, RefCounterNode **, RefCounterNode *))0x00125fa0)
#define BankMap_Insert ((RefCounterInsertResult *(__fastcall *)(URefCounterMap *, int, RefCounterInsertResult *, const RefCounterValue *))0x001265f0)
#define BankMap_EraseRange ((RefCounterNode **(__fastcall *)(URefCounterMap *, int, RefCounterNode **, RefCounterNode *, RefCounterNode *))0x00126500)
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
    RefCounterNode *node = RefCounterMap_LowerBound(this, 0, key);
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
    for (RefCounterNode *node = head->left; node != head; RefCounterNode_Increment(&node, 0)) {
        if (node->value.entry.object == object && --node->value.entry.references == 0) {
            RefCounterNode *after;
            erase(this, 0, &after, node);
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
    head = RefCounterMap_BuyHeadNode(this, 0);
    head->isNil = 1;
    head->parent = head;
    head->left = head;
    head->right = head;
    size = 0;
    CRT_atexit(destroy);
}

// ---- ActModelDatabase::ModelInfo

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

// FUNC_AT(0x0001bbd0)
void WeaponInfoRefCounter::AddReference(const char *name, ActWeaponInfo *info) {
    URefCounter::AddReference(name, info, WeaponInfoMap_Insert);
}

// FUNC_AT(0x0001bef0)
WeaponInfoRefCounter* WeaponInfoRefCounter::Get() {
    WeaponInfoRefs.ConstructOnce(&WeaponInfoRefsGuard, kDestroyWeaponInfoRefs);
    return &WeaponInfoRefs;
}

// ---- RCARPFile

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
