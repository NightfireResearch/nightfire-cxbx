#include "AnimEngine.h"

#include <algorithm>
#include <bit>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "../../common/xbeOverload.h"     // XbeVirtual
#include "../../helpers.h"
#include "../Scheduler.hpp"               // RegisterEvent
#include "../engine/UMemory.hpp"
#include "../platform/RealMath.h"
#include "../platform/RealPrint.h"        // MEM_copy, MEM_fill
#include "../platform/X87.h"
#include "../render/RSceneObj.hpp"
#include "../world/Trigger.h"             // TriggerEvents, gEventDynamicData
#include "../world/World.h"               // ProcAnimState, WorldInstanceFlags

#pragma fp_contract(off)

// ---------------------------------------------------------------------------------------------------------------
// RAnimEngine and its handles (AnimEngine.h). The active list is the game's (256 systems at 0x001eb878, the count
// after it); a system is on it from the stimulus that starts its animation to the Update that ends it.
// ---------------------------------------------------------------------------------------------------------------

// ---- the game's code not ported yet
// out.xyz = from + (to - from) * t
#define BlendVectors ((void (*)(float *out, const float *from, const float *to, float t))0x00022870)

// ---- the game's globals
#define ActiveSystems ((RAnimEngine::System **)0x001eb878)     // [kMaxActiveSystems]
#define ActiveSystemCount U32_AT(0x001ebc78)
#define LastUpdateFrame U32_AT(0x001ebc7c)                     // RAnimEngine::Update's

using RAnimEngine::System;
using CARP::AnimInfo;

namespace {

constexpr uint32_t kMaxActiveSystems = 0x100;
constexpr int kMaxAnimInfos = 100;               // a system's animInfoCount before its instances lower it
constexpr int kTicksPerFrame = 4;                // the systems' 60ths a frame
constexpr int kLoopEnd = 4;                      // a looping animation wraps this many 60ths before its end
constexpr int kLoopRestart = 5;                  // ... and its start moves on by its length less this
constexpr double kFramesPerSecond = 60.0;
constexpr float kSixtieth = 1.0f / 60.0f;
constexpr float kDimensionSteps[2] = { 0.25f, 16.0f };   // CARP::Instance::packedDimensions' x, by bit 30
constexpr uint32_t kDimensionMask = 0x3ff;
constexpr int kDimensionCoarseShift = 30;
constexpr int kSetEventDynamicDataSlot = 11;     // RSceneObj's vtable
constexpr int kQueueAll = 2;                     // System::ProcessStimuli's mode: queue up to four
static_assert(std::bit_cast<uint32_t>(kSixtieth) == 0x3c888889, "the original's constant");

// A tick in 60ths of a second (inlined in the game)
uint32_t FrameOf(uint32_t tick) {
    return Ftol((double)tick * TickSeconds * kFramesPerSecond);
}

// An instance's size in x, from its packed dimensions
double DimensionX(const CARP::Instance *instance) {
    uint32_t packed = instance->packedDimensions;
    return double(int32_t(packed & kDimensionMask)) * kDimensionSteps[(packed >> kDimensionCoarseShift) & 1];
}

// The instance's matrix, its rows' fourth words cleared (inlined in the game)
void CopyInstanceMatrix(const CARP::Instance *instance, MATRIX4 *out) {
    *out = instance->Matrix();
    out->mtx[0][3] = 0.0f;
    out->mtx[1][3] = 0.0f;
    out->mtx[2][3] = 0.0f;
    out->mtx[3][3] = 1.0f;
}

// __allshl(1, n): 0 from 64 on
uint64_t Bit64(uint32_t n) {
    return n < 64 ? uint64_t(1) << n : 0;
}

// RSceneObj::SetEventDynamicData, its vtable's slot 11
void SetEventDynamicData(RSceneObj *object) {
    typedef void (RSceneObj::*Method)();
    (object->*XbeVirtual<Method>(object, kSetEventDynamicDataSlot))();
}

} // namespace

// ---- the animations

