#pragma fp_contract(off)

#include "RenderHigh.h"

#include <bit>
#include <intrin.h>                       // __rdtsc

#include "Colorize.h"
#include "DebugView.h"
#include "Decals.h"
#include "Fog.h"
#include "Gain.h"
#include "LensFlare.h"
#include "Lightning.h"
#include "Lights.h"
#include "Particles.h"
#include "Particulate.h"
#include "PostProcessing.h"
#include "Reflection.h"
#include "Renderer.h"
#include "RGlareManager.hpp"
#include "RSceneObj.hpp"
#include "ShadowMap.h"
#include "SkyWater.h"
#include "StateManager.h"
#include "TextureContext.h"
#include "TimeData.h"
#include "../anim/Weapon.h"               // ActWeapon::LoadAttributes
#include "../camera/CameraSpline.h"       // FreeEAGLMaterials
#include "../camera/WorldCamera.h"        // RPlayerViewCamera, RRenderWorldCamera::LoadAttributes
#include "../data/DebugVariables.h"
#include "../data/RCARPFile.h"            // GlobalSymbolTable
#include "../data/Tuning.h"
#include "../eagl/RenderContext.h"
#include "../engine/CoreFoundation.h"     // NullFunction
#include "../engine/GameLoop.h"           // LaunchPage
#include "../engine/MissionManager.h"
#include "../engine/StaticInit.h"
#include "../engine/UMemory.hpp"
#include "../physics/PhysicsNamespace.h"
#include "../physics/PhysicsObject.h"
#include "../platform/RealPrint.h"        // MEM_fill
#include "../world/Render.h"              // WRender
#include "../../common/xbeOverload.h"     // XbeVirtual
#include "../../helpers.h"

// ---------------------------------------------------------------------------------------------------------------
// RRenderHigh (0x0008bac0-0x0008c8d0), the managers' USingleton<T>::Init copies (0x0008b5b0-0x0008bf30) and the
// "GIOT" namespace's destructor (0x0008ba80), ported from the listing. Tested by lockstep runs: every frame is
// drawn through Render, split screens through RearrangeSplitScreens.
// ---------------------------------------------------------------------------------------------------------------

class RDebris;

namespace {

// The Simulation's list of cars: the first is the player's
struct SimCarList {
    PhysicsObject **first;
};

// Any of the game's objects with a vtable: slot 0 the scalar deleting destructor, and RShadowMap's USingleton base
struct VirtualObject {
    void **vtable;
};

// The split screens: rows and columns of views by the number of views
const int8_t kSplitRows[10] = { 1, 1, 2, 2, 2, 3, 3, 3, 3, 3 };
const int8_t kSplitColumns[10] = { 1, 1, 1, 2, 2, 2, 2, 3, 3, 3 };

constexpr float kAspectSlope = 2.5f;            // AspectScale: (width / height * 2.5 - 1.5) * unknown50 / unknown54
constexpr float kAspectOffset = 1.5f;
constexpr float kLetterboxY = 0.125f;           // a single view with RRenderer::kVideoLetterbox
constexpr float kLetterboxHeight = 0.75f;
constexpr float kInitialFramesPerSecond = 60.0f;
constexpr float kCyclesPerFrameSecond = 733000000.0f;   // the time stamp counter's rate
constexpr float kFramesPerSecondNew = 0.100000024f;     // the smoothing: new * 0.1 + old * 0.9
constexpr float kFramesPerSecondOld = 0.9f;
static_assert(std::bit_cast<uint32_t>(kCyclesPerFrameSecond) == 0x4e2ec2c5 &&
              std::bit_cast<uint32_t>(kFramesPerSecondNew) == 0x3dccccd0 &&
              std::bit_cast<uint32_t>(kFramesPerSecondOld) == 0x3f666666, "the original's constants");
constexpr uint32_t kViewZBufferRange = 0x00ff3cae;
constexpr int32_t kDebugViewId = 7;
constexpr float kFarPlaneMin = 50.0f;
constexpr float kFarPlaneMax = 4000.0f;
constexpr int kMipmapMin = -128;

// The managers' sizes, as their Init copies allocate them
constexpr uint32_t kDebrisSize = 0x90;
constexpr uint32_t kMissileCamSize = 0x1c;
constexpr uint32_t kSniperZoomSize = 0xc70;
constexpr uint32_t kPlayerViewCameraSize = 0x4c;
constexpr uint32_t kPlayerCameraSize = 0x370;
constexpr uint32_t kHudViewSize = 0x4c;
constexpr uint32_t kHudSize = 0x4f0;
constexpr uint32_t kPhysicsNamespaceSize = 4;

} // namespace

