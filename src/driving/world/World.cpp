#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "World.h"

#include "CollisionManager.h"
#include "Grid.h"
#include "SoundGroup.h"                   // WSound
#include "Tree.h"
#include "TriggerManager.h"
#include "WorldPos.h"

#include <stdio.h>
#include <string.h>

#include "../../common/xbeOverload.h"
#include "../../helpers.h"
#include "../data/RCARPFile.h"
#include "../engine/CoreFoundation.h"     // NullFunction, ThrowLengthError
#include "../engine/MissionManager.h"
#include "../engine/UGroup.h"
#include "../engine/UMemory.hpp"
#include "../render/RSceneObj.hpp"

// ---------------------------------------------------------------------------------------------------------------
// WWorld (0x000d1210-0x000d2860 and its InitSingleton at 0x000595c0), ported from the listing, with the compiled
// copy of std::vector<WorldSceneObject> the linker put among its methods. The systems the world starts and stops
// are called through their ports where they exist, at their addresses otherwise.
// ---------------------------------------------------------------------------------------------------------------

// ---- the game's globals
#define TrackName ((char *)0x0023f290)                  // 0x80 bytes
#define Sim ((void *)0x00233ff0)                        // the Simulation
#define SimStepCount I32_AT(0x00234e34)
#define DefaultVector (*(const Coord4 *)0x001d4c00)     // (0, 0, 0, 1)

// ---- the game's code not ported yet
#define GLoadingScreen_Status ((void (*)(const char *))0x000e2ff0)
#define SMissionManager_Construct ((void (*)(void))0x000b8ff0)
#define SMissionManager_Destruct ((void (*)(void))0x000b9060)
#define SMissionManager_Reset ((void (__fastcall *)(void *, int))0x000b8bf0)
#define SMissionManager_Load ((void (__fastcall *)(void *, int, UData *rules, UData *missionSets))0x000b9140)
#define SMissionManager_ChangeStage ((void (__fastcall *)(void *, int, int stage))0x000b64b0)
#define SMissionManager_Start ((void (__fastcall *)(void *, int))0x000b6380)
#define AIElementController_Construct ((void (*)(UData *elements))0x000285b0)
#define AIElementController_Destruct ((void (*)(void))0x00028b60)
#define Simulation_UntrackAllInstances ((void (__fastcall *)(void *, int))0x000b30c0)
#define Simulation_TrackInstance ((void (__fastcall *)(void *, int, CARP::Instance *, int))0x000b4810)
#define RPathEngine_Reset ((void (*)(float time))0x00080330)
#define RPathEngine_Purge ((void (*)(void))0x000803e0)
#define RPathEngine_AddInstanceList ((void (*)(CARP::Instance *, ProcAnimState *, uint32_t count, float time))0x00080520)
#define RPathEngine_GetFirstPathHandle ((RPathHandle *(*)(void))0x0007ffa0)
#define RPathEngine_GetNextPathHandle ((RPathHandle *(*)(void))0x0007ffc0)
#define RPathHandle_GetPosition ((const Coord3 *(__fastcall *)(RPathHandle *, int))0x0007ff50)
#define RSceneObj_Construct ((RSceneObj *(__fastcall *)(void *, int, CARP::Instance *))0x0008eec0)
#define RSceneObj_UseArticle ((void (__fastcall *)(RSceneObj *, int, UGroup *model, int article))0x000908d0)
#define RSceneObj_SetTransform ((void (__fastcall *)(RSceneObj *, int, const MATRIX4 *))0x0008dcb0)
#define RSceneObj_EnableTarget ((void (__fastcall *)(RSceneObj *, int))0x0008f160)
#define GFX_Trigger ((void *(*)(void *effect, const float *position, const Coord4 *direction, ArticleEffect *owner, void *data, int, int, int))0x000d34e0)