// FUNC_AT(0x00076c60)
CARP::AnimInfo* AnimInfoLowerBound(CARP::AnimInfo *first, CARP::AnimInfo *last, const CARP::AnimInfo *value, int *) {
    int count = last - first;
    while (count > 0) {
        int half = count / 2;
        AnimInfo *mid = first + half;
        bool less = mid->stimulus != value->stimulus ? mid->stimulus < value->stimulus : mid->state < value->state;
        if (less) {
            first = mid + 1;
            count -= half + 1;
        } else {
            count = half;
        }
    }
    return first;
}

CARP::AnimInfo* FindAnimInfo(const WorldArticle *article, uint32_t count, uint8_t state, uint8_t stimulus) {
    AnimInfo key;
    key.stimulus = stimulus;
    key.state = state;
    AnimInfo *infos = article->animInfos;
    AnimInfo *end = infos + count;
    AnimInfo *found = AnimInfoLowerBound(infos, end, &key, NULL);
    if (found == end || found->stimulus != stimulus || found->state != state)
        return NULL;
    return found;
}

// AUTOLTCG
__declspec(naked) void FUN_000776d0() {
    __asm {
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        push eax
        push edx
        call FindAnimInfo
        add esp, 16
        ret
    }
}

// FUNC_AT(0x00076a60)
void RAnimEngine::EvaluateInstance(CARP::Instance *instance, MATRIX4 *out) {
    const WorldArticle *article = ArticleOf(instance);
    if (instance->procAnimType >= kMaxAnimInfos || article == NULL || article->animInfos == NULL ||
        article->animInfos[instance->procAnimType].keys == NULL) {
        CopyInstanceMatrix(instance, out);
        return;
    }

    // The frame the instance's time falls in, and how far into it
    const AnimInfo *info = &article->animInfos[instance->procAnimType];
    double time = double(info->frameRate * instance->procAnimIndex) * kSixtieth;
    uint32_t key = RoundToInt((float)floor(time));
    if (key >= info->frameCount)
        key = info->frameCount - 1;
    uint32_t next = key + 1;
    if (next >= info->frameCount)
        next = info->frameCount - 1;
    float t = (float)((double)(float)time - (double)key);

    alignas(16) Coord4 rotation;
    VU0_fastqslerp(&info->keys[key].rotation, &info->keys[next].rotation, &rotation, t);
    VU0_quattom4(out, &rotation);
    out->mtx[3][3] = 1.0f;
    BlendVectors(out->mtx[3], &info->keys[key].position.x, &info->keys[next].position.x, t);
    if (instance->flags & kAnimInstanceMirrored) {
        out->mtx[0][0] = -out->mtx[0][0];
        out->mtx[1][0] = -out->mtx[1][0];
        out->mtx[2][0] = -out->mtx[2][0];
        out->mtx[3][0] = -out->mtx[3][0];
    }
}

// ---- the systems

void RAnimEngine::System::Construct(Handle *owner, uint8_t systemId) {
    handle = owner;
    startFrame = 0;
    zones = 0;
    length = 0;
    queuedState = 1;
    id = systemId;
    state = 1;
    nextState = 1;
    looping = false;
    playing = 0;
    instanceCount = 0;
    animInfoCount = kMaxAnimInfos;
    unknown1C = 0;
    started = false;
    queue[3] = -1;
    queue[2] = -1;
    queue[1] = -1;
    queue[0] = -1;
    MEM_fill(instances, 0, sizeof(instances));
}

