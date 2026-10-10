#pragma fp_contract(off)
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "Particles.h"

#include "../audio/Bank.h"              // SharedTreeIterator
#include "../data/Carp.h"               // CARP::ResolverMap::Find
#include "../engine/InputConfig.h"      // BuildFileName
#include "../engine/UMemory.hpp"
#include "../platform/RealMath.h"
#include "../platform/RealPrint.h"      // MEM_copy
#include "../platform/X87.h"
#include "../../helpers.h"
#include "TextureContext.h"

#include <bit>
#include <math.h>
#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// The particle systems, their manager and the library of system types. See Particles.h.
// ---------------------------------------------------------------------------------------------------------------

// ---- originals called by address

#define CRT_printf ((int (*)(const char *format, ...))0x00132192)
#define TreeIterator_Increment ((void (__fastcall *)(TreeNode **, int))0x000b2920)

// ---- globals

#define GameTick U32_AT(0x001f2a4c)
#define ParticleDensity U32_AT(0x001f2dd8)          // FeatureManager's, in 256ths (name ours)
#define NoVelocity (*(const Coord4 *)0x00201740)    // what CreateParticles moves its particles back along (zero)
#define LastSystem (*(RParticleSystem **)0x0020174c)    // AddOrRefresh's answer
#define LastSystemIsNew BOOL8_AT(0x00201750)
#define DefaultSystem (*(RParticleSystemData *)0x001c4e90)  // Reload's 'dflt'

const RParticleSystemVtable *const kRMovableParticleSystemVtable = (const RParticleSystemVtable *)0x00192da8;

constexpr uint32_t kTagDefault = 0x64666c74;        // 'dflt'
constexpr int kMaxSystems = 256;
constexpr uint32_t kMaxLife = 255;
constexpr uint32_t kRunOnceOffTime = 99999;
constexpr int32_t kKeepAlive = 30;                  // updates a system lives unrefreshed
constexpr uint32_t kMaxTicks = 3;                   // updates a frame at most
constexpr float kOwedScale = 0.75f;                 // MungeData's scale of the spawns owed
constexpr float kCountScale = 0.5f;                 // MungeDataForPlatform's of the count ...
constexpr float kSizeScale = 0.75f;                 // ... and the sizes
constexpr float kRadiansPerDegree = 0.017453292f;
constexpr float kColourScale = 1.0f / 256.0f;
constexpr float kTooSmall = 0.004f;                 // maxSize / unknown34 below this: no spawning
constexpr float kSteep = 0.1f;                      // CreateParticles: an x or z this small is ignored, a y below it is down
static_assert(std::bit_cast<uint32_t>(kRadiansPerDegree) == 0x3c8efa35 &&
              std::bit_cast<uint32_t>(kTooSmall) == 0x3b83126f && std::bit_cast<uint32_t>(kSteep) == 0x3dcccccd,
              "the original's constants");

static const char kLifeTooLong[] = "ERROR IN PARTICLE DATA: lifeTime of %d is too large. Capping at 255.\n";

// ---- the maps

// FUNC_AT(0x000a23c0)
TreeNode** ParticleMap::InsertAt(TreeNode **result, bool addLeft, TreeNode *where, const TreePair *value) {
    return Tree::InsertAt(result, addLeft, where, value);
}

// FUNC_AT(0x000a2660)
TreeNode** ParticleMap::EraseAt(TreeNode **result, TreeNode *where) {
    return Tree::EraseAt(result, where);
}

// insert(value): walks down from the root remembering the last node and direction, then either inserts there or
// finds the key already present one step back.
// FUNC_AT(0x000a2a70)
TreeInsertResult* ParticleMap::InsertUnique(TreeInsertResult *result, const TreePair *value) {
    TreeNode *where = head;
    bool addLeft = true;
    for (TreeNode *node = head->parent; !node->isNil; node = addLeft ? node->left : node->right) {
        where = node;
        addLeft = value->tag < node->value.tag;
    }
    SharedTreeIterator previous = {where};
    if (addLeft) {
        if (where == head->left) {
            TreeNode *inserted;
            result->where = *InsertAt(&inserted, true, where, value);
            result->inserted = true;
            return result;
        }
        previous.Dec();
    }
    if (previous.node->value.tag < value->tag) {
        TreeNode *inserted;
        result->where = *InsertAt(&inserted, addLeft, where, value);
        result->inserted = true;
        return result;
    }
    result->where = previous.node;
    result->inserted = false;
    return result;
}

