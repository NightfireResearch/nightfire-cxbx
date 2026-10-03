#include "AnimObjects.h"

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
// 53-bit precision is the same; float-to-int truncation is CVTTSS2SI, as there.
// devtools/AnimShadow.cpp builds every anim in every bank with both and compares what they evaluate.
// ---------------------------------------------------------------------------------------------------------------

#define EaglMalloc   (*(void *(**)(uint32_t size, const char *name))0x001caf68u)
#define EaglFree     (*(void (**)(void *data, uint32_t size))0x001caf6cu)

// the pool (MemoryPoolManager)
#define FreeByType   ((void **)0x002414b0u)        // [22] free blocks per anim type
#define FreeBySize   ((void **)0x00241520u)        // [256] free blocks per 16-byte size class
#define PoolBase     (*(uint8_t **)0x00241510u)
#define PoolSize     (*(uint32_t *)0x00241514u)
#define PoolCursor   (*(uint8_t **)0x00241518u)
#define PoolWord     (*(uint16_t *)0x0024150cu)
#define BlockSizes   ((const uint16_t *)0x001ceab0u)   // per anim type
#define FactoryBlock (*(void **)0x00241b80u)

#define VtFnAnim         ((const void *)0x001a0c6cu)
#define VtCompound       ((const void *)0x001a0be8u)
#define VtRawPose        ((const void *)0x001a0ca8u)
#define VtRawEvent       ((const void *)0x001a0cf0u)
#define VtRawLinear      ((const void *)0x001a0d38u)
#define VtKeyDelta       ((const void *)0x001a0d80u)
#define VtKeyLerp        ((const void *)0x001a0dc8u)
#define VtKeyQuat        ((const void *)0x001a0e10u)
#define VtDeltaChan      ((const void *)0x001a0e58u)
#define VtGraft          ((const void *)0x001a0ea0u)
#define VtPoseBlender    ((const void *)0x001a0edcu)
#define VtPoseMirror     ((const void *)0x001a0f18u)
#define VtCycle          ((const void *)0x001a0f54u)
#define VtEventBlender   ((const void *)0x001a0f90u)
#define VtPhase          ((const void *)0x001a0fd0u)
#define VtRawState       ((const void *)0x001a1018u)
#define VtDeltaLerp      ((const void *)0x001a1060u)
#define VtDeltaQuat      ((const void *)0x001a10a8u)
#define VtMemoryMap      ((const void *)0x001a1130u)

static inline int Truncate(float f) {   // CVTTSS2SI
    return _mm_cvtt_ss2si(_mm_set_ss(f));
}

// A pool block back on its size class's free list (its class is in the dword before it).
static inline void FreeSized(void *block) {
    uint32_t sizeClass = ((uint32_t *)block)[-1];
    *(void **)block = FreeBySize[sizeClass];
    FreeBySize[sizeClass] = block;
}

// An anim's destructor run (scalar deleting, flags 0), and the object back on its type's free list.
static inline void ReleaseAnim(FnAnim *anim) {
    AnimVCall<void *>(anim, kSlotDelete, 0u);
    uint16_t type = (uint16_t)anim->type;
    *(void **)anim = FreeByType[type];
    FreeByType[type] = anim;
}

// ---- the pool

// FUNC_AT(0x000141d0)
uint16_t* AnimData::GetType(uint16_t *out) {
    *out = *(uint16_t *)this;
    return out;
}

// FUNC_AT(0x000141e0)
void AnimPool_DeleteFnAnim(FnAnim *anim) {
    ReleaseAnim(anim);
}

// The factory's object, and the anim data attached through slot 15 (whatever the type's vtable has there).
// FUNC_AT(0x00014210)
FnAnim* AnimPool_NewFnAnim(uint8_t *data) {
    uint16_t type;
    ((AnimData *)data)->GetType(&type);
    FnAnim *anim = AnimPool_Construct(type);
    AnimVCall<void>(anim, kSlotSetAnimMemoryMap, data);
    return anim;
}

// FUNC_AT(0x000f7c40)
void* AnimPool_NewBlockByIdx(uint16_t type) {
    void *block = FreeByType[type];
    if (block == NULL) {
        *(void **)PoolCursor = NULL;
        FreeByType[type] = PoolCursor;
        PoolCursor += BlockSizes[type];
        block = FreeByType[type];
    }
    FreeByType[type] = *(void **)block;
    return block;
}

