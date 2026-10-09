#pragma fp_contract(off)

#include "RSceneObj.hpp"

#include <bit>
#include <string.h>

#include "Lights.h"
#include "PathEngine.h"
#include "Reflection.h"
#include "Renderer.h"
#include "RenderHigh.h"                    // fgRenderHigh
#include "RGlareManager.hpp"
#include "RPathHandle.hpp"
#include "../anim/ProcAnim.h"             // GetInstanceMatrix
#include "../data/Carp.h"
#include "../engine/StaticInit.h"
#include "../engine/UGroup.h"
#include "../engine/UMemory.hpp"
#include "../physics/PhysicsObject.h"
#include "../physics/RigidBody.h"
#include "../physics/SimpleRigidBody.h"
#include "../physics/Simulation.h"
#include "../platform/RealMath.h"
#include "../platform/RealPrint.h"        // MEM_fill
#include "../platform/X87.h"
#include "../world/CollisionManager.h"    // fgCollisionMgr
#include "../world/Render.h"              // CachedDrawInfo
#include "../world/Targeting.h"           // WTargetable
#include "../world/Tree.h"                // WTree, WMapNode, WMapHeader
#include "../world/Trigger.h"             // gEventDynamicData
#include "../world/World.h"               // fgWorld, ArticleOf, ArticleEffect, ProcAnimState
#include "../world/WorldPos.h"
#include "../../helpers.h"

// ---------------------------------------------------------------------------------------------------------------
// RSceneObj (0x0008d6b0-0x00090b90), RAutonomousObj's destructors (0x0001aa70, 0x0001b180), and the helpers the
// linker placed among them, ported from the listing.
//
// The calls into the gallery's effects (GFX), which are not ours yet, go to the originals by address. The frame's
// drawing and the effects are tested by lockstep runs, the queries, the culling walk and the transforms by
// devtools/SceneObjShadow.
// ---------------------------------------------------------------------------------------------------------------

namespace {

// A bone of the model's skeleton ('Skel', 0x20 bytes)
struct SkeletonBone {
    uint8_t unknown00[0x1c];
    uint16_t nameOffset;                // +0x1c from the skeleton's data
    uint16_t unknown1e;
};
static_assert(sizeof(SkeletonBone) == 0x20, "a bone is 32 bytes");

// CARP::Instance::flags as these functions read them
enum InstanceFlags : uint8_t {
    kInstanceSceneObj = 0x01,
    kInstanceStatic = 0x08,             // RSceneObj::kStatic
    kInstanceProcAnim = 0x10,           // its proc-anim fields are in its state
    kInstanceFarScaled = 0x41,          // either: RSceneObj::kFarScaled
    kInstanceMirrored = 0x80,
};

constexpr uint32_t kTagIn = 0x696e2020;         // 'in  '
constexpr uint32_t kTagPs = 0x70732020;         // 'ps  '
constexpr uint32_t kTagAs = 0x61732020;         // 'as  '
constexpr uint32_t kTagVd = 0x76642020;         // 'vd  '
constexpr uint32_t kTagName = 0x4e616d65;       // 'Name'
constexpr uint32_t kTagSkel = 0x536b656c;       // 'Skel'
constexpr uint32_t kTagArti = 0x41727469;       // 'Arti'
constexpr uint32_t kTagTypeMask = 0xffff0000;   // an indexed tag's type

constexpr uint32_t kNoIndex = 0xffffffff;
constexpr uint32_t kSceneObjLists = 0x200;
constexpr uint8_t kInitialBrightness = 100;
constexpr uint8_t kStimulusDamage = 0x12;       // TriggerFX kFXDamage's (the name is ours)
constexpr int kStimulusMode = 2;
constexpr uint32_t kEffectDecayTicks = 600;
constexpr uint32_t kHandleSize = 0x40;
constexpr uint32_t kCarpFileSize = 0x38;
constexpr uint32_t kTargetableSize = 0x50;
constexpr uint32_t kDimensionMask = 0x3ff;
constexpr uint32_t kDimensionsSeparate = 0x80000000;
constexpr int kDimensionCoarseShift = 30;
constexpr int kDimensionYShift = 10;
constexpr float kDimensionSteps[2] = { 0.25f, 16.0f };   // CARP::Instance::packedDimensions, by bit 30
constexpr float kDefaultRadius = 10.0f;         // for an instance with no size
constexpr float kFarScale = 0.3f;               // GetNextSceneObjCullInfo's for kFarScaled
constexpr float kShadowBrightnessScale = 1.4f;
constexpr float kFullBrightness = 2.0f;         // an article with kDescFullyLit
constexpr uint32_t kDescFullyLit = 0x2000;      // CARP::BaseDesc::flags
constexpr float kUnmovedDistance2 = 1e-5f;      // UpdatePosition: nearer than this, not refiled
constexpr float kEffectDecayDistance2 = 28900.0f;   // within 170 of the first view's camera, effects stay on
static_assert(std::bit_cast<uint32_t>(kFarScale) == 0x3e99999a && std::bit_cast<uint32_t>(kShadowBrightnessScale) == 0x3fb33333 &&
              std::bit_cast<uint32_t>(kUnmovedDistance2) == 0x3727c5ac && std::bit_cast<uint32_t>(kEffectDecayDistance2) == 0x46e1c800,
              "the original's constants");

const char kUnknownName[] = "<<unknown>>";
const char kFragmentName[] = "<<fragment1>>";

// VU0_SQTquattom4's scale: none
const Coord4 kUnitScale = { 1.0f, 1.0f, 1.0f, 1.0f };

RSceneObj_vtbl *const kSceneObjVtable = (RSceneObj_vtbl *)0x00191be0;

} // namespace

// ---- the game's code not ported yet
#define Instance_SetMatrix ((void (__fastcall *)(CARP::Instance *, int, const MATRIX4 *))0x00035610)
#define GFX_Trigger ((void *(*)(void *effect, const Coord3 *velocity, const Coord4 *origin, ArticleEffect *source, void *parameters, RSceneObj *owner, uint32_t instance, Coord3 *ownerVelocity))0x000d34e0)
#define GFX_SetIntensity ((void (*)(void *triggered, float intensity))0x000d35b0)
#define GFX_Purge ((void (*)(void *triggered))0x000d3560)
#define GFX_SuppressEffect ((void (*)(void *triggered))0x000d3590)
#define GFX_PurgeSceneObjEffects ((void (*)(RSceneObj *owner))0x000d3490)
#define CRT_atexit ((int (*)(void (*)(void)))0x00132a7b)

// ---- globals
#define Sim ((void *)0x00233ff0)                                // the Simulation
#define GameTick U32_AT(0x001f2a4c)
#define ZeroVector (*(const Coord3 *)0x00243030)
#define Origin (*(const Coord4 *)0x001d4c00)                    // (0, 0, 0, 1)
#define IdentityMatrix4 (*(const MATRIX4 *)0x001d4c10)
// This frame's draw lists (normal, and drawn last), the culling walk's cursor and the object being loaded
#define SceneObjListCapacity U32_AT(0x001f1cf0)                 // 0x200
#define DrawLastSceneObjCount U32_AT(0x001f1cf4)
#define NormalSceneObjCount U32_AT(0x001f1cf8)
#define DrawLastSceneObjs ((RSceneObj **)0x001f1d00)            // 32 of them; only the normal count is checked
#define NormalSceneObjs ((RSceneObj **)0x001f1d80)              // 0x200
#define SceneObjCullList (*(CachedDrawInfo **)0x001f2584)
#define SceneObjCullNode (*(int32_t *)0x001f2588)               // the list's visible node being walked
#define LoadingSceneObj (*(RSceneObj **)0x001f258c)             // Load's, for ResolveData
#define SceneObjCullObject (*(RSceneObj **)0x001f2590)          // the walk's last object
// InitializeLighting's static: a world position, made on the first call (bit 0 of its guard)
#define LightingWorldPos (*(WWorldPos *)0x001f25b0)
#define LightingWorldPosGuard U32_AT(0x001f25f0)

