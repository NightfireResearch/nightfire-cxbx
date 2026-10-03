#include "AnimObjects.h"
#include "AnimChannels.h"
#include "AnimDecode.h"
#include "AnimDeltaF.h"
#include "AnimDeltaQ.h"
#include "AnimLocoBlend.h"
#include "AnimMisc.h"
#include "Skeleton.h"
#include "../EaglGlobals.h"
#include "../EaglOriginals.h"
#include "../Loader.h"
#include "../../platform/X87.h"
#include "../../../helpers.h"

#include <string.h>
#include <xmmintrin.h>

#ifdef _MSC_VER
#pragma float_control(precise, on)
#pragma fp_contract(off)
#else
#pragma STDC FP_CONTRACT OFF
#endif

// ---------------------------------------------------------------------------------------------------------------
// EAGLAnim's object layer (docs/driving/eagl.md 4.10, 9.2 step 5): the pool and the factory, every FnAnim's
// constructor, destructor and scalar deleting destructor, the trivial virtuals, the compound channel (sub-channels
// built on first use, time scaled by its FPS attribute), the cycle (time wrapped into [start, end]) and graft
// wrappers, the raw linear channel, AnimationBank, and EventTarget. Each function is the original at the same
// address; the objects keep the game's vtables, so virtual calls go through those (AnimVCall). The x87 arithmetic
// is done in double in the original's order, a float store where the original stores one, which at the game's
// 53-bit precision is the same (a single operation on floats is written in float: the same bits); float-to-int
// truncation is CVTTSS2SI, as there.
// devtools/AnimShadow.cpp builds every anim in every bank with both and compares what they evaluate.
// ---------------------------------------------------------------------------------------------------------------

namespace {


// A free block's first word links the next.
struct FreeBlock {
    FreeBlock *next;
};

// MemoryPoolManager's statics, at 0x002414b0. A size-classed block has its class in the dword before it.
struct MemoryPool {
    FreeBlock *freeByType[kAnimTypeCount];   // +0x000 free objects per anim type
    uint32_t unknown58;
    uint16_t unknown5c;              // +0x5c cleared by Init
    uint16_t unknown5e;
    uint8_t *base;                   // +0x60
    uint32_t size;                   // +0x64
    uint8_t *cursor;                 // +0x68 the next new block
    uint32_t unknown6c;
    FreeBlock *freeBySize[256];      // +0x70 free blocks per 16-byte size class
};
static_assert(offsetof(MemoryPool, base) == 0x60, "the pool's base is at 0x00241510");
static_assert(sizeof(MemoryPool) == 0x470, "the pool's free lists run to 0x00241920");

#define Pool (*(MemoryPool *)0x002414b0)
#define FactoryBlock PTR_AT(0x00241b80)

// The object size of each anim type (a table only the pool reads)
constexpr uint16_t kBlockSizes[kAnimTypeCount] = {
    0x14, 0x18, 0x14, 0x1c, 0x2c, 0x14, 0x80, 0x1c, 0x80, 0x5c, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x14, 0x30, 0x40,
    0x30, 0x30, 0x30,
};

// The names the allocations and the bank type are registered under: the original's strings, the pointers kept
#define PoolName ((const char *)0x001ceadc)                 // "EAGLAnim Memory Pool"
#define FactoryName ((const char *)0x001ceb30)              // "EAGLAnim::FnAnimFactory new"
#define BankTypeName ((const char *)0x001a0a04)             // "AnimationBank"

// The game's vtables, which the objects keep
#define VtFnAnim ((const void *)0x001a0c6c)
#define VtCompound ((const void *)0x001a0be8)
#define VtRawPose ((const void *)0x001a0ca8)
#define VtRawEvent ((const void *)0x001a0cf0)
#define VtRawLinear ((const void *)0x001a0d38)
#define VtKeyDelta ((const void *)0x001a0d80)
#define VtKeyLerp ((const void *)0x001a0dc8)
#define VtKeyQuat ((const void *)0x001a0e10)
#define VtDeltaChan ((const void *)0x001a0e58)
#define VtGraft ((const void *)0x001a0ea0)
#define VtPoseBlender ((const void *)0x001a0edc)
#define VtPoseMirror ((const void *)0x001a0f18)
#define VtCycle ((const void *)0x001a0f54)
#define VtEventBlender ((const void *)0x001a0f90)
#define VtPhase ((const void *)0x001a0fd0)
#define VtRawState ((const void *)0x001a1018)
#define VtDeltaLerp ((const void *)0x001a1060)
#define VtDeltaQuat ((const void *)0x001a10a8)
#define VtMemoryMap ((const void *)0x001a1130)


// The anim data behind each channel type
CompoundData *Compound(uint8_t *anim) {
    return reinterpret_cast<CompoundData *>(anim);
}

DeltaChanData *Delta(uint8_t *anim) {
    return reinterpret_cast<DeltaChanData *>(anim);
}

KeyChanData *Keyed(uint8_t *anim) {
    return reinterpret_cast<KeyChanData *>(anim);
}

}  // namespace

// ---- the pool