// A block of (size / 16) 16-byte units plus one, its class in the dword before it.
// FUNC_AT(0x000f7c90)
void* AnimPool_NewBlock(uint32_t size) {
    uint32_t sizeClass = (uint8_t)(size >> 4);
    void *block = FreeBySize[sizeClass];
    if (block != NULL) {
        FreeBySize[sizeClass] = *(void **)block;
        return block;
    }
    *(uint32_t *)PoolCursor = sizeClass;
    block = PoolCursor + 4;
    PoolCursor += sizeClass * 16 + 0x14;
    return block;
}

// FUNC_AT(0x000f7cd0)
void AnimPool_ResetPool() {
    PoolCursor = PoolBase;
    memset(FreeBySize, 0, 0x100 * 4);
    memset(FreeByType, 0, 0x16 * 4);
}

// FUNC_AT(0x000f7d00)
void AnimPool_Init(uint32_t size) {
    PoolSize = size;
    PoolBase = (uint8_t *)EaglMalloc(size, (const char *)0x001ceadcu);   // "EAGLAnim Memory Pool"
    PoolCursor = PoolBase;
    memset(FreeBySize, 0, 0x100 * 4);
    memset(FreeByType, 0, 0x16 * 4);
    PoolWord = 0;
}

// FUNC_AT(0x000f7d50)
void AnimPool_Cleanup() {
    EaglFree(PoolBase, PoolSize);
}

// The data's one-off preparation: a raw pose decides its decoders, a compound prepares its sub-anims.
// FUNC_AT(0x000f7d70)
void AnimPool_InitAnimMemoryMap(uint8_t *data) {
    uint16_t type;
    ((AnimData *)data)->GetType(&type);
    if (type == kRawPose)
        ((void (*)(uint8_t *))0x000fda70)(data);   // RawPoseChannel::InitAnimMemoryMap
    else if (type == kCompound)
        ((void (*)(uint8_t *))0x000faf60)(data);   // CompoundChannel::InitAnimMemoryMap
}

// FUNC_AT(0x000f7de0)
FnAnim* AnimPool_Construct(uint16_t type) {
    FnAnim *a = (FnAnim *)AnimPool_NewBlockByIdx(type);
    switch (type) {
    case kRawPose: return ((FnRawPoseChannel *)a)->Construct();
    case kRawEvent: return ((FnRawEventChannel *)a)->Construct();
    case kRawLinear: return ((FnRawLinearChannel *)a)->Construct();
    case kCycle:
        a->stat = 0;
        a->vtable = VtCycle;
        a->type = 3;
        return a;
    case kEventBlender:
        a->stat = 0;
        a->vtable = VtEventBlender;
        a->type = 4;
        return a;
    case kGraft:
        a->stat = 0;
        a->vtable = VtGraft;
        a->type = 5;
        return a;
    case kPoseBlender: return ((FnPoseBlender *)a)->Construct();
    case kPoseMirror: return ((FnPoseMirror *)a)->Construct();
    case kRunBlender: return ((FnAnim *(__fastcall *)(void *, int))0x00105000)(a, 0);    // FnRunBlender
    case kTurnBlender: return ((FnAnim *(__fastcall *)(void *, int))0x001042d0)(a, 0);   // FnTurnBlender
    case kDeltaLerp: return ((FnDeltaChan *)a)->ConstructLerp();
    case kDeltaQuat: return ((FnDeltaChan *)a)->ConstructQuat();
    case kKeyLerp: return ((FnKeyDeltaChan *)a)->ConstructLerp();
    case kKeyQuat: return ((FnKeyDeltaChan *)a)->ConstructQuat();
    case kPhase: return ((FnPhaseChan *)a)->Construct();
    case kCompound: return ((FnCompoundChannel *)a)->Construct();
    case kRawState: return ((FnRawStateChan *)a)->Construct();
    case kDeltaQ: return ((FnAnim *(__fastcall *)(void *, int))0x001033b0)(a, 0);        // FnDeltaQ
    case kDeltaQFast: return ((FnAnim *(__fastcall *)(void *, int))0x00101f40)(a, 0);    // FnDeltaQFast
    case kDeltaSingleQ: return ((FnAnim *(__fastcall *)(void *, int))0x00100980)(a, 0);  // FnDeltaSingleQ
    case kDeltaF3: return ((FnAnim *(__fastcall *)(void *, int))0x000ff280)(a, 0);       // FnDeltaF3
    case kDeltaF1: return ((FnAnim *(__fastcall *)(void *, int))0x000fe280)(a, 0);       // FnDeltaF1
    }
    return a;
}