namespace {

// A transform in an instance's place: GetArticleViewDistance reads only the position, the matrix's row 3
const CARP::Instance *InstanceAt(const MATRIX4 *transform) {
    return reinterpret_cast<const CARP::Instance *>(transform);
}

// A packed dimension of an instance: ten bits in steps of 0.25, or of 16 with bit 30 set
float Dimension(uint32_t packed, int shift) {
    return float((packed >> shift) & kDimensionMask) * kDimensionSteps[(packed >> kDimensionCoarseShift) & 1];
}

// The instance's axes and position as a matrix, the rows' fourth words made 0, 0, 0, 1
void InstanceTransform(const CARP::Instance *instance, MATRIX4 *transform) {
    *transform = instance->Matrix();
    transform->mtx[0][3] = 0.0f;
    transform->mtx[1][3] = 0.0f;
    transform->mtx[2][3] = 0.0f;
    transform->mtx[3][3] = 1.0f;
}

// The map's square, as WTree::FindNode takes it: its middle and half its side
Coord4 MapBox() {
    const WMapHeader *map = fgWorld->map;
    double half = (double)map->size * 0.5;
    Coord4 box = { (float)(half + map->corner.x), map->corner.y, (float)(half + map->corner.z), (float)half };
    return box;
}

// The circle an instance is filed by: its position, its x dimension (10 if none) as the radius
void InstanceCircle(const CARP::Instance *instance, Coord4 *circle) {
    circle->w = Dimension(instance->packedDimensions, 0);
    if (circle->w == 0.0f)
        circle->w = kDefaultRadius;
}

// Half the extents of the article's bounding box
void HalfExtents(const CARP::BaseDesc *desc, Coord4 *half) {
    half->x = (float)(((double)desc->bboxMax[0] - desc->bboxMin[0]) * 0.5);
    half->y = (float)(((double)desc->bboxMax[1] - desc->bboxMin[1]) * 0.5);
    half->z = (float)(((double)desc->bboxMax[2] - desc->bboxMin[2]) * 0.5);
}

// The body of a physics object: a RigidBody, or with kSimpleBody a SimpleRigidBody (their positions are both at
// +0x10, their velocities at +0x20)
bool HasSimpleBody(const PhysicsObject *physics) {
    return (physics->flags & PhysicsObject::kSimpleBody) != 0;
}

const Coord3 *BodyPosition(const PhysicsObject *physics) {
    if (!HasSimpleBody(physics))
        return &Simulation_GetRigidBody(Sim, 0, physics->rigidBodySlot)->position;
    return &Simulation_GetSimpleRigidBody(Sim, 0, physics->rigidBodySlot)->position;
}

// The renderer's shadow brightness at a point of a collision instance, as a scene object's brightness
float ShadowBrightness(const Coord3 *position, uint16_t renderIndex) {
    return (float)(RRenderSharedData::GetShadowBrightness(*position, renderIndex) * kShadowBrightnessScale);
}

// The 64-bit masks of the handle's effects, read and written as the original's __aullshr and __allshl do (a
// shift of 64 or more gives 0)
bool EffectBit(uint64_t mask, uint32_t index) {
    uint8_t shift = uint8_t(index);
    return shift < 64 && ((mask >> shift) & 1) != 0;
}

uint64_t EffectMask(uint32_t index) {
    uint8_t shift = uint8_t(index);
    return shift < 64 ? uint64_t(1) << shift : 0;
}

// Whether the high halves of the effect's zone bits match `zones`'s: both empty, or any bit in common
bool ZonesMatch(uint32_t zones, uint16_t bits) {
    uint32_t effectZones = uint32_t(bits) << 16;
    return (zones == 0 && effectZones == 0) || (zones & effectZones) != 0;
}

// The CARP file's one copy among its users, released: freed with its last reference
void ReleaseCarpFile(RCARPFile *file) {
    if (CarpFileRefCounter::Get()->RemoveReference(file) && file != NULL) {
        file->Destruct();
        UMemory::FastFree(file, kCarpFileSize);
    }
}

// The file, loaded unless a user holds it already, and one reference more
RCARPFile *LoadCarpFile(const char *directory, const char *file, const char *label) {
    RCARPFile *loaded = CarpFileRefCounter::Get()->GetReference(file);
    if (loaded == NULL) {
        RCARPFileLoader loader;
        loader.Construct();
        loaded = loader.LoadCARPFile(directory, file, label, true);
        loader.Destruct();
    }
    return loaded;
}

void DeleteHandle(Handle *handle) {
    handle->StopThunk();
    Handle::OperatorDelete(handle, kHandleSize);
}

} // namespace

// ---- helpers the linker placed here

// FUNC_AT(0x0001aab0)
uint32_t RandomShort() {
    uint32_t product = RandomMultiplier * RandomSeed;
    RandomSeed = product & 0xffff;
    return (int32_t(product) >> 8) & 0xffff;
}

// FUNC_AT(0x0001aae0)
double RandomScaled(float scale) {
    constexpr float kOneIn65536 = 1.0f / 65536.0f;
    return double(int32_t(RandomShort())) * scale * kOneIn65536;
}

// ---- RAutonomousObj

// FUNC_AT(0x0001aa70)
void RAutonomousObj::Destruct() {
    RSceneObj::Destruct();
}

// FUNC_AT(0x0001b180)
RAutonomousObj* RAutonomousObj::Delete(unsigned flags) {
    Destruct();
    if (flags & 1)
        UMemory::FastFree(this, sizeof(RAutonomousObj));
    return this;
}

// ---- construction

// FUNC_AT(0x0008eec0)
RSceneObj* RSceneObj::Construct(CARP::Instance *instance) {
    flags = (flags & ~(kFarScaled | kStatic | kDrawLast | kVehicle)) | kVisible;
    sourceInstance = instance;
    baseDesc = NULL;
    carpFile = NULL;
    mapNode = NULL;
    renderTypeIndex = 0;
    variation = 0;
    damagedZones = 0;
    damageEffectZones = 0;
    effectDecayTime = 0;
    physics = NULL;
    animHandle = NULL;
    pathHandle = NULL;
    target = NULL;
    collisionInfo = NULL;
    name = NULL;
    vtable = kSceneObjVtable;
    brightness = kInitialBrightness;
    const float *position = instance->position;
    Coord4 circle = { position[0], position[1], position[2], 1.0f };
    InstanceCircle(instance, &circle);
    Coord4 box = MapBox();
    SetMapNode(WTree::FindNode(fgWorld->map->root, &box, &circle));
    pathHandle = RPathEngine::GetPathHandle(instance);
    return this;
}