// ---- the game's code not ported yet
typedef void *(__fastcall *ManagerConstructor)(void *memory, int);   // the unported managers'
#define RDebris_Construct ((void *(__fastcall *)(void *, int))0x000a91b0)
#define RMissileCam_Construct ((void *(__fastcall *)(void *, int))0x000a0a50)
#define RSniperZoom_Construct ((void *(__fastcall *)(void *, int))0x000a69a0)
#define GHud_Construct ((GHud *(__fastcall *)(void *, int))0x000dab50)
#define GHud_Render ((void (__fastcall *)(GHud *, int))0x000e0140)
#define Simulation_FUN_000b2d40 ((const Coord3 *(__fastcall *)(void *, int))0x000b2d40)
#define BuildFileName ((char *(__fastcall *)(char *, int, const char *directory, const char *name, const char *extension))0x00051e90)
#define GetParticFileForMission ((const char *(*)(void))0x000e3e50)
#define RVehicleParticle_Init ((void (*)(void))0x000a8240)
#define RVehicleParticle_LoadAttributes ((void (*)(void))0x000a6db0)
#define RVehicleParticle_UpdateAll ((void (*)(void))0x000a8120)
#define RVehicleParticle_ResetAll ((void (*)(void))0x000a7300)
#define RVehicleParticle_Kill ((void (*)(void))0x000a8430)
#define REmpBolts_LoadAttributes ((void (*)(void))0x0009bd50)
#define RBulletStreak_Init ((void (*)(void))0x00099f10)
#define RMuzzleFlash_Init ((void (*)(void))0x000a1b50)
#define RMissileStreak_Init ((void (*)(void))0x000a1070)
#define RMissileStreak_Kill ((void (*)(void))0x000a1460)
#define REmp_Init ((void (*)(void))0x0009b680)
#define REmp_Kill ((void (*)(void))0x0009bce0)
#define RTyreTrack_Init ((void (*)(void))0x000ac050)
#define RVehicle_InitSharedBuffers ((void (*)(void))0x000958a0)
#define RDebris_Reset ((void (__fastcall *)(RDebris *, int))0x000a8f10)
#define CRT_atexit ((int (*)(void (*)(void)))0x00132a7b)

// ---- globals
#define Sim ((void *)0x00233ff0)                                // the Simulation
#define SimCars (*(SimCarList *)0x00234e40)
#define Symbols (*(USymbolTable **)0x001ebcdc)
#define Launch (*(LaunchPage *)0x00243b90)
#define AspectScale FLOAT_AT(0x001c47a0)                        // RViewCamera::AspectRatio's scale
#define RenderLodBias U32_AT(0x001c4614)                        // EAGL's TAR LOD bias override, set each frame
#define CullDistance FLOAT_AT(0x001c4624)                       // "Far plane dist"
#define FrameAnimationTime U32_AT(0x001c4668)                   // ESetFrameAnimationTime's
#define RenderFeaturesUnknown U8_AT(0x001f2df8)                 // FeatureManager::SetUserSpecifiedRenderFeatures reads it
#define TheGalleryNamespace (*(GalleryNamespace *)0x001c4610)
#define PhysNamespace (*(PhysicsNamespace **)0x001ec48c)
#define TheRenderSingletonManager (*(USingletonManager *)0x001ec4a4)
#define RenderSingletonManagerMade U32_AT(0x001ec4b4)
#define RHUDViewVtable ((void **)0x00191aa8)
#define GalleryNamespaceVtable ((void *)0x00191aa0)
// The managers not ported yet (the others are in their headers)
#define Debris (*(RDebris **)0x00202a80)
#define MissileCam (*(void **)0x00200f54)
#define SniperZoom (*(void **)0x00201818)