// FUNC_AT(0x000a2080)
void ParticleMap::EraseSystemSubtree(TreeNode *node) {
    Tree::EraseSubtree(node);
}

// FUNC_AT(0x000a25a0)
TreeNode** ParticleMap::EraseSystemRange(TreeNode **result, TreeNode *first, TreeNode *last) {
    return Tree::EraseRange(result, first, last);
}

// FUNC_AT(0x000a20c0)
void ParticleMap::EraseTypeSubtree(TreeNode *node) {
    Tree::EraseSubtree(node);
}

// FUNC_AT(0x000a2b30)
TreeNode** ParticleMap::EraseTypeRange(TreeNode **result, TreeNode *first, TreeNode *last) {
    return Tree::EraseRange(result, first, last);
}

TreeNode** ParticleMap::Find(TreeNode **result, const uint32_t *key) {
    return static_cast<CARP::ResolverMap *>(static_cast<Tree *>(this))->Find(result, key);
}

// FUNC_AT(0x000a1ec0)
PointerHolder* PointerHolder::Construct(void *value) {
    pointer = value;
    return this;
}

// ---- the library

// FUNC_AT(0x000a2e20)
RParticleLibrary::RPartLibraryData* RParticleLibrary::RPartLibraryData::Construct() {
    count = 0;
    types.allocator = 0;   // copied from an uninitialised local in the original; nothing reads it
    types.Init();
    return this;
}

// FUNC_AT(0x000a2de0)
void RParticleLibrary::RPartLibraryData::Destruct() {
    TreeNode *ignored;
    types.EraseTypeRange(&ignored, types.head->left, types.head);
    if (types.head != NULL)
        UMemory::FastFree(types.head, sizeof(TreeNode));
    types.head = NULL;
    types.size = 0;
}

// FUNC_AT(0x000a2c80)
int RParticleLibrary::RPartLibraryData::AddSystem(RParticleSystemData *data, uint32_t tag) {
    if (count >= kMaxSystems)
        return -1;
    data->index = count;
    MEM_copy(&systems[count], data, sizeof(RParticleSystemData));
    TreePair value;
    value.tag = tag;
    value.type = &systems[count];
    TreeInsertResult inserted;
    types.InsertUnique(&inserted, &value);
    return count++;
}

// FUNC_AT(0x000a2e70)
RParticleLibrary* RParticleLibrary::Construct() {
    RPartLibraryData *list = static_cast<RPartLibraryData *>(
        UMemory::FastAlloc(sizeof(RPartLibraryData), "RParticleLibrary::RPartLibraryList"));
    data = list != NULL ? list->Construct() : NULL;
    return this;
}

// FUNC_AT(0x000a2ee0)
void RParticleLibrary::Destruct() {
    RPartLibraryData *list = data;
    if (list == NULL)
        return;
    list->Destruct();
    UMemory::FastFree(list, sizeof(RPartLibraryData));
}

// FUNC_AT(0x000a2d50)
void RParticleLibrary::Reload() {
    data->count = 0;
    ParticleMap *types = &data->types;
    types->EraseTypeSubtree(types->head->parent);
    types->head->parent = types->head;
    types->size = 0;
    types->head->left = types->head;
    types->head->right = types->head;

    char path[256];
    sprintf(path, "%sparticle.dat", "data\\render\\");   // made and not used
    int index = data->AddSystem(&DefaultSystem, kTagDefault);
    MungeData(&data->systems[index], &data->systems[index]);
}

// FUNC_AT(0x000a2d10)
int RParticleLibrary::AddSystem(RParticleSystemData *source, uint32_t tag) {
    int index = data->AddSystem(source, tag);
    MungeData(&data->systems[index], &data->systems[index]);
    return index;
}

// FUNC_AT(0x000a1e40)
RParticleSystemData* RParticleLibrary::FindSystemViaIndex(int index) {
    return &data->systems[index];
}

// FUNC_AT(0x000a2100)
RParticleSystemData* RParticleLibrary::FindSystemViaTag(uint32_t tag) {
    TreeNode *found;
    data->types.Find(&found, &tag);
    if (found == data->types.head)
        found = found->left;
    return found->value.type;
}

// FUNC_AT(0x000a1e60)
void RParticleLibrary::MungeData(RParticleSystemData *source, RParticleSystemData *data) {
    if (data->life > kMaxLife) {
        CRT_printf(kLifeTooLong, data->life);
        data->life = kMaxLife;
    }
    if (data->unknown48 != 0)
        data->unknown48 = Ftol(double(data->life) * kOwedScale);
    MungeDataForPlatform(source, data);
}