// FUNC_AT(0x00077720)
void RAnimEngine::System::ProcessStimuli(uint8_t stimulus, uint32_t frame, int mode) {
    if (playing > 0 || ActiveSystemCount >= kMaxActiveSystems) {
        // Busy: queue the stimulus if the queued state has an animation for it
        if (mode == 0)
            return;
        CARP::Instance *first = &handle->Instances()[instances[0]];
        const AnimInfo *info = FindAnimInfo(ArticleOf(first), animInfoCount, queuedState, stimulus);
        if (info == NULL)
            return;
        int slots = mode == kQueueAll ? 4 : 1;
        for (int i = 0; i < slots; i++) {
            if (queue[i] < 0) {
                queue[i] = stimulus;
                queuedState = info->nextState;
                return;
            }
        }
        return;
    }

    bool loops = false;
    uint32_t count = 0;
    uint32_t longest = 0;
    uint8_t next = 0;
    for (int i = 0; i < instanceCount; i++) {
        CARP::Instance *instance = &handle->Instances()[instances[i]];
        WorldArticle *article = ArticleOf(instance);
        const AnimInfo *info = FindAnimInfo(article, animInfoCount, state, stimulus);
        if (info == NULL || info->keys == NULL)
            continue;
        if (info->frameCount > longest)
            longest = info->frameCount;
        next = info->nextState;
        if (info->flags & AnimInfo::kLoops)
            loops = true;
        instance->procAnimType = info - article->animInfos;
        count++;
        instance->procAnimIndex = 0;
    }
    if (count == 0)
        return;

    ActiveSystems[ActiveSystemCount++] = this;
    startFrame = frame;
    lastFrame = frame - 1;
    playing = count;
    length = longest * kTicksPerFrame;
    looping = loops;
    nextState = next;
    started = false;
    if (queue[0] < 0)
        queuedState = next;
}

// FUNC_AT(0x000778f0)
void RAnimEngine::System::SetFrame(uint8_t stimulus, uint32_t time) {
    for (int i = 0; i < instanceCount; i++) {
        CARP::Instance *instance = &handle->Instances()[instances[i]];
        WorldArticle *article = ArticleOf(instance);
        if (article == NULL)
            continue;
        const AnimInfo *info = FindAnimInfo(article, animInfoCount, state, stimulus);
        if (info == NULL)
            continue;
        uint32_t frames = info->frameCount * 60 / info->frameRate;
        instance->procAnimType = info - article->animInfos;
        instance->procAnimIndex = frames > time ? time : frames;
    }
}

// FUNC_AT(0x000779c0)
void RAnimEngine::System::SetFrameRate(uint8_t stimulus, uint8_t frameRate) {
    for (int i = 0; i < instanceCount; i++) {
        CARP::Instance *instance = &handle->Instances()[instances[i]];
        WorldArticle *article = ArticleOf(instance);
        if (article == NULL)
            continue;
        AnimInfo *info = FindAnimInfo(article, animInfoCount, state, stimulus);
        if (info != NULL)
            info->frameRate = frameRate;
    }
}

// FUNC_AT(0x00077a50)
uint32_t RAnimEngine::System::NumFrames(uint8_t stimulus) {
    uint32_t longest = 0;
    for (int i = 0; i < instanceCount; i++) {
        CARP::Instance *instance = &handle->Instances()[instances[i]];
        WorldArticle *article = ArticleOf(instance);
        if (article == NULL)
            continue;
        const AnimInfo *info = FindAnimInfo(article, animInfoCount, state, stimulus);
        if (info == NULL)
            continue;
        uint32_t frames = info->frameCount * 60 / info->frameRate;
        if (frames > longest)
            longest = frames;
    }
    return longest;
}

// FUNC_AT(0x00077ed0)
uint32_t RAnimEngine::System::Update(uint32_t index, uint32_t frame) {
    if (handle->sceneObj != NULL)
        SetEventDynamicData(handle->sceneObj);

    uint32_t start = startFrame;
    uint32_t elapsed = frame - start;
    uint32_t passed;                     // the frames the last Update reached; -1 for none
    if (started) {
        passed = (lastFrame - start) / kTicksPerFrame;
    } else {
        passed = 0xffffffff;
        started = true;
    }
    lastFrame = frame;

    if (looping) {
        uint32_t loopLength = length - kLoopEnd;
        if (elapsed >= loopLength) {
            if (queue[0] >= 0) {
                index = DeactivateActiveSystem(index, frame, true);
            } else {
                elapsed %= loopLength;
                playing = instanceCount;
                startFrame = length + start - kLoopRestart;
            }
        }
    } else if (elapsed >= length && --playing == 0) {
        index = DeactivateActiveSystem(index, frame, true);
        elapsed = length;
    }

    int32_t reached = elapsed / kTicksPerFrame;
    for (int i = 0; i < instanceCount; i++) {
        CARP::Instance *instance = &handle->Instances()[instances[i]];
        if (instance->procAnimType >= animInfoCount)
            continue;
        instance->procAnimIndex = elapsed;
        const AnimInfo *info = &ArticleOf(instance)->animInfos[instance->procAnimType];
        uint32_t end = info->frameCount * kTicksPerFrame;
        if (uint16_t(elapsed) >= end)
            instance->procAnimIndex = end - 1;

        // The events of the frames passed since the last Update
        int32_t last = std::min(reached, info->frameCount - 1);
        for (uint32_t f = passed + 1; f <= uint32_t(last); f++) {
            TriggerEvents *events = info->keys[f].events;
            if (events == NULL)
                continue;
            gEventDynamicData.instance = instance;
            gEventDynamicData.unknown04 = 0;
            gEventDynamicData.instanceIndex = instances[i];
            for (uint32_t e = 0; e < uint32_t(events->count); e++) {
                TriggerEvent *event = &events->Events()[e];
                TaskCallback callback = RegisterEvent::LookupEvent(event->type);
                if (callback != NULL)
                    callback(reinterpret_cast<uintptr_t>(event->Data()));
            }
        }
    }
    return index;
}