// ---- Initializer

// FUNC_AT(0x000fa9c0)
void EAGLAnim_InitInternal(uint32_t poolSize) {
    AnimPool_Init(poolSize);
    FactoryBlock = EaglMalloc(1, (const char *)0x001ceb30u);   // "EAGLAnim::FnAnimFactory new"
    ((void (__fastcall *)(void *, int, const char *, void *, void *))0x000f3a40)(   // ConstructorPool::AddType
        (void *)0x0023fbe0u, 0, (const char *)0x001a0a04u, (void *)0x000f7170u, (void *)0x000f71a0u);
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
    return (uint32_t)(PoolCursor - PoolBase);
}

// FUNC_AT(0x000faa40)
void EAGLAnim_ResetPool() {
    AnimPool_ResetPool();
}

// FUNC_AT(0x000faa50)
void EAGLAnim_ShutDown() {
    ((void (__fastcall *)(void *, int, const char *))0x000f3a20)(   // ConstructorPool::RemoveType
        (void *)0x0023fbe0u, 0, (const char *)0x001a0a04u);
    ((void (*)())0x00106790)();   // ScratchBuffer::FreeScratchBuffers
    if (FactoryBlock != NULL)
        EaglFree(FactoryBlock, 1);
    AnimPool_Cleanup();
}

// ---- AnimationBank: u32, count, u32, anims[] at +0x0c, names[] at +0x10 (sorted)

// The loader's constructor for AnimationBank symbols: every anim's data prepared, last first.
// FUNC_AT(0x000f7170)
void AnimBank_Constructor(uint8_t *bank, void *loader) {
    (void)loader;
    uint8_t **anims = *(uint8_t ***)(bank + 0xc);
    for (int i = *(int32_t *)(bank + 4) - 1; i >= 0; i--)
        AnimPool_InitAnimMemoryMap(anims[i]);
}

// FUNC_AT(0x000f71a0)
void AnimBank_Destructor(uint8_t *bank) {
    (void)bank;
}

// FUNC_AT(0x000f71b0)
int AnimBank::FindAnim(const char *name) {
    int lo = 0, hi = *(int32_t *)((uint8_t *)this + 4) - 1;
    const char **names = *(const char ***)((uint8_t *)this + 0x10);
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
    FnCompoundChannel *c = (FnCompoundChannel *)AnimPool_NewBlockByIdx(kCompound);
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
        ((void (*)(void *))0x001146e0)(this);   // operator delete
    return this;
}

// FUNC_AT(0x000f72f0)
uint16_t FnAnim::GetTargetCheckSum() {
    return 0;
}

// FUNC_AT(0x000f7300)
bool FnAnim::GetLength(float *length) {
    (void)length;
    return false;
}

// FUNC_AT(0x000f7310)
bool FnAnim::EvalSQT(float time, float *sqt, void *mask) {
    (void)time;
    (void)sqt;
    (void)mask;
    return false;
}

// FUNC_AT(0x000f7320)
bool FnAnim::EvalEvent(float previous, float time, void **handlers, void *data) {
    (void)previous;
    (void)time;
    (void)handlers;
    (void)data;
    return false;
}

// FUNC_AT(0x000fb110)
void* FnAnim::GetAttributes() {
    return NULL;
}

// FUNC_AT(0x000f70e0)
bool FnAnim::NotImplemented(float a, void *b) {
    (void)a;
    (void)b;
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
    return *(uint16_t *)(anim + 2);
}

// FUNC_AT(0x000faaf0)
FnAnimMemoryMap* FnAnimMemoryMap::ScalarDelete(unsigned flags) {
    Destruct();
    if (flags & 1)
        ((void (*)(void *))0x001146e0)(this);   // operator delete
    return this;
}