// FUNC_AT(0x000ab460)
void RParticleLibrary::MungeDataForPlatform(RParticleSystemData *source, RParticleSystemData *data) {
    if (source != data)
        memcpy(data, source, sizeof(RParticleSystemData));
    data->maxSize = data->size < data->sizeChange ? data->sizeChange : data->size;
    if (data->flags & kParticleFlag02)
        data->offTime = kRunOnceOffTime;
    if (data->life > kMaxLife) {
        CRT_printf(kLifeTooLong, data->life);
        data->life = kMaxLife;
    }
    data->invLife = float(1.0 / double(data->life));
    data->count = Ftol(double(uint32_t(data->count)) * kCountScale);

    // The sizes become the size at death and the change from it to the size at spawn. The change is taken from
    // the rounded spawn size and the unrounded death size, as the original's x87 stack has them.
    uint32_t flags = data->flags;
    if (!(flags & kParticleStreak)) {
        double atSpawn = double(data->size) * kSizeScale;
        double atDeath = double(data->sizeChange) * kSizeScale;
        data->size = float(atDeath);
        data->sizeChange = float(double(float(atSpawn)) - atDeath);
    }
    if (data->spin != 0.0f) {
        flags |= kParticleRotates;
        data->flags = flags;
    }
    if (data->flags & kParticleSpinFixed)
        data->flags |= kParticleRotates;
    data->spin = data->spin * kRadiansPerDegree;

    for (int i = 0; i < 3; i++) {
        Coord4 *colour = &data->colours[i];
        colour->x = colour->x * kColourScale;
        colour->y = colour->y * kColourScale;
        colour->z = colour->z * kColourScale;
        colour->w = colour->w * kColourScale;
        if (colour->x > 1.0f)
            colour->x = 1.0f;
        if (colour->y > 1.0f)
            colour->y = 1.0f;
        if (colour->z > 1.0f)
            colour->z = 1.0f;
        if (colour->w > 1.0f)
            colour->w = 1.0f;
    }
    Coord4 last = data->colours[2];
    data->colours[2] = data->colours[0];
    data->colours[0] = last;
}

// ---- the systems

// Particles a tick: the type's count over its life, or over the on times its life spans when it cycles. The
// fields are read as signed.
static float EmissionRate(const RParticleSystemData *data, int32_t count) {
    int32_t life = data->life;
    int32_t onTime = data->onTime;
    int32_t offTime = data->offTime;
    if (offTime != 0 && life >= onTime) {
        int32_t cycle = onTime + offTime;
        if (life <= cycle)
            return float(double(count) / onTime);
        return float(double(count) / (double(life) / cycle * onTime));
    }
    return float(double(count) / life);
}

// The type's count, scaled by the density setting
static int32_t ScaledCount(const RParticleSystemData *data) {
    return int32_t(uint32_t(data->count) * ParticleDensity >> 8);
}

// FUNC_AT(0x000a2140)
RParticleSystem* RParticleSystem::Construct(float unknown34, RParticleEmitter *emitter, bool initData) {
    this->unknown34 = unknown34;
    vtable = kRMovableParticleSystemVtable;
    axis.w = 5.0f;
    keepAlive = kKeepAlive;
    time = 0;
    nextSpawn = 1.0f;
    spawns = 0.0f;
    data = fgParticleSystems->library.FindSystemViaTag(emitter->systemTag);
    this->emitter = emitter;
    active = 1;
    systemFlags = 0;
    velocityPerSecond = NULL;
    rateScale = 1.0f;
    unknown5C = 1.0f;
    index = data->index;
    scaledCount = ScaledCount(data);
    unknown60 = 0;
    rate = EmissionRate(data, data->count);   // the count as loaded; SetSystem's is the scaled one
    if (initData)
        InitData();
    if (data->size == 0.0f && data->sizeChange == 0.0f)
        systemFlags |= kSystemNoSize;
    if (data->flags & kParticleFlag02)
        active = 0;
    return this;
}

// FUNC_AT(0x000a1c00)
void RParticleSystem::InitData() {
    positionSource = &emitter->position;
    lastPosition = emitter->position;
    VU0_v4crossprodxyz(&emitter->side, &emitter->direction, &axis);
    spawns = float(double(rateScale) * rate * double(data->unknown48) + spawns);
}