namespace {

// An object's scalar deleting destructor (vtable slot 0) with 1: destroyed and freed
void DeleteObject(void *object) {
    VirtualObject *target = static_cast<VirtualObject *>(object);
    typedef void (VirtualObject::*Method)(unsigned flags);
    (target->*XbeVirtual<Method>(target, 0))(1);
}

// The manager's USingleton base, as the singleton manager keeps it: the manager itself
template <class T> USingleton *SingletonOf(T *manager) {
    return reinterpret_cast<USingleton *>(manager);
}

// USingleton<T>::Init's body, the copies made with operator new and those made from the pools
template <class T> T *NewManager() {
    void *memory = OperatorNew(sizeof(T));
    return memory != NULL ? static_cast<T *>(memory)->Construct() : NULL;
}

template <class T> T *NewPooledManager(const char *name) {
    void *memory = UMemory::FastAlloc(sizeof(T), name);
    return memory != NULL ? static_cast<T *>(memory)->Construct() : NULL;
}

// ... for the managers not ported yet, by their constructors' addresses
void *NewPooledManager(uint32_t size, const char *name, ManagerConstructor construct) {
    void *memory = UMemory::FastAlloc(size, name);
    return memory != NULL ? construct(memory, 0) : NULL;
}

} // namespace

// ---- the managers' Init copies

// FUNC_AT(0x0008b5b0)
void RenderManagers::InitStateManager() {
    TheStateManager = NewManager<RStateManager>();
}

// FUNC_AT(0x0008b620)
void RenderManagers::InitLightManager() {
    fgLightManager = NewManager<RLightManager>();
}

// FUNC_AT(0x0008b690)
void RenderManagers::InitFog() {
    Fog = NewManager<RFog>();
}

// FUNC_AT(0x0008b700)
void RenderManagers::InitTextureContextManager() {
    TheTextureContextManager = NewManager<RTextureContextManager>();
}

// FUNC_AT(0x0008b770)
void RenderManagers::InitColorize() {
    Colorize = NewManager<RColorize>();
}

// FUNC_AT(0x0008b7e0)
void RenderManagers::InitDecalManager() {
    TheDecalManager = NewManager<RDecalManager>();
}

// FUNC_AT(0x0008b850)
void RenderManagers::InitGlareManager() {
    TheGlareManager = NewManager<RGlareManager>();
}

// FUNC_AT(0x0008b8c0)
void RenderManagers::InitLensFlareManager() {
    TheLensFlareManager = NewManager<RLensFlareManager>();
}

// FUNC_AT(0x0008b930)
void RenderManagers::InitLightning() {
    TheLightning = NewManager<RLightning>();
}

// FUNC_AT(0x0008b9a0)
void RenderManagers::InitWater() {
    TheWater = NewManager<RWater>();
}

// FUNC_AT(0x0008ba10)
void RenderManagers::InitDebris() {
    Debris = static_cast<RDebris *>(NewPooledManager(kDebrisSize, "RDebris", RDebris_Construct));
}

// FUNC_AT(0x0008bc80)
void RenderManagers::InitPostProcessing() {
    ThePostProcessing = NewPooledManager<RPostProcessing>("RPostProcessing");
}

// FUNC_AT(0x0008bcf0)
void RenderManagers::InitGain() {
    TheGain = NewPooledManager<RGain>("RGain");
}