// A byte attribute (AttributeBlock::GetAttribute writes into a dword).
// FUNC_AT(0x000fab10)
bool FnAnimMemoryMap::GetAttributeByte(uint16_t id, uint8_t *out) {
    void *attributes = AnimVCall<void *>(this, kSlotGetAttributes);
    if (attributes == NULL)
        return false;
    uint32_t value = id;
    if (!((bool (__fastcall *)(void *, int, uint32_t, void *))0x00106800)(attributes, 0, id, &value))
        return false;
    *out = (uint8_t)value;
    return true;
}

// ---- FnCompoundChannel: u16 type, u16 checksum, attributes at +4, u16 count at +8, u16 frames at +0xa,
// sub-anim data[] at +0xc

#define CompoundCount(anim) (*(uint16_t *)((anim) + 8))
#define CompoundSubs(anim)  ((uint8_t **)((anim) + 0xc))

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
        for (int i = CompoundCount(anim) - 1; i >= 0; i--)
            ReleaseAnim(channels[i]);
        FreeSized(channels);
    }
    FnAnimMemoryMap::Destruct();
}

// FUNC_AT(0x000f7140)
FnCompoundChannel* FnCompoundChannel::ScalarDelete(unsigned flags) {
    Destruct();
    if (flags & 1)
        EaglFree(this, 0x18);
    return this;
}

// FUNC_AT(0x000f70f0)
uint16_t FnCompoundChannel::GetTargetCheckSum() {
    return *(uint16_t *)(anim + 2);
}

// FUNC_AT(0x000f7100)
bool FnCompoundChannel::GetLength(float *length) {
    int frames = *(uint16_t *)(anim + 0xa);
    *length = (float)frames;
    if (useFPS)
        *length = (float)((double)frames / (double)(int)fps);
    return true;
}

// New data: the old sub-channels go (counted by the new data's count, as the original does), to be rebuilt.
// FUNC_AT(0x000fac00)
void FnCompoundChannel::SetAnimMemoryMap(uint8_t *data) {
    anim = data;
    if (channels != NULL) {
        for (int i = CompoundCount(data) - 1; i >= 0; i--)
            ReleaseAnim(channels[i]);
        FreeSized(channels);
    }
    channels = NULL;
}

// FUNC_AT(0x000fb050)
void FnCompoundChannel::InitSubChannels() {
    uint8_t *data = anim;
    channels = (FnAnim **)AnimPool_NewBlock((uint32_t)CompoundCount(data) * 4);
    for (int i = CompoundCount(data) - 1; i >= 0; i--)
        channels[i] = AnimPool_NewFnAnim(CompoundSubs(data)[i]);
}

// FUNC_AT(0x000fb0a0)
void FnCompoundChannel::Eval(float previous, float time, float *out) {
    uint8_t *data = anim;
    if (channels == NULL)
        InitSubChannels();
    if (useFPS) {
        double f = (double)(int)fps;
        previous = (float)((double)previous * f);
        time = (float)(f * (double)time);
    }
    for (int i = CompoundCount(data) - 1; i >= 0; i--)
        AnimVCall<void>(channels[i], kSlotEval, previous, time, out);
}

// FUNC_AT(0x000fac70)
bool FnCompoundChannel::EvalEvent(float previous, float time, void **handlers, void *data) {
    uint8_t *a = anim;
    bool any = false;
    if (channels == NULL)
        InitSubChannels();
    if (useFPS) {
        double f = (double)(int)fps;
        previous = (float)((double)previous * f);
        time = (float)(f * (double)time);
    }
    for (int i = CompoundCount(a) - 1; i >= 0; i--)
        any |= AnimVCall<bool>(channels[i], kSlotEvalEvent, previous, time, handlers, data);
    return any;
}

// FUNC_AT(0x000facf0)
bool FnCompoundChannel::EvalSQT(float time, float *sqt, void *mask) {
    uint8_t *a = anim;
    bool any = false;
    if (channels == NULL)
        InitSubChannels();
    if (useFPS)
        time = (float)((double)(int)fps * (double)time);
    for (int i = CompoundCount(a) - 1; i >= 0; i--)
        any |= AnimVCall<bool>(channels[i], kSlotEvalSQT, time, sqt, mask);
    return any;
}