// FUNC_AT(0x000a2290)
void RParticleSystem::SetSystem(uint32_t tag) {
    if (emitter->systemTag == tag)
        return;
    emitter->systemTag = tag;
    RParticleSystemData *type = fgParticleSystems->library.FindSystemViaTag(tag);
    scaledCount = ScaledCount(type);
    rate = EmissionRate(type, scaledCount);
    data = type;
    index = type->index;
}

// FUNC_AT(0x000a1f00)
void RParticleSystem::Update(int ticks) {
    if (!active || emitter == NULL)
        return;
    Coord3 *source = (systemFlags & kSystemAtEmitter) ? &emitter->position : positionSource;
    Coord4 motion;
    VU0_v4sub(&lastPosition, source, &motion);
    lastPosition = *source;

    if (data->offTime != 0 && time > data->onTime) {
        if (time > data->onTime + data->offTime) {
            time = time - data->onTime - data->offTime;
            if (data->flags & kParticleFlag02)
                active = 0;
        } else {
            time += ticks;
        }
        return;
    }

    // The new count of spawns owed stays unrounded for the first test, as the original keeps it on the x87 stack.
    float scale = float(double(rateScale) * rate);
    RParticleSystemData *type = fgParticleSystems->library.FindSystemViaIndex(index);
    float ticksAsFloat = float(ticks);
    double owed = double(ticks) * scale + spawns;
    spawns = float(owed);
    if (unknown34 > 0.0f && double(type->maxSize) / unknown34 < kTooSmall) {
        nextSpawn = float(owed);
        time += ticks;
        return;
    }
    if (owed > nextSpawn) {
        do {
            RParticle *particle = fgParticleSystems->cache.Spawn();
            if (particle != NULL)
                particle->ResetParticle(source, type, emitter, &axis, velocityPerSecond, &motion,
                                        float((double(spawns) - nextSpawn) / scale / ticksAsFloat), ticksAsFloat);
            nextSpawn = nextSpawn + 1.0f;
        } while (spawns > nextSpawn);
    }
    time += ticks;
}

// FUNC_AT(0x000a2350)
RParticleSystem* RParticleSystem::RParticleCreator::Create(float unknown34, RParticleEmitter *emitter) {
    RParticleSystem *system = static_cast<RParticleSystem *>(UMemory::FastAlloc(sizeof(RParticleSystem), "RParticleSystem"));
    if (system == NULL)
        return NULL;
    return system->Construct(unknown34, emitter, true);
}

// FUNC_AT(0x000a1ed0)
RMovableParticleSystem* RMovableParticleSystem::Delete(unsigned flags) {
    vtable = kRMovableParticleSystemVtable;
    if (emitter != NULL)
        emitter->system = NULL;
    if (flags & 1)
        UMemory::FastFree(this, sizeof(RParticleSystem));
    return this;
}

// ---- the manager

// Every system deleted and its node erased, in the map's order
static void DeleteSystems(ParticleMap *systems) {
    for (TreeNode *node = systems->Begin(); node != systems->head;) {
        TreeNode *erased = node;
        node = TreeNext(node);
        RParticleSystem *system = erased->value.system;
        if (system != NULL)
            system->vtable->destroy(system, 0, 1);
        TreeNode *ignored;
        systems->EraseAt(&ignored, erased);
    }
}

// FUNC_AT(0x000a2f00)
RParticleSystemManager* RParticleSystemManager::Construct() {
    lastTick = 0;
    library.Construct();
    cache.Construct();
    fgParticleSystems = this;
    ParticleMap *map = static_cast<ParticleMap *>(
        UMemory::FastAlloc(sizeof(ParticleMap), "RParticleSystemManager::RPartSysList"));
    if (map != NULL) {
        map->allocator = 0;   // copied from an uninitialised local in the original; nothing reads it
        map->Init();
    }
    systems = map;
    library.Reload();
    return this;
}

// FUNC_AT(0x000a2fc0)
void RParticleSystemManager::Destruct() {
    DeleteSystems(systems);
    ParticleMap *map = systems;
    if (map != NULL) {
        TreeNode *ignored;
        map->EraseSystemRange(&ignored, map->head->left, map->head);
        if (map->head != NULL)
            UMemory::FastFree(map->head, sizeof(TreeNode));
        map->head = NULL;
        map->size = 0;
        UMemory::FastFree(map, sizeof(ParticleMap));
    }
    systems = NULL;
    // The cache's destructor is empty.
    library.Destruct();
}