// FUNC_AT(0x0008bd60)
void RenderManagers::InitParticulate() {
    fgParticulate = NewPooledManager<RParticulate>("RParticulate");
}

// FUNC_AT(0x0008bdd0)
void RenderManagers::InitMissileCam() {
    MissileCam = NewPooledManager(kMissileCamSize, "RMissileCam", RMissileCam_Construct);
}

// FUNC_AT(0x0008be40)
void RenderManagers::InitSniperZoom() {
    SniperZoom = NewPooledManager(kSniperZoomSize, "RSniperZoom", RSniperZoom_Construct);
}

// FUNC_AT(0x0008beb0)
void RenderManagers::InitShadowMap() {
    TheShadowMap = NewPooledManager<RShadowMap>("RShadowMap");
}

// ---- the renderer's singleton manager

// FUNC_AT(0x0008c370)
USingletonManager* RenderSingletonManager() {
    if ((RenderSingletonManagerMade & 1) == 0) {
        RenderSingletonManagerMade |= 1;
        TheRenderSingletonManager.singletons.first = NULL;
        TheRenderSingletonManager.singletons.last = NULL;
        TheRenderSingletonManager.singletons.end = NULL;
        CRT_atexit(DestroyStatic_001ec4a4);
    }
    return &TheRenderSingletonManager;
}

// ---- the "GIOT" namespace

// FUNC_AT(0x0008ba80)
GalleryNamespace* GalleryNamespace::Delete(unsigned flags) {
    SCENEOBJ_UNTESTED("GalleryNamespace scalar deleting destructor");
    vtable = GalleryNamespaceVtable;
    if (flags & 1)
        OperatorDelete(this);
    return this;
}

// FUNC_AT(0x0008bab0)
void FreeEAGLMaterialsThunk() {
    FreeEAGLMaterials();
}

// ---- RRenderHigh

// FUNC_AT(0x0008c210)
RRenderHigh* RRenderHigh::Construct() {
    unknown24 = NULL;
    lastTimestamp = 0;
    framesPerSecond = kInitialFramesPerSecond;
    viewCount = 0;
    MEM_fill(views, 0, sizeof(views));
    RViewCamera *view = views[PushPlayerView() - 1].view;
    view->SetZBufferRange(kViewZBufferRange, 0);
    view->SetFillColour(Fog->FogColour());

    RViewCamera *hudMemory = static_cast<RViewCamera *>(UMemory::FastAlloc(kHudViewSize, "RRenderHUDView"));
    if (hudMemory != NULL) {
        hudMemory->Construct(NULL);
        hudMemory->vtable = RHUDViewVtable;
    }
    hudView = hudMemory;

    void *memory = UMemory::FastAlloc(sizeof(RRenderDebugViewPerspective), "RRenderDebugViewPerspective");
    debugViewPerspective = memory != NULL ? static_cast<RRenderDebugViewPerspective *>(memory)->Construct(view) : NULL;
    debugViewPerspective->viewId = kDebugViewId;
    memory = UMemory::FastAlloc(sizeof(RRenderDebugViewScreenSpace), "RRenderDebugViewScreenSpace");
    debugViewScreenSpace = memory != NULL ? static_cast<RRenderDebugViewScreenSpace *>(memory)->Construct() : NULL;
    debugViewScreenSpace->viewId = kDebugViewId;

    fgRenderHigh = this;
    memory = OperatorNew(kHudSize);
    hud = memory != NULL ? GHud_Construct(memory, 0) : NULL;
    return this;
}