// ---- the engine

uint32_t DeactivateActiveSystem(uint32_t index, uint32_t frame, bool next) {
    System *system = ActiveSystems[index];
    system->state = system->nextState;
    system->playing = 0;
    if (next && system->queue[0] >= 0) {
        system->ProcessStimuli(system->queue[0], frame, 0);
        system->queue[0] = system->queue[1];
        system->queue[1] = system->queue[2];
        system->queue[2] = system->queue[3];
        system->queue[3] = -1;
    }
    MEM_copy(&ActiveSystems[index], &ActiveSystems[index + 1], (ActiveSystemCount - index) * sizeof(System *));
    ActiveSystemCount--;
    return index - 1;
}

// AUTOLTCG
__declspec(naked) void RAnimEngine::DeactivateSystem() {
    __asm {
        movzx eax, byte ptr [esp + 4]
        push eax
        push ecx
        push edi
        call DeactivateActiveSystem
        add esp, 12
        ret
    }
}

// FUNC_AT(0x00078190)
void RAnimEngine::Update(uint32_t tick) {
    uint32_t frame = FrameOf(tick);
    if (frame == LastUpdateFrame)
        return;
    for (uint32_t i = 0; i < ActiveSystemCount; i++)
        i = ActiveSystems[i]->Update(i, frame);
    LastUpdateFrame = frame;
}

// ---- the handle

RAnimEngine::System *Handle::FindSystem(uint32_t id) {
    uint8_t key = id;
    uint8_t *end = systemIds + systemCount;
    uint8_t *found = SystemIdSort::LowerBound(systemIds, end, &key, NULL);
    if (found == end || *found != id)
        return NULL;
    return &systems[found - systemIds];
}

// FUNC_AT(0x00078210)
Handle* Handle::Create(uint32_t count, uint32_t tick, CARP::Instance *instances, ProcAnimState *states,
                       RSceneObj *sceneObj) {
    uint8_t ids[0x400];
    uint32_t idCount = 0;
    uint32_t stateCount = 0;
    uint32_t effectCount = 0;
    uint32_t frame = FrameOf(tick);

    // What the handle will hold: the instances' states, their articles' systems and kept effects
    for (uint32_t i = 0; i < count; i++) {
        bool mirrored = instances[i].flags & kAnimInstanceMirrored;
        if (states != NULL && (instances[i].flags & kWorldInstanceProcAnim))
            stateCount++;
        WorldArticle *article = ArticleOf(&instances[i]);
        if (article == NULL)
            continue;
        if (article->animInfos != NULL) {
            article->systemId = uint8_t(article->animInfos->systemId);    // the low bytes
            article->mirroredSystemId = uint8_t(article->animInfos->mirroredSystemId);
            ids[idCount++] = mirrored ? article->mirroredSystemId : article->systemId;
        }
        if (article->effects == NULL)
            continue;
        for (ArticleEffect *effect = article->effects; effect->type != ArticleEffect::kTypeEnd; effect++)
            if ((effect->flags & ArticleEffect::kHandleFlags) && (!mirrored || (effect->flags & ArticleEffect::kFlag200)))
                effectCount++;
    }
    SystemIdSort::Sort(ids, ids + idCount, idCount);
    uint32_t systemCount = std::unique(ids, ids + idCount) - ids;

    uint32_t size = count * sizeof(CARP::Instance) + effectCount * sizeof(ArticleEffect) +
                    stateCount * sizeof(ProcAnimState) + systemCount * sizeof(System) +
                    effectCount * (sizeof(uint16_t) + sizeof(uint8_t)) + systemCount;
    Handle *handle = static_cast<Handle *>(UMemory::FastAlloc(sizeof(Handle) + size, "RAnimEngine::Handle"));
    if (handle != NULL)
        handle = handle->Construct(instances, states, size, count, stateCount, effectCount, systemCount, ids,
                                   sceneObj);
    handle->ProcessStimuli(kStimulusStart, frame, 0);
    return handle;
}