// FUNC_AT(0x000905a0)
void RSceneObj::Destruct() {
    vtable = kSceneObjVtable;
    GFX_PurgeSceneObjEffects(this);
    if (animHandle != NULL)
        DeleteHandle(animHandle);
    animHandle = NULL;
    ReleaseCarpFile(carpFile);
    carpFile = NULL;
    if (mapNode->firstSceneObj == this)
        mapNode->firstSceneObj = next;
    if (target != NULL) {
        target->RemoveReference();
        target = NULL;
    }
    TheReflection->DeregisterSceneObj(this);
    if (next != NULL)
        next->prev = prev;
    if (prev != NULL)
        prev->next = next;
    next = reinterpret_cast<RSceneObj *>(uintptr_t(0xabababab));   // the debug heap's "no man's land"
    OperatorDelete(collisionInfo);
    collisionInfo = NULL;
}

// FUNC_AT(0x000908b0)
RSceneObj* RSceneObj::Delete(unsigned flags) {
    Destruct();
    if (flags & 1)
        UMemory::FastFree(this, sizeof(RSceneObj));
    return this;
}

// ---- loading

// FUNC_AT(0x0008d6b0)
void* RSceneObj::ResolveData(const char *name, bool *found) {
    *found = false;
    if (LoadingSceneObj == NULL)
        return NULL;
    return LoadingSceneObj->ResolveObjectDataVirtual(name, found);
}

// FUNC_AT(0x0008d6d0)
void* RSceneObj::ResolveObjectData(const char *, bool *found) {
    *found = false;
    return NULL;
}

// FUNC_AT(0x00090a60)
void RSceneObj::Load(const char *directory, const char *file, const char *label, uint32_t article) {
    LoadingSceneObj = this;
    RegisterCallback::Add(ResolveData);
    RCARPFile *loaded = LoadCarpFile(directory, file, label);
    GFX_PurgeSceneObjEffects(this);
    if (animHandle != NULL)
        DeleteHandle(animHandle);
    animHandle = NULL;
    ReleaseCarpFile(carpFile);
    carpFile = loaded;
    CarpFileRefCounter::Get()->AddReference(file, loaded);
    RegisterCallback::Remove(ResolveData);
    UseArticle(carpFile->root->GroupLocateTag(kTagArti), article);
    LoadingSceneObj = NULL;
}

// FUNC_AT(0x000904c0)
void RSceneObj::PreLoad(const char *directory, const char *file, const char *label) {
    RegisterCallback::Add(ResolveData);
    RCARPFile *loaded = LoadCarpFile(directory, file, label);
    CarpFileRefCounter::Get()->AddReference(file, loaded);
    RegisterCallback::Remove(ResolveData);
}

// FUNC_AT(0x00090560)
void RSceneObj::PurgePreloaded(const char *, const char *file) {
    ReleaseCarpFile(CarpFileRefCounter::Get()->GetReference(file));
}

// FUNC_AT(0x000908d0)
bool RSceneObj::UseArticle(UGroup *model, uint32_t article) {
    UData *list = model->DataLocateIndexed(kTagIn, int(article));
    UData *nameItem = model->DataLocateTag(kTagName);
    const char *modelName = nameItem != model->DataEnd() ? reinterpret_cast<const char *>(nameItem->Data()) : NULL;
    ProcAnimState *states = NULL;
    CARP::Instance *instances;
    uint32_t count;
    CARP::Instance single;
    if (list != NULL) {
        count = list->count;
        instances = reinterpret_cast<CARP::Instance *>(list->Data());
        UData *stateList = model->DataLocateIndexed(kTagPs, int(article));
        if (stateList != NULL)
            states = reinterpret_cast<ProcAnimState *>(stateList->Data());
        variation = uint8_t(article);
        if (count == 0)
            return false;
    } else {
        // A copy of the source instance at the origin, without a proc-anim function
        single = *Instance();
        single.flags &= ~(kInstanceSceneObj | kInstanceProcAnim);
        single.procAnimType = 0xff;
        Instance_SetMatrix(&single, 0, &IdentityMatrix4);
        count = 1;
        instances = &single;
        if (single.articleDesc.value == 0) {
            UData *first = model->DataLocateFirst(kTagAs, 0, 0xffff);
            if (first == model->DataEnd())
                return false;
            single.articleDesc.value = uint32_t(uintptr_t(first->Data()));
        }
    }
    UseInstanceList(instances, int(count), states, false, modelName);
    return true;
}

// FUNC_AT(0x00090660)
void RSceneObj::UseInstanceList(CARP::Instance *instances, int count, ProcAnimState *states, bool initialEffects,
                                const char *listName) {
    GFX_PurgeSceneObjEffects(this);
    if (animHandle != NULL)
        DeleteHandle(animHandle);
    animHandle = NULL;
    name = listName != NULL ? listName : kUnknownName;
    damagedZones = 0;
    damageEffectZones = 0;
    effectDecayTime = 0;
    OperatorDelete(collisionInfo);
    collisionInfo = NULL;
    WorldArticle *article = ArticleOf(instances);
    if (article != NULL) {
        baseDesc = article->model;
        if (reinterpret_cast<const uint8_t *>(article)[0x1e] == 1)     // the article's byte after drawPass
            flags |= kDrawLast;
    } else {
        baseDesc = NULL;
    }
    animHandle = Handle::Create(uint32_t(count), GameTick, instances, states, this);
    flags |= kFarScaled;
    CARP::Instance *copies = animHandle->Instances();
    for (uint32_t i = 0; i < animHandle->instanceCount && (flags & kFarScaled); i++) {
        if ((copies[i].flags & kInstanceFarScaled) == 0)
            flags &= ~kFarScaled;
    }
    if ((Instance()->packedDimensions & kDimensionMask) == 0) {
        Coord4 half = {};
        HalfExtents(Desc(), &half);
        Instance()->SetDimensions(false, VU0_v3length(&half), 0.0f, 0.0f);
    }
    InitEffects(initialEffects);
    PostLoadVirtual();
    if ((Instance()->flags & kInstanceStatic) == kInstanceStatic)
        flags |= kStatic;
    else
        flags &= ~kStatic;
    if (flags & kStatic)
        SetMapNode(fgWorld->map->root);
}

// FUNC_AT(0x00090820)
void RSceneObj::UseInstance(RSceneObj *source, uint32_t index, const MATRIX4 *transform, uint8_t procAnimType,
                            uint16_t procAnimIndex) {
    Handle *sourceHandle = source->animHandle;
    if (index > sourceHandle->instanceCount)
        index = 0;
    UseInstanceList(&sourceHandle->Instances()[index], 1, sourceHandle->states, true, kFragmentName);
    ProcAnimState *states = animHandle->states;
    CARP::Instance *instance = animHandle->Instances();
    Instance_SetMatrix(instance, 0, transform);
    instance->flags &= ~kInstanceSceneObj;
    if (instance->flags & kInstanceProcAnim) {
        states[instance->procAnimIndex].procAnimType = procAnimType;
        states[instance->procAnimIndex].parameter = procAnimIndex;
    } else {
        instance->procAnimType = procAnimType;
        instance->procAnimIndex = procAnimIndex;
    }
}

// FUNC_AT(0x00090a40)
void RSceneObj::Reset() {
    if (baseDesc != NULL)
        UseArticle(ModelGroup(), variation);
}