// The HUD view is not deleted.
// FUNC_AT(0x0008c090)
void RRenderHigh::Destruct() {
    if (hud != NULL)
        DeleteObject(hud);
    fgRenderHigh = NULL;
    if (debugViewPerspective != NULL)
        DeleteObject(debugViewPerspective);
    if (debugViewScreenSpace != NULL)
        DeleteObject(debugViewScreenSpace);
    if (unknown24 != NULL)
        DeleteObject(unknown24);
    while (viewCount != 0) {
        viewCount--;
        if (views[viewCount].view != NULL)
            DeleteObject(views[viewCount].view);
        views[viewCount].view = NULL;
        if (views[viewCount].camera != NULL)
            DeleteObject(views[viewCount].camera);
        views[viewCount].camera = NULL;
        RearrangeSplitScreens();
    }
}

// FUNC_AT(0x0008c130)
int RRenderHigh::PushPlayerView() {
    RPlayerViewCamera *view = static_cast<RPlayerViewCamera *>(UMemory::FastAlloc(kPlayerViewCameraSize, "RPlayerViewCamera"));
    if (view != NULL) {
        RPlayerCamera *camera = static_cast<RPlayerCamera *>(UMemory::FastAlloc(kPlayerCameraSize, "RPlayerCamera"));
        if (camera != NULL)
            camera = camera->Construct();
        view->Construct(camera);
    }
    if (viewCount >= kMaxViews)
        return -1;
    views[viewCount].camera = static_cast<RPlayerCamera *>(view->camera);
    views[viewCount].view = view;
    view->viewId = 0;
    viewCount++;
    RearrangeSplitScreens();
    return int(viewCount);
}

// FUNC_AT(0x0008bb10)
void RRenderHigh::RearrangeSplitScreens() {
    int rows = kSplitRows[viewCount];
    int columns = kSplitColumns[viewCount];
    float height = (float)(1.0 / rows);
    float width = (float)(1.0 / columns);
    float y = 0.0f;
    uint32_t view = 0;
    float aspect = (float)((double)fgRenderer->unknown50 / fgRenderer->unknown54 *
                           ((double)width / height * kAspectSlope - kAspectOffset));
    for (int row = 0; row < rows; row++) {
        float x = 0.0f;
        for (int column = 0; column < columns; column++) {
            if (view >= viewCount)
                return;
            if (fgRenderer->videoMode == RRenderer::kVideoLetterbox)
                views[view].view->SetExtents(0.0f, kLetterboxY, 1.0f, kLetterboxHeight);
            else
                views[view].view->SetExtents(x, y, width, height);
            view++;
            AspectScale = aspect;
            x = x + width;
        }
        y = y + height;
    }
}

// FUNC_AT(0x0008bad0)
void RRenderHigh::ReceiveCameraInput() {
    if (fgCameraTables.modeCount < 1)
        return;
    for (uint32_t i = 0; i < viewCount; i++) {
        RPlayerCamera *camera = views[i].camera;
        typedef void (RPlayerCamera::*Method)();
        (camera->*XbeVirtual<Method>(camera, 5))();     // RWorldCamera::ReceiveCameraInput
    }
}

// FUNC_AT(0x0008bf30)
void RRenderHigh::Render() {
    TheReflection->UpdateMaps(Simulation_FUN_000b2d40(Sim, 0));
    if (RShadowMap::ProjectedShadowsEnabled())
        SimCars.first[0]->renderObject->RenderShadowVirtual();
    else
        TheShadowMap->ClearShadowMap();
    reinterpret_cast<EAGL::RenderContextExtension *>(fgRenderer->renderContext)->SetGlobal23ff0c(RenderLodBias);
    fgRenderer->StartFrame();
    fgRenderer->DisableAlphaWrites();
    RearrangeSplitScreens();
    NullFunction();
    RTimeData::Update();
    for (uint32_t i = 0; i < viewCount; i++)
        views[i].view->Render();
    GHud_Render(hud, 0);

    uint32_t now = uint32_t(__rdtsc());
    int32_t cycles = int32_t(now - lastTimestamp);
    lastTimestamp = now;
    framesPerSecond = (float)(kCyclesPerFrameSecond / (double)cycles * kFramesPerSecondNew +
                              (double)framesPerSecond * kFramesPerSecondOld);

    debugViewPerspective->Render();
    debugViewScreenSpace->Render();
    Fog->UpdateScale();
    RVehicleParticle_UpdateAll();
    fgLightManager->FinishLights();
    if (glbMissionManager->unknown4f0 != 0)
        Colorize->DisableMotionBlur();
    fgRenderer->EnableAlphaWrites();
    fgRenderer->EndFrame();
    reinterpret_cast<EAGL::RenderContextExtension *>(fgRenderer->renderContext)->SetGlobal23ff0c(0);
}