// FUNC_AT(0x000fad60)
bool FnCompoundChannel::EvalWeights(float time, float *weights) {
    uint8_t *a = anim;
    bool any = false;
    if (channels == NULL)
        InitSubChannels();
    if (useFPS)
        time = (float)((double)(int)fps * (double)time);
    for (int i = CompoundCount(a) - 1; i >= 0; i--)
        any |= AnimVCall<bool>(channels[i], kSlotEvalWeights, time, weights);
    return any;
}

// FUNC_AT(0x000fadc0)
bool FnCompoundChannel::EvalVel2D(float time, float *velocity) {
    uint8_t *a = anim;
    bool any = false;
    if (channels == NULL)
        InitSubChannels();
    if (useFPS)
        time = (float)((double)(int)fps * (double)time);
    for (int i = CompoundCount(a) - 1; i >= 0; i--)
        any |= AnimVCall<bool>(channels[i], kSlotEvalVel2D, time, velocity);
    return any;
}

// FUNC_AT(0x000fae20)
bool FnCompoundChannel::EvalState(float time, void *state) {
    uint8_t *a = anim;
    bool any = false;
    if (channels == NULL)
        InitSubChannels();
    if (useFPS)
        time = (float)((double)(int)fps * (double)time);
    for (int i = CompoundCount(a) - 1; i >= 0; i--)
        any |= AnimVCall<bool>(channels[i], kSlotEvalState, time, state);
    return any;
}

// The first sub-channel (last first) that finds the time; no sub-channels built here, as in the original.
// FUNC_AT(0x000fae80)
bool FnCompoundChannel::FindTime(void *test, float from, float *time) {
    uint8_t *a = anim;
    if (useFPS)
        from = (float)((double)(int)fps * (double)from);
    for (int i = CompoundCount(a) - 1; i >= 0; i--) {
        if (AnimVCall<bool>(channels[i], kSlotFindTime, test, from, time)) {
            if (useFPS)
                *time = (float)((double)*time / (double)(int)fps);
            return true;
        }
    }
    return false;
}

// FUNC_AT(0x000faf00)
bool FnCompoundChannel::EvalPhase(float time, void *phase) {
    uint8_t *a = anim;
    bool any = false;
    if (useFPS)
        time = (float)((double)(int)fps * (double)time);
    for (int i = CompoundCount(a) - 1; i >= 0; i--)
        any |= AnimVCall<bool>(channels[i], kSlotEvalPhase, time, phase);
    return any;
}

// FUNC_AT(0x000faf90)
void* FnCompoundChannel::GetPhaseChan() {
    uint8_t *a = anim;
    if (channels == NULL)
        InitSubChannels();
    for (int i = CompoundCount(a) - 1; i >= 0; i--) {
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
            if (((bool (__fastcall *)(void *, int, uint32_t, void *))0x00106800)(attributes, 0, 1, &value))
                fps = (uint8_t)value;
        }
    }
    return fps;
}

// FUNC_AT(0x000fb040)
void* FnCompoundChannel::GetAttributes() {
    return *(void **)(anim + 4);
}

// FUNC_AT(0x000faf60)
void CompoundChannel_InitAnimMemoryMap(uint8_t *data) {
    for (int i = CompoundCount(data) - 1; i >= 0; i--)
        AnimPool_InitAnimMemoryMap(CompoundSubs(data)[i]);
}

// ---- FnRawPoseChannel (its decoding, RawPoseChannel::Eval 0x000fdd40, is module L)

#define RawPoseChannel_Eval ((void (__fastcall *)(uint8_t *, int, float, float *, uint32_t, void *))0x000fdd40)

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
        EaglFree(this, 0x14);
    return this;
}

// FUNC_AT(0x000f7380)
bool FnRawPoseChannel::GetLength(float *length) {
    *length = (float)*(int32_t *)(anim + 0xc);
    return true;
}

// FUNC_AT(0x000fb120)
void FnRawPoseChannel::Eval(float previous, float time, float *out) {
    (void)previous;
    RawPoseChannel_Eval(anim, 0, time, out, interpolate, NULL);
}