// FUNC_AT(0x0008d760)
bool RSceneObj::SetVariation(uint32_t index) {
    if (baseDesc == NULL)
        return false;
    UGroup *model = ModelGroup();
    UData *list = model->DataLocateTag(index == kNoIndex ? kTagIn : (index | (kTagIn & kTagTypeMask)));
    uint8_t *data = list != model->DataEnd() ? list->Data() : NULL;
    uint32_t count = animHandle->instanceCount;
    if (data == NULL || list->count != count)
        return false;
    const CARP::Instance *source = reinterpret_cast<const CARP::Instance *>(data);
    CARP::Instance *copies = animHandle->Instances();
    for (uint32_t i = 0; i < count; i++) {
        uint8_t procAnimType = copies[i].procAnimType;
        uint16_t procAnimIndex = copies[i].procAnimIndex;
        copies[i] = source[i];
        copies[i].procAnimType = procAnimType;
        copies[i].procAnimIndex = procAnimIndex;
    }
    variation = uint8_t(index);
    return true;
}

// ---- the scene tree and the culling

// FUNC_AT(0x0008dc00)
void RSceneObj::SetMapNode(WMapNode *node) {
    if (node == mapNode)
        return;
    if (mapNode != NULL) {
        if (mapNode->firstSceneObj == this)
            mapNode->firstSceneObj = next;
        if (next != NULL)
            next->prev = prev;
        if (prev != NULL)
            prev->next = next;
    }
    mapNode = node;
    next = node->firstSceneObj;
    prev = NULL;
    if (next != NULL)
        next->prev = this;
    mapNode->firstSceneObj = this;
}

// FUNC_AT(0x0008d830)
void RSceneObj::PrepareSceneObjsForCulling(CachedDrawInfo *list) {
    NormalSceneObjCount = 0;
    DrawLastSceneObjCount = 0;
    SceneObjCullObject = NULL;
    SceneObjCullNode = -1;
    SceneObjCullList = list;
    SceneObjListCapacity = kSceneObjLists;
}

// FUNC_AT(0x0008d860)
int RSceneObj::GetNextSceneObjCullInfo(Coord4 *sphere, float *height, bool *checkFar, float *farScale) {
    CachedDrawInfo *list = SceneObjCullList;
    if (list->nodeCount == 0)
        return -1;
    RSceneObj *object = SceneObjCullObject;
    if (object != NULL)
        object = SceneObjCullObject = object->next;
    if (object == NULL) {
        do {
            SceneObjCullNode++;
            if (SceneObjCullNode >= list->nodeCount)
                return -1;
        } while (list->nodes[SceneObjCullNode]->firstSceneObj == NULL);
        object = SceneObjCullObject = list->nodes[SceneObjCullNode]->firstSceneObj;
    }

    if (object->physics != NULL) {
        const Coord3 *position = BodyPosition(object->physics);
        sphere->x = position->x;
        sphere->y = position->y;
        sphere->z = position->z;
    } else {
        *sphere = *reinterpret_cast<const Coord4 *>(object->Instance()->position);
    }
    const CARP::Instance *instance = SceneObjCullObject->Instance();
    if ((instance->packedDimensions & kDimensionsSeparate) == kDimensionsSeparate) {
        sphere->w = Dimension(instance->packedDimensions, 0);
        *height = (float)instance->SizeY();
    } else {
        float radius = Dimension(instance->packedDimensions, 0);
        *height = radius;
        sphere->w = radius;
    }
    *checkFar = (SceneObjCullObject->flags & kStatic) == 0;
    *farScale = (SceneObjCullObject->flags & kFarScaled) ? kFarScale : 1.0f;
    return int(uintptr_t(SceneObjCullObject));
}

// FUNC_AT(0x0008f000)
void RSceneObj::SetSceneObjectCull(int item, bool culled, float) {
    if (!culled)
        reinterpret_cast<RSceneObj *>(uintptr_t(item))->SetNowVisible();
}

// FUNC_AT(0x0008dbc0)
void RSceneObj::SetNowVisible() {
    if ((flags & kVisible) == 0 || NormalSceneObjCount >= SceneObjListCapacity)
        return;
    if (flags & kDrawLast)
        DrawLastSceneObjs[DrawLastSceneObjCount++] = this;
    else
        NormalSceneObjs[NormalSceneObjCount++] = this;
}

// FUNC_AT(0x0008d9f0)
void RSceneObj::RenderAllNormal() {
    TheReflection->ResetSceneObjDistances();
    uint32_t count = NormalSceneObjCount;
    for (uint32_t i = 0; i < count; i++)
        NormalSceneObjs[i]->RenderVirtual();
}

// FUNC_AT(0x0008da30)
void RSceneObj::RenderAllDrawLast() {
    uint32_t count = DrawLastSceneObjCount;
    for (uint32_t i = 0; i < count; i++)
        DrawLastSceneObjs[i]->RenderVirtual();
}

// FUNC_AT(0x0008da60)
void RSceneObj::RenderAllDeferredEffects() {
    uint32_t count = DrawLastSceneObjCount;
    for (uint32_t i = 0; i < count; i++)
        DrawLastSceneObjs[i]->RenderDeferredEffectsVirtual();
}

// FUNC_AT(0x0008da90)
void RSceneObj::DestroySceneObjectList(WMapNode *node) {
    RSceneObj *object = node->firstSceneObj;
    while (object != NULL) {
        RSceneObj *following = object->next;
        object->DeleteVirtual(1);
        object = following;
    }
    node->firstSceneObj = NULL;
}

// FUNC_AT(0x0008dac0)
void RSceneObj::DestroyAll() {
    WTree::WalkTree(fgWorld->map->root, DestroySceneObjectList);
}

// FUNC_AT(0x0008f050)
void RSceneObj::InitializeLighting() {
    if (Desc()->flags & kDescFullyLit) {
        SetBrightness(kFullBrightness);
        return;
    }
    if ((LightingWorldPosGuard & 1) == 0) {
        LightingWorldPosGuard |= 1;
        LightingWorldPos.Construct();
        CRT_atexit(DestroyStatic_001f25b0);
    }
    const float *instancePosition = Instance()->position;
    Coord3 position = { instancePosition[0], instancePosition[1], instancePosition[2] };
    LightingWorldPos.FindClosestFace(&position, true);
    float level;
    if (LightingWorldPos.valid && LightingWorldPos.instance != NULL)
        level = ShadowBrightness(&position, LightingWorldPos.instance->renderIndex);
    else
        level = fgLightManager->lightMapLightingBias;
    SetBrightness(level);
}

// FUNC_AT(0x0008fac0)
void RSceneObj::InitializeSceneObjectListLighting(WMapNode *node) {
    for (RSceneObj *object = node->firstSceneObj; object != NULL; object = object->next)
        object->InitializeLighting();
}

// FUNC_AT(0x0008fae0)
void RSceneObj::InitializeAllLighting() {
    WTree::WalkTree(fgWorld->map->root, InitializeSceneObjectListLighting);
}

// ---- drawing

// FUNC_AT(0x0008f9f0)
void RSceneObj::Render() {
    MATRIX4 transform;
    InstanceTransform(Instance(), &transform);
    SetProcAnimStateVirtual();
    double distance = GetArticleViewDistance(InstanceAt(&transform), Dimension(Instance()->packedDimensions, 0));
    float viewDistance = (float)distance;
    if (distance < TheReflection->farthestDistance)
        TheReflection->PrivateSubmitSceneObj(viewDistance, this);
    DrawInstanceList(animHandle->Instances(), &transform, animHandle->states, animHandle->instanceCount, viewDistance,
                     this);
    RenderEffects(false);
}

