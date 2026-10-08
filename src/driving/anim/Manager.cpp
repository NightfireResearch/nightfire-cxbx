#include "Manager.h"

#include "AnimationDatabase.h"         // ActAnimationDatabase
#include "Character.h"                 // LightBlock, LightingView
#include "Events.h"                    // ActEvents, ActEventResolver
#include "Model.h"
#include "Skeleton.h"
#include "Weapon.h"                    // ActWeaponDatabase
#include "../data/RCARPFile.h"          // RegisterCallback
#include "../eagl/EaglGlobals.h"
#include "../eagl/Loader.h"
#include "../eagl/anim/AnimObjects.h"   // EAGLAnim_InitInternal, EAGLAnim_ShutDown
#include "../eagl/anim/EventTarget.h"
#include "../engine/UMemory.hpp"
#include "../../helpers.h"

#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// ActManager (0x00016ec0-0x00017450) and the helpers at 0x0001a9f0-0x0001aa70, ported from the listings.
// ---------------------------------------------------------------------------------------------------------------

// ---- globals

#define EventResolver (*(ActEventResolver **)0x001dd9d0)
#define NormalLight (*(LightBlock **)0x001dd9d4)    // the scene's lights, saved when IR mode is first set
#define IRLight (*(LightBlock **)0x001dd9d8)
#define IRLightReady BOOL8_AT(0x001dd9dc)

constexpr uint32_t kAnimPoolSize = 0xc800;     // EAGLAnim's pool; the original also passes a 1 nothing reads

// The resolver and the symbol callback, registered by their original addresses
constexpr uint32_t kResolveEAGLReferences = 0x0007b8a0;
constexpr uint32_t kSymbolResolverAt = 0x00017010;

static LightBlock* NewLightBlock() {
    LightBlock *block = static_cast<LightBlock *>(EaglMalloc(sizeof(LightBlock), "EAGL::LightBlock new"));
    if (block != NULL)
        memset(block, 0, sizeof(LightBlock));
    return block;
}

// FUNC_AT(0x000172c0)
void ActManager::StartUp(float frameTime) {
    NormalLight = NewLightBlock();
    IRLight = NewLightBlock();
    ActEventResolver *resolver =
        static_cast<ActEventResolver *>(UMemory::FastAlloc(sizeof(ActEventResolver), "ActEventResolver"));
    EventResolver = resolver != NULL ? resolver->Construct() : NULL;
    ActManager *manager = static_cast<ActManager *>(OperatorNew(sizeof(ActManager)));
    if (manager != NULL)
        manager->drawOptions.Construct();
    TheActManager = manager;
    manager->Init(frameTime);
}

// FUNC_AT(0x000173d0)
void ActManager::ShutDown() {
    ActManager *manager = TheActManager;
    if (manager != NULL) {
        manager->Destruct();
        OperatorDelete(manager);
    }
    TheActManager = NULL;
    ActEventResolver *resolver = EventResolver;
    if (resolver != NULL) {
        resolver->Destruct();
        UMemory::FastFree(resolver, sizeof(ActEventResolver));
    }
    EaglFree(NormalLight, sizeof(LightBlock));
    NormalLight = NULL;
    EaglFree(IRLight, sizeof(LightBlock));
    IRLight = NULL;
}

// FUNC_AT(0x00016ed0)
void ActManager::SetIRMode(bool on) {
    if (!IRLightReady) {
        IRLightReady = 1;
        *NormalLight = Lighting->lights;
        *IRLight = Lighting->lights;
        const Coord4 white = {1.0f, 1.0f, 1.0f, 1.0f};
        IRLight->colours[0] = IRLight->colours[1] = IRLight->colours[2] = IRLight->colours[3] = white;
    }
    if (on) {
        if (!IRModeOn) {
            IRModeOn = 1;
            Lighting->lights = *IRLight;
        }
    } else if (IRModeOn) {
        IRModeOn = 0;
        Lighting->lights = *NormalLight;
    }
}