// FUNC_AT(0x000fb140)
bool FnRawPoseChannel::EvalSQT(float time, float *sqt, void *mask) {
    RawPoseChannel_Eval(anim, 0, time, sqt, interpolate, mask);
    return true;
}

// ---- FnRawEventChannel (RawEventChannel::Eval 0x000fb170 is module L)

// FUNC_AT(0x000f73e0)
FnRawEventChannel* FnRawEventChannel::Construct() {
    FnAnimMemoryMap::Construct();
    lastIndex = 0;
    lastCount = 0;
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
        EaglFree(this, 0x18);
    return this;
}

// FUNC_AT(0x000f7410)
void FnRawEventChannel::SetAnimMemoryMap(uint8_t *data) {
    anim = data;
    lastIndex = 0;
    lastCount = 0;
}

// FUNC_AT(0x000f7430)
bool FnRawEventChannel::EvalEvent(float previous, float time, void **handlers, void *data) {
    ((void (__fastcall *)(uint8_t *, int, float, float, int32_t *, int32_t *, void **, void *))0x000fb170)(
        anim, 0, previous, time, &lastIndex, &lastCount, handlers, data);   // RawEventChannel::Eval
    return true;
}

// FUNC_AT(0x000f7460)
void FnRawEventChannel::Eval(float previous, float time, float *out) {
    AnimVCall<bool>(this, kSlotEvalEvent, previous, time, (void **)out, (void *)NULL);
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
        EaglFree(this, 0x14);
    return this;
}

// FUNC_AT(0x000f74e0)
void FnRawLinearChannel::Eval(float previous, float time, float *out) {
    (void)previous;
    ((RawLinearData *)anim)->Eval(time, out, interpolate != 0);
}

// FUNC_AT(0x000f7650)
bool FnRawLinearChannel::GetLength(float *length) {
    *length = (float)(int)*(uint16_t *)(anim + 6);
    return true;
}

#define RawLinearChannels(d) (*(uint16_t *)((uint8_t *)(d) + 4))
#define RawLinearFrames(d)   (*(uint16_t *)((uint8_t *)(d) + 6))
#define RawLinearIndex(d)    ((uint16_t *)((uint8_t *)(d) + 8))

static inline float* RawLinearFrame(void *d, int frame) {
    int n = RawLinearChannels(d);
    return (float *)((uint8_t *)d + (((n + 1) & ~1) + n * frame * 2 + 4) * 2);
}

// The frame below the time, or a lerp to the next one when there is a fraction and interpolation; times outside
// the frames clamp.
// FUNC_AT(0x000f7500)
void RawLinearData::Eval(float time, float *out, bool interpolate) {
    int frame = Truncate(time);
    if (frame < 0) {
        Copy(0, out);
        return;
    }
    int last = RawLinearFrames(this) - 1;
    if (frame >= last) {
        Copy(last, out);
        return;
    }
    double fraction = (double)time - (double)frame;
    float t = (float)fraction;
    if (!(fraction == 0.0) && interpolate) {
        Lerp(t, frame, frame + 1, out);
        return;
    }
    Copy(frame, out);
}

// FUNC_AT(0x000f75a0)
void RawLinearData::Copy(int frame, float *out) {
    float *values = RawLinearFrame(this, frame);
    for (int i = 0; i < RawLinearChannels(this); i++)
        out[RawLinearIndex(this)[i]] = values[i];
}

// FUNC_AT(0x000f75e0)
void RawLinearData::Lerp(float t, int frame0, int frame1, float *out) {
    float *a = RawLinearFrame(this, frame0);
    float *b = RawLinearFrame(this, frame1);
    for (int i = 0; i < RawLinearChannels(this); i++)
        out[RawLinearIndex(this)[i]] = (float)(((double)b[i] - (double)a[i]) * (double)t + (double)a[i]);
}

// ---- FnKeyDeltaChan and FnDeltaChan: decoded values in a pool block, sized by the data's value count

#define DeltaInfo(anim)  (*(uint16_t **)((anim) + 4))
#define DecompressValues ((void (__fastcall *)(uint16_t *, int, int, int, int, int, float *, float *))0x001069d0)