void AnimPool_FreeBlock(void *block) {
    uint32_t sizeClass = static_cast<uint32_t *>(block)[-1];
    FreeBlock *freed = static_cast<FreeBlock *>(block);
    freed->next = Pool.freeBySize[sizeClass];
    Pool.freeBySize[sizeClass] = freed;
}

void AnimPool_ReleaseFnAnim(FnAnim *anim) {
    AnimVCall<void *>(anim, kSlotDelete, 0u);
    uint16_t type = uint16_t(anim->type);
    FreeBlock *freed = reinterpret_cast<FreeBlock *>(anim);
    freed->next = Pool.freeByType[type];
    Pool.freeByType[type] = freed;
}

// FUNC_AT(0x000141d0)
uint16_t* AnimData::GetType(uint16_t *out) {
    *out = type;
    return out;
}

// FUNC_AT(0x000141e0)
void AnimPool_DeleteFnAnim(FnAnim *anim) {
    AnimPool_ReleaseFnAnim(anim);
}

// The factory's object, and the anim data attached through slot 15 (whatever the type's vtable has there).
// FUNC_AT(0x00014210)
FnAnim* AnimPool_NewFnAnim(uint8_t *data) {
    uint16_t type;
    reinterpret_cast<AnimData *>(data)->GetType(&type);
    FnAnim *anim = AnimPool_Construct(type);
    AnimVCall<void>(anim, kSlotSetAnimMemoryMap, data);
    return anim;
}

// FUNC_AT(0x000f7c40)
void* AnimPool_NewBlockByIdx(uint16_t type) {
    FreeBlock *block = Pool.freeByType[type];
    if (block == NULL) {
        FreeBlock *fresh = reinterpret_cast<FreeBlock *>(Pool.cursor);
        fresh->next = NULL;
        Pool.freeByType[type] = fresh;
        Pool.cursor += kBlockSizes[type];
        block = Pool.freeByType[type];
    }
    Pool.freeByType[type] = block->next;
    return block;
}

// A block of (size / 16) 16-byte units plus one, its class in the dword before it. The class is a byte: it wraps.
// FUNC_AT(0x000f7c90)
void* AnimPool_NewBlock(uint32_t size) {
    uint32_t sizeClass = uint8_t(size >> 4);
    FreeBlock *block = Pool.freeBySize[sizeClass];
    if (block != NULL) {
        Pool.freeBySize[sizeClass] = block->next;
        return block;
    }
    *reinterpret_cast<uint32_t *>(Pool.cursor) = sizeClass;
    block = reinterpret_cast<FreeBlock *>(Pool.cursor + 4);
    Pool.cursor += sizeClass * 16 + 0x14;
    return block;
}

// FUNC_AT(0x000f7cd0)
void AnimPool_ResetPool() {
    Pool.cursor = Pool.base;
    memset(Pool.freeBySize, 0, sizeof(Pool.freeBySize));
    memset(Pool.freeByType, 0, sizeof(Pool.freeByType));
}

// FUNC_AT(0x000f7d00)
void AnimPool_Init(uint32_t size) {
    Pool.size = size;
    Pool.base = static_cast<uint8_t *>(EaglMalloc(size, PoolName));
    Pool.cursor = Pool.base;
    memset(Pool.freeBySize, 0, sizeof(Pool.freeBySize));
    memset(Pool.freeByType, 0, sizeof(Pool.freeByType));
    Pool.unknown5c = 0;
}

// FUNC_AT(0x000f7d50)
void AnimPool_Cleanup() {
    EaglFree(Pool.base, Pool.size);
}

// The data's one-off preparation: a raw pose decides its decoders, a compound prepares its sub-anims.
// FUNC_AT(0x000f7d70)
void AnimPool_InitAnimMemoryMap(uint8_t *data) {
    uint16_t type;
    reinterpret_cast<AnimData *>(data)->GetType(&type);
    if (type == kRawPose)
        RawPoseChannel_InitAnimMemoryMap(data);
    else if (type == kCompound)
        CompoundChannel_InitAnimMemoryMap(data);
}

// FUNC_AT(0x000f7de0)
FnAnim* AnimPool_Construct(uint16_t type) {
    FnAnim *a = static_cast<FnAnim *>(AnimPool_NewBlockByIdx(type));
    switch (type) {
    case kRawPose: return static_cast<FnRawPoseChannel *>(a)->Construct();
    case kRawEvent: return static_cast<FnRawEventChannel *>(a)->Construct();
    case kRawLinear: return static_cast<FnRawLinearChannel *>(a)->Construct();
    case kCycle:
        a->stat = 0;
        a->vtable = VtCycle;
        a->type = kCycle;
        return a;
    case kEventBlender:
        a->stat = 0;
        a->vtable = VtEventBlender;
        a->type = kEventBlender;
        return a;
    case kGraft:
        a->stat = 0;
        a->vtable = VtGraft;
        a->type = kGraft;
        return a;
    case kPoseBlender: return static_cast<FnPoseBlender *>(a)->Construct();
    case kPoseMirror: return static_cast<FnPoseMirror *>(a)->Construct();
    case kRunBlender: return static_cast<FnRunBlender *>(a)->Construct();
    case kTurnBlender: return static_cast<FnTurnBlender *>(a)->Construct();
    case kDeltaLerp: return static_cast<FnDeltaChan *>(a)->ConstructLerp();
    case kDeltaQuat: return static_cast<FnDeltaChan *>(a)->ConstructQuat();
    case kKeyLerp: return static_cast<FnKeyDeltaChan *>(a)->ConstructLerp();
    case kKeyQuat: return static_cast<FnKeyDeltaChan *>(a)->ConstructQuat();
    case kPhase: return static_cast<FnPhaseChan *>(a)->Construct();
    case kCompound: return static_cast<FnCompoundChannel *>(a)->Construct();
    case kRawState: return static_cast<FnRawStateChan *>(a)->Construct();
    case kDeltaQ: return static_cast<FnDeltaQ *>(a)->Construct();
    case kDeltaQFast: return static_cast<FnDeltaQFast *>(a)->Construct();
    case kDeltaSingleQ: return static_cast<FnDeltaSingleQ *>(a)->Construct();
    case kDeltaF3: return static_cast<FnDeltaF *>(a)->ConstructF3();
    case kDeltaF1: return static_cast<FnDeltaF *>(a)->ConstructF1();
    }
    return a;
}