namespace {

constexpr uint32_t kTagMap = 0x4d617020;            // 'Map '
constexpr uint32_t kTagWorldMap = 0x576d6170;       // 'Wmap'
constexpr uint32_t kTagAIElements = 0x4149456c;     // 'AIEl'
constexpr uint32_t kTagRules = 0x52756c65;          // 'Rule'
constexpr uint32_t kTagMissionSets = 0x4d536574;    // 'MSet'
constexpr uint32_t kTagAudio = 0x41756469;          // 'Audi'
constexpr uint32_t kTagTriggers = 0x54726772;       // 'Trgr'
constexpr uint32_t kTagInstances = 0x696e2020;      // 'in  '
constexpr uint32_t kTagProcAnims = 0x70732020;      // 'ps  '
constexpr uint32_t kTagEnviro = 0x456e7669;         // 'Envi'
constexpr uint32_t kTagAttributeName = 0x44424e20;  // 'DBN '

static_assert(sizeof(RSceneObj) == 0x40, "a scene object is 64 bytes");

char *CopyString(const char *text) {
    char *copy = static_cast<char *>(OperatorNewArray(unsigned(strlen(text) + 1)));
    strcpy(copy, text);
    return copy;
}

RSceneObj *NewSceneObj(CARP::Instance *instance) {
    void *memory = UMemory::FastAlloc(sizeof(RSceneObj), "RSceneObj");
    return memory != NULL ? RSceneObj_Construct(memory, 0, instance) : NULL;
}

void DeleteSceneObj(RSceneObj *sceneObj) {
    typedef RSceneObj *(RSceneObj::*DeletingDestructor)(unsigned flags);
    (sceneObj->*XbeVirtual<DeletingDestructor>(sceneObj, 0))(1);
}

// The simulation tracks the instance unless its model is the one it never tracks.
void TrackInstance(CARP::Instance *instance) {
    const WorldModelInfo *info = ArticleOf(instance)->model->info;
    if (info == NULL || info->id != kUntrackedModel)
        Simulation_TrackInstance(Sim, 0, instance, 0);
}

}  // namespace

// ---- the scene object vector

// FUNC_AT(0x000d1520)
uint32_t WorldSceneObjectList::Size() const {
    return first == NULL ? 0 : uint32_t(last - first);
}

// FUNC_AT(0x000d1550)
void WorldSceneObjectList::Deallocate(WorldSceneObject *block, uint32_t count) {
    if (block != NULL)
        UMemory::FastFree(block, count * sizeof(WorldSceneObject));
}

// FUNC_AT(0x000d16b0)
void WorldSceneObjectFill(WorldSceneObject *from, WorldSceneObject *to, const WorldSceneObject *value) {
    for (; from != to; from++)
        *from = *value;
}

// FUNC_AT(0x000d16e0)
WorldSceneObject** WorldSceneObjectCopyBackwardTagged(WorldSceneObject **result, WorldSceneObject *from,
                                                      WorldSceneObject *to, WorldSceneObject *destEnd) {
    while (to != from)
        *--destEnd = *--to;
    *result = destEnd;
    return result;
}

// FUNC_AT(0x000d1720)
WorldSceneObject* WorldSceneObjectUninitializedCopy(WorldSceneObject *from, WorldSceneObject *to,
                                                    WorldSceneObject *dest) {
    for (; from != to; from++, dest++)
        if (dest != NULL)
            *dest = *from;
    return dest;
}

// FUNC_AT(0x000d1a10)
WorldSceneObject** WorldSceneObjectCopyBackward(WorldSceneObject **result, WorldSceneObject *from,
                                                WorldSceneObject *to, WorldSceneObject *destEnd) {
    return WorldSceneObjectCopyBackwardTagged(result, from, to, destEnd);
}

// FUNC_AT(0x000d1a50)
void WorldSceneObjectUninitializedFill(WorldSceneObject *dest, uint32_t count, const WorldSceneObject *value) {
    for (; count != 0; count--, dest++)
        if (dest != NULL)
            *dest = *value;
}

// FUNC_AT(0x000d1a80)
WorldSceneObject* WorldSceneObjectList::Ucopy(WorldSceneObject *from, WorldSceneObject *to, WorldSceneObject *dest) {
    return WorldSceneObjectUninitializedCopy(from, to, dest);
}

// FUNC_AT(0x000d1ab0)
WorldSceneObject* WorldSceneObjectList::Ufill(WorldSceneObject *dest, uint32_t count, const WorldSceneObject *value) {
    WorldSceneObjectUninitializedFill(dest, count, value);
    return dest + count;
}

// FUNC_AT(0x000d1ae0)
void WorldSceneObjectList::Tidy() {
    if (first != NULL)
        Deallocate(first, Capacity());
    first = NULL;
    last = NULL;
    end = NULL;
}

// FUNC_AT(0x000d1ba0)
void WorldSceneObjectList::Xlen() {
    WORLD_UNTESTED("WorldSceneObjectList::Xlen");
    ThrowLengthError("vector<T> too long");
}

