#include "SoundGroup.h"
#include "SoundMap.h"                       // BuyMapHead

#include "../../common/xbeOverload.h"
#include "../audio/Bank.h"                  // ABank
#include "../engine/CoreFoundation.h"      // NullFunction
#include "../engine/UMemory.hpp"

#include <stddef.h>
#include <stdint.h>

// ---------------------------------------------------------------------------------------------------------------
// WSoundGroup, its map, and the sounds' constructor and destructors (0x000cc8f0..0x000cd660), ported from the
// listings. See SoundGroup.h.
// ---------------------------------------------------------------------------------------------------------------

// The audio framework's, not ported.
#define AWorldSound_Destruct ((void (__fastcall *)(AWorldSound *, int))0x0012e090)
// AVoice::View's destructor, by the address the original hands the C runtime's `eh vector destructor iterator'
#define AVoiceView_Destruct ((void (__fastcall *)(AVoice::View *, int))0x00123c50)
#define EhVectorDestructor ((void (__stdcall *)(void *, uint32_t, int, void (__fastcall *)(AVoice::View *, int)))0x0013332e)

// The maps' shared helpers: --iterator (SharedTreeIterator::Dec, audio/Bank.h, on the data layer's node type).
#define SoundMap_Decrement ((void (__fastcall *)(SoundMapNode **it, int))0x00126bd0)

// The C runtime's.
#define Crt_sscanf ((int (*)(const char *text, const char *format, ...))0x00133234)

namespace {

constexpr uint32_t kAWorldSoundVtable = 0x00193ae8;
constexpr uint32_t kWSoundVtable = 0x00193ac4;

SoundMapNode *Next(SoundMapNode *node) {
    return reinterpret_cast<SoundMapNode *>(TreeNext(reinterpret_cast<TreeNode *>(node)));
}

TreeNode *AsTreeNode(SoundMapNode *node) {
    return reinterpret_cast<TreeNode *>(node);
}

TreeNode **AsTreeNodes(SoundMapNode **node) {
    return reinterpret_cast<TreeNode **>(node);
}

// The sound's deleting destructor, through its vtable.
void DeleteSound(WSound *sound) {
    typedef WSound *(WSound::*DeleteMethod)(unsigned flags);
    (sound->*XbeVirtual<DeleteMethod>(sound, 0))(1);
}

// new WSound(bank, index), inlined twice in Add.
WSound *NewSound(int bank, int index) {
    WSound *sound = static_cast<WSound *>(ABaseSound::OperatorNew(sizeof(WSound), "WSound"));
    return sound != NULL ? sound->Construct(bank, index) : NULL;
}

}  // namespace

// =============================================================================================================
// The sounds
// =============================================================================================================

// FUNC_AT(0x000cc930)
AWorldSound* AWorldSound::Construct(int bank, int index, const char *name) {
    ABaseSound::Construct(name, kSoundViewsActive);
    vtable = kAWorldSoundVtable;
    voice.Construct(mix, bank, index);
    inUse = 1;
    unknown138 = 1;
    unknown118 = 0.0f;
    unknown11c = 0.0f;
    unknown120 = 0;
    unknown124 = 0.0f;
    unknown128 = 0;
    unknown130 = 0;
    unknown134 = 0;
    extraVoice = NULL;
    unknown140 = 0;
    volume = 1.0f;
    pitch = 1.0f;
    return this;
}

// FUNC_AT(0x000cca00)
AWorldSound* AWorldSound::Delete(unsigned flags) {
    AWorldSound_Destruct(this, 0);
    if (flags & 1)
        OperatorDelete(this, sizeof(AWorldSound));
    return this;
}

WSound* WSound::Construct(int bank, int index) {
    AWorldSound::Construct(bank, index, "World Sounds");
    vtable = kWSoundVtable;
    pathHandle = NULL;
    return this;
}

// FUNC_AT(0x000cc8f0)
void WSound::Destruct() {
    vtable = kWSoundVtable;
    AWorldSound_Destruct(this, 0);
}