// ---- Initializer

// The bank type is registered with the original entry points of its constructor and destructor (which run ours).
// FUNC_AT(0x000fa9c0)
void EAGLAnim_InitInternal(uint32_t poolSize) {
    AnimPool_Init(poolSize);
    FactoryBlock = EaglMalloc(1, FactoryName);
    TheConstructorPool.AddType(BankTypeName, (void *)0x000f7170, (void *)0x000f71a0);
}

// FUNC_AT(0x000faa00)
void EAGLAnim_Nothing0() {
}

// FUNC_AT(0x000faa10)
void EAGLAnim_Nothing1() {
}

// FUNC_AT(0x000faa20)
void EAGLAnim_Nothing2() {
}

// FUNC_AT(0x000faa30)
uint32_t EAGLAnim_GetMemoryUsage() {
    return Pool.cursor - Pool.base;
}

// FUNC_AT(0x000faa40)
void EAGLAnim_ResetPool() {
    AnimPool_ResetPool();
}

// FUNC_AT(0x000faa50)
void EAGLAnim_ShutDown() {
    TheConstructorPool.RemoveType(BankTypeName);
    ScratchBuffer_FreeScratchBuffers();
    if (FactoryBlock != NULL)
        EaglFree(FactoryBlock, 1);
    AnimPool_Cleanup();
}

// ---- AnimationBank

// The loader's constructor for AnimationBank symbols: every anim's data prepared, last first.
// FUNC_AT(0x000f7170)
void AnimBank_Constructor(AnimBank *bank, void *) {
    for (int i = bank->count - 1; i >= 0; i--)
        AnimPool_InitAnimMemoryMap(bank->anims[i]);
}

// FUNC_AT(0x000f71a0)
void AnimBank_Destructor(AnimBank *) {
}

// FUNC_AT(0x000f71b0)
int AnimBank::FindAnim(const char *name) {
    int lo = 0, hi = count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        int c = strcmp(name, names[mid]);
        if (c > 0)
            lo = mid + 1;
        else if (c < 0)
            hi = mid - 1;
        else
            return mid;
    }
    return -1;
}

// A compound channel around an anim's data (the anim database's).
// FUNC_AT(0x000f7240)
FnAnim* AnimBank_NewCompound(uint8_t *data) {
    FnCompoundChannel *c = static_cast<FnCompoundChannel *>(AnimPool_NewBlockByIdx(kCompound));
    if (c != NULL) {
        c->FnAnimMemoryMap::Construct();
        c->vtable = VtCompound;
        c->channels = NULL;
        c->useFPS = 0;
        c->fps = 0;
        c->type = kCompound;
    }
    AnimVCall<void>(c, kSlotSetAnimMemoryMap, data);
    return c;
}

// ---- FnAnim

// FUNC_AT(0x000f72e0)
void FnAnim::Destruct() {
    vtable = VtFnAnim;
}

// FUNC_AT(0x000f7340)
FnAnim* FnAnim::ScalarDelete(unsigned flags) {
    vtable = VtFnAnim;
    if (flags & 1)
        BuiltinDelete(this);
    return this;
}

// FUNC_AT(0x000f72f0)
uint16_t FnAnim::GetTargetCheckSum() {
    return 0;
}

// FUNC_AT(0x000f7300)
bool FnAnim::GetLength(float *) {
    return false;
}

// FUNC_AT(0x000f7310)
bool FnAnim::EvalSQT(float, float *, void *) {
    return false;
}

// FUNC_AT(0x000f7320)
bool FnAnim::EvalEvent(float, float, void **, void *) {
    return false;
}

// FUNC_AT(0x000fb110)
void* FnAnim::GetAttributes() {
    return NULL;
}

// FUNC_AT(0x000f70e0)
bool FnAnim::NotImplemented(float, void *) {
    return false;
}

// ---- FnAnimMemoryMap

// FUNC_AT(0x000faa80)
FnAnimMemoryMap* FnAnimMemoryMap::Construct() {
    stat = 0;
    vtable = VtMemoryMap;
    anim = NULL;
    return this;
}