// FUNC_AT(0x0008f930)
void RSceneObj::RenderSimple() {
    MATRIX4 transform;
    InstanceTransform(Instance(), &transform);
    SetProcAnimStateVirtual();
    uint32_t count = animHandle->instanceCount;
    ProcAnimState *states = animHandle->states;
    CARP::Instance *instances = animHandle->Instances();
    float distance = (float)GetArticleViewDistance(InstanceAt(&transform), Dimension(Instance()->packedDimensions, 0));
    DrawInstanceList(instances, &transform, states, count, distance, this);
    RenderEffects(true);
}

// FUNC_AT(0x0008ed50)
double RSceneObj::GetViewDistance() {
    MATRIX4 transform;
    InstanceTransform(Instance(), &transform);
    return GetArticleViewDistance(InstanceAt(&transform), Dimension(Instance()->packedDimensions, 0));
}

// FUNC_AT(0x0008db50)
void* RSceneObj::GetViewDrawList(uint32_t index, uint32_t *count) {
    UData *list = ModelGroup()->DataLocateTag(index == kNoIndex ? kTagVd : (index | (kTagVd & kTagTypeMask)));
    if (list == ModelGroup()->DataEnd())
        return NULL;
    *count = list->count;
    return list->Data();
}

// ---- position

// FUNC_AT(0x0008dc60)
void RSceneObj::GetTransform(MATRIX4 *transform) {
    InstanceTransform(Instance(), transform);
}

// FUNC_AT(0x0008dc90)
void RSceneObj::GetPosition(Coord3 *position) {
    const float *instancePosition = Instance()->position;
    position->x = instancePosition[0];
    position->y = instancePosition[1];
    position->z = instancePosition[2];
}

// FUNC_AT(0x0008dcb0)
void RSceneObj::SetTransform(const MATRIX4 *transform) {
    if (physics != NULL)
        return;
    Instance_SetMatrix(Instance(), 0, transform);
    UpdatePositionVirtual(true);
}

// FUNC_AT(0x0008dce0)
float RSceneObj::GetRenderOffset() {
    return 0.0f;
}

// FUNC_AT(0x0008f270)
void RSceneObj::UpdatePosition(bool update) {
    if (!update)
        return;
    MATRIX4 transform;
    Coord4 *position = MatrixRow(&transform, 3);
    if (physics != NULL) {
        if (!HasSimpleBody(physics)) {
            RigidBody *body = Simulation_GetRigidBody(Sim, 0, physics->rigidBodySlot);
            MatrixCopy(&body->info->orientation, &transform);
            position->x = body->position.x;
            position->y = body->position.y;
            position->z = body->position.z;
            double offset = GetRenderOffsetVirtual();
            float scale = (float)offset;
            if (offset != 0.0) {
                // Down the body's up axis by the offset
                const Coord4 *up = MatrixRow(&transform, 1);
                Coord4 lift = { up->x, up->y, up->z, 0.0f };
                VU0_v4scale(&lift, scale, &lift);
                VU0_v4sub(position, &lift, position);
            }
        } else {
            SimpleRigidBody *body = Simulation_GetSimpleRigidBody(Sim, 0, physics->rigidBodySlot);
            body->RecalcOrientMat(&transform);
            position->x = body->position.x;
            position->y = body->position.y;
            position->z = body->position.z;
        }
        bool unmoved = VU0_v3distancesquare(Instance()->position, position) < kUnmovedDistance2;
        Instance_SetMatrix(Instance(), 0, &transform);
        if (unmoved)
            return;
    } else {
        InstanceTransform(Instance(), &transform);
    }
    if (flags & kStatic)
        return;
    InstanceCircle(Instance(), position);
    Coord4 box = MapBox();
    SetMapNode(WTree::FindNode(fgWorld->map->root, mapNode, &box, position));
    if (physics != NULL && physics->worldPos.valid && physics->worldPos.instance != NULL) {
        uint16_t renderIndex = physics->worldPos.instance->renderIndex;
        SetBrightness(ShadowBrightness(BodyPosition(physics), renderIndex));
    }
}

// FUNC_AT(0x0008f580)
void RSceneObj::SetPhysics(PhysicsObject *newPhysics) {
    if (physics == newPhysics)
        return;
    if (physics != NULL) {
        // The instance left where the body last was
        const Coord4 *orientation;
        const Coord3 *bodyPosition;
        if (!HasSimpleBody(physics)) {
            RigidBody *body = Simulation_GetRigidBody(Sim, 0, physics->rigidBodySlot);
            bodyPosition = &body->position;
            orientation = &body->orientation;
        } else {
            SimpleRigidBody *body = Simulation_GetSimpleRigidBody(Sim, 0, physics->rigidBodySlot);
            bodyPosition = &body->position;
            orientation = &body->orientation;
        }
        Coord4 position = { bodyPosition->x, bodyPosition->y, bodyPosition->z, 1.0f };
        Coord4 rotation = *orientation;
        MATRIX4 transform;
        VU0_SQTquattom4(&kUnitScale, &rotation, &position, &transform);
        Instance_SetMatrix(Instance(), 0, &transform);
    }
    physics = newPhysics;
    if (target != NULL) {
        target->RemoveReference();
        target = NULL;
        EnableTarget();
    }
}

// FUNC_AT(0x0008e1a0)
Coord3* RSceneObj::GetVelocity() {
    if (physics != NULL) {
        if (!HasSimpleBody(physics))
            return &Simulation_GetRigidBody(Sim, 0, physics->rigidBodySlot)->velocity;
        return &Simulation_GetSimpleRigidBody(Sim, 0, physics->rigidBodySlot)->velocity;
    }
    if (pathHandle != NULL)
        return &pathHandle->velocity;
    return NULL;
}

// FUNC_AT(0x0008f160)
void RSceneObj::EnableTarget() {
    if (target != NULL)
        return;
    if (physics != NULL) {
        void *memory = UMemory::FastAlloc(kTargetableSize, "WTargetable");
        target = memory != NULL ? static_cast<WTargetable *>(memory)->Construct(physics) : NULL;
        return;
    }
    MATRIX4 transform;
    InstanceTransform(Instance(), &transform);
    float rotation[9];
    Coord4 position;
    ExtractRotTrans(&transform, rotation, &position);
    void *memory = UMemory::FastAlloc(kTargetableSize, "WTargetable");
    target = memory != NULL ? static_cast<WTargetable *>(memory)->Construct(position.x, position.y, position.z) : NULL;
}

// FUNC_AT(0x0008dcf0)
uint32_t RSceneObj::GetInstanceSystemID(uint32_t index) {
    if (index >= animHandle->instanceCount)
        return 0;
    const CARP::Instance *instance = &animHandle->Instances()[index];
    const WorldArticle *article = ArticleOf(instance);
    return (instance->flags & kInstanceMirrored) ? article->mirroredSystemId : article->systemId;
}

// The instance's frame through its proc-anim function (with the matrix's row 3 as its offset), times `frame`
static uint32_t PlaceInstance(RSceneObj *object, uint32_t index, MATRIX4 *frame) {
    if (index >= object->animHandle->instanceCount)
        return 0;
    CARP::Instance *instance = &object->animHandle->Instances()[index];
    object->SetProcAnimStateVirtual();
    MATRIX4 placed;
    GetInstanceMatrix(instance, MatrixRow(frame, 3), &placed, object->animHandle->states);
    VU0_MATRIX4_mult(frame, &placed, frame);
    const WorldArticle *article = ArticleOf(instance);
    return (instance->flags & kInstanceMirrored) ? article->mirroredSystemId : article->systemId;
}