// FUNC_AT(0x00076fe0)
Handle* Handle::Construct(CARP::Instance *instances, ProcAnimState *statesIn, uint32_t size, uint32_t count,
                          uint8_t stateCountIn, uint8_t effectCountIn, uint8_t systemCountIn,
                          const uint8_t *systemIdsIn, RSceneObj *owner) {
    dataSize = size;
    sceneObj = owner;
    instanceCount = count;
    systemCount = systemCountIn;
    effectCount = effectCountIn;
    stateCount = stateCountIn;
    effects = reinterpret_cast<ArticleEffect *>(Instances() + instanceCount);
    states = reinterpret_cast<ProcAnimState *>(effects + effectCount);
    systems = reinterpret_cast<System *>(states + stateCount);
    effectBits = reinterpret_cast<uint16_t *>(systems + systemCount);
    systemIds = EffectIds() + effectCount;
    unknown20 = 0;
    effectMask28 = 0;
    effectMask30 = 0;
    effectMask38 = 0;
    for (int i = 0; i < systemCount; i++) {
        systems[i].Construct(this, systemIdsIn[i]);
        systemIds[i] = systems[i].id;
    }

    ArticleEffect *effect = effects;
    uint8_t *effectId = EffectIds();
    uint16_t *bits = effectBits;
    uint32_t effectIndex = 0;
    uint32_t stateIndex = 0;
    CARP::BaseDesc *damageDesc = owner != NULL ? static_cast<CARP::BaseDesc *>(owner->baseDesc) : NULL;
    for (uint32_t i = 0; i < count; i++) {
        CARP::Instance *instance = &Instances()[i];
        bool mirrored = instances[i].flags & kAnimInstanceMirrored;
        *instance = instances[i];
        if (statesIn != NULL) {
            if (instances[i].flags & kWorldInstanceProcAnim) {
                instance->procAnimIndex = stateIndex;
                states[stateIndex] = statesIn[instances[i].procAnimIndex];
                stateIndex++;
            }
        } else if (instances[i].flags & kWorldInstanceProcAnim) {
            instance->flags &= ~kWorldInstanceProcAnim;
            instance->procAnimType = 0xff;
            instance->procAnimIndex = 0;
        }

        WorldArticle *article = ArticleOf(&instances[i]);
        if (article == NULL)
            continue;
        const AnimInfo *infos = article->animInfos;
        if (infos != NULL) {
            uint8_t infoCount = 0;
            while (infos[infoCount].frameRate != 0)
                infoCount++;
            System *system = FindSystem(mirrored ? infos->mirroredSystemId : infos->systemId);
            system->instances[system->instanceCount++] = i;
            system->animInfoCount = std::min(system->animInfoCount, infoCount);
            if (damageDesc != NULL) {
                int zone = damageDesc->CalcDamageZone(instances[i].position);
                system->zones |= 1 << zone;
                if (DimensionX(&instances[i]) > 1.0)
                    system->zones |= damageDesc->GetZoneBits(zone);
            }
        }

        if (article->effects == NULL)
            continue;
        for (ArticleEffect *source = article->effects; source->type != ArticleEffect::kTypeEnd; source++) {
            if (!(source->flags & ArticleEffect::kHandleFlags))
                continue;
            if (mirrored && !(source->flags & ArticleEffect::kFlag200))
                continue;
            *effect = *source;
            *effectId = source->id;
            *bits = source->bits;
            if (source->flags & ArticleEffect::kFlag08)
                effectMask28 |= Bit64(effectIndex);
            if (source->flags & ArticleEffect::kMask38Flags)
                effectMask38 |= Bit64(effectIndex);
            if (source->type == ArticleEffect::kTypeGfx)
                effectMask30 |= Bit64(effectIndex);
            if (mirrored)
                effect->instance = i;
            if (effect->instance >= count)
                effect->instance = 0;
            effect++;
            effectId++;
            bits++;
            effectIndex++;
        }
    }
    return this;
}