// FUNC_AT(0x000faaa0)
void FnAnimMemoryMap::Destruct() {
    vtable = VtFnAnim;
}

// FUNC_AT(0x000faab0)
void FnAnimMemoryMap::SetAnimMemoryMap(uint8_t *data) {
    anim = data;
}

// FUNC_AT(0x000faac0)
uint8_t* FnAnimMemoryMap::GetAnimMemoryMap() {
    return anim;
}

// FUNC_AT(0x000faad0)
uint8_t* FnAnimMemoryMap::GetAnimMemoryMap2() {
    return anim;
}

// FUNC_AT(0x000faae0)
uint16_t FnAnimMemoryMap::GetTargetCheckSum() {
    return reinterpret_cast<AnimData *>(anim)->checksum;
}

// FUNC_AT(0x000faaf0)
FnAnimMemoryMap* FnAnimMemoryMap::ScalarDelete(unsigned flags) {
    Destruct();
    if (flags & 1)
        BuiltinDelete(this);
    return this;
}

// A byte attribute (AttributeBlock::GetAttribute writes into a dword, which starts out holding the id).
// FUNC_AT(0x000fab10)
bool FnAnimMemoryMap::GetAttributeByte(uint16_t id, uint8_t *out) {
    void *attributes = AnimVCall<void *>(this, kSlotGetAttributes);
    if (attributes == NULL)
        return false;
    uint32_t value = id;
    if (!static_cast<AttributeBlock *>(attributes)->GetAttribute(id, &value))
        return false;
    *out = uint8_t(value);
    return true;
}

// ---- FnCompoundChannel

// FUNC_AT(0x000f70b0)
FnCompoundChannel* FnCompoundChannel::Construct() {
    FnAnimMemoryMap::Construct();
    channels = NULL;
    useFPS = 0;
    fps = 0;
    vtable = VtCompound;
    type = kCompound;
    return this;
}

// The sub-channels released, last first, and their array.
// FUNC_AT(0x000fab60)
void FnCompoundChannel::Destruct() {
    vtable = VtCompound;
    if (channels != NULL) {
        for (int i = Compound(anim)->count - 1; i >= 0; i--)
            AnimPool_ReleaseFnAnim(channels[i]);
        AnimPool_FreeBlock(channels);
    }
    FnAnimMemoryMap::Destruct();
}

// FUNC_AT(0x000f7140)
FnCompoundChannel* FnCompoundChannel::ScalarDelete(unsigned flags) {
    Destruct();
    if (flags & 1)
        EaglFree(this, sizeof(*this));
    return this;
}

// FUNC_AT(0x000f70f0)
uint16_t FnCompoundChannel::GetTargetCheckSum() {
    return Compound(anim)->checksum;
}

// FUNC_AT(0x000f7100)
bool FnCompoundChannel::GetLength(float *length) {
    int frames = Compound(anim)->frames;
    *length = float(frames);
    if (useFPS)
        *length = float(frames) / float(fps);
    return true;
}

// New data: the old sub-channels go (counted by the new data's count, as the original does), to be rebuilt.
// FUNC_AT(0x000fac00)
void FnCompoundChannel::SetAnimMemoryMap(uint8_t *data) {
    anim = data;
    if (channels != NULL) {
        for (int i = Compound(data)->count - 1; i >= 0; i--)
            AnimPool_ReleaseFnAnim(channels[i]);
        AnimPool_FreeBlock(channels);
    }
    channels = NULL;
}

// FUNC_AT(0x000fb050)
void FnCompoundChannel::InitSubChannels() {
    CompoundData *data = Compound(anim);
    channels = static_cast<FnAnim **>(AnimPool_NewBlock(data->count * sizeof(FnAnim *)));
    for (int i = data->count - 1; i >= 0; i--)
        channels[i] = AnimPool_NewFnAnim(data->subs[i]);
}

// FUNC_AT(0x000fb0a0)
void FnCompoundChannel::Eval(float previous, float time, float *out) {
    CompoundData *data = Compound(anim);
    if (channels == NULL)
        InitSubChannels();
    if (useFPS) {
        previous *= fps;
        time *= fps;
    }
    for (int i = data->count - 1; i >= 0; i--)
        AnimVCall<void>(channels[i], kSlotEval, previous, time, out);
}

// FUNC_AT(0x000fac70)
bool FnCompoundChannel::EvalEvent(float previous, float time, void **handlers, void *data) {
    CompoundData *compound = Compound(anim);
    bool any = false;
    if (channels == NULL)
        InitSubChannels();
    if (useFPS) {
        previous *= fps;
        time *= fps;
    }
    for (int i = compound->count - 1; i >= 0; i--)
        any |= AnimVCall<bool>(channels[i], kSlotEvalEvent, previous, time, handlers, data);
    return any;
}

// FUNC_AT(0x000facf0)
bool FnCompoundChannel::EvalSQT(float time, float *sqt, void *mask) {
    CompoundData *data = Compound(anim);
    bool any = false;
    if (channels == NULL)
        InitSubChannels();
    if (useFPS)
        time *= fps;
    for (int i = data->count - 1; i >= 0; i--)
        any |= AnimVCall<bool>(channels[i], kSlotEvalSQT, time, sqt, mask);
    return any;
}