// FUNC_AT(0x0008c3d0)
void RRenderHigh::InitGameRender() {
    RenderFeaturesUnknown = 0;
    FeatureManager::SetUserSpecifiedRenderFeatures();
    USingletonManager *singletons = RenderSingletonManager();
    RenderManagers::InitTextureContextManager();
    singletons->Register(SingletonOf(TheTextureContextManager));
    char fileName[0x40];
    TheTextureContextManager->NewContext(BuildFileName(fileName, 0, "data\\render\\", "common.xsh", ""), 0);
    RParticleSystemManager::Init(GetParticFileForMission());
    RVehicleParticle_Init();

    singletons = RenderSingletonManager();
    RenderManagers::InitStateManager();
    singletons->Register(SingletonOf(TheStateManager));
    singletons = RenderSingletonManager();
    RenderManagers::InitGlareManager();
    singletons->Register(SingletonOf(TheGlareManager));
    singletons = RenderSingletonManager();
    RenderManagers::InitDecalManager();
    singletons->Register(SingletonOf(TheDecalManager));
    singletons = RenderSingletonManager();
    RenderManagers::InitFog();
    singletons->Register(SingletonOf(Fog));
    singletons = RenderSingletonManager();
    RenderManagers::InitLightManager();
    singletons->Register(SingletonOf(fgLightManager));
    singletons = RenderSingletonManager();
    RenderManagers::InitWater();
    singletons->Register(SingletonOf(TheWater));
    singletons = RenderSingletonManager();
    RenderManagers::InitColorize();
    singletons->Register(SingletonOf(Colorize));
    singletons = RenderSingletonManager();
    RenderManagers::InitGain();
    singletons->Register(SingletonOf(TheGain));
    singletons = RenderSingletonManager();
    RenderManagers::InitParticulate();
    singletons->Register(SingletonOf(fgParticulate));
    singletons = RenderSingletonManager();
    RenderManagers::InitMissileCam();
    singletons->Register(SingletonOf(MissileCam));
    singletons = RenderSingletonManager();
    RenderManagers::InitLightning();
    singletons->Register(SingletonOf(TheLightning));

    TuningDBMgr->LoadDatabase("Render:FX", Launch.missionName, 0, false);
    singletons = RenderSingletonManager();
    RenderManagers::InitPostProcessing();
    singletons->Register(SingletonOf(ThePostProcessing));
    RVehicleParticle_LoadAttributes();
    TheWater->LoadAttributes();
    fgParticulate->LoadAttributes();
    TheLightning->LoadAttributes();
    REmpBolts_LoadAttributes();
    RShadowMap::LoadAttributes();
    RRenderWorldCamera::LoadAttributes();
    ActWeapon::LoadAttributes();
    RBulletStreak_Init();
    TuningDBMgr->CloseCurrent();

    TuningDBMgr->LoadDatabase("Render", Launch.missionName, 0, false);
    dbattrib_float("Far plane dist", &CullDistance, kFarPlaneMin, kFarPlaneMax, 0, 1.0f, NULL);
    dbattrib_s8("PS2 mipmap", &fgRenderer->ps2Mipmap, kMipmapMin, 0, 0, 1.0f, NULL);
    TuningDBMgr->CloseCurrent();

    singletons = RenderSingletonManager();
    RenderManagers::InitSniperZoom();
    singletons->Register(SingletonOf(SniperZoom));
    RMuzzleFlash_Init();
    RMissileStreak_Init();
    RReflection::Init();
    REmp_Init();
    RTyreTrack_Init();
    singletons = RenderSingletonManager();
    RenderManagers::InitShadowMap();
    singletons->Register(TheShadowMap != NULL ? &TheShadowMap->singleton.base : NULL);
    RenderManagers::InitDebris();
    RWindow::Init();
    GlobalSymbolTable::Init();
    // The state manager's symbol namespace is its base at +4
    SymbolNamespace *states =
        TheStateManager != NULL ? reinterpret_cast<SymbolNamespace *>(&TheStateManager->symbols) : NULL;
    Symbols->AddNamespace("STATE", states);
    Symbols->AddNamespace("GIOT", &TheGalleryNamespace);
    void *memory = OperatorNew(kPhysicsNamespaceSize);
    PhysicsNamespace *physicsNamespace = memory != NULL ? static_cast<PhysicsNamespace *>(memory)->Construct() : NULL;
    PhysNamespace = physicsNamespace;
    Symbols->AddNamespace("PHYS", reinterpret_cast<SymbolNamespace *>(physicsNamespace));
}