// FUNC_AT(0x000cc900)
WSound* WSound::Delete(unsigned flags) {
    Destruct();
    if (flags & 1)
        OperatorDelete(this, sizeof(WSound));
    return this;
}

// =============================================================================================================
// The map
// =============================================================================================================

// FUNC_AT(0x000cca30)
void WSoundMap::Lrotate(SoundMapNode *node) {
    SoundMapNode *pivot = node->right;
    node->right = pivot->left;
    if (!pivot->left->isNil)
        pivot->left->parent = node;
    pivot->parent = node->parent;
    if (node == head->parent)
        head->parent = pivot;
    else if (node == node->parent->left)
        node->parent->left = pivot;
    else
        node->parent->right = pivot;
    pivot->left = node;
    node->parent = pivot;
}

// FUNC_AT(0x000cca90)
void WSoundMap::EraseSubtree(SoundMapNode *node) {
    AsTree()->EraseSubtree(AsTreeNode(node));
}

// FUNC_AT(0x000ccbb0)
SoundMapNode** WSoundMap::Find(SoundMapNode **result, const int32_t *id) {
    SoundMapNode *bound = head;
    for (SoundMapNode *node = head->parent; !node->isNil;) {
        if (node->value.id < *id) {
            node = node->right;
        } else {
            bound = node;
            node = node->left;
        }
    }
    *result = bound == head || *id < bound->value.id ? head : bound;
    return result;
}

// FUNC_AT(0x000ccc20)
SoundMapNode** WSoundMap::EraseAt(SoundMapNode **result, SoundMapNode *where) {
    AsTree()->EraseAt(AsTreeNodes(result), AsTreeNode(where));
    return result;
}

// FUNC_AT(0x000ccef0)
SoundMapNode** WSoundMap::InsertAt(SoundMapNode **result, bool addLeft, SoundMapNode *where,
                                   const SoundMapValue *value) {
    AsTree()->InsertAt(AsTreeNodes(result), addLeft, AsTreeNode(where), reinterpret_cast<const TreePair *>(value));
    return result;
}

// FUNC_AT(0x000cd0d0)
SoundMapNode** WSoundMap::EraseRange(SoundMapNode **result, SoundMapNode *first, SoundMapNode *last) {
    AsTree()->EraseRange(AsTreeNodes(result), AsTreeNode(first), AsTreeNode(last));
    return result;
}

// FUNC_AT(0x000cd2b0)
SoundMapInsert* WSoundMap::InsertUnique(SoundMapInsert *result, const SoundMapValue *value) {
    SoundMapNode *where = head;
    bool addLeft = true;
    for (SoundMapNode *node = head->parent; !node->isNil; node = addLeft ? node->left : node->right) {
        where = node;
        addLeft = value->id < node->value.id;
    }
    SoundMapNode *at = where;
    if (addLeft) {
        if (where == head->left) {
            SoundMapNode *node;
            result->node = *InsertAt(&node, true, where, value);
            result->inserted = true;
            return result;
        }
        SoundMap_Decrement(&at, 0);
    }
    if (at->value.id < value->id) {
        SoundMapNode *node;
        result->node = *InsertAt(&node, addLeft, where, value);
        result->inserted = true;
        return result;
    }
    result->node = at;
    result->inserted = false;
    return result;
}

// FUNC_AT(0x000cd560)
void WSoundMap::Destruct() {
    AsTree()->Destroy();
}

// =============================================================================================================
// The group
// =============================================================================================================

// FUNC_AT(0x000cd5a0)
WSoundGroup* WSoundGroup::Construct() {
    WSoundMap *map = static_cast<WSoundMap *>(UMemory::FastAlloc(sizeof(WSoundMap), "WSoundMap"));
    if (map != NULL) {
        map->allocator = 0;     // (the original copies an uninitialised stack byte)
        map->head = BuyMapHead<SoundMapNode>();
        map->head->isNil = 1;
        map->head->parent = map->head;
        map->head->left = map->head;
        map->head->right = map->head;
        map->size = 0;
    }
    sounds = map;
    unknown00 = 0;
    unknown04 = 0;
    unknown08 = 0;
    return this;
}