// FUNC_AT(0x000fad60)
bool FnCompoundChannel::EvalWeights(float time, float *weights) {
    CompoundData *data = Compound(anim);
    bool any = false;
    if (channels == NULL)
        InitSubChannels();
    if (useFPS)
        time *= fps;
    for (int i = data->count - 1; i >= 0; i--)
        any |= AnimVCall<bool>(channels[i], kSlotEvalWeights, time, weights);
    return any;
}

// FUNC_AT(0x000fadc0)
bool FnCompoundChannel::EvalVel2D(float time, float *velocity) {
    CompoundData *data = Compound(anim);
    bool any = false;
    if (channels == NULL)
        InitSubChannels();
    if (useFPS)
        time *= fps;
    for (int i = data->count - 1; i >= 0; i--)
        any |= AnimVCall<bool>(channels[i], kSlotEvalVel2D, time, velocity);
    return any;
}

// FUNC_AT(0x000fae20)
bool FnCompoundChannel::EvalState(float time, void *state) {
    CompoundData *data = Compound(anim);
    bool any = false;
    if (channels == NULL)
        InitSubChannels();
    if (useFPS)
        time *= fps;
    for (int i = data->count - 1; i >= 0; i--)
        any |= AnimVCall<bool>(channels[i], kSlotEvalState, time, state);
    return any;
}

// The first sub-channel (last first) that finds the time; no sub-channels built here, as in the original.
// FUNC_AT(0x000fae80)
bool FnCompoundChannel::FindTime(void *test, float from, float *time) {
    CompoundData *data = Compound(anim);
    if (useFPS)
        from *= fps;
    for (int i = data->count - 1; i >= 0; i--) {
        if (AnimVCall<bool>(channels[i], kSlotFindTime, test, from, time)) {
            if (useFPS)
                *time /= fps;
            return true;
        }
    }
    return false;
}

// FUNC_AT(0x000faf00)
bool FnCompoundChannel::EvalPhase(float time, void *phase) {
    CompoundData *data = Compound(anim);
    bool any = false;
    if (useFPS)
        time *= fps;
    for (int i = data->count - 1; i >= 0; i--)
        any |= AnimVCall<bool>(channels[i], kSlotEvalPhase, time, phase);
    return any;
}

// FUNC_AT(0x000faf90)
void* FnCompoundChannel::GetPhaseChan() {
    CompoundData *data = Compound(anim);
    if (channels == NULL)
        InitSubChannels();
    for (int i = data->count - 1; i >= 0; i--) {
        void *phase = AnimVCall<void *>(channels[i], kSlotGetPhaseChan);
        if (phase != NULL)
            return phase;
    }
    return NULL;
}

// FUNC_AT(0x000fafd0)
void FnCompoundChannel::UseFPS(bool use) {
    useFPS = use;
    if (fps == 0 && use)
        GetAttributeByte(1, &fps);
}

// FUNC_AT(0x000fb000)
uint8_t FnCompoundChannel::GetFPS() {
    if (fps == 0) {
        void *attributes = AnimVCall<void *>(this, kSlotGetAttributes);
        if (attributes != NULL) {
            uint32_t value;
            if (static_cast<AttributeBlock *>(attributes)->GetAttribute(1, &value))
                fps = uint8_t(value);
        }
    }
    return fps;
}

// FUNC_AT(0x000fb040)
void* FnCompoundChannel::GetAttributes() {
    return Compound(anim)->attributes;
}

// FUNC_AT(0x000faf60)
void CompoundChannel_InitAnimMemoryMap(uint8_t *data) {
    CompoundData *compound = Compound(data);
    for (int i = compound->count - 1; i >= 0; i--)
        AnimPool_InitAnimMemoryMap(compound->subs[i]);
}

// ---- FnRawPoseChannel (its decoding is RawPoseChannel's, AnimMisc.cpp)

// FUNC_AT(0x000f7360)
FnRawPoseChannel* FnRawPoseChannel::Construct() {
    FnAnimMemoryMap::Construct();
    vtable = VtRawPose;
    interpolate = 1;
    type = kRawPose;
    return this;
}

// FUNC_AT(0x000f73d0)
void FnRawPoseChannel::Destruct() {
    vtable = VtRawPose;
    FnAnimMemoryMap::Destruct();
}

// FUNC_AT(0x000f73a0)
FnRawPoseChannel* FnRawPoseChannel::ScalarDelete(unsigned flags) {
    Destruct();
    if (flags & 1)
        EaglFree(this, sizeof(*this));
    return this;
}

// FUNC_AT(0x000f7380)
bool FnRawPoseChannel::GetLength(float *length) {
    *length = float(reinterpret_cast<RawPoseChannel *>(anim)->frames);
    return true;
}

// FUNC_AT(0x000fb120)
void FnRawPoseChannel::Eval(float, float time, float *out) {
    reinterpret_cast<RawPoseChannel *>(anim)->Eval(time, out, interpolate != 0, NULL);
}

// FUNC_AT(0x000fb140)
bool FnRawPoseChannel::EvalSQT(float time, float *sqt, void *mask) {
    reinterpret_cast<RawPoseChannel *>(anim)->Eval(time, sqt, interpolate != 0, mask);
    return true;
}