// FUNC_AT(0x0008dd20)
uint32_t RSceneObj::GetInstancePosition(uint32_t index, MATRIX4 *transform, bool world) {
    if (world)
        InstanceTransform(Instance(), transform);
    else
        VU0_MATRIX4Init(transform);
    uint32_t id = PlaceInstance(this, index, transform);
    if (world)
        TransformMatrixToWorldSpaceVirtual(transform);
    return id;
}

// FUNC_AT(0x0008ddf0)
uint32_t RSceneObj::TransformPointByInstancePosition(uint32_t index, Coord4 *point, bool world) {
    MATRIX4 frame;
    if (world)
        InstanceTransform(Instance(), &frame);
    else
        VU0_MATRIX4Init(&frame);
    uint32_t id = PlaceInstance(this, index, &frame);
    VU0_MATRIX4_vect4mult(point, &frame, point);
    if (world)
        TransformPointToWorldSpaceVirtual(point);
    return id;
}

// FUNC_AT(0x0008dee0)
uint32_t RSceneObj::TransformMatrixByInstancePosition(uint32_t index, MATRIX4 *transform, bool world) {
    MATRIX4 frame;
    if (world)
        InstanceTransform(Instance(), &frame);
    else
        VU0_MATRIX4Init(&frame);
    uint32_t id = PlaceInstance(this, index, &frame);
    VU0_MATRIX4_mult(transform, transform, &frame);
    if (world)
        TransformMatrixToWorldSpaceVirtual(transform);
    return id;
}

// FUNC_AT(0x0008edd0)
CARP::Instance* RSceneObj::ConvertInstanceToLocal(CARP::Instance *instance) {
    if (ArticleOf(instance) == NULL)
        return instance;
    UGroup *model = ArticleOf(instance)->model->group;
    for (UData *list = model->DataLocateFirst(kTagIn, 0, kNoIndex); list != model->DataEnd(); list++) {
        if (list->MatchTag() != kTagIn)
            return instance;
        CARP::Instance *first = reinterpret_cast<CARP::Instance *>(list->Data());
        if (first <= instance && instance < first + list->count)
            return &animHandle->Instances()[instance - first];
    }
    return instance;
}

// ---- size and collision

// FUNC_AT(0x0008dfd0)
void RSceneObj::GetBoundingDimensions(Coord4 *halfExtents) {
    HalfExtents(Desc(), halfExtents);
}

// FUNC_AT(0x0008e010)
float RSceneObj::ComputeBoundingRadius() {
    Coord4 half = {};
    HalfExtents(Desc(), &half);
    return VU0_v3length(&half);
}

// FUNC_AT(0x0008f500)
float RSceneObj::GetBoundingRadius() {
    if ((Instance()->packedDimensions & kDimensionsSeparate) == kDimensionsSeparate)
        return ComputeBoundingRadius();
    return Dimension(Instance()->packedDimensions, 0);
}

// FUNC_AT(0x0008f550)
void RSceneObj::ScaleBoundingRadius(float scale) {
    float radius = ComputeBoundingRadius() * scale;
    Instance()->SetDimensions(false, radius, 0.0f, 0.0f);
}

// FUNC_AT(0x0008e060)
void* RSceneObj::GetCollisionGeometry(uint32_t *count) {
    *count = Desc()->numCollisionPrims;
    return &Desc()->collisionGeometry;
}

// FUNC_AT(0x0008e080)
uint32_t RSceneObj::GetNumBones() {
    UGroup *model = ModelGroup();
    UData *skeleton = model->DataLocateTag(kTagSkel);
    return skeleton != model->DataEnd() ? skeleton->count : 0;
}

// FUNC_AT(0x0008e0c0)
int RSceneObj::GetBoneIndex(const char *boneName) {
    UGroup *model = ModelGroup();
    UData *skeleton = model->DataLocateTag(kTagSkel);
    if (skeleton == model->DataEnd())
        return 0;
    const uint8_t *data = skeleton->Data();
    const SkeletonBone *bones = reinterpret_cast<const SkeletonBone *>(data);
    for (uint32_t i = 0; i < skeleton->count; i++) {
        if (strcmp(boneName, reinterpret_cast<const char *>(data + bones[i].nameOffset)) == 0)
            return int(i + 1);
    }
    return 0;
}

// FUNC_AT(0x0008e1f0)
void RSceneObj::AddCollisionInfo(const Coord3 *centre, const Coord3 *extents) {
    uint32_t tick = GameTick;
    if (collisionInfo == NULL) {
        SceneObjCollisionInfo *info = static_cast<SceneObjCollisionInfo *>(OperatorNew(sizeof(SceneObjCollisionInfo)));
        if (info != NULL)
            MEM_fill(info, 0, sizeof(SceneObjCollisionInfo));
        collisionInfo = info;
        collisionInfo->tick = tick;
    }
    SceneObjCollisionInfo *info = collisionInfo;
    if (tick != info->tick) {
        info->centreSum = *centre;
        info->extentsSum = *extents;
        info->count = 1;
        info->tick = tick;
        return;
    }
    info->count++;
    info->centreSum.x = info->centreSum.x + centre->x;
    info->centreSum.y = centre->y + info->centreSum.y;
    info->centreSum.z = centre->z + info->centreSum.z;
    info->extentsSum.x = info->extentsSum.x + extents->x;
    info->extentsSum.y = extents->y + info->extentsSum.y;
    info->extentsSum.z = extents->z + info->extentsSum.z;
}

// FUNC_AT(0x0008e2d0)
void RSceneObj::GetCollisionInfo(Coord4 *centre, Coord4 *extents) {
    if (collisionInfo != NULL) {
        if (GameTick == collisionInfo->tick) {
            if (collisionInfo->count > 0) {
                double inverse = 1.0 / double(collisionInfo->count);
                centre->x = collisionInfo->centreSum.x;
                centre->y = collisionInfo->centreSum.y;
                centre->z = collisionInfo->centreSum.z;
                centre->w = 1.0f;
                centre->x = (float)(inverse * centre->x);
                centre->y = (float)(inverse * centre->y);
                centre->z = (float)(inverse * centre->z);
                extents->x = collisionInfo->extentsSum.x;
                extents->y = collisionInfo->extentsSum.y;
                extents->z = collisionInfo->extentsSum.z;
                extents->w = 0.0f;
                return;
            }
        } else {
            OperatorDelete(collisionInfo);
            collisionInfo = NULL;
        }
    }
    const float *position = Instance()->position;
    centre->x = position[0];
    centre->y = position[1];
    centre->z = position[2];
    centre->w = 1.0f;
    const Coord3 *velocity = GetVelocityVirtual();
    if (velocity != NULL) {
        extents->x = velocity->x;
        extents->y = velocity->y;
        extents->z = velocity->z;
        extents->w = 1.0f;
    } else {
        *extents = Origin;
    }
}

// ---- effects