// _Insert_n: room made by moving the tail, or a new array half as large again (or as large as needed). Open
// reserves exactly what it pushes, so nothing reaches it.
// FUNC_AT(0x000d1c20)
void WorldSceneObjectList::InsertN(WorldSceneObject *where, uint32_t count, const WorldSceneObject *value) {
    WORLD_UNTESTED("WorldSceneObjectList::InsertN");
    WorldSceneObject held = *value;   // in case value is in the vector
    uint32_t capacity = Capacity();
    if (count == 0)
        return;
    if (kMaxSize - Size() < count) {
        Xlen();
        return;
    }
    if (capacity < Size() + count) {
        capacity = kMaxSize - capacity / 2 < capacity ? 0 : capacity + capacity / 2;
        if (capacity < Size() + count)
            capacity = Size() + count;
        WorldSceneObject *storage =
            static_cast<WorldSceneObject *>(UMemory::FastAlloc(capacity * sizeof(WorldSceneObject), "STL"));
        WorldSceneObject *at = WorldSceneObjectUninitializedCopy(first, where, storage);
        WorldSceneObjectUninitializedFill(at, count, &held);
        WorldSceneObjectUninitializedCopy(where, last, at + count);
        count += Size();
        Deallocate(first, Capacity());
        end = storage + capacity;
        last = storage + count;
        first = storage;
    } else if (uint32_t(last - where) < count) {
        Ucopy(where, last, where + count);
        Ufill(last, count - uint32_t(last - where), &held);
        last += count;
        WorldSceneObjectFill(where, last - count, &held);
    } else {
        WorldSceneObject *oldLast = last;
        last = Ucopy(oldLast - count, oldLast, oldLast);
        WorldSceneObject *copied;
        WorldSceneObjectCopyBackward(&copied, where, oldLast - count, oldLast);
        WorldSceneObjectFill(where, where + count, &held);
    }
}

// FUNC_AT(0x000d1f50)
void WorldSceneObjectList::Reserve(uint32_t count) {
    if (count > kMaxSize) {
        Xlen();
        return;
    }
    if (Capacity() < count) {
        WorldSceneObject *storage =
            static_cast<WorldSceneObject *>(UMemory::FastAlloc(count * sizeof(WorldSceneObject), "STL"));
        WorldSceneObjectUninitializedCopy(first, last, storage);
        uint32_t size = Size();
        Deallocate(first, Capacity());
        end = storage + count;
        first = storage;
        last = storage + size;
    }
}

// FUNC_AT(0x000d20a0)
WorldSceneObject** WorldSceneObjectList::Insert(WorldSceneObject **result, WorldSceneObject *where,
                                                const WorldSceneObject *value) {
    uint32_t offset = Size() == 0 ? 0 : uint32_t(where - first);
    InsertN(where, 1, value);
    *result = first + offset;
    return result;
}

// FUNC_AT(0x000d2110)
void WorldSceneObjectList::PushBack(const WorldSceneObject *value) {
    if (first != NULL && Size() < Capacity()) {
        WorldSceneObjectUninitializedFill(last, 1, value);
        last++;
    } else {
        WorldSceneObject *where;
        Insert(&where, last, value);
    }
}

// ---- the world

// FUNC_AT(0x000595c0)
void WWorld::InitSingleton() {
    void *memory = OperatorNew(sizeof(WWorld));
    fgWorld = memory != NULL ? static_cast<WWorld *>(memory)->Construct() : NULL;
}

// FUNC_AT(0x000d1210)
void WWorld::SetTrackName(const char *name, bool copy) {
    if (copy)
        strcpy(TrackName, name);
}

// directory, map, enviroCount and sceneObjects are left as they were
// FUNC_AT(0x000d1230)
WWorld* WWorld::Construct() {
    attributes.Construct("world", "default");
    fileName = NULL;
    trackName = NULL;
    attributeName = NULL;
    group = NULL;
    carp = NULL;
    instances = NULL;
    procAnims = NULL;
    instanceCount = 0;
    enviroDescs = NULL;
    void *memory = UMemory::FastAlloc(sizeof(WSoundGroup), "WSoundGroup");
    soundGroup = memory != NULL ? static_cast<WSoundGroup *>(memory)->Construct() : NULL;
    return this;
}