// Keep the block if the new data needs no more values than the old; otherwise a new one.
static float* ResizeValues(float *values, uint8_t *oldAnim, uint8_t *newAnim) {
    if (values != NULL) {
        if (*DeltaInfo(newAnim) < *DeltaInfo(oldAnim))
            return values;
        FreeSized(values);
    }
    return (float *)AnimPool_NewBlock((uint32_t)*DeltaInfo(newAnim) * 4);
}

// FUNC_AT(0x000fb890)
void FnKeyDeltaChan::Destruct() {
    vtable = VtKeyDelta;
    if (values != NULL)
        FreeSized(values);
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
        EaglFree(this, 0x18);
    return this;
}

// FUNC_AT(0x000f8290)
FnKeyDeltaChan* FnKeyDeltaChan::ScalarDeleteLerp(unsigned flags) {
    DestructThunk();
    if (flags & 1)
        EaglFree(this, 0x18);
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
    if (values != NULL) {
        values = ResizeValues(values, anim, data);
    } else {
        values = (float *)AnimPool_NewBlock((uint32_t)*DeltaInfo(data) * 4);
    }
    anim = data;
    key = -1;
}

// FUNC_AT(0x000fb930)
void FnKeyDeltaChan::EvalToPrevValues(int k) {
    uint16_t *info = DeltaInfo(anim);
    DecompressValues(info, 0, 0, *info, key, k, values, values);
    key = k;
}

// FUNC_AT(0x000fb360)
void FnDeltaChan::Destruct() {
    vtable = VtDeltaChan;
    if (values != NULL)
        FreeSized(values);
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
        EaglFree(this, 0x18);
    return this;
}

// FUNC_AT(0x000f8250)
FnDeltaChan* FnDeltaChan::ScalarDeleteQuat(unsigned flags) {
    DestructThunk();
    if (flags & 1)
        EaglFree(this, 0x18);
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
    *length = (float)(int)*(uint16_t *)(anim + 8);
    return true;
}

// FUNC_AT(0x000fb390)
void FnDeltaChan::SetAnimMemoryMap(uint8_t *data) {
    if (values != NULL) {
        values = ResizeValues(values, anim, data);
    } else {
        values = (float *)AnimPool_NewBlock((uint32_t)*DeltaInfo(data) * 4);
    }
    anim = data;
    frame = -1;
}

// The frame clamped to the data's frames, decoded from the last one decoded.
// FUNC_AT(0x000fb400)
void FnDeltaChan::DecodeFrame(int f) {
    uint16_t *info = DeltaInfo(anim);
    int frames = *(uint16_t *)(anim + 8);
    if (f >= frames)
        f = frames - 1;
    else if (f < 0)
        f = 0;
    DecompressValues(info, 0, 0, *info, frame, f, values, values);
    frame = f;
}

// ---- the factory's inline classes, the blenders' and mirror's object functions

// FUNC_AT(0x000f7790)
FnGraft* FnGraft::ScalarDelete(unsigned flags) {
    FnAnim::Destruct();
    if (flags & 1)
        EaglFree(this, 0x14);
    return this;
}

// FUNC_AT(0x000f77c0)
void FnGraft::Eval(float previous, float time, float *out) {
    for (int i = 0; i < count; i++)
        AnimVCall<void>(anims[i], kSlotEval, previous, time, out);
}

// Times below start wrap back from end, times past end wrap forward from start, by whole periods.
// FUNC_AT(0x000f79b0)
double FnCycle::Wrap(float time) {
    if ((double)time < (double)start) {
        double r = (double)time - (double)start;
        float rf = (float)r;
        int periods = Truncate((float)(r / (double)period));
        return (double)end - ((double)rf - (double)periods * (double)period);
    }
    if (!((double)time < (double)end || (double)time == (double)end || time != time)) {
        double r = (double)time - (double)end;
        float rf = (float)r;
        int periods = Truncate((float)(r / (double)period));
        return ((double)rf - (double)periods * (double)period) + (double)start;
    }
    return (double)time;
}

// FUNC_AT(0x000f7970)
void FnCycle::Eval(float previous, float time, float *out) {
    float t = (float)Wrap(time);
    float p = (float)Wrap(previous);
    AnimVCall<void>(anim, kSlotEval, p, t, out);
}