// The original asks for the zone's spread and then sets the effect's bits to 0 (which they already are).
// FUNC_AT(0x0008f690)
void RSceneObj::InitEffects(bool initial) {
    uint32_t count = animHandle->effectCount;
    ArticleEffect *effect = animHandle->effects;
    uint16_t *bits = animHandle->effectBits;
    for (uint32_t i = 0; i < count; i++, effect++) {
        if (EffectBit(animHandle->effectMask28, i)) {
            if (bits[i] == 0) {
                Desc()->GetZoneBits(Desc()->CalcDamageZone(&effect->position.x));
                animHandle->SetEffectBits(i, 0);
            }
            continue;
        }
        bool on = (effect->flags & ArticleEffect::kStartsOn) != 0;
        if (initial)
            on |= (effect->flags & ArticleEffect::kStartsOnInitial) != 0;
        if (on)
            ToggleEffect(true, i, effect, -1.0f);
    }
}

// FUNC_AT(0x0008e430)
void RSceneObj::ToggleEffect(bool on, uint32_t index, ArticleEffect *effect, float intensity) {
    Handle *handle = animHandle;
    if (on) {
        if (EffectBit(handle->effectsOn, index))
            return;
        if (!EffectBit(handle->effectMask30, index)) {
            handle->effectsOn |= EffectMask(index);
            return;
        }
        if (effect->gfx.triggered == NULL) {
            Coord3 *velocity = GetVelocityVirtual();
            effect->gfx.triggered = GFX_Trigger(effect->gfx.reference, &ZeroVector, &Origin, effect,
                                                &effect->gfx.rotation, this, effect->instance, velocity);
            if (effect->gfx.triggered != NULL)
                animHandle->SetEffectOn(index);
        }
        if (!(intensity >= 0.0f) || effect->gfx.triggered == NULL)
            return;
        GFX_SetIntensity(effect->gfx.triggered, intensity);
        return;
    }
    if (!EffectBit(handle->effectsOn, index))
        return;
    handle->effectsOn &= ~EffectMask(index);
    if (EffectBit(animHandle->effectMask30, index) && effect->gfx.triggered != NULL) {
        GFX_Purge(effect->gfx.triggered);
        effect->gfx.triggered = NULL;
    }
}

// The effects stay on near the first view's camera; elsewhere the gallery's go off.
// FUNC_AT(0x0008e580)
void RSceneObj::DecayEffects() {
    const float *instancePosition = Instance()->position;
    Coord4 position = { instancePosition[0], instancePosition[1], instancePosition[2], 0.0f };
    if (VU0_v3distancesquare(&position, MatrixRow(&fgRenderHigh->views[0].camera->matrix, 3)) < kEffectDecayDistance2) {
        effectDecayTime = GameTick + kEffectDecayTicks;
        return;
    }
    if (animHandle->effectMask30 != 0 && animHandle->effectsOn != 0) {
        uint32_t count = animHandle->effectCount;
        ArticleEffect *effect = animHandle->effects;
        for (uint32_t i = 0; i < count; i++, effect++) {
            Handle *handle = animHandle;
            if (EffectBit(handle->effectsOn, i) && EffectBit(handle->effectMask30, i) &&
                (effect->flags & ArticleEffect::kPermanent) == 0)
                ToggleEffect(false, i, effect, -1.0f);
        }
    }
    if (animHandle->effectsOn == 0)
        effectDecayTime = 0;
}

// FUNC_AT(0x0008e6a0)
void RSceneObj::TriggerEffects(bool on, bool byId, uint32_t which, float intensity) {
    if (animHandle->effectMask38 == 0)
        return;
    if (byId) {
        ArticleEffect *effect = animHandle->FindEffectByID(which);
        if (effect != NULL)
            ToggleEffect(on, uint32_t(effect - animHandle->effects), effect, -1.0f);
    } else {
        uint32_t count = animHandle->effectCount;
        ArticleEffect *effect = animHandle->effects;
        uint16_t *bits = animHandle->effectBits;
        uint32_t zones = which & kTagTypeMask;
        for (uint32_t i = 0; i < count; i++, effect++) {
            Handle *handle = animHandle;
            if (EffectBit(handle->effectMask38, i) && !EffectBit(handle->effectMask28, i) && ZonesMatch(zones, bits[i]))
                ToggleEffect(on, i, effect, intensity);
        }
    }
    effectDecayTime = animHandle->effectsOn != 0 ? GameTick + kEffectDecayTicks : 0;
}

// FUNC_AT(0x0008e7d0)
void RSceneObj::SuppressEffects(bool byId, uint32_t which) {
    if (animHandle->effectMask38 == 0)
        return;
    if (byId) {
        ArticleEffect *effect = animHandle->FindEffectByID(which);
        if (effect != NULL && effect->type == ArticleEffect::kTypeGfx && effect->gfx.triggered != NULL)
            GFX_SuppressEffect(effect->gfx.triggered);
        return;
    }
    uint32_t count = animHandle->effectCount;
    ArticleEffect *effect = animHandle->effects;
    uint16_t *bits = animHandle->effectBits;
    uint32_t zones = which & kTagTypeMask;
    for (uint32_t i = 0; i < count; i++, effect++) {
        Handle *handle = animHandle;
        if (EffectBit(handle->effectMask38, i) && !EffectBit(handle->effectMask28, i) &&
            EffectBit(handle->effectMask30, i) && ZonesMatch(zones, bits[i]) && effect->gfx.triggered != NULL)
            GFX_SuppressEffect(effect->gfx.triggered);
    }
}

// FUNC_AT(0x0008e900)
void RSceneObj::DamageEffects(uint32_t zones) {
    if (animHandle->effectMask28 == 0)
        return;
    uint32_t count = animHandle->effectCount;
    ArticleEffect *effect = animHandle->effects;
    uint16_t *bits = animHandle->effectBits;
    for (uint32_t i = 0; i < count; i++, effect++) {
        if (!EffectBit(animHandle->effectMask28, i))
            continue;
        bool on = (zones & bits[i]) != 0;
        ToggleEffect(on, i, effect, -1.0f);
        // A gallery effect that did not start is still marked on
        if (on && EffectBit(animHandle->effectMask30, i) && effect->gfx.triggered == NULL)
            animHandle->effectsOn |= EffectMask(i);
    }
    effectDecayTime = animHandle->effectsOn != 0 ? GameTick + kEffectDecayTicks : 0;
}

// FUNC_AT(0x0008ea70)
void RSceneObj::TriggerFX(int type, uint32_t which, uint32_t, uint32_t, uint32_t, float intensity) {
    switch (type) {
    case kFXOn:
    case kFXOnById:
        TriggerEffects(true, type == kFXOnById, which, intensity);
        break;
    case kFXOff:
    case kFXOffById:
        TriggerEffects(false, type == kFXOffById, which, intensity);
        break;
    case kFXSuppress:
    case kFXSuppressById:
        SuppressEffects(type == kFXSuppressById, which);
        break;
    case kFXDamage: {
        uint32_t fresh = which & ~uint32_t(damagedZones);
        if (fresh != 0) {
            animHandle->ProcessStimuliZones(kStimulusDamage, uint16_t(fresh), GameTick, kStimulusMode);
            damagedZones = uint16_t(which);
        }
        break;
    }
    case kFXDamageEffects:
        if ((which & ~uint32_t(damageEffectZones)) != 0) {
            DamageEffects(damageEffectZones | which);
            damageEffectZones |= uint16_t(which);
        }
        break;
    }
}