// FUNC_AT(0x00076810)
void Handle::OperatorDelete(Handle *handle, uint32_t size) {
    UMemory::FastFree(handle, handle->dataSize + size);
}

// FUNC_AT(0x00076830)
void Handle::SetEffectBits(uint32_t index, uint16_t bits) {
    effectBits[index] = bits;
}

// FUNC_AT(0x00076850)
ArticleEffect* Handle::FindEffectByID(uint32_t id) {
    for (uint32_t i = 0; i < effectCount; i++)
        if (EffectIds()[i] == id)
            return &effects[i];
    return NULL;
}

// FUNC_AT(0x00076890)
void Handle::InitAllSystemStates(uint8_t state) {
    for (uint32_t i = 0; i < systemCount; i++)
        systems[i].state = state;
}

// FUNC_AT(0x000768c0)
bool Handle::AnySystemPlaying() {
    if (ActiveSystemCount == 0)
        return false;
    for (uint32_t i = 0; i < systemCount; i++)
        if (systems[i].playing > 0)
            return true;
    return false;
}

// FUNC_AT(0x00076900)
uint32_t Handle::GetInstanceSystemID(uint32_t index) {
    const WorldArticle *article = ArticleOf(&Instances()[index]);
    if (article == NULL || article->animInfos == NULL)
        return 0;
    return article->animInfos->systemId;
}

// FUNC_AT(0x000774a0)
bool Handle::IsSystemPlaying(uint32_t id) {
    System *system = FindSystem(id);
    return system != NULL && system->playing > 0;
}

// FUNC_AT(0x00077500)
uint32_t Handle::GetSystemState(uint32_t id) {
    System *system = FindSystem(id);
    return system != NULL ? system->state : 0;
}

// FUNC_AT(0x00077560)
int32_t Handle::GetFirstSystemInstanceIndex(uint32_t id) {
    System *system = FindSystem(id);
    return system != NULL ? system->instances[0] : -1;
}

// FUNC_AT(0x000775c0)
int32_t Handle::GetBestSystemInstanceIndex(uint32_t id) {
    System *system = FindSystem(id);
    if (system == NULL)
        return -1;
    int best = 0;
    double bestSize = 0.0;
    for (int i = 0; i < system->instanceCount; i++) {
        const CARP::Instance *instance = &Instances()[system->instances[i]];
        double size = DimensionX(instance);
        if (size > bestSize && !(instance->flags & kWorldInstanceSceneObj)) {
            best = i;
            bestSize = size;
        }
    }
    return system->instances[best];
}

// FUNC_AT(0x000776a0)
CARP::Instance* Handle::GetFirstSystemInstance(uint32_t id) {
    int32_t index = GetFirstSystemInstanceIndex(id);
    if (index == -1)
        return NULL;
    return &Instances()[index];
}

// The list is moved down over each system taken off, one entry more than it holds.
// FUNC_AT(0x00077be0)
void Handle::Stop() {
    for (uint32_t i = 0; i < ActiveSystemCount; i++) {
        System *system = ActiveSystems[i];
        if (system->handle != this)
            continue;
        system->state = system->nextState;
        system->playing = 0;
        MEM_copy(&ActiveSystems[i], &ActiveSystems[i + 1], (ActiveSystemCount - i) * sizeof(System *));
        ActiveSystemCount--;
        i--;
    }
}

// FUNC_AT(0x00078200)
void Handle::StopThunk() {
    Stop();
}

// FUNC_AT(0x00077c50)
void Handle::SetFrame(uint32_t id, uint8_t stimulus, uint32_t time) {
    System *system = FindSystem(id);
    if (system != NULL)
        system->SetFrame(stimulus, time);
}