// FUNC_AT(0x000cd630)
void WSoundGroup::Destruct() {
    Clear();
    WSoundMap *map = sounds;
    if (map != NULL) {
        map->Destruct();
        UMemory::FastFree(map, sizeof(WSoundMap));
    }
}

// FUNC_AT(0x000ccad0)
void WSoundGroup::Start() {
    for (SoundMapNode *node = sounds->head->left; node != sounds->head; node = Next(node))
        node->value.sound->inUse = 0;
}

// FUNC_AT(0x000ccb40)
void WSoundGroup::Update() {
    for (SoundMapNode *node = sounds->head->left; node != sounds->head; node = Next(node))
        NullFunction();     // each sound's update, an empty function in this build
}

// FUNC_AT(0x000cd190)
void WSoundGroup::End() {
    for (SoundMapNode *node = sounds->head->left; node != sounds->head;) {
        SoundMapNode *where = node;
        node = Next(node);
        if (!where->value.sound->inUse) {
            DeleteSound(where->value.sound);
            SoundMapNode *next;
            sounds->EraseAt(&next, where);
        }
    }
}

// FUNC_AT(0x000cd220)
void WSoundGroup::Clear() {
    for (SoundMapNode *node = sounds->head->left; node != sounds->head;) {
        SoundMapNode *where = node;
        node = Next(node);
        if (where->value.sound != NULL)
            DeleteSound(where->value.sound);
        SoundMapNode *next;
        sounds->EraseAt(&next, where);
    }
}

// FUNC_AT(0x000cd370)
WSound* WSoundGroup::Add(int id, int bank, int index) {
    WSoundMap *map = sounds;
    SoundMapNode *found;
    map->Find(&found, &id);
    if (found == map->head) {
        WSound *sound = NewSound(bank, index);
        SoundMapValue value = {id, sound};
        SoundMapInsert inserted;
        sounds->InsertUnique(&inserted, &value);
        sound->inUse = 1;
        return sound;
    }
    if (index != found->value.sound->voice.views[0].patch) {
        DeleteSound(found->value.sound);
        found->value.sound = NewSound(bank, index);
    }
    found->value.sound->inUse = 1;
    return found->value.sound;
}

// FUNC_AT(0x000cd4c0)
WSound* WSoundGroup::Add(int id, const char *name) {
    char bankName[0x20] = {};
    char soundName[0x40] = {};
    Crt_sscanf(name, "%[^:]: %s", bankName, soundName);
    ABank *bank = ABank::Get(bankName);
    int handle = bank->handle;
    return Add(id, handle, bank->index.Lookup(soundName));
}

// FUNC_AT(0x000cd540)
WSound* WSoundGroup::AddPacked(int id, int packed) {
    return Add(id, packed >> 7, packed & 0x7f);
}

// ---- the two methods WWorld::Open calls

// FUNC_AT(0x000d11a0)
void WSound::SetUnknown118(float first, float second) {
    double sum = double(first) + second;
    if (sum != 0.0) {
        for (int i = 0; i < 3; i++)
            voice.views[i].loop = 0;
    }
    if (sum < double(unknown11c) + unknown118)
        unknown130 = 0;
    unknown118 = first;
    unknown11c = second;
}

// FUNC_AT(0x000d1570)
void WSound::SetVoice(int id) {
    if (extraVoice != NULL) {
        // AVoice's destructor
        EhVectorDestructor(extraVoice->views, sizeof(AVoice::View), 3, AVoiceView_Destruct);
        UMemory::FastFree(extraVoice, sizeof(AVoice));
    }
    void *memory = UMemory::FastAlloc(sizeof(AVoice), "AVoice");
    extraVoice = memory != NULL ? static_cast<AVoice *>(memory)->Construct(mix, id >> 7, id & 0x7f) : NULL;
}