// FUNC_AT(0x00017060)
void ActManager::Init(float frameTime) {
    this->frameTime = frameTime;
    IRLightReady = 0;
    IRModeOn = 0;
    EAGLAnim_InitInternal(kAnimPoolSize);
    DynamicLoader::RegisterVar("GAME::CharacterOptions", &drawOptions.alphaScale);
    RegisterCallback::AddMajorCallbacks();
    RegisterCallback::Add(reinterpret_cast<SymbolCallback>(uintptr_t(kSymbolResolverAt)));

    ActSkeletonDatabase *skeletonMemory = static_cast<ActSkeletonDatabase *>(
        UMemory::FastAlloc(sizeof(ActSkeletonDatabase), "ActSkeletonDatabase"));
    skeletons = skeletonMemory != NULL ? skeletonMemory->Construct() : NULL;
    ActTextureDatabase *textureMemory = static_cast<ActTextureDatabase *>(
        UMemory::FastAlloc(sizeof(ActTextureDatabase), "ActTextureDatabase"));
    textures = textureMemory != NULL ? textureMemory->Construct() : NULL;
    ActModelDatabase *modelMemory = static_cast<ActModelDatabase *>(
        UMemory::FastAlloc(sizeof(ActModelDatabase), "ActModelDatabase"));
    models = modelMemory != NULL ? modelMemory->Construct() : NULL;
    ActAnimationDatabase *animationMemory = static_cast<ActAnimationDatabase *>(
        UMemory::FastAlloc(sizeof(ActAnimationDatabase), "ActAnimationDatabase"));
    animations = animationMemory != NULL ? animationMemory->Construct() : NULL;
    ActWeaponDatabase *weaponMemory = static_cast<ActWeaponDatabase *>(
        UMemory::FastAlloc(sizeof(ActWeaponDatabase), "ActWeaponDatabase"));
    weapons = weaponMemory != NULL ? weaponMemory->Construct() : NULL;
    ActEvents *eventsMemory = static_cast<ActEvents *>(UMemory::FastAlloc(sizeof(ActEvents), "ActEvents"));
    events = eventsMemory != NULL ? eventsMemory->Construct(EventResolver) : NULL;
}

// FUNC_AT(0x00017200)
void ActManager::Destruct() {
    if (events != NULL) {
        events->Destruct();
        UMemory::FastFree(events, sizeof(ActEvents));
    }
    if (weapons != NULL) {
        weapons->Destruct();
        UMemory::FastFree(weapons, sizeof(ActWeaponDatabase));
    }
    if (animations != NULL) {
        animations->Destruct();
        UMemory::FastFree(animations, sizeof(ActAnimationDatabase));
    }
    if (models != NULL) {
        models->Destruct();
        UMemory::FastFree(models, sizeof(ActModelDatabase));
    }
    if (textures != NULL) {
        textures->Destruct();
        UMemory::FastFree(textures, sizeof(ActTextureDatabase));
    }
    if (skeletons != NULL) {
        skeletons->Destruct();
        UMemory::FastFree(skeletons, sizeof(ActSkeletonDatabase));
    }
    RegisterCallback::Remove(reinterpret_cast<SymbolCallback>(uintptr_t(kSymbolResolverAt)));
    EAGLAnim_ShutDown();
}

// FUNC_AT(0x00016ec0)
void* ActManager::GetSymbolResolver() {
    return reinterpret_cast<void *>(uintptr_t(kResolveEAGLReferences));
}

// FUNC_AT(0x00017010)
int ActManager::SymbolResolver(const char *name, bool *found) {
    *found = true;
    int id;
    if (strncmp(name, "event.", 6) == 0 && EventResolver->target->ResolveEventId(name, &id))
        return id;
    *found = false;
    return 0;
}

// ---- helpers the linker placed here

// FUNC_AT(0x0001a9f0)
Coord4* MakeCoord4(Coord4 *result, const Coord3 *v, float w) {
    result->x = v->x;
    result->y = v->y;
    result->z = v->z;
    result->w = w;
    return result;
}

// FUNC_AT(0x0001aa40)
UGroup* IndexedTagGroup::LocateTag(uint32_t tag, int index) {
    if (index != -1)
        tag = (tag & 0xffff0000) | index;
    return GroupLocateTag(tag);
}

// FUNC_AT(0x0001aa60)
void* Field2cGetter::Get() {
    return unknown2c;
}