// FUNC_AT(0x00077cb0)
void Handle::SetFrameRate(uint32_t id, uint8_t stimulus, uint8_t frameRate) {
    System *system = FindSystem(id);
    if (system != NULL)
        system->SetFrameRate(stimulus, frameRate);
}

// FUNC_AT(0x00077d10)
uint32_t Handle::NumFrames(uint32_t id, uint8_t stimulus) {
    System *system = FindSystem(id);
    return system != NULL ? system->NumFrames(stimulus) : 0;
}

// FUNC_AT(0x00077d70)
void Handle::ProcessStimuli(uint32_t id, uint8_t stimulus, uint32_t tick, int mode) {
    uint32_t frame = FrameOf(tick);
    System *system = FindSystem(id);
    if (system != NULL)
        system->ProcessStimuli(stimulus, frame, mode);
}

// FUNC_AT(0x00077e00)
void Handle::ProcessStimuli(uint8_t stimulus, uint32_t tick, int mode) {
    uint32_t frame = FrameOf(tick);
    for (uint32_t i = 0; i < systemCount; i++)
        systems[i].ProcessStimuli(stimulus, frame, mode);
}

// FUNC_AT(0x00077e60)
void Handle::ProcessStimuliZones(uint8_t stimulus, uint16_t zones, uint32_t tick, int mode) {
    uint32_t frame = FrameOf(tick);
    for (uint32_t i = 0; i < systemCount; i++)
        if (systems[i].zones & zones)
            systems[i].ProcessStimuli(stimulus, frame, mode);
}

// ---- std::sort for the system ids (Dinkumware's, as compiled for unsigned char)

// FUNC_AT(0x00076930)
void SystemIdSort::PushHeap(uint8_t *first, int hole, int top, uint8_t value) {
    for (int index = (hole - 1) / 2; top < hole && first[index] < value; index = (hole - 1) / 2) {
        first[hole] = first[index];
        hole = index;
    }
    first[hole] = value;
}

// FUNC_AT(0x00076980)
void SystemIdSort::Rotate(uint8_t *first, uint8_t *mid, uint8_t *last, int *, uint8_t *) {
    int shift = mid - first;
    int count = last - first;
    for (int factor = shift; factor != 0;) {
        int remainder = count % factor;
        count = factor;
        factor = remainder;
    }
    if (count < last - first) {
        for (; count > 0; count--) {
            uint8_t *hole = first + count;
            uint8_t *next = hole;
            uint8_t holeValue = *hole;
            uint8_t *next1 = next + shift == last ? first : next + shift;
            while (next1 != hole) {
                *next = *next1;
                next = next1;
                next1 = shift < last - next1 ? next1 + shift : first + (shift - (last - next1));
            }
            *next = holeValue;
        }
    }
}

// FUNC_AT(0x00076bc0)
void SystemIdSort::AdjustHeap(uint8_t *first, int hole, int bottom, uint8_t value) {
    int top = hole;
    int index = 2 * hole + 2;
    for (; index < bottom; index = 2 * index + 2) {
        if (first[index] < first[index - 1])
            index--;
        first[hole] = first[index];
        hole = index;
    }
    if (index == bottom) {
        first[hole] = first[bottom - 1];
        hole = bottom - 1;
    }
    PushHeap(first, hole, top, value);
}

// FUNC_AT(0x00076c20)
uint8_t* SystemIdSort::LowerBound(uint8_t *first, uint8_t *last, const uint8_t *value, int *) {
    int count = last - first;
    while (count > 0) {
        int half = count / 2;
        uint8_t *mid = first + half;
        if (*mid < *value) {
            first = mid + 1;
            count -= half + 1;
        } else {
            count = half;
        }
    }
    return first;
}

namespace {

// _Med3: the three in order
void Median3(uint8_t *first, uint8_t *mid, uint8_t *last) {
    if (*mid < *first)
        std::swap(*mid, *first);
    if (*last < *mid)
        std::swap(*last, *mid);
    if (*mid < *first)
        std::swap(*mid, *first);
}

} // namespace