// ---- FnRawEventChannel (RawEventData::Eval is AnimChannels.cpp's)

// FUNC_AT(0x000f73e0)
FnRawEventChannel* FnRawEventChannel::Construct() {
    FnAnimMemoryMap::Construct();
    lastIndex = 0;
    lastTime = 0.0f;
    vtable = VtRawEvent;
    type = kRawEvent;
    return this;
}

// FUNC_AT(0x000f74b0)
void FnRawEventChannel::Destruct() {
    vtable = VtRawEvent;
    FnAnimMemoryMap::Destruct();
}

// FUNC_AT(0x000f7480)
FnRawEventChannel* FnRawEventChannel::ScalarDelete(unsigned flags) {
    Destruct();
    if (flags & 1)
        EaglFree(this, sizeof(*this));
    return this;
}

// FUNC_AT(0x000f7410)
void FnRawEventChannel::SetAnimMemoryMap(uint8_t *data) {
    anim = data;
    lastIndex = 0;
    lastTime = 0.0f;
}

// FUNC_AT(0x000f7430)
bool FnRawEventChannel::EvalEvent(float previous, float time, void **handlers, void *data) {
    reinterpret_cast<RawEventData *>(anim)->Eval(previous, time, &lastIndex, &lastTime, handlers, data);
    return true;
}

// Eval's output is taken as the handler table.
// FUNC_AT(0x000f7460)
void FnRawEventChannel::Eval(float previous, float time, float *out) {
    AnimVCall<bool>(this, kSlotEvalEvent, previous, time, reinterpret_cast<void **>(out), (void *)NULL);
}

// ---- FnRawLinearChannel

// FUNC_AT(0x000f74c0)
FnRawLinearChannel* FnRawLinearChannel::Construct() {
    FnAnimMemoryMap::Construct();
    vtable = VtRawLinear;
    interpolate = 1;
    type = kRawLinear;
    return this;
}

// FUNC_AT(0x000f76a0)
void FnRawLinearChannel::Destruct() {
    vtable = VtRawLinear;
    FnAnimMemoryMap::Destruct();
}

// FUNC_AT(0x000f7670)
FnRawLinearChannel* FnRawLinearChannel::ScalarDelete(unsigned flags) {
    Destruct();
    if (flags & 1)
        EaglFree(this, sizeof(*this));
    return this;
}

// FUNC_AT(0x000f74e0)
void FnRawLinearChannel::Eval(float, float time, float *out) {
    reinterpret_cast<RawLinearData *>(anim)->Eval(time, out, interpolate != 0);
}

// FUNC_AT(0x000f7650)
bool FnRawLinearChannel::GetLength(float *length) {
    *length = float(reinterpret_cast<RawLinearData *>(anim)->frames);
    return true;
}

// The frame below the time, or a lerp to the next one when there is a fraction and interpolation; times outside
// the frames clamp. (The fraction is exact in float: a float less its truncation.)
// FUNC_AT(0x000f7500)
void RawLinearData::Eval(float time, float *out, bool interpolate) {
    int frame = Truncate(time);
    if (frame < 0) {
        Copy(0, out);
        return;
    }
    int last = frames - 1;
    if (frame >= last) {
        Copy(last, out);
        return;
    }
    float t = time - float(frame);
    if (t != 0.0f && interpolate) {
        Lerp(t, frame, frame + 1, out);
        return;
    }
    Copy(frame, out);
}

// FUNC_AT(0x000f75a0)
void RawLinearData::Copy(int frame, float *out) {
    float *values = Frame(frame);
    for (int i = 0; i < channels; i++)
        out[index[i]] = values[i];
}

// FUNC_AT(0x000f75e0)
void RawLinearData::Lerp(float t, int frame0, int frame1, float *out) {
    float *a = Frame(frame0);
    float *b = Frame(frame1);
    for (int i = 0; i < channels; i++)
        out[index[i]] = float((double(b[i]) - a[i]) * t + a[i]);
}

// ---- FnKeyDeltaChan and FnDeltaChan: decoded values in a pool block, sized by the data's value count

namespace {

// Keep the block if the new data needs no more values than the old; otherwise a new one.
float* ResizeValues(float *values, DeltaCompressedData *oldInfo, DeltaCompressedData *newInfo) {
    if (values != NULL) {
        if (newInfo->count < oldInfo->count)
            return values;
        AnimPool_FreeBlock(values);
    }
    return static_cast<float *>(AnimPool_NewBlock(newInfo->count * sizeof(float)));
}

}  // namespace

// FUNC_AT(0x000fb890)
void FnKeyDeltaChan::Destruct() {
    vtable = VtKeyDelta;
    if (values != NULL)
        AnimPool_FreeBlock(values);
    FnAnimMemoryMap::Destruct();
}

// FUNC_AT(0x000f82c0)
void FnKeyDeltaChan::DestructThunk() {
    Destruct();
}

// FUNC_AT(0x000f76b0)
FnKeyDeltaChan* FnKeyDeltaChan::ScalarDelete(unsigned flags) {
    Destruct();
    if (flags & 1)
        EaglFree(this, sizeof(*this));
    return this;
}