// FUNC_AT(0x000d12d0)
void WWorld::LoadTrackFile(const char *trackDirectory, const char *track) {
    AttributeSystemInstance->SetCollectionSection(track);
    char carpName[80];
    sprintf(carpName, "%s.crp", track);
    trackName = CopyString(track);
    fileName = CopyString(carpName);
    directory = CopyString(trackDirectory);

    RCARPFileLoader loader;
    loader.Construct();
    carp = loader.LoadCARPFile(trackDirectory, fileName, NULL, false);
    group = carp->root;
    UData *name = carp->root->DataLocateTag(kTagAttributeName);
    if (name != carp->root->DataEnd()) {
        attributeName = CopyString(reinterpret_cast<const char *>(name->Data()));
        attributes.SetName(attributeName, true);
    }
    loader.Destruct();
}

// The environment of the instance under the place; the first for an index past the end
// FUNC_AT(0x000d1470)
EnviroDesc* WWorld::GetEnviroDesc(const WWorldPos *position) {
    int index = uint16_t(instances[position->instance->renderIndex].unknown2c);
    if (index >= enviroCount)
        index = 0;
    return &enviroDescs[index];
}

// FUNC_AT(0x000d14a0)
RSceneObj* WWorld::GetSceneObjFromInstance(const CARP::Instance *instance) {
    if (instance >= instances && instance < instances + instanceCount && (instance->flags & kWorldInstanceProcAnim))
        return procAnims[instance->procAnimIndex].sceneObj;
    return NULL;
}

// FUNC_AT(0x000d14e0)
ProcAnimState* WWorld::GetProcAnimStateFromInstance(const CARP::Instance *instance) {
    if (instance >= instances && instance < instances + instanceCount && (instance->flags & kWorldInstanceProcAnim))
        return &procAnims[instance->procAnimIndex];
    return NULL;
}

// The scene object vector (Close frees it) and the sound group are left to Close.
// FUNC_AT(0x000d1610)
void WWorld::Destruct() {
    if (carp != NULL) {
        carp->Destruct();
        UMemory::FastFree(carp, sizeof(RCARPFile));
    }
    group = NULL;
    sceneObjects = NULL;
    OperatorDelete(directory);
    directory = NULL;
    OperatorDelete(fileName);
    fileName = NULL;
    OperatorDelete(trackName);
    trackName = NULL;
    OperatorDelete(attributeName);
    attributeName = NULL;
    attributes.Destruct();
}

// FUNC_AT(0x000d1750)
void WWorld::Reset() {
    UGroup *mapGroup = group->GroupLocateTag(kTagMap);
    mapGroup->DataLocateTag(kTagWorldMap);   // looked up and not used
    AIElementController_Construct(mapGroup->DataLocateTag(kTagAIElements));
    SMissionManager_Reset(glbMissionManager, 0);
    UData *rules = mapGroup->DataLocateTag(kTagRules);
    UData *missionSets = mapGroup->DataLocateTag(kTagMissionSets);
    if (missionSets == mapGroup->DataEnd())
        missionSets = NULL;
    if (rules != mapGroup->DataEnd()) {
        SMissionManager_Load(glbMissionManager, 0, rules, missionSets);
        SMissionManager_ChangeStage(glbMissionManager, 0, 0);
        SMissionManager_Start(glbMissionManager, 0);
    }
    WTriggerManager::Restart();
    WCollisionMgr::Restart();
    WGrid::Restart();
    Simulation_UntrackAllInstances(Sim, 0);

    // the scene objects made again from what Open saved
    if (sceneObjects != NULL && sceneObjects->first != sceneObjects->last) {
        for (WorldSceneObject *object = sceneObjects->first; object != sceneObjects->last; object++) {
            if (object->procAnim->sceneObj != NULL)
                DeleteSceneObj(object->procAnim->sceneObj);
            memcpy(object->procAnim, object->savedState, sizeof(object->savedState));
            CARP::Instance *instance = &instances[object->instanceIndex];
            object->procAnim->sceneObj = NewSceneObj(instance);
            RSceneObj_UseArticle(object->procAnim->sceneObj, 0, ArticleOf(instance)->model->group,
                                 object->procAnim->article);
            RSceneObj_SetTransform(object->procAnim->sceneObj, 0, object->Transform());
            if (object->targetable)
                RSceneObj_EnableTarget(object->procAnim->sceneObj, 0);
            if (object->procAnim->flags & kProcAnimTracked)
                TrackInstance(instance);
        }
    }

    RPathEngine_Reset(float(SimStepCount));

    // the effects of the instances without a proc-anim state triggered again
    for (uint32_t i = 0; i < instanceCount; i++) {
        CARP::Instance *instance = &instances[i];
        if (instance->flags & kWorldInstanceProcAnim)
            continue;
        WorldArticle *article = ArticleOf(instance);
        if (article == NULL || article->effects == NULL)
            continue;
        for (ArticleEffect *effect = article->effects; effect->type != kArticleEffectEnd; effect++) {
            if (effect->flags & kArticleEffectFlag10) {
                if (effect->type == kArticleEffectGfx && effect->reference != NULL)
                    effect->triggered = GFX_Trigger(effect->reference, instance->position, &DefaultVector, effect,
                                                    effect->unknown20, 0, 0, 0);
            } else {
                effect->triggered = NULL;
            }
        }
    }
}