// FUNC_AT(0x000f7a50)
bool FnCycle::EvalEvent(float previous, float time, void **handlers, void *data) {
    float t = (float)Wrap(time);
    float p = (float)Wrap(previous);
    return AnimVCall<bool>(anim, kSlotEvalEvent, p, t, handlers, data);
}

// FUNC_AT(0x000f7a90)
bool FnCycle::EvalSQT(float time, float *sqt, void *mask) {
    return AnimVCall<bool>(anim, kSlotEvalSQT, (float)Wrap(time), sqt, mask);
}

// FUNC_AT(0x000f7ac0)
bool FnCycle::EvalPhase(float time, void *phase) {
    return AnimVCall<bool>(anim, kSlotEvalPhase, (float)Wrap(time), phase);
}

// FUNC_AT(0x000f7810)
FnPoseBlender* FnPoseBlender::Construct() {
    uint8_t *p = (uint8_t *)this;
    stat = 0;
    vtable = VtPoseBlender;
    *(uint32_t *)(p + 0xc) = 0;
    *(int32_t *)(p + 0x28) = -1;
    *(uint32_t *)(p + 0x14) = 0;
    *(uint32_t *)(p + 0x10) = 0;
    p[0x7d] = 0;
    p[0x7c] = 0;
    type = kPoseBlender;
    return this;
}

// FUNC_AT(0x000f7840)
FnPoseBlender* FnPoseBlender::ScalarDelete(unsigned flags) {
    FnAnim::Destruct();
    if (flags & 1)
        EaglFree(this, 0x80);
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
        EaglFree(this, 0x1c);
    return this;
}

#define MirrorPose ((void (__fastcall *)(void *, int, float *, float *, uint32_t, void *))0x000f8be0)

// FUNC_AT(0x000f78a0)
void FnPoseMirror::Eval(float previous, float time, float *out) {
    if (!enabled) {
        AnimVCall<void>(anim, kSlotEval, previous, time, out);
        return;
    }
    AnimVCall<void>(anim, kSlotEval, previous, time, pose);
    MirrorPose(skeleton, 0, pose, out, flags, NULL);
}

// FUNC_AT(0x000f78f0)
bool FnPoseMirror::EvalSQT(float time, float *sqt, void *mask) {
    if (!enabled)
        return AnimVCall<bool>(anim, kSlotEvalSQT, time, sqt, mask);
    bool any = AnimVCall<bool>(anim, kSlotEvalSQT, time, pose, mask);
    if (any)
        MirrorPose(skeleton, 0, pose, sqt, flags, mask);
    return any;
}

// FUNC_AT(0x000f7af0)
FnEventBlender* FnEventBlender::ScalarDelete(unsigned flags) {
    FnAnim::Destruct();
    if (flags & 1)
        EaglFree(this, 0x2c);
    return this;
}

// ---- FnPhaseChan and FnRawStateChan

// FUNC_AT(0x000f7b20)
FnPhaseChan* FnPhaseChan::Construct() {
    FnAnimMemoryMap::Construct();
    vtable = VtPhase;
    *(uint16_t *)((uint8_t *)this + 0x10) = 0;
    *((uint8_t *)this + 0x15) = 1;
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
        EaglFree(this, 0x18);
    return this;
}

// FUNC_AT(0x000f7b60)
FnRawStateChan* FnRawStateChan::Construct() {
    FnAnimMemoryMap::Construct();
    vtable = VtRawState;
    *(uint32_t *)((uint8_t *)this + 0x10) = 0;
    type = kRawState;
    return this;
}

// FUNC_AT(0x000f7b80)
bool FnRawStateChan::GetLength(float *length) {
    *length = (float)(int)*(uint16_t *)(anim + 4);
    return true;
}

// FUNC_AT(0x000f7ba0)
void FnRawStateChan::Eval(float previous, float time, float *out) {
    (void)previous;
    AnimVCall<bool>(this, kSlotEvalState, time, (void *)out);
}

// FUNC_AT(0x000f7bc0)
FnRawStateChan* FnRawStateChan::ScalarDelete(unsigned flags) {
    Destruct();
    if (flags & 1)
        EaglFree(this, 0x14);
    return this;
}

// FUNC_AT(0x000f7bf0)
void FnRawStateChan::Destruct() {
    vtable = VtRawState;
    FnAnimMemoryMap::Destruct();
}