// FUNC_AT(0x0008eb60)
void RSceneObj::StopFX() {
    if (animHandle->effectsOn == 0)
        return;
    uint32_t count = animHandle->effectCount;
    ArticleEffect *effect = animHandle->effects;
    for (uint32_t i = 0; i < count; i++, effect++) {
        Handle *handle = animHandle;
        if (!EffectBit(handle->effectsOn, i))
            continue;
        handle->effectsOn &= ~EffectMask(i);
        if (EffectBit(animHandle->effectMask30, i) && effect->gfx.triggered != NULL) {
            GFX_Purge(effect->gfx.triggered);
            effect->gfx.triggered = NULL;
        }
    }
}

// FUNC_AT(0x0008ec20)
ArticleEffect* RSceneObj::LookupFXHandle(const char *locatorName, int instance) {
    uint32_t count = animHandle->effectCount;
    ArticleEffect *effect = animHandle->effects;
    for (uint32_t i = 0; i < count; i++, effect++) {
        if (effect->type == ArticleEffect::kTypeLocator && effect->locator.instance == instance &&
            strcmp(effect->locator.name, locatorName) == 0)
            return effect;
    }
    return NULL;
}

// FUNC_AT(0x000143e0)
void RSceneObj::SetBrightness(float level) {
    double clamped = level;
    if (clamped > 1.0)
        clamped = 1.0;
    else if (clamped < 0.0)
        clamped = 0.0;
    brightness = uint8_t(Ftol(clamped * 255.0));
}

// FUNC_AT(0x0008eca0)
void RSceneObj::LocateFX(const ArticleEffect *effect, MATRIX4 *transform, bool world) {
    if (effect->type == ArticleEffect::kTypeLocator) {
        VU0_SQTquattom4(&kUnitScale, &effect->locator.rotation, &effect->position, transform);
    } else if (effect->type == ArticleEffect::kTypeGfx) {
        VU0_SQTquattom4(&kUnitScale, &effect->gfx.rotation, &effect->position, transform);
    } else {
        VU0_MATRIX4Init(transform);
        Coord4 *position = MatrixRow(transform, 3);
        position->x = effect->position.x;
        position->y = effect->position.y;
        position->z = effect->position.z;
        position->w = 1.0f;
    }
    TransformMatrixByInstancePosition(effect->instance, transform, world);
    if (effect->flags & ArticleEffect::kOnGround) {
        Coord4 *position = MatrixRow(transform, 3);
        if (!fgCollisionMgr->GetWorldHeightAtPoint(reinterpret_cast<Coord3 *>(position), &position->y, true))
            position->y = 0.0f;
    }
}

// FUNC_AT(0x0008ea20)
bool RSceneObj::RenderEffect(ArticleEffect *effect, MATRIX4 *transform) {
    switch (effect->type) {
    case ArticleEffect::kTypeGlare:
        TheGlareManager->AddModelGlare(reinterpret_cast<Glare *>(effect), transform, 0.0f);
        return true;
    case ArticleEffect::kTypeLight:
        fgLightManager->highLevel.AddDynamicLightEffect(effect, transform);
        return true;
    case ArticleEffect::kTypeGfx:
        return true;
    default:
        return effect->type == ArticleEffect::kTypeSpring;
    }
}

// The effects switched on, each at its instance's frame (made once per instance); an effect whose instance's
// system has stopped goes off.
// FUNC_AT(0x0008f740)
void RSceneObj::RenderEffects(bool glaresOnly) {
    if (animHandle->effectsOn == 0)
        return;
    if (effectDecayTime != 0) {
        if (GameTick >= effectDecayTime)
            DecayEffects();
        else
            effectDecayTime = GameTick + kEffectDecayTicks;
    }
    uint32_t count = animHandle->effectCount;
    ArticleEffect *effect = animHandle->effects;
    int32_t lastInstance = -1;
    bool placed = false;
    bool systemPlaying = false;
    MATRIX4 transform;
    for (uint32_t i = 0; i < count; i++, effect++) {
        if (!EffectBit(animHandle->effectsOn, i))
            continue;
        if (glaresOnly && effect->type != ArticleEffect::kTypeGlare)
            continue;
        bool followsSystem = (effect->flags & ArticleEffect::kFollowsSystem) != 0;
        bool hidden = (damagedZones & (1u << (effect->zone & 31))) != 0 &&
                      (effect->flags & ArticleEffect::kShowsDamaged) == 0;
        int32_t instance = effect->instance;
        if (instance != lastInstance) {
            placed = false;
            lastInstance = instance;
            if (followsSystem)
                systemPlaying = animHandle->IsSystemPlaying(animHandle->GetInstanceSystemID(uint32_t(instance)));
        }
        if (followsSystem && !systemPlaying) {
            ToggleEffect(false, i, effect, -1.0f);
            continue;
        }
        if (hidden)
            continue;
        if (!placed) {
            placed = true;
            GetInstancePosition(uint32_t(instance), &transform, true);
            if (effect->flags & ArticleEffect::kOnGround) {
                Coord4 *position = MatrixRow(&transform, 3);
                if (!fgCollisionMgr->GetWorldHeightAtPoint(reinterpret_cast<Coord3 *>(position), &position->y, true))
                    position->y = 0.0f;
            }
        }
        if (!RenderEffect(effect, &transform))
            animHandle->effectsOn &= ~EffectMask(i);
    }
}

// FUNC_AT(0x0008dae0)
void RSceneObj::SetEventDynamicData() {
    MEM_fill(&gEventDynamicData, 0, sizeof(EventDynamicData));
    gEventDynamicData.index = -1;
    gEventDynamicData.instanceIndex = -1;
    gEventDynamicData.sceneObj = this;
    if (physics != NULL) {
        gEventDynamicData.flag = uint8_t(physics->flags & PhysicsObject::kSimpleBody);
        gEventDynamicData.index = physics->rigidBodySlot;
    }
}

// FUNC_AT(0x0008db40)
void RSceneObj::Show() {
    flags |= kVisible;
}

// FUNC_AT(0x0008db30)
void RSceneObj::Hide() {
    flags &= ~kVisible;
}

// ---- URefCounter<RCARPFile>'s tree

// FUNC_AT(0x0008fb00)
void CarpFileRefTree::EraseSubtree(RefCounterNode *node) {
    RefCounterTree::EraseSubtree(node);
}

// FUNC_AT(0x0008fb50)
RefCounterNode** CarpFileRefTree::EraseAt(RefCounterNode **result, RefCounterNode *where) {
    return RefCounterTree::EraseAt(result, where);
}

// FUNC_AT(0x0008fec0)
RefCounterNode** CarpFileRefTree::InsertAt(RefCounterNode **result, bool addLeft, RefCounterNode *where, const RefCounterValue *value) {
    return RefCounterTree::InsertAt(result, addLeft, where, value);
}

// FUNC_AT(0x000900b0)
RefCounterNode** CarpFileRefTree::EraseRange(RefCounterNode **result, RefCounterNode *first, RefCounterNode *last) {
    return RefCounterTree::EraseRange(result, first, last);
}

// FUNC_AT(0x000901a0)
RefCounterInsertResult* CarpFileRefTree::InsertUnique(RefCounterInsertResult *result, const RefCounterValue *value) {
    return RefCounterTree::InsertUnique(result, value);
}

// FUNC_AT(0x00090360)
void CarpFileRefTree::DestroyRange() {
    RefCounterTree::DestroyRange();
}

// FUNC_AT(0x000903a0)
void CarpFileRefTree::Destruct() {
    Destroy();
}