// The sound group, the scene object vector's pointer and the trigger manager's are not cleared.
// FUNC_AT(0x000d1b30)
void WWorld::Close() {
    WorldSceneObjectList *list = sceneObjects;
    if (list != NULL) {
        list->Tidy();
        OperatorDelete(list);
    }
    RPathEngine_Purge();
    SMissionManager_Destruct();
    AIElementController_Destruct();
    WTriggerManager *triggers = fgTriggerManager;
    if (triggers != NULL) {
        NullFunction();   // the trigger manager's destructor, an empty function
        OperatorDelete(triggers);
    }
    WCollisionMgr::Shutdown();
    WGrid::Shutdown();
    WSoundGroup *sounds = soundGroup;
    if (sounds != NULL) {
        sounds->Destruct();
        UMemory::FastFree(sounds, sizeof(WSoundGroup));
    }
}

// FUNC_AT(0x000d21a0)
bool WWorld::Open() {
    RCARPFileLoader loader;
    loader.Construct();
    loader.ResolveCARPFile(directory, carp);
    loader.Destruct();
    GLoadingScreen_Status("Resolving misc world elements");

    UGroup *mapGroup = group->GroupLocateTag(kTagMap);
    map = reinterpret_cast<WMapHeader *>(mapGroup->DataLocateTag(kTagWorldMap)->Data());
    WTree::InitializeTree(map->root, 0, 0, 0);

    UData *rules = mapGroup->DataLocateTag(kTagRules);
    UData *missionSets = mapGroup->DataLocateTag(kTagMissionSets);
    if (missionSets == mapGroup->DataEnd())
        missionSets = NULL;
    SMissionManager_Construct();
    if (rules != mapGroup->DataEnd()) {
        SMissionManager_Load(glbMissionManager, 0, rules, missionSets);
        SMissionManager_ChangeStage(glbMissionManager, 0, 0);
        SMissionManager_Start(glbMissionManager, 0);
    }

    // the track's sounds; the lookup is compared with the root group's end, not the map group's
    int soundId = 0;
    UData *soundData = mapGroup->DataLocateTag(kTagAudio);
    if (soundData != group->DataEnd()) {
        soundGroup->Start();
        const WorldSoundRecord *records = reinterpret_cast<const WorldSoundRecord *>(soundData->Data());
        int count = int(soundData->count);
        for (; soundId < count; soundId++) {
            const WorldSoundRecord *record = &records[soundId];
            WSound *sound = soundGroup->Add(soundId, record->name);
            sound->volume = record->volume;
            sound->pitch = record->pitch;
            sound->position = record->position;
            sound->minDistance = record->unknown58;
            sound->maxDistance = record->radius;
            sound->maxDistanceSq = record->radius * record->radius;
            sound->SetUnknown118(record->unknown48, record->unknown4c);
            sound->unknown124 = record->unknown54;
            sound->unknown120 = record->unknown50;
            sound->unknown128 = record->unknown3f == 0;
        }
        soundGroup->End();
    }

    UData *triggers = mapGroup->DataLocateTag(kTagTriggers);
    WTriggerManager::Init(triggers != mapGroup->DataEnd() ? triggers : NULL);

    UData *instanceData = mapGroup->DataLocateFirst(kTagInstances, 0, 0xffffffff);
    UData *procAnimData = mapGroup->DataLocateFirst(kTagProcAnims, 0, 0xffffffff);
    instances = reinterpret_cast<CARP::Instance *>(instanceData->Data());
    instanceCount = instanceData->count;
    procAnims = NULL;
    if (procAnimData != mapGroup->DataEnd())
        procAnims = reinterpret_cast<ProcAnimState *>(procAnimData->Data());
    RPathEngine_AddInstanceList(instances, procAnims, instanceCount, float(SimStepCount));

    // a sound, and its voice, for each path whose model has one
    for (RPathHandle *path = RPathEngine_GetFirstPathHandle(); path != NULL; path = RPathEngine_GetNextPathHandle()) {
        const WorldArticle *article = ArticleOf(path->instance);
        if (article == NULL)
            continue;
        const WorldModel *model = article->model;
        if (model == NULL || model->info == NULL || model->info->sound < 0)
            continue;
        WSound *sound = soundGroup->AddPacked(soundId, model->info->sound);
        soundId++;
        sound->position = *RPathHandle_GetPosition(path, 0);
        sound->pathHandle = path;
        sound->minDistance = 10.0f;
        sound->maxDistance = 100.0f;
        sound->maxDistanceSq = 10000.0f;
        if (model->info->voice >= 0)
            sound->SetVoice(model->info->voice);
    }

    // a scene object for each proc-anim state that names an article; the effects of the other instances
    WorldSceneObjectList *list = static_cast<WorldSceneObjectList *>(OperatorNew(sizeof(WorldSceneObjectList)));
    if (list != NULL) {
        list->first = NULL;
        list->last = NULL;
        list->end = NULL;
    }
    sceneObjects = list;
    uint32_t sceneObjectCount = 0;
    for (uint32_t i = 0; i < instanceCount; i++) {
        const CARP::Instance *instance = &instances[i];
        if ((instance->flags & kWorldInstanceProcAnim) && procAnims[instance->procAnimIndex].article != kNoArticle &&
            instance->articleDesc.value != 0)
            sceneObjectCount++;
    }
    list->Reserve(sceneObjectCount);

    for (uint32_t i = 0; i < instanceCount; i++) {
        CARP::Instance *instance = &instances[i];
        if (instance->flags & kWorldInstanceProcAnim) {
            ProcAnimState *state = &procAnims[instance->procAnimIndex];
            if (state->article != kNoArticle) {
                instance->flags |= kWorldInstanceSceneObj;
                if (instance->articleDesc.value != 0) {
                    state->sceneObj = NewSceneObj(instance);
                    RSceneObj_UseArticle(state->sceneObj, 0, ArticleOf(instance)->model->group, state->article);
                    MATRIX4 transform = instance->Matrix();
                    transform.mtx[0][3] = 0.0f;
                    transform.mtx[1][3] = 0.0f;
                    transform.mtx[2][3] = 0.0f;
                    transform.mtx[3][3] = 1.0f;
                    RSceneObj_SetTransform(state->sceneObj, 0, &transform);

                    WorldSceneObject object;
                    memcpy(&object, &transform, sizeof(transform));
                    memcpy(object.savedState, state, sizeof(object.savedState));
                    object.instanceIndex = i;
                    object.procAnim = state;
                    if (instance->flags & kWorldInstanceTargetable) {
                        RSceneObj_EnableTarget(state->sceneObj, 0);
                        object.targetable = 1;
                    } else {
                        object.targetable = 0;
                    }
                    sceneObjects->PushBack(&object);
                }
            }
            if (state->flags & kProcAnimTracked)
                TrackInstance(instance);
        } else {
            WorldArticle *article = ArticleOf(instance);
            if (article == NULL || article->effects == NULL)
                continue;
            for (ArticleEffect *effect = article->effects; effect->type != kArticleEffectEnd; effect++) {
                if (effect->flags & kArticleEffectFlag10) {
                    effect->flags |= kArticleEffectFlag01;
                    if (effect->type == kArticleEffectGfx && effect->reference != NULL)
                        effect->triggered = GFX_Trigger(effect->reference, instance->position, &DefaultVector, effect,
                                                        effect->unknown20, 0, 0, 0);
                } else if (effect->type == kArticleEffect2) {
                    effect->flags |= kArticleEffectFlag01;
                }
            }
        }
    }

    UData *enviro = mapGroup->DataLocateTag(kTagEnviro);
    enviroCount = int32_t(enviro->count);
    enviroDescs = reinterpret_cast<EnviroDesc *>(enviro->Data());
    WCollisionMgr::Init(group);
    WGrid::Init(group);
    return group != NULL;
}