// FUNC_AT(0x000f8290)
FnKeyDeltaChan* FnKeyDeltaChan::ScalarDeleteLerp(unsigned flags) {
    DestructThunk();
    if (flags & 1)
        EaglFree(this, sizeof(*this));
    return this;
}

// FUNC_AT(0x000f76e0)
FnKeyDeltaChan* FnKeyDeltaChan::ConstructLerp() {
    FnAnimMemoryMap::Construct();
    key = -1;
    values = NULL;
    vtable = VtKeyLerp;
    type = kKeyLerp;
    return this;
}

// FUNC_AT(0x000f7710)
FnKeyDeltaChan* FnKeyDeltaChan::ConstructQuat() {
    FnAnimMemoryMap::Construct();
    key = -1;
    values = NULL;
    vtable = VtKeyQuat;
    type = kKeyQuat;
    return this;
}

// FUNC_AT(0x000fb8c0)
void FnKeyDeltaChan::SetAnimMemoryMap(uint8_t *data) {
    if (values != NULL)
        values = ResizeValues(values, Keyed(anim)->info, Keyed(data)->info);
    else
        values = static_cast<float *>(AnimPool_NewBlock(Keyed(data)->info->count * sizeof(float)));
    anim = data;
    key = -1;
}

// FUNC_AT(0x000fb930)
void FnKeyDeltaChan::EvalToPrevValues(int k) {
    DeltaCompressedData *info = Keyed(anim)->info;
    info->DecompressValues(0, info->count, key, k, values, values);
    key = k;
}

// FUNC_AT(0x000fb360)
void FnDeltaChan::Destruct() {
    vtable = VtDeltaChan;
    if (values != NULL)
        AnimPool_FreeBlock(values);
    FnAnimMemoryMap::Destruct();
}

// FUNC_AT(0x000f8280)
void FnDeltaChan::DestructThunk() {
    Destruct();
}

// FUNC_AT(0x000f7740)
FnDeltaChan* FnDeltaChan::ScalarDelete(unsigned flags) {
    Destruct();
    if (flags & 1)
        EaglFree(this, sizeof(*this));
    return this;
}

// FUNC_AT(0x000f8250)
FnDeltaChan* FnDeltaChan::ScalarDeleteQuat(unsigned flags) {
    DestructThunk();
    if (flags & 1)
        EaglFree(this, sizeof(*this));
    return this;
}

// FUNC_AT(0x000f81f0)
FnDeltaChan* FnDeltaChan::ConstructLerp() {
    FnAnimMemoryMap::Construct();
    frame = -1;
    values = NULL;
    vtable = VtDeltaLerp;
    type = kDeltaLerp;
    return this;
}

// FUNC_AT(0x000f8220)
FnDeltaChan* FnDeltaChan::ConstructQuat() {
    FnAnimMemoryMap::Construct();
    frame = -1;
    values = NULL;
    vtable = VtDeltaQuat;
    type = kDeltaQuat;
    return this;
}

// FUNC_AT(0x000f7770)
bool FnDeltaChan::GetLength(float *length) {
    *length = float(Delta(anim)->frames);
    return true;
}

// FUNC_AT(0x000fb390)
void FnDeltaChan::SetAnimMemoryMap(uint8_t *data) {
    if (values != NULL)
        values = ResizeValues(values, Delta(anim)->info, Delta(data)->info);
    else
        values = static_cast<float *>(AnimPool_NewBlock(Delta(data)->info->count * sizeof(float)));
    anim = data;
    frame = -1;
}

// The frame clamped to the data's frames, decoded from the last one decoded.
// FUNC_AT(0x000fb400)
void FnDeltaChan::DecodeFrame(int f) {
    DeltaCompressedData *info = Delta(anim)->info;
    int frames = Delta(anim)->frames;
    if (f >= frames)
        f = frames - 1;
    else if (f < 0)
        f = 0;
    info->DecompressValues(0, info->count, frame, f, values, values);
    frame = f;
}

// ---- the factory's inline classes, the blenders' and mirror's object functions

// FUNC_AT(0x000f7790)
FnGraft* FnGraft::ScalarDelete(unsigned flags) {
    FnAnim::Destruct();
    if (flags & 1)
        EaglFree(this, sizeof(*this));
    return this;
}

// FUNC_AT(0x000f77c0)
void FnGraft::Eval(float previous, float time, float *out) {
    for (int i = 0; i < count; i++)
        AnimVCall<void>(anims[i], kSlotEval, previous, time, out);
}

// Times below start wrap back from end, times past end wrap forward from start, by whole periods. The remainder
// stays on the x87 stack: rounded to float once (rf) while the division uses it unrounded.
// FUNC_AT(0x000f79b0)
double FnCycle::Wrap(float time) {
    if (time < start) {
        double r = double(time) - start;
        float rf = float(r);
        int periods = Truncate(float(r / period));
        return end - (rf - double(periods) * period);
    }
    if (time > end) {
        double r = double(time) - end;
        float rf = float(r);
        int periods = Truncate(float(r / period));
        return (rf - double(periods) * period) + start;
    }
    return time;
}