// FUNC_AT(0x000a3140)
void RParticleSystemManager::Init(const char *name) {
    char path[64];
    TheTextureContextManager->NewContext(BuildFileName(path, 0, "data\\render\\", name, ""), 1);
    RParticleSystemManager *manager = static_cast<RParticleSystemManager *>(
        UMemory::FastAlloc(sizeof(RParticleSystemManager), "RParticleSystemManager"));
    fgParticleSystems = manager != NULL ? manager->Construct() : NULL;
}

// FUNC_AT(0x000a31e0)
void RParticleSystemManager::Shutdown() {
    RParticleSystemManager *manager = fgParticleSystems;
    if (manager != NULL) {
        manager->Destruct();
        UMemory::FastFree(manager, sizeof(RParticleSystemManager));
    }
    fgParticleSystems = NULL;
}

// FUNC_AT(0x000a29c0)
void RParticleSystemManager::Reset() {
    cache.count = 0;
    cache.block = -1;
    cache.buffer = 0;
    cache.spawned = 0;
    DeleteSystems(systems);
}

// FUNC_AT(0x000a2bf0)
RParticleSystem** RParticleSystemManager::AddOrRefresh(uint32_t key, RParticleEmitter *emitter,
                                                       RParticleSystem::RParticleCreator *creator, float unknown34) {
    TreeNode *found;
    systems->Find(&found, &key);
    RParticleSystem *system;
    if (found == systems->head) {
        system = creator->vtable->create(creator, 0, unknown34, emitter);
        TreePair value;
        value.tag = key;
        value.system = system;
        TreeInsertResult inserted;
        systems->InsertUnique(&inserted, &value);
        LastSystemIsNew = 1;
    } else {
        system = found->value.system;
        LastSystemIsNew = 0;
    }
    system->keepAlive = kKeepAlive;
    system->unknown34 = unknown34;
    LastSystem = system;
    return &LastSystem;
}

// FUNC_AT(0x000a2930)
void RParticleSystemManager::UpdateSpawnAllSystems() {
    uint32_t tick = GameTick;
    uint32_t ticks = tick - lastTick;
    if (ticks > kMaxTicks)
        ticks = kMaxTicks;
    else if (ticks == 0)
        return;
    lastTick = tick;
    TreeNode *next = systems->Begin();
    while (next != systems->head) {
        TreeNode *node = next;
        TreeIterator_Increment(&next, 0);
        RParticleSystem *system = node->value.system;
        if (--system->keepAlive > 0) {
            system->vtable->update(system, 0, ticks);
        } else {
            system->vtable->destroy(system, 0, 1);
            TreeNode *ignored;
            systems->EraseAt(&ignored, node);
        }
    }
}

// FUNC_AT(0x000a1e30)
void RParticleSystemManager::UpdateAndRenderAllSystems() {
    cache.UpdateAndRender();
}

// A direction mostly along y is crossed with x rather than y; one whose y is below kSteep is first replaced by z.
// FUNC_AT(0x000a1c60)
void RParticleSystemManager::CreateParticles(RParticleSystemData *data, const Coord3 *position,
                                             const Coord3 *direction, int count) {
    Coord4 forward = { direction->x, direction->y, direction->z, 0.0f };
    RParticleEmitter emitter = {};
    emitter.direction = *direction;
    memset(&emitter.body, 0xff, 4);     // the direction's fourth word, -1: no body
    Coord4 up = { 0.0f, 1.0f, 0.0f, 0.0f };
    if ((fabsf(forward.x) < kSteep || fabs(double(forward.y) / forward.x) > 1.0) &&
        (fabsf(forward.z) < kSteep || fabs(double(forward.y) / forward.z) > 1.0)) {
        up.x = 1.0f;
        up.y = 0.0f;
        up.z = 0.0f;
        if (forward.y < kSteep) {
            forward.x = 0.0f;
            forward.y = 0.0f;
            forward.z = 1.0f;
        }
    }
    VU0_v4unitcrossprodxyz(&forward, &up, &emitter.side);
    VU0_v4unitcrossprodxyz(&emitter.side, &forward, &up);
    for (int i = 0; i < count; i++) {
        RParticle *particle = fgParticleSystems->cache.Spawn();
        if (particle != NULL)
            particle->ResetParticle(position, data, &emitter, &up, direction, &NoVelocity, 0.0f, 0.0f);
    }
}
