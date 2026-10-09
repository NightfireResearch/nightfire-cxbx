#ifndef DRIVING_RENDER_RENDERHIGH_H_
#define DRIVING_RENDER_RENDERHIGH_H_

// ---------------------------------------------------------------------------------------------------------------
// RRenderHigh (0x44 bytes, one, fgRenderHigh): the renderer's top level. It holds the players' views (up to four,
// each an RPlayerViewCamera and its RPlayerCamera, laid out as split screens), the HUD and the two debug views,
// and draws the frame (Render). Its statics bring the renderer up and down: InitGameRender makes the renderer's
// managers - each a USingleton, made by its Init and registered with the renderer's own singleton manager - and
// loads their tuning; KillGameRender takes them down; InitTrackRenderPostSim, RestartTrackRender and
// KillTrackRenderPostSim do the per-track part. See RenderHigh.cpp.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "../camera/PlayerCamera.h"       // CameraView, RPlayerCamera
#include "../data/SymbolTable.h"          // SymbolNamespace
#include "../engine/USingleton.h"

class GHud;
class RRenderDebugViewPerspective;
class RRenderDebugViewScreenSpace;

class RRenderHigh {
public:
    static constexpr uint32_t kMaxViews = 4;

    CameraView views[kMaxViews];        // +0x00 the players' views and their cameras
    uint32_t viewCount;                 // +0x20
    void *unknown24;                    // +0x24 deleted with the views; nothing here sets it
    RViewCamera *hudView;               // +0x28 an RRenderHUDView (never deleted)
    uint32_t unknown2c;
    RRenderDebugViewPerspective *debugViewPerspective;      // +0x30
    RRenderDebugViewScreenSpace *debugViewScreenSpace;      // +0x34
    GHud *hud;                          // +0x38
    uint32_t lastTimestamp;             // +0x3c the time stamp counter's low word at the last frame
    float framesPerSecond;              // +0x40 smoothed; 60 at construction

    RRenderHigh* Construct();           // one player view, the HUD, the debug views             // 0x0008c210
    void Destruct();                                                                            // 0x0008c090
    // A new player view and camera; the number of views, or -1 if there are four already
    int PushPlayerView();                                                                       // 0x0008c130
    // The views' extents for their number, and the LOD bias that goes with their shape
    void RearrangeSplitScreens();                                                               // 0x0008bb10
    void ReceiveCameraInput();          // each camera's queued input                           // 0x0008bad0
    void Render();                                                                              // 0x0008bf30

    static void InitGameRender();                                                               // 0x0008c3d0
    static void KillGameRender();                                                               // 0x0008c740
    static void InitTrackRenderPostSim();                                                       // 0x0008c800
    static void RestartTrackRender();                                                           // 0x0008c840
    static void KillTrackRenderPostSim();                                                       // 0x0008bac0
};
static_assert(sizeof(RRenderHigh) == 0x44, "RRenderHigh is 0x44 bytes");
static_assert(offsetof(RRenderHigh, viewCount) == 0x20 && offsetof(RRenderHigh, debugViewPerspective) == 0x30 &&
              offsetof(RRenderHigh, framesPerSecond) == 0x40, "RRenderHigh layout");

// The one instance (its constructor sets it)
#define fgRenderHigh (*(RRenderHigh **)0x001ec488)

// The renderer's singleton manager: a second copy of SingletonManager (engine/USingleton.h) with its own static
// (0x001ec4a4), which only the renderer's managers are registered with (FUN_0008c370; the name is ours)
USingletonManager* RenderSingletonManager();                                                    // 0x0008c370

// USingleton<T>::Init, one compiled copy per manager (Ghidra: T::Init): a new T into the manager's pointer, NULL if
// the allocation fails.
namespace RenderManagers {
void InitStateManager();                // RStateManager::Init                                  // 0x0008b5b0
void InitLightManager();                // RLightManager::Init                                  // 0x0008b620
void InitFog();                         // RFog::Init                                           // 0x0008b690
void InitTextureContextManager();       // RTextureContextManager::Init                         // 0x0008b700
void InitColorize();                    // RColorize::Init                                      // 0x0008b770
void InitDecalManager();                // RDecalManager::Init                                  // 0x0008b7e0
void InitGlareManager();                // RGlareManager::Init                                  // 0x0008b850
void InitLensFlareManager();            // RLensFlareManager::Init                              // 0x0008b8c0
void InitLightning();                   // RLightning::Init                                     // 0x0008b930
void InitWater();                       // RWater::Init                                         // 0x0008b9a0
void InitDebris();                      // RDebris's (FUN_0008ba10); never registered           // 0x0008ba10
void InitPostProcessing();              // RPostProcessing::Init                                // 0x0008bc80
void InitGain();                        // RGain::Init                                          // 0x0008bcf0
void InitParticulate();                 // RParticulate::Init                                   // 0x0008bd60
void InitMissileCam();                  // RMissileCam::Init                                    // 0x0008bdd0
void InitSniperZoom();                  // RSniperZoom::Init                                    // 0x0008be40
void InitShadowMap();                   // RShadowMap::Init                                     // 0x0008beb0
}

// The "GIOT" namespace (one, at 0x001c4610, vtable 0x00191aa0): answers a name with the gallery's effect canvas of
// that name (slot 0, 0x000d3670). The name is ours.
class GalleryNamespace : public SymbolNamespace {
public:
    // The scalar deleting destructor, slot 1; nothing deletes the static
    GalleryNamespace* Delete(unsigned flags);                                                   // 0x0008ba80
};

// The linker's thunk to FreeEAGLMaterials (camera/CameraSpline.h; Ghidra: thunk_FUN_0007aee0)
void FreeEAGLMaterialsThunk();                                                                  // 0x0008bab0

#endif // DRIVING_RENDER_RENDERHIGH_H_