// FUNC_AT(0x000f7970)
void FnCycle::Eval(float previous, float time, float *out) {
    float t = float(Wrap(time));
    float p = float(Wrap(previous));
    AnimVCall<void>(anim, kSlotEval, p, t, out);
}

// FUNC_AT(0x000f7a50)
bool FnCycle::EvalEvent(float previous, float time, void **handlers, void *data) {
    float t = float(Wrap(time));
    float p = float(Wrap(previous));
    return AnimVCall<bool>(anim, kSlotEvalEvent, p, t, handlers, data);
}

// FUNC_AT(0x000f7a90)
bool FnCycle::EvalSQT(float time, float *sqt, void *mask) {
    return AnimVCall<bool>(anim, kSlotEvalSQT, float(Wrap(time)), sqt, mask);
}

// FUNC_AT(0x000f7ac0)
bool FnCycle::EvalPhase(float time, void *phase) {
    return AnimVCall<bool>(anim, kSlotEvalPhase, float(Wrap(time)), phase);
}

// FUNC_AT(0x000f7810)
FnPoseBlender* FnPoseBlender::Construct() {
    stat = 0;
    vtable = VtPoseBlender;
    skeleton = NULL;
    bone = -1;
    poseB = NULL;
    poseA = NULL;
    stillB = 0;
    stillA = 0;
    type = kPoseBlender;
    return this;
}

// FUNC_AT(0x000f7840)
FnPoseBlender* FnPoseBlender::ScalarDelete(unsigned flags) {
    FnAnim::Destruct();
    if (flags & 1)
        EaglFree(this, sizeof(*this));
    return this;
}

// FUNC_AT(0x000f7870)
FnPoseMirror* FnPoseMirror::Construct() {
    stat = 0;
    vtable = VtPoseMirror;
    anim = NULL;
    skeleton = NULL;
    flags = 0;
    enabled = 1;
    type = kPoseMirror;
    return this;
}

// FUNC_AT(0x000f7940)
FnPoseMirror* FnPoseMirror::ScalarDelete(unsigned deleteFlags) {
    FnAnim::Destruct();
    if (deleteFlags & 1)
        EaglFree(this, sizeof(*this));
    return this;
}

// FUNC_AT(0x000f78a0)
void FnPoseMirror::Eval(float previous, float time, float *out) {
    if (!enabled) {
        AnimVCall<void>(anim, kSlotEval, previous, time, out);
        return;
    }
    AnimVCall<void>(anim, kSlotEval, previous, time, pose);
    skeleton->MirrorPose(pose, out, flags != 0, NULL);
}

// FUNC_AT(0x000f78f0)
bool FnPoseMirror::EvalSQT(float time, float *sqt, void *mask) {
    if (!enabled)
        return AnimVCall<bool>(anim, kSlotEvalSQT, time, sqt, mask);
    bool any = AnimVCall<bool>(anim, kSlotEvalSQT, time, pose, mask);
    if (any)
        skeleton->MirrorPose(pose, sqt, flags != 0, static_cast<BoneMask *>(mask));
    return any;
}

// FUNC_AT(0x000f7af0)
FnEventBlender* FnEventBlender::ScalarDelete(unsigned flags) {
    FnAnim::Destruct();
    if (flags & 1)
        EaglFree(this, sizeof(*this));
    return this;
}

// ---- FnPhaseChan and FnRawStateChan

// FUNC_AT(0x000f7b20)
FnPhaseChan* FnPhaseChan::Construct() {
    FnAnimMemoryMap::Construct();
    vtable = VtPhase;
    index = 0;
    step = 1;
    type = kPhase;
    return this;
}

// FUNC_AT(0x000f7b50)
void* FnPhaseChan::GetPhaseChan() {
    return anim;
}

// FUNC_AT(0x000f8300)
void FnPhaseChan::Destruct() {
    FnAnimMemoryMap::Destruct();
}

// FUNC_AT(0x000f82d0)
FnPhaseChan* FnPhaseChan::ScalarDelete(unsigned flags) {
    Destruct();
    if (flags & 1)
        EaglFree(this, sizeof(*this));
    return this;
}

// FUNC_AT(0x000f7b60)
FnRawStateChan* FnRawStateChan::Construct() {
    FnAnimMemoryMap::Construct();
    vtable = VtRawState;
    frame = 0;
    type = kRawState;
    return this;
}

// FUNC_AT(0x000f7b80)
bool FnRawStateChan::GetLength(float *length) {
    *length = float(reinterpret_cast<RawStateChanData *>(anim)->length);
    return true;
}

// FUNC_AT(0x000f7ba0)
void FnRawStateChan::Eval(float, float time, float *out) {
    AnimVCall<bool>(this, kSlotEvalState, time, (void *)out);
}

// FUNC_AT(0x000f7bc0)
FnRawStateChan* FnRawStateChan::ScalarDelete(unsigned flags) {
    Destruct();
    if (flags & 1)
        EaglFree(this, sizeof(*this));
    return this;
}

// FUNC_AT(0x000f7bf0)
void FnRawStateChan::Destruct() {
    vtable = VtRawState;
    FnAnimMemoryMap::Destruct();
}