// FUNC_AT(0x00076cd0)
void SystemIdSort::Median(uint8_t *first, uint8_t *mid, uint8_t *last) {
    if (last - first > 40) {
        int step = (last - first + 1) / 8;
        Median3(first, first + step, first + 2 * step);
        Median3(mid - step, mid, mid + step);
        Median3(last - 2 * step, last - step, last);
        Median3(first + step, mid, last - step);
    } else {
        Median3(first, mid, last);
    }
}

// FUNC_AT(0x00076e00)
void SystemIdSort::MakeHeap(uint8_t *first, uint8_t *last, int *, uint8_t *) {
    int bottom = last - first;
    for (int hole = bottom / 2; hole > 0;) {
        hole--;
        AdjustHeap(first, hole, bottom, first[hole]);
    }
}

// FUNC_AT(0x00076e40)
SystemIdSort::Range* SystemIdSort::UnguardedPartition(Range *result, uint8_t *first, uint8_t *last) {
    uint8_t *mid = first + (last - first) / 2;
    Median(first, mid, last - 1);
    uint8_t *pfirst = mid;
    uint8_t *plast = pfirst + 1;
    while (first < pfirst && !(pfirst[-1] < *pfirst) && !(*pfirst < pfirst[-1]))
        pfirst--;
    while (plast < last && !(*plast < *pfirst) && !(*pfirst < *plast))
        plast++;

    uint8_t *gfirst = plast;
    uint8_t *glast = pfirst;
    for (;;) {
        for (; gfirst < last; gfirst++) {
            if (*pfirst < *gfirst)
                continue;
            if (*gfirst < *pfirst)
                break;
            std::swap(*plast++, *gfirst);
        }
        for (; first < glast; glast--) {
            if (glast[-1] < *pfirst)
                continue;
            if (*pfirst < glast[-1])
                break;
            std::swap(*--pfirst, glast[-1]);
        }
        if (glast == first && gfirst == last) {
            result->first = pfirst;
            result->second = plast;
            return result;
        }
        if (glast == first) {
            if (plast != gfirst)
                std::swap(*pfirst, *plast);
            plast++;
            std::swap(*pfirst++, *gfirst++);
        } else if (gfirst == last) {
            if (--glast != --pfirst)
                std::swap(*glast, *pfirst);
            std::swap(*pfirst, *--plast);
        } else {
            std::swap(*gfirst++, *--glast);
        }
    }
}

// FUNC_AT(0x00076f70)
void SystemIdSort::InsertionSort(uint8_t *first, uint8_t *last) {
    if (first == last)
        return;
    for (uint8_t *next = first; ++next != last;) {
        uint8_t *next1 = next + 1;
        if (*next < *first) {
            if (first != next && next != next1)
                Rotate(first, next, next1, NULL, NULL);
        } else {
            uint8_t *dest = next;
            for (uint8_t *dest0 = dest; *next < *--dest0;)
                dest = dest0;
            if (dest != next && next != next1)
                Rotate(dest, next, next1, NULL, NULL);
        }
    }
}

// FUNC_AT(0x00077b20)
void SystemIdSort::SortHeap(uint8_t *first, uint8_t *last) {
    for (; last - first > 1; last--) {
        uint8_t value = last[-1];
        last[-1] = *first;
        AdjustHeap(first, 0, last - 1 - first, value);
    }
}

// FUNC_AT(0x000780d0)
void SystemIdSort::Sort(uint8_t *first, uint8_t *last, int ideal) {
    constexpr int kInsertionSortMax = 32;
    int count;
    for (; (count = last - first) > kInsertionSortMax && ideal > 0;) {
        Range mid;
        UnguardedPartition(&mid, first, last);
        ideal /= 2;
        ideal += ideal / 2;
        if (mid.first - first < last - mid.second) {
            Sort(first, mid.first, ideal);
            first = mid.second;
        } else {
            Sort(mid.second, last, ideal);
            last = mid.first;
        }
    }
    if (count > kInsertionSortMax) {
        if (last - first > 1)
            MakeHeap(first, last, NULL, NULL);
        SortHeap(first, last);
    } else if (count > 1) {
        InsertionSort(first, last);
    }
}