// FUNC_AT(0x0008c740)
void RRenderHigh::KillGameRender() {
    Symbols->RemoveNamespace("PHYS");
    if (PhysNamespace != NULL) {
        SymbolNamespace *physicsNamespace = reinterpret_cast<SymbolNamespace *>(PhysNamespace);
        typedef void (SymbolNamespace::*Method)(unsigned flags);
        (physicsNamespace->*XbeVirtual<Method>(physicsNamespace, 1))(1);   // its scalar deleting destructor
    }
    PhysNamespace = NULL;
    Symbols->RemoveNamespace("GIOT");
    Symbols->RemoveNamespace("STATE");
    GlobalSymbolTable::Kill();
    NullFunction();
    REmp_Kill();
    // RDebris's destructor does nothing; the pointer is left set
    if (Debris != NULL) {
        RDebris *debris = Debris;
        NullFunction();
        UMemory::FastFree(debris, kDebrisSize);
    }
    RReflection::Kill();
    NullFunction();
    NullFunction();
    NullFunction();
    RMissileStreak_Kill();
    RVehicleParticle_Kill();
    RParticleSystemManager::Shutdown();
    RenderSingletonManager()->KillAll();
    Colorize->DisableMotionBlur();
}

// FUNC_AT(0x0008c800)
void RRenderHigh::InitTrackRenderPostSim() {
    WRender::Init();
    TheReflection->InitPostSim();
    RRenderSharedData::Init();
    RVehicle_InitSharedBuffers();
    RSceneObj::InitializeAllLighting();
    USingletonManager *singletons = RenderSingletonManager();
    RenderManagers::InitLensFlareManager();
    singletons->Register(SingletonOf(TheLensFlareManager));
}

// FUNC_AT(0x0008c840)
void RRenderHigh::RestartTrackRender() {
    for (uint32_t i = 0; i < fgRenderHigh->viewCount; i++) {
        RPlayerCamera *camera = fgRenderHigh->views[i].camera;
        typedef void (RPlayerCamera::*Method)();
        (camera->*XbeVirtual<Method>(camera, 4))();     // RestartCamera
    }
    fgParticleSystems->Reset();
    RVehicleParticle_ResetAll();
    fgRenderer->frameCount = 0;
    SingletonOf(TheLightning)->vtable->reset(SingletonOf(TheLightning), 0);
    NullFunction();             // called on the shadow map
    NullFunction();             // ... and on the reflection
    RDebris_Reset(Debris, 0);
    RSceneObj::InitializeAllLighting();
    RenderSingletonManager()->ResetAll();
    FrameAnimationTime = 0;
}

// FUNC_AT(0x0008bac0)
void RRenderHigh::KillTrackRenderPostSim() {
    NullFunction();
    WRender::ShutDown();
}
