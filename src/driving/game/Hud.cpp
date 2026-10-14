#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
// GHud's construction, state, pause menu and cheats. See Hud.h.

#pragma fp_contract(off)

#include "Hud.h"
#include "../Vtables.h"   // our copies of the game's vtables

#include "EventsC.h"                      // EPause, EResume
#include "FrontEndSystem.h"
#include "Gallery.h"
#include "Girl.h"
#include "LoadingScreen.h"
#include "MissionManager.h"
#include "Simulation.h"
#include "Subtitles.h"
#include "WeaponSound.h"                  // ATargeting
#include "WeaponCameras.h"                // SniperZoom
#include "../audio/Sound.h"               // AMenuSound
#include "../camera/Camera.h"             // RViewCamera
#include "../camera/CameraIniLoader.h"    // fgCameraModeIndices
#include "../camera/PlayerCamera.h"       // RPlayerCamera
#include "../data/AttributeSet.h"
#include "../eagl/EaglGlobals.h"          // EaglMalloc, EaglFree
#include "../eagl/Realgraph.h"            // ShapeFile
#include "../eagl/RenderContext.h"
#include "../eagl/Tar.h"
#include "../eagl/View.h"
#include "../engine/ActionQueue.hpp"
#include "../engine/CoreFoundation.h"     // GetFoundationVideoMode
#include "../engine/GameLoop.h"           // LaunchPage, Bond_SetExitToDriving
#include "../engine/InputConfig.h"
#include "../engine/UFileLoader.h"
#include "../engine/UMemory.hpp"
#include "../platform/RealMath.h"         // VU0_MATRIX4_vect3mult
#include "../platform/X87.h"
#include "../render/RenderHigh.h"         // fgRenderHigh
#include "../render/Renderer.h"           // fgRenderer
#include "../world/World.h"               // fgWorld
#include "../EventManager.hpp"
#include "../Scheduler.hpp"
#include "../../common/xbeOverload.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// ---- the game's state

extern Scheduler *fgScheduler;

// XBE_GLOBAL(0x0023f44c, 0x4)
GHud *fHudApp;
// XBE_GLOBAL(0x0023f448, 0x4)
ActionQueue *fHudActionQueue;
// XBE_GLOBAL(0x0023f450, 0x4)
RViewCamera *fHudView;

// The colours: the static initialisers convert them into these (StaticInitTable.cpp)
// XBE_GLOBAL(0x0023f4b0, 0x4)
uint32_t HudGadgetColour;
// XBE_GLOBAL(0x0023f4b4, 0x4)
uint32_t HudHealthCriticalColour;
// XBE_GLOBAL(0x0023f4b8, 0x4)
uint32_t HudHealthLowColour;
// XBE_GLOBAL(0x0023f4bc, 0x4)
uint32_t HudHealthColour;
// XBE_GLOBAL(0x0023f4c0, 0x4)
uint32_t HudHealthCriticalDimColour;
// XBE_GLOBAL(0x0023f4c4, 0x4)
uint32_t HudHealthLowDimColour;
// XBE_GLOBAL(0x0023f4c8, 0x4)
uint32_t HudHealthDimColour;
// XBE_GLOBAL(0x0023f4cc, 0x4)
uint32_t HudTextColour;
// XBE_GLOBAL(0x0023f4d0, 0x4)
uint32_t HudObjectiveColour;
// XBE_GLOBAL(0x0023f4d8, 0x4)
uint32_t HudColour4d8;
// XBE_GLOBAL(0x0023f4dc, 0x4)
uint32_t HudShadowColour;
// XBE_GLOBAL(0x0023f4e0, 0x4)
uint32_t HudColour4e0;
// XBE_GLOBAL(0x0023f4e4, 0x4)
uint32_t HudCrossColour;
// XBE_GLOBAL(0x0023f4e8, 0x4)
uint32_t HudCrossColour4e8;
// XBE_GLOBAL(0x0023f4ec, 0x4)
uint32_t HudCrossColour4ec;
// XBE_GLOBAL(0x0023f4f0, 0x4)
uint32_t HudCrossColour4f0;
// XBE_GLOBAL(0x0023f4f4, 0x4)
uint32_t HudCrossColour4f4;
// XBE_GLOBAL(0x0023f4f8, 0x4)
uint32_t HudCrossColour4f8;
// XBE_GLOBAL(0x0023f4fc, 0x4)
uint32_t HudCrossColour4fc;
// XBE_GLOBAL(0x0023f500, 0x4)
uint32_t HudCrossColour500;
// XBE_GLOBAL(0x0023f504, 0x4)
uint32_t HudCrossColour504;
// XBE_GLOBAL(0x0023f508, 0x4)
uint32_t HudCrossColour508;
// XBE_GLOBAL(0x0023f50c, 0x4)
uint32_t HudCrossColour50c;
// XBE_GLOBAL(0x0023f510, 0x4)
uint32_t HudCrossColour510;
// XBE_GLOBAL(0x0023f514, 0x4)
uint32_t HudHoleColour;
// XBE_GLOBAL(0x0023f518, 0x4)
uint32_t HudBloodColour;
// XBE_GLOBAL(0x0023f51c, 0x4)
uint32_t HudBloodColour2;
// XBE_GLOBAL(0x0023f520, 0x4)
uint32_t HudLetterBoxColour;
// XBE_GLOBAL(0x0023f53c, 0x4)
uint32_t HudTimerColour;
// XBE_GLOBAL(0x0023f540, 0x4)
uint32_t HudTimerWarningColour;
// XBE_GLOBAL(0x0023f544, 0x4)
uint32_t HudStylinColour;
// XBE_GLOBAL(0x0023f548, 0x4)
float HudArrowFadeStep = 0.016666668f;
// XBE_GLOBAL(0x0023f54c, 0x4)
uint32_t HudColour54c;
// XBE_GLOBAL(0x0023f550, 0x4)
uint32_t HudColour550;
// XBE_GLOBAL(0x0023f558, 0x4)
uint32_t HudMessageBoxColour;
// XBE_GLOBAL(0x0023f55c, 0x4)
uint32_t HudTimerBoxColour;
// XBE_GLOBAL(0x0023f560, 0x4)
uint32_t HudCrossColour560;

// The debug message (dbMessage): its text and the step it goes at (names ours)
// XBE_GLOBAL(0x0023f414, 0x21)
char HudDebugMessage[33];
// XBE_GLOBAL(0x0023f454, 0x4)
int32_t HudDebugMessageEndStep;

namespace {

const uint32_t kHudViewVtable = uint32_t(reinterpret_cast<uintptr_t>(Vtable_0019ff84));     // GOrthoHudView
const uint32_t kHudVtable = uint32_t(reinterpret_cast<uintptr_t>(Vtable_001a06c0));
constexpr uint32_t kActionQueueSize = 0x974;
constexpr uint32_t kGallerySize = 0xc0;
constexpr int kPuppetKind = 3;

// GLocale::GetLanguage's answers with their own trigger puppets (names after the puppets')
enum : int {
    kLanguageFrench = 1,
    kLanguageSpanish = 3,
    kLanguageItalian = 4,
    kLanguageSwedish = 6,
};

constexpr float kHealthShowTime = 3.0f;
constexpr float kWeaponShowTime = 3.0f;
constexpr float kWeaponFadeTime = 0.5f;
constexpr float kNoBloodCurtain = -1.0f;
constexpr float kStylinZoom = 200.0f;
constexpr float kRadarRadius = 16.0f;
constexpr float kObjectiveMsgTime = 5.0f;
constexpr float kButtonMsgFade = 0.5f;
constexpr float kGadgetGrow = 0.5f;
constexpr float kGadgetPulse = 0.5f;
constexpr int32_t kGadgetPulses = 2;
constexpr float kLockStart = -5000.0f;
constexpr int32_t kLockDelay = 15;
constexpr int32_t kDamageFlashMax = 20;
constexpr int32_t kTimerNone = -1;
constexpr float kScreenEdge = 9.5f;                 // IsTargetPointOnScreen's margin
constexpr float kNearW = 0.01f;
constexpr float kProjectionScale = 0.77f;
constexpr int kMissionMsgLong = 32;                 // characters
constexpr int kDebugMessageMax = 32;
constexpr int kMissionEndRestart = 2;               // the launch page's unknown978 for a relaunch

// The missions whose lock-on has sounds (an ATargeting)
const char *const kTargetingMissions[] = {"junglea_mis13a", "snow2a_mis4", "uw_mis11"};

// The cheats: a length, then the actions (the original keeps them at 0x001ca6a0, 0x48 bytes apart)
const int32_t kCheatCheats1[] = {14, kCheatTriggerLeft, kCheatButLeft, kCheatButLeftRelease, kCheatButUp,
                                 kCheatButUpRelease, kCheatButRight, kCheatButRightRelease, kCheatButLeft,
                                 kCheatButLeftRelease, kCheatButUp, kCheatButUpRelease, kCheatButRight,
                                 kCheatButRightRelease, kCheatTriggerLeftRelease};
const int32_t kCheatCheats2[] = {10, kCheatTriggerLeft, kCheatButLeft, kCheatButLeftRelease, kCheatButRight,
                                 kCheatButRightRelease, kCheatButRight, kCheatButRightRelease, kCheatButLeft,
                                 kCheatButLeftRelease, kCheatTriggerLeftRelease};
const int32_t kCheatRace[] = {12, kCheatTriggerLeft, kCheatButRight, kCheatButRightRelease, kCheatButRight,
                              kCheatButRightRelease, kCheatButLeft, kCheatButLeftRelease, kCheatButLeft,
                              kCheatButLeftRelease, kCheatButUp, kCheatButUpRelease, kCheatTriggerLeftRelease};
const int32_t kCheatRelaunch[] = {12, kCheatTriggerLeft, kCheatButLeft, kCheatButLeftRelease, kCheatButRight,
                                  kCheatButRightRelease, kCheatButUp, kCheatButUpRelease, kCheatButLeft,
                                  kCheatButLeftRelease, kCheatButUp, kCheatButUpRelease, kCheatTriggerLeftRelease};
const int32_t kCheatShield2[] = {12, kCheatTriggerLeft, kCheatButRight, kCheatButRightRelease, kCheatButUp,
                                 kCheatButUpRelease, kCheatButLeft, kCheatButLeftRelease, kCheatButRight,
                                 kCheatButRightRelease, kCheatButRight, kCheatButRightRelease,
                                 kCheatTriggerLeftRelease};
const int32_t kCheatShield3[] = {14, kCheatTriggerLeft, kCheatButRight, kCheatButRightRelease, kCheatButUp,
                                 kCheatButUpRelease, kCheatButLeft, kCheatButLeftRelease, kCheatButRight,
                                 kCheatButRightRelease, kCheatButRight, kCheatButRightRelease, kCheatButRight,
                                 kCheatButRightRelease, kCheatTriggerLeftRelease};
const int32_t kCheatShield4[] = {16, kCheatTriggerLeft, kCheatButRight, kCheatButRightRelease, kCheatButUp,
                                 kCheatButUpRelease, kCheatButLeft, kCheatButLeftRelease, kCheatButRight,
                                 kCheatButRightRelease, kCheatButRight, kCheatButRightRelease, kCheatButRight,
                                 kCheatButRightRelease, kCheatButRight, kCheatButRightRelease,
                                 kCheatTriggerLeftRelease};
const int32_t kCheatAll[] = {14, kCheatTriggerLeft, kCheatButLeft, kCheatButLeftRelease, kCheatButUp,
                             kCheatButUpRelease, kCheatButUp, kCheatButUpRelease, kCheatButLeft,
                             kCheatButLeftRelease, kCheatButUp, kCheatButUpRelease, kCheatButRight,
                             kCheatButRightRelease, kCheatTriggerLeftRelease};
const int32_t kCheatFlag1000[] = {10, kCheatTriggerLeft, kCheatButRight, kCheatButRightRelease, kCheatButRight,
                                  kCheatButRightRelease, kCheatButRight, kCheatButRightRelease, kCheatButRight,
                                  kCheatButRightRelease, kCheatTriggerLeftRelease};

// The launch page's flags and cheats the cheats toggle
constexpr uint32_t kCheat1 = 0x1;
constexpr uint32_t kCheat2 = 0x2;
constexpr uint32_t kLaunchFlag6 = 0x6;
constexpr uint32_t kLaunchFlag80 = 0x80;
constexpr uint32_t kLaunchFlag100 = 0x100;
constexpr uint32_t kLaunchFlag2000 = 0x2000;

constexpr int kRaceMission = 8;

const char kMenuSounds[] = "Menu Sounds";

void PlayMenuSound(const char *patch) {
    AMenuSound::Trigger(8, patch, kMenuSounds, 2);
}

} // namespace

// ---------------------------------------------------------------------------------------------------------------
// Construction

// FUNC_AT(0x000dab50)
GHud* GHud::Construct() {
    vtable = reinterpret_cast<const void *>(kHudVtable);
    lockFramesShown = 0;
    targeting = NULL;

    // The orthographic view (GOrthoHudView's constructor, inlined)
    RViewCamera *view = static_cast<RViewCamera *>(UMemory::FastAlloc(sizeof(RViewCamera), "RViewCamera"));
    if (view != NULL) {
        view->Construct(NULL);
        view->vtable = reinterpret_cast<void **>(kHudViewVtable);
        view->transformMode = RViewCamera::kDeviceTransform;
    }
    fHudView = view;
    ActionQueue *queue = static_cast<ActionQueue *>(UMemory::FastAlloc(kActionQueueSize, "ActionQueue"));
    fHudActionQueue = queue != NULL ? queue->Construct(const_cast<char *>("HUDQ")) : NULL;
    void *memory = OperatorNew(kGallerySize);
    gallery = memory != NULL ? static_cast<GGallery *>(memory)->Construct("hudNTSC.gal") : NULL;

    ShapeFile *blood = static_cast<ShapeFile *>(UFileLoader::ShapeFileLoad(const_cast<char *>("data\\render\\blood.xsh"), 0x100));
    EAGL::TAR *tar = static_cast<EAGL::TAR *>(EaglMalloc(sizeof(EAGL::TAR), "EAGL::TAR new"));
    bloodTar = tar != NULL ? tar->Construct(reinterpret_cast<uint8_t *>(blood) + blood->entries[0].offset) : NULL;
    gallery->FONT_Load("data\\loading\\loading.xfn");

    hudGallery = gallery->GET_Gallery();
    hudBaseCanvas = gallery->GET_Canvas(hudGallery, "hud_base");
    hudMsgCanvas = gallery->GET_Canvas(hudGallery, "hud_msg");
    pauseCanvas = gallery->GET_Canvas(hudGallery, "pauseN");
    pauseErrCanvas = gallery->GET_Canvas(hudGallery, "paus_err");
    pauseErrTextPuppets[0] = gallery->GET_Puppet(pauseErrCanvas, "text1", kPuppetKind, 0);
    pauseErrTextPuppets[1] = gallery->GET_Puppet(pauseErrCanvas, "text2", kPuppetKind, 0);
    pauseErrTextPuppets[2] = gallery->GET_Puppet(pauseErrCanvas, "text3", kPuppetKind, 0);
    pauseErrTextPuppets[3] = gallery->GET_Puppet(pauseErrCanvas, "text4", kPuppetKind, 0);
    pauseErrQuadPuppets[0] = gallery->GET_Puppet(pauseErrCanvas, "quad1", kPuppetKind, 0);
    pauseErrQuadPuppets[1] = gallery->GET_Puppet(pauseErrCanvas, "quad2", kPuppetKind, 0);

    crossNormPuppet = gallery->GET_Puppet(hudBaseCanvas, "crossNorm", kPuppetKind, 0);
    crossZoomPuppet = gallery->GET_Puppet(hudBaseCanvas, "crossZoom", kPuppetKind, 0);
    crossTorpNormPuppet = gallery->GET_Puppet(hudBaseCanvas, "crossTorpNorm", kPuppetKind, 0);
    crossTorpRoamPuppet = gallery->GET_Puppet(hudBaseCanvas, "crossTorpRoam", kPuppetKind, 0);
    crossTorpOffscreenPuppet = gallery->GET_Puppet(hudBaseCanvas, "crossTorpOffscreen", kPuppetKind, 0);
    targetZonePuppet = gallery->GET_Puppet(hudBaseCanvas, "TargetZone", kPuppetKind, 0);
    targetLockPuppet = gallery->GET_Puppet(hudBaseCanvas, "TargetLock", kPuppetKind, 0);
    holePuppet = gallery->GET_Puppet(hudBaseCanvas, "hole", kPuppetKind, 0);
    zoomLevelPuppet = gallery->GET_Puppet(hudBaseCanvas, "zoomLevel", kPuppetKind, 0);
    zoomCirclePuppet = gallery->GET_Puppet(hudBaseCanvas, "zoomCircle", kPuppetKind, 0);
    rdrarrowPuppet = gallery->GET_Puppet(hudBaseCanvas, "rdrarrow", kPuppetKind, 0);
    rdrarrowKey = gallery->GET_Key(gallery->GET_Puppet(hudBaseCanvas, "rdrarrow", kPuppetKind, 0), 1);
    rdrarrowbrdrPuppet = gallery->GET_Puppet(hudBaseCanvas, "rdrarrowbrdr", kPuppetKind, 0);
    radarPuppet = gallery->GET_Puppet(hudBaseCanvas, "radar", kPuppetKind, 0);
    radarBorderPuppet = gallery->GET_Puppet(hudBaseCanvas, "radar_border", kPuppetKind, 0);
    radarStencilPuppet = gallery->GET_Puppet(hudBaseCanvas, "radar_stencil", kPuppetKind, 0);
    pulsePuppet = gallery->GET_Puppet(hudBaseCanvas, "pulse", kPuppetKind, 0);
    fbwPuppets[0] = gallery->GET_Puppet(hudBaseCanvas, "fbw1", kPuppetKind, 0);
    fbwPuppets[1] = gallery->GET_Puppet(hudBaseCanvas, "fbw2", kPuppetKind, 0);
    fbwPuppets[2] = gallery->GET_Puppet(hudBaseCanvas, "fbw3", kPuppetKind, 0);
    fbwPuppets[3] = gallery->GET_Puppet(hudBaseCanvas, "fbw4", kPuppetKind, 0);
    guidedTxtPuppet = gallery->GET_Puppet(hudBaseCanvas, "guidedtxt", kPuppetKind, 0);
    guidedDotPuppet = gallery->GET_Puppet(hudBaseCanvas, "guideddot", kPuppetKind, 0);
    autoAimDotPuppet = gallery->GET_Puppet(hudBaseCanvas, "autoAimDot", kPuppetKind, 0);
    autoAimTextPuppet = gallery->GET_Puppet(hudBaseCanvas, "autoAimText", kPuppetKind, 0);

    statusPuppet = gallery->GET_Puppet(hudMsgCanvas, "status", kPuppetKind, 0);
    statusBackPuppet = gallery->GET_Puppet(hudMsgCanvas, "statusback", kPuppetKind, 0);
    statusBackTopPuppet = gallery->GET_Puppet(hudMsgCanvas, "statusbacktop", kPuppetKind, 0);
    statusBackBotPuppet = gallery->GET_Puppet(hudMsgCanvas, "statusbackbot", kPuppetKind, 0);
    for (int key = 1; key <= 2; key++) {
        statusBackKeys[key - 1] = gallery->GET_Key(gallery->GET_Puppet(hudMsgCanvas, "statusback", kPuppetKind, 0), key);
        statusBackTopKeys[key - 1] =
            gallery->GET_Key(gallery->GET_Puppet(hudMsgCanvas, "statusbacktop", kPuppetKind, 0), key);
        statusBackBotKeys[key - 1] =
            gallery->GET_Key(gallery->GET_Puppet(hudMsgCanvas, "statusbackbot", kPuppetKind, 0), key);
    }
    buttonTxtPuppet = gallery->GET_Puppet(hudMsgCanvas, "buttonTxt", kPuppetKind, 0);

    pausePagePuppets[0] = gallery->GET_Puppet(pauseCanvas, "00", kPuppetKind, 0);
    pausePagePuppets[1] = gallery->GET_Puppet(pauseCanvas, "01", kPuppetKind, 0);
    pausePagePuppets[2] = gallery->GET_Puppet(pauseCanvas, "02", kPuppetKind, 0);
    continueMisPuppet = gallery->GET_Puppet(pauseCanvas, "continueMis", kPuppetKind, 0);
    restartMisPuppet = gallery->GET_Puppet(pauseCanvas, "restartMis", kPuppetKind, 0);
    quitMisPuppet = gallery->GET_Puppet(pauseCanvas, "quitMis", kPuppetKind, 0);
    missionPuppet = gallery->GET_Puppet(pauseCanvas, "mission", kPuppetKind, 0);
    objectivesPuppet = gallery->GET_Puppet(pauseCanvas, "objectives", kPuppetKind, 0);
    controlsPuppet = gallery->GET_Puppet(pauseCanvas, "controls", kPuppetKind, 0);
    scorePuppet = gallery->GET_Puppet(pauseCanvas, "score", kPuppetKind, 0);
    arrowPuppet = gallery->GET_Puppet(pauseCanvas, "arrow", kPuppetKind, 0);
    controlsArrowLPuppet = gallery->GET_Puppet(pauseCanvas, "controlsArrowL", kPuppetKind, 0);
    controlsArrowRPuppet = gallery->GET_Puppet(pauseCanvas, "controlsArrowR", kPuppetKind, 0);
    controlsGelLPuppet = gallery->GET_Puppet(pauseCanvas, "controlsGelL", kPuppetKind, 0);
    controlsGelMPuppet = gallery->GET_Puppet(pauseCanvas, "controlsGelM", kPuppetKind, 0);
    controlsGelRPuppet = gallery->GET_Puppet(pauseCanvas, "controlsGelR", kPuppetKind, 0);
    controlsHeaderPuppet = gallery->GET_Puppet(pauseCanvas, "controlsHeader", kPuppetKind, 0);
    controlsDetailsPuppet = gallery->GET_Puppet(pauseCanvas, "controlsDetails", kPuppetKind, 0);
    controlsYAxisPuppet = gallery->GET_Puppet(pauseCanvas, "controlsYAxis", kPuppetKind, 0);
    controlsInvertedPuppet = gallery->GET_Puppet(pauseCanvas, "controlsInverted", kPuppetKind, 0);
    continuePuppet = gallery->GET_Puppet(pauseCanvas, "continue", kPuppetKind, 0);
    selectPuppet = gallery->GET_Puppet(pauseCanvas, "select", kPuppetKind, 0);
    hintPuppet = gallery->GET_Puppet(pauseCanvas, "hint", kPuppetKind, 0);
    backPuppet = gallery->GET_Puppet(pauseCanvas, "back", kPuppetKind, 0);
    invertPuppet = gallery->GET_Puppet(pauseCanvas, "invert", kPuppetKind, 0);
    changepagePuppet = gallery->GET_Puppet(pauseCanvas, "changepage", kPuppetKind, 0);
    scrollPuppet = gallery->GET_Puppet(pauseCanvas, "scroll", kPuppetKind, 0);
    restartConfirmPuppet = gallery->GET_Puppet(pauseCanvas, "restartConfirm", kPuppetKind, 0);
    quitConfirmPuppet = gallery->GET_Puppet(pauseCanvas, "quitConfirm", kPuppetKind, 0);
    yesPuppet = gallery->GET_Puppet(pauseCanvas, "yes", kPuppetKind, 0);
    noPuppet = gallery->GET_Puppet(pauseCanvas, "no", kPuppetKind, 0);
    scratchTxtPuppet = gallery->GET_Puppet(pauseCanvas, "scratchTxt", kPuppetKind, 0);
    objCheckPuppet = gallery->GET_Puppet(pauseCanvas, "objCheck", kPuppetKind, 0);
    objBulletPuppet = gallery->GET_Puppet(pauseCanvas, "objBullet", kPuppetKind, 0);
    hintTitlePuppet = gallery->GET_Puppet(pauseCanvas, "hintTitle", kPuppetKind, 0);
    hintTextPuppet = gallery->GET_Puppet(pauseCanvas, "hintText", kPuppetKind, 0);
    hintQuitPuppet = gallery->GET_Puppet(pauseCanvas, "hintQuit", kPuppetKind, 0);
    continueButXBOXPuppet = gallery->GET_Puppet(pauseCanvas, "continueButXBOX", kPuppetKind, 0);
    selectButXBOXPuppet = gallery->GET_Puppet(pauseCanvas, "selectButXBOX", kPuppetKind, 0);
    backButXBOXPuppet = gallery->GET_Puppet(pauseCanvas, "backButXBOX", kPuppetKind, 0);
    changepageButXBOXPuppet = gallery->GET_Puppet(pauseCanvas, "changepageButXBOX", kPuppetKind, 0);
    scrollButXBOXPuppet = gallery->GET_Puppet(pauseCanvas, "scrollButXBOX", kPuppetKind, 0);
    invertButXBOXPuppet = gallery->GET_Puppet(pauseCanvas, "invertButXBOX", kPuppetKind, 0);
    controlsXBOXAPuppet = gallery->GET_Puppet(pauseCanvas, "controlsXBOXA", kPuppetKind, 0);
    controlsXBOXYPuppet = gallery->GET_Puppet(pauseCanvas, "controlsXBOXY", kPuppetKind, 0);
    controlsXBOXXPuppet = gallery->GET_Puppet(pauseCanvas, "controlsXBOXX", kPuppetKind, 0);
    controlsXBOXBPuppet = gallery->GET_Puppet(pauseCanvas, "controlsXBOXB", kPuppetKind, 0);
    controlsXBOXDHorPuppet = gallery->GET_Puppet(pauseCanvas, "controlsXBOXDHor", kPuppetKind, 0);
    controlsXBOXDVertPuppet = gallery->GET_Puppet(pauseCanvas, "controlsXBOXDVert", kPuppetKind, 0);
    controlsXBOXLAPuppet = gallery->GET_Puppet(pauseCanvas, "controlsXBOXLA", kPuppetKind, 0);

    // The triggers' pictures carry the language's words
    const char *trigger;
    switch (GLocale::GetLanguage()) {
    case kLanguageFrench:
        controlsLeftTriggerPuppet = gallery->GET_Puppet(pauseCanvas, "controlsXBOXFrLeft", kPuppetKind, 0);
        controlsRightTriggerPuppet = gallery->GET_Puppet(pauseCanvas, "controlsXBOXFrSpItRight", kPuppetKind, 0);
        trigger = "XBOXFrSpItRT";
        break;
    case kLanguageSpanish:
        controlsLeftTriggerPuppet = gallery->GET_Puppet(pauseCanvas, "controlsXBOXSpLeft", kPuppetKind, 0);
        controlsRightTriggerPuppet = gallery->GET_Puppet(pauseCanvas, "controlsXBOXFrSpItRight", kPuppetKind, 0);
        trigger = "XBOXFrSpItRT";
        break;
    case kLanguageItalian:
        controlsLeftTriggerPuppet = gallery->GET_Puppet(pauseCanvas, "controlsXBOXItLeft", kPuppetKind, 0);
        controlsRightTriggerPuppet = gallery->GET_Puppet(pauseCanvas, "controlsXBOXFrSpItRight", kPuppetKind, 0);
        trigger = "XBOXFrSpItRT";
        break;
    case kLanguageSwedish:
        controlsLeftTriggerPuppet = gallery->GET_Puppet(pauseCanvas, "controlsXBOXSwLeft", kPuppetKind, 0);
        controlsRightTriggerPuppet = gallery->GET_Puppet(pauseCanvas, "controlsXBOXSwRight", kPuppetKind, 0);
        trigger = "XBOXSwRT";
        break;
    default:
        controlsLeftTriggerPuppet = gallery->GET_Puppet(pauseCanvas, "controlsXBOXLT", kPuppetKind, 0);
        controlsRightTriggerPuppet = gallery->GET_Puppet(pauseCanvas, "controlsXBOXRT", kPuppetKind, 0);
        trigger = "XBOXRT";
        break;
    }
    triggerPuppet = gallery->GET_Puppet(hudMsgCanvas, trigger, kPuppetKind, 0);
    xboxRTPuppet = gallery->GET_Puppet(hudMsgCanvas, trigger, kPuppetKind, 0);
    controlsXBOXBlackPuppet = gallery->GET_Puppet(pauseCanvas, "controlsXBOXBlack", kPuppetKind, 0);
    controlsXBOXRAPuppet = gallery->GET_Puppet(pauseCanvas, "controlsXBOXRA", kPuppetKind, 0);
    controlsXBOXWhitePuppet = gallery->GET_Puppet(pauseCanvas, "controlsXBOXWhite", kPuppetKind, 0);
    xboxAPuppet = gallery->GET_Puppet(hudMsgCanvas, "XBOXA", kPuppetKind, 0);
    xboxXPuppet = gallery->GET_Puppet(hudMsgCanvas, "XBOXX", kPuppetKind, 0);

    char name[12];
    for (int i = 0; i < 6; i++) {
        sprintf(name, "gadget%d", i);
        gadgetPuppets[i] = gallery->GET_Puppet(hudBaseCanvas, name, kPuppetKind, 0);
    }
    stylinPuppet = gallery->GET_Puppet(hudBaseCanvas, "stylin", kPuppetKind, 0);
    weaponPuppet = gallery->GET_Puppet(hudBaseCanvas, "weapon", kPuppetKind, 0);
    ammoPuppet = gallery->GET_Puppet(hudBaseCanvas, "ammo", kPuppetKind, 0);
    char irisName[128];
    for (int i = 0; i < 7; i++) {
        sprintf(irisName, "iris%d", i + 1);
        irisPuppets[i] = gallery->GET_Puppet(hudBaseCanvas, irisName, kPuppetKind, 0);
    }
    armPuppets[0] = gallery->GET_Puppet(hudBaseCanvas, "a007", kPuppetKind, 0);
    char armName[128];
    for (int i = 1; i < 8; i++) {
        sprintf(armName, "arm%d", i);
        armPuppets[i] = gallery->GET_Puppet(hudBaseCanvas, armName, kPuppetKind, 0);
    }
    healthBorderPuppet = gallery->GET_Puppet(hudBaseCanvas, "healthBorder", kPuppetKind, 0);
    blipPuppet = gallery->GET_Puppet(hudBaseCanvas, "blip", kPuppetKind, 0);
    blipBondPuppet = gallery->GET_Puppet(hudBaseCanvas, "blipBond", kPuppetKind, 0);
    blipEnemyPuppet = gallery->GET_Puppet(hudBaseCanvas, "blipEnemy", kPuppetKind, 0);
    blipPowerupPuppet = gallery->GET_Puppet(hudBaseCanvas, "blipPowerup", kPuppetKind, 0);
    blipObjectivePuppet = gallery->GET_Puppet(hudBaseCanvas, "blipObjective", kPuppetKind, 0);
    rechargeBarPuppet = gallery->GET_Puppet(hudBaseCanvas, "rechargeBar", kPuppetKind, 0);
    aredPuppet = gallery->GET_Puppet(hudBaseCanvas, "ared", kPuppetKind, 0);
    radioactivePuppet = gallery->GET_Puppet(hudBaseCanvas, "radioactive", kPuppetKind, 0);
    ayellowPuppet = gallery->GET_Puppet(hudBaseCanvas, "ayellow", kPuppetKind, 0);
    txtPuppet = gallery->GET_Puppet(hudMsgCanvas, "txt", kPuppetKind, 0);
    objCompTxtPuppet = gallery->GET_Puppet(hudMsgCanvas, "objcomptxt", kPuppetKind, 0);
    objNewTxtPuppet = gallery->GET_Puppet(hudMsgCanvas, "objnewtxt", kPuppetKind, 0);
    objObjTxtPuppet = gallery->GET_Puppet(hudMsgCanvas, "objobjtxt", kPuppetKind, 0);
    objBodyPuppet = gallery->GET_Puppet(hudMsgCanvas, "objbody", kPuppetKind, 0);

    // The gauges move up 10, and out towards the corners on other screens
    weaponPuppet->position.y -= 10.0f;
    ammoPuppet->position.y -= 10.0f;
    for (int i = 0; i < 7; i++)
        irisPuppets[i]->position.y -= 10.0f;
    for (int i = 0; i < 6; i++)
        gadgetPuppets[i]->position.y -= 10.0f;
    for (int i = 0; i < 8; i++)
        armPuppets[i]->position.y -= 10.0f;
    healthBorderPuppet->position.y -= 10.0f;

    if (fgRenderer->widescreen && fgRenderer->videoMode != RRenderer::kVideoLetterbox &&
        fgRenderer->videoMode != RRenderer::kVideoWide) {
        weaponPuppet->position.x += 110.0f;
        weaponPuppet->position.y += 60.0f;
        ammoPuppet->position.x += 110.0f;
        ammoPuppet->position.y += 60.0f;
        stylinPuppet->position.x += 110.0f;
        stylinPuppet->position.y += 60.0f;
        for (int i = 0; i < 7; i++) {
            irisPuppets[i]->position.x -= 110.0f;
            irisPuppets[i]->position.y += 60.0f;
        }
        for (int i = 0; i < 8; i++) {
            armPuppets[i]->position.x -= 110.0f;
            armPuppets[i]->position.y += 60.0f;
        }
    } else if (fgRenderer->videoMode == RRenderer::kVideoLetterbox ||
               fgRenderer->videoMode == RRenderer::kVideoWide) {
        weaponPuppet->position.x += 78.0f;
        ammoPuppet->position.x += 78.0f;
        stylinPuppet->position.x += 78.0f;
        for (int i = 0; i < 7; i++)
            irisPuppets[i]->position.x -= 78.0f;
        for (int i = 0; i < 8; i++)
            armPuppets[i]->position.x -= 78.0f;
        for (int i = 0; i < 6; i++)
            gadgetPuppets[i]->position.x += 78.0f;
        healthBorderPuppet->position.x -= 78.0f;
    }

    // The message boxes widen
    if (fgRenderer->widescreen || fgRenderer->videoMode == RRenderer::kVideoLetterbox) {
        int widen = fgRenderer->videoMode == RRenderer::kVideoWide ? 78 : 110;
        GalleryKey *boxes[9] = {
            statusBackPuppet, statusBackTopPuppet, statusBackBotPuppet,
            statusBackKeys[0], statusBackTopKeys[0], statusBackBotKeys[0],
            statusBackKeys[1], statusBackTopKeys[1], statusBackBotKeys[1],
        };
        for (int i = 0; i < 9; i++) {
            boxes[i]->quad.corners[0][0] -= widen;
            boxes[i]->quad.corners[1][0] += widen;
            boxes[i]->quad.corners[2][0] -= widen;
            boxes[i]->quad.corners[3][0] += widen;
        }
        rechargeBarPuppet->position.x += widen;
    }

    switch (GetFoundationVideoMode()) {
    case 2:
        frameRate = 25.0f;
        break;
    case 3:
        frameRate = 60.0f;
        break;
    default:
        frameRate = 30.0f;
        break;
    }
    Reset();
    fHudApp = this;
    return this;
}

// The view is destroyed through its own vtable; the action queue and the gallery are not cleared.
// FUNC_AT(0x000dc420)
void GHud::Destruct() {
    vtable = reinterpret_cast<const void *>(kHudVtable);
    fHudApp = NULL;
    if (bloodTar != NULL) {
        bloodTar->Destruct();
        EaglFree(bloodTar, sizeof(EAGL::TAR));
    }
    if (fHudView != NULL) {
        typedef RViewCamera* (RViewCamera::*DeleteMethod)(unsigned flags);
        (fHudView->*XbeVirtual<DeleteMethod>(fHudView, 0))(1);
    }
    ActionQueue *queue = fHudActionQueue;
    if (queue != NULL) {
        queue->Destruct();
        UMemory::FastFree(queue, kActionQueueSize);
    }
    if (gallery != NULL) {
        gallery->Destruct();
        OperatorDelete(gallery);
    }
}

// FUNC_AT(0x000d7c10)
GHud* GHud::TheApp() {
    return fHudApp;
}

// FUNC_AT(0x000d7da0)
void GHud::Reset() {
    paused = false;
    controllerUnplugged = false;
    letterBoxMode = kLetterBoxOff;
    letterBoxSize = 0;
    gadgetTime = 0.0f;
    gadgetPulse = 0.0f;
    gadgetPulses = 0;
    gadgetGrow = 0.0f;
    zoomCrossHairsOn = false;
    lockFrames = 0;
    lockFramesShown = 0;
    timerLastSecond = 10;
    healthShowTime = kHealthShowTime;
    healthFadeTime = 0.0f;
    healthBlinkTime = 0.0f;
    weaponShowTime = kWeaponShowTime;
    unknown2E0 = 0.0f;
    unknown2E4 = 0.0f;
    weaponFadeTime = kWeaponFadeTime;
    stylinState = 0;
    stylinTime = 0.0f;
    stylinFade = 0.0f;
    buttonMsg = 0;
    buttonMsgTime = 0.0f;
    buttonMsgFade = 0.0f;
    objectiveMsgFlag = false;
    missionResultText = -1;
    missionResultState = 0;
    missionResultHold = 0.0f;
    missionResultFadeIn = 0.0f;
    missionResultDelay = 0.0f;
    missionResultStep = 0;
    damageFlash = 0;
    damageFlashLength = 0;
    lavaDamageStep = 0;
    shownHealth = 1.0f;
    autoDrive = false;
    missionMsgTime = 0.0f;
    missionMsgLong = false;
    missionMsg = -1;
    lastStep = 0;
    unknown338 = 0;
    unknown334 = 0;
    unknown33C = 0;
    unknown374 = 0;
    unknown2B3 = 0;
    unknown378 = 0;
    targetLockState = 0;
    lockStep = 0;
    unknown3D8 = 0;
    instanceTargetShown = false;
    objectiveTargetShown = false;
    language = GLocale::GetLanguage();
    unknown3A4 = 0;
    aimOn = false;
    zoomPercent = 0.0f;
    blackFadePct = 0.0f;
    blackFadeAnimating = false;
    blackFadeStartStep = 0;
    blackFadeStart = 0.0f;
    blackFadeRate = 0.0f;
    timer = kTimerNone;
    levelLoadText = false;
    unknown340 = 26;
    unknown344 = 13;
    unknown348 = 0;
    unknown431 = Launch.unknown998 != 0;
    unknown432 = Launch.unknown99c != 0;
    objectiveMsgId = -1;
    nextObjectiveMsgId = -1;
    nextObjectiveMsgKind = -1;
    objectiveMsgKind = -1;
    objectivePulseTime = 0.0f;
    objectivePulses = 0;
    objectiveMsgTime = 0.0f;
    restartRequested = false;
    exitRequested = false;
    unknown3FC = 0;
    unknown400 = 0;
    if (GSubtitles::TheApp() != NULL)
        GSubtitles::TheApp()->ClearSubtitles();
    pausePage = kPauseMission;
    pauseSelection = 0;
    pauseObjective = 0;
    unknown47C = 0;
    pauseConfirm = 0;
    pauseError = 0;
    pauseObjectivesBrowse = 0;
    unknown494 = 0;
    unknown498 = 0;
    memset(cheatInput, 0, sizeof(cheatInput));
    target.y = 0.0f;
    target.x = 0.0f;

    int width = fgRenderer->screenWidth;
    screenWidth = (float)width;
    int height = fgRenderer->screenHeight;
    screenHeight = (float)height;
    centerX = (float)(width * 0.5);
    centerY = (float)(height * 0.5);
    superEasy = glbMissionManager->CheckProgrammerEvent(kEventSuperEasy);
    bloodCurtainTime = kNoBloodCurtain;
    targetScaleX = 1.0f;
    targetScaleY = 1.0f;
    targetOffsetX = 0.0f;
    targetOffsetY = 0.0f;

    if (targeting != NULL) {
        OperatorDelete(targeting);
        targeting = NULL;
    }
    if (_stricmp(Launch.missionName, kTargetingMissions[0]) == 0 ||
        _stricmp(Launch.missionName, kTargetingMissions[1]) == 0 ||
        _stricmp(Launch.missionName, kTargetingMissions[2]) == 0) {
        void *memory = OperatorNew(sizeof(ATargeting));
        targeting = memory != NULL ? static_cast<ATargeting *>(memory)->Construct() : NULL;
    }
    fHudActionQueue->Flush();
}

// ---------------------------------------------------------------------------------------------------------------
// State

// FUNC_AT(0x000d7c20)
void GHud::SetLevelLoadText() {
    levelLoadText = true;
}

// FUNC_AT(0x000d7c30)
void GHud::EnableLavaDamageEffect(bool on) {
    if (on)
        lavaDamageStep = fgSimulation.stepCount + 1;
    else
        lavaDamageStep = 0;
}

// FUNC_AT(0x000d7c60)
bool GHud::LavaDamageEffectEnabled() {
    return lavaDamageStep != 0;
}

// FUNC_AT(0x000d7c70)
void GHud::FadeInFromBlack(int steps) {
    blackFadeAnimating = true;
    blackFadeStartStep = fgSimulation.stepCount;
    if (steps < 1)
        steps = 1;
    blackFadeStart = 1.0f;
    blackFadePct = 1.0f;
    blackFadeRate = (float)(-1.0 / steps);
    ScreenFade = 1;
}

// FUNC_AT(0x000d7cd0)
void GHud::FadeOutToBlack(int steps) {
    blackFadeAnimating = true;
    blackFadeStartStep = fgSimulation.stepCount;
    if (steps < 1)
        steps = 1;
    blackFadeStart = 0.0f;
    blackFadePct = 0.0f;
    blackFadeRate = (float)(1.0 / steps);
    if (glbMissionManager->GoingToSkipCinematic())
        GSubtitles::TheApp()->ClearSubtitles();
    ScreenFade = -1;
}

// FUNC_AT(0x000d7d40)
bool GHud::GetBlackFadeAnimating() {
    return blackFadeAnimating;
}

// FUNC_AT(0x000d7d50)
float GHud::GetBlackFadePct() {
    return blackFadePct;
}

// FUNC_AT(0x000d7d60)
void GHud::Stylin() {
    stylinFade = 0.0f;
    stylinState = 1;
    stylinTime = kStylinZoom / frameRate;
}

// FUNC_AT(0x000d7d90)
float GHud::RadarRadiusInMeters() {
    return kRadarRadius;
}

// A flash already counting down past 5 is not lengthened. (The counts compare unsigned.)
// FUNC_AT(0x000d87a0)
void GHud::TriggerDamageFlash(float damage) {
    if ((uint32_t)damageFlash >= 5)
        return;
    damageFlashLength += Ftol((double)damage + 0.5) + 1;
    if ((uint32_t)damageFlashLength > kDamageFlashMax)
        damageFlashLength = kDamageFlashMax;
    damageFlash = damageFlashLength;
}

// FUNC_AT(0x000d87f0)
void GHud::TriggerBloodCurtain() {
    bloodCurtainTime = 8.0f;
}

// FUNC_AT(0x000d8800)
void GHud::TriggerTimerOn(int timer) {
    this->timer = timer;
}

// FUNC_AT(0x000d8810)
void GHud::TriggerTimerOff(int) {
    timer = kTimerNone;
}

// FUNC_AT(0x000d9040)
void GHud::SetAutoDrive(bool on) {
    autoDrive = on;
}

// FUNC_AT(0x000d9b30)
void GHud::LetterBoxOn(bool immediate) {
    aimOn = false;
    if (immediate) {
        letterBoxMode = kLetterBoxOn;
        letterBoxSize = kLetterBoxSteps;
    } else {
        letterBoxMode = kLetterBoxOpening;
        letterBoxSize = 0;
    }
}

// FUNC_AT(0x000d9b70)
void GHud::LetterBoxOff(bool immediate) {
    letterBoxMode = immediate ? kLetterBoxOff : kLetterBoxClosing;
}

// FUNC_AT(0x000d9b90)
int GHud::GetLetterBoxMode() {
    return letterBoxMode;
}

// FUNC_AT(0x000d9ba0)
void GHud::AimOn() {
    aimOn = true;
    if (superEasy)
        AMenuSound::Trigger(0, "SFX_whoosh", "1:Effects", 0);
}

// FUNC_AT(0x000d9bd0)
void GHud::AimOff() {
    aimOn = false;
    SniperZoom->active = 0;
}

// FUNC_AT(0x000d9bf0)
void GHud::GadgetOn(int gadget, float time) {
    this->gadget = gadget;
    gadgetTime = time;
    gadgetPulse = kGadgetPulse;
    gadgetPulses = kGadgetPulses;
    gadgetGrow = kGadgetGrow;
}

// FUNC_AT(0x000d9c30)
void GHud::GadgetOff() {
    gadgetTime = 0.0f;
    gadgetPulse = 0.0f;
    gadgetPulses = 0;
    gadgetGrow = 0.0f;
}

// FUNC_AT(0x000d9f50)
void GHud::SetTarget(const ScreenPos *cursor) {
    target = *cursor;
    if (!fgRenderer->widescreen)
        target.x = (float)(((double)target.x - targetOffsetX) / targetScaleX);
    target.y = (float)(((double)target.y - targetOffsetY) / targetScaleY);
}

// FUNC_AT(0x000d9fb0)
void GHud::SetTargetLockState(int state) {
    if (targetLockState == kLockNone && state == kLockTracking) {
        lockStep = fgSimulation.stepCount + kLockDelay;
        lockFrames = 0;
        lockX = kLockStart;
        lockY = kLockStart;
    }
    targetLockState = state;
    if (targeting != NULL && fgRenderHigh->views[0].camera->cameraMode != fgCameraModeIndices.missile)
        targeting->SetState((TargetLockState)state);
}

// A new message waits while a mission result shows. It also takes the frame's step as the last Render's.
// FUNC_AT(0x000da030)
void GHud::TriggerMissionMsg(int message, float time) {
    if (missionResultState != 0)
        return;
    missionMsg = message;
    missionMsgTime = time;
    missionMsgLong = false;
    if (!fgRenderer->widescreen && strlen(GSystem::LOCALE_Symbol(message)) > kMissionMsgLong)
        missionMsgLong = true;
    lastStep = fgSimulation.stepCount;
}

// While a message shows, a different one is kept for later; either way the button message goes.
// FUNC_AT(0x000da0a0)
void GHud::TriggerObjectiveMsg(int kind, int id, bool flag) {
    if (missionResultState != 0)
        return;
    if (objectiveMsgTime > 0.0f) {
        if (objectiveMsgId != id || objectiveMsgKind != kind) {
            nextObjectiveMsgId = id;
            nextObjectiveMsgKind = kind;
        }
    } else {
        nextObjectiveMsgId = -1;
        nextObjectiveMsgKind = -1;
        objectiveMsgId = id;
        objectiveMsgFlag = flag;
        objectiveMsgKind = kind;
        objectivePulses = 0;
        objectivePulseTime = 0.0f;
        objectiveMsgTime = kObjectiveMsgTime;
        PlayMenuSound("SFX_misstatus");
    }
    buttonMsgTime = 0.0f;
}

// FUNC_AT(0x000da180)
void GHud::TriggerButtonMsg(int message, int, int, int unknown, float time) {
    buttonMsg = message;
    buttonMsgUnknown = unknown;
    buttonMsgTime = time;
    buttonMsgFade = kButtonMsgFade;
    objectivePulses = 0;
    objectivePulseTime = 0.0f;
    objectiveMsgTime = 0.0f;
}

// FUNC_AT(0x000da260)
void GHud::KillButtonMessage() {
    buttonMsg = 0;
    buttonMsgTime = 0.0f;
    buttonMsgFade = 0.0f;
}

// FUNC_AT(0x000da280)
void GHud::dbMessage(const char *text, int steps) {
    if (text == NULL)
        return;
    int length = (int)strlen(text);
    if (length >= kDebugMessageMax)
        length = kDebugMessageMax;
    strncpy(HudDebugMessage, text, length);
    HudDebugMessage[length] = '\0';
    HudDebugMessageEndStep = fgSimulation.stepCount + steps;
}

// The button markers GetButton looks for, by its answer
static const char kButtonMarkers[4][4] = {"*A*", "*G*", "*P*", "*D*"};

// FUNC_AT(0x000da1d0)
int GHud::GetButton(const char *text, char *before, char *after) {
    for (int button = 0; button < 4; button++) {
        const char *marker = strstr(text, kButtonMarkers[button]);
        if (marker != NULL) {
            memcpy(before, text, strlen(text) - strlen(marker));
            strcpy(after, marker + 3);
            return button;
        }
    }
    return -1;
}

// FUNC_AT(0x000d8170)
bool GHud::IsTargetPointOnScreen(const ScreenPos *point) {
    return point->y > kScreenEdge && (double)screenHeight - kScreenEdge > point->y && point->x > kScreenEdge &&
           (double)screenWidth - kScreenEdge > point->x;
}

// The point through the current view port's matrix: on screen if in front and inside the margins.
// FUNC_AT(0x000dc4a0)
void GHud::GetScreenCoords(const Coord3 *position, bool *onScreen, ScreenPos *screen) {
    *onScreen = false;
    float *matrix = fgRenderer->renderContext->GetCurrentViewPort()->GetViewMatrix();
    Coord3 in = *position;
    Coord4 out;
    VU0_MATRIX4_vect3mult(&in, matrix, &out);
    double w = -(double)out.z;
    if (!(w >= kNearW))
        return;
    double inverse = 1.0 / w;
    float y = (float)(-inverse * out.y);
    ScreenPos point;
    point.x = (float)(((double)kProjectionScale * inverse * out.x + 0.5) * screenWidth);
    point.y = (float)((y + 0.5) * screenHeight);
    if (IsTargetPointOnScreen(&point)) {
        *onScreen = true;
        *screen = point;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// The pause menu

// Not while the loading screen shows.
// FUNC_AT(0x000dcae0)
void GHud::PauseOn() {
    if (LoadState != kLoadScreenOff)
        return;
    healthShowTime = kHealthShowTime;
    weaponShowTime = kWeaponShowTime;
    arrowAlpha = 1.0f;
    arrowState = 0;
    if (missionResultText == kTextMissionFailed) {
        pausePage = kPauseFailed;
        pauseSelection = 1;
    } else {
        pausePage = kPauseMission;
        pauseSelection = 0;
    }
    unknown47C = 0;
    pauseConfirm = 0;
    memset(cheatInput, 0, sizeof(cheatInput));
    // The last current objective, counted from 1
    for (int i = glbMissionManager->objectives.size; i >= 0; i--) {
        MissionObjective *objective = glbMissionManager->GetObjectiveByIndex(i - 1);
        if (objective != NULL && objective->state == kObjectiveCurrent) {
            pauseObjective = i;
            break;
        }
    }
    paused = true;
    pauseConfig = InputConfigManager::Get()->GetCurrentConfig(autoDrive);
    EPause *event = new EPause;
    if (event != NULL)
        event->Construct();
}

// FUNC_AT(0x000d9050)
void GHud::PauseOff() {
    if (!paused)
        return;
    GGirl::KillGirl();
    pauseObjectivesBrowse = 0;
    paused = false;
    InputConfigManager::Get()->SetConfig(autoDrive, pauseConfig);
    EResume *event = new EResume;
    if (event != NULL)
        event->Construct();
}

// The quit: back to the action engine unless the mission has already ended, and the HUD asks for the exit.
static void QuitMission(GHud *hud) {
    if (!Launch.IsEndState1Or4())
        Launch.unknown978 = kMissionEndRestart;
    hud->exitRequested = true;
}

// The pause menu's actions: left and right turn the pages, up and down choose, accept and cancel answer.
// (Jump table 0x000dd310, decoded from its bytes: Up, Down, Left, Right, Accept, Cancel, -, -, Invert.)
// FUNC_AT(0x000dcc10)
void GHud::PauseMenuMsgHandler(int action) {
    switch (action) {
    case kFrontEndLeft:
    case kFrontEndRight:
        if (superEasy || missionResultText == kTextMissionFailed)
            return;
        if (pausePage == kPauseObjectives && pauseObjectivesBrowse == 1)
            return;
        if (pauseConfirm != 0)
            return;
        if (action == kFrontEndLeft) {
            pausePage--;
            if (pausePage < 0)
                pausePage = kPauseScore;
        } else {
            pausePage++;
            if (pausePage >= kPausePageCount)
                pausePage = kPauseMission;
        }
        PlayMenuSound("SFX_Toggle");
        return;

    case kFrontEndCancel:
        if (pausePage == kPauseMission && pauseConfirm != 0) {
            pauseConfirm = 0;
            PlayMenuSound("SFX_Toggle");
        }
        return;

    case kFrontEndInvert:
        if (pausePage == kPauseControls && autoDrive) {
            bool inverted = InputConfigManager::Get()->IsInverted();
            InputConfigManager::Get()->SetInverted(!inverted);
            PlayMenuSound("SFX_Toggle");
        }
        return;

    case kFrontEndAccept:
        if (pausePage == kPauseMission) {
            if (pauseSelection == 0) {
                PlayMenuSound("SFX_Exit");
                PauseOff();
            } else if (pauseSelection == 2 && pauseConfirm == 1) {
                PlayMenuSound("SFX_Exit");
                QuitMission(this);
            } else if (pauseSelection == 1 && pauseConfirm == 1) {
                int chain = fgWorld->attributes.LookupInt("CHAIN_RESTART_MISSION", 0);
                const char *chainName = fgWorld->attributes.LookupString("CHAIN_RESTART_MISSION_NAME", 0);
                if (chainName != NULL) {
                    Bond_SetExitToDriving(chain, chainName, true);
                    QuitMission(this);
                } else {
                    glbMissionManager->RestoreLives();
                    restartRequested = true;
                }
            } else if (pauseSelection == 1 || pauseSelection == 2) {
                // The question: asked (no is chosen), or answered no
                if (pauseConfirm == 2) {
                    pauseConfirm = 0;
                    PlayMenuSound("SFX_Toggle");
                } else if (pauseConfirm == 0) {
                    pauseConfirm = 2;
                    PlayMenuSound("SFX_Toggle");
                }
            }
        } else if (pausePage == kPauseObjectives) {
            if (glbMissionManager->objectives.size != 0) {
                pauseObjectivesBrowse = pauseObjectivesBrowse == 0;
                PlayMenuSound("SFX_Toggle");
            }
        }
        if (pausePage == kPauseFailed) {
            // (looked up and not used)
            fgWorld->attributes.LookupInt("CHAIN_RESTART_MISSION", 0);
            fgWorld->attributes.LookupString("CHAIN_RESTART_MISSION_NAME", 0);
            switch (pauseSelection) {
            case 0:
                PlayMenuSound("SFX_Exit");
                // fall through
            case 1:
                glbMissionManager->RestoreLives();
                restartRequested = true;
                return;
            case 2:
                PlayMenuSound("SFX_Exit");
                QuitMission(this);
                return;
            }
        }
        return;

    case kFrontEndUp:
        if (pausePage == kPauseMission || pausePage == kPauseFailed) {
            if (pauseConfirm == 0) {
                pauseSelection--;
                PlayMenuSound("SFX_Toggle");
                if (pausePage == kPauseMission && !(superEasy && missionResultText == kTextMissionFailed)) {
                    if (pauseSelection < 0)
                        pauseSelection = 2;
                } else if (missionResultText == kTextMissionFailed && pauseSelection < 1) {
                    pauseSelection = 2;
                }
            } else if (pauseConfirm == 2) {
                pauseConfirm = 1;
                PlayMenuSound("SFX_Toggle");
            }
        } else if (pausePage == kPauseControls) {
            unsigned configs = InputConfigManager::Get()->GetNumConfigs(autoDrive);
            if (configs > 1) {
                pauseConfig--;
                PlayMenuSound("SFX_Toggle");
            }
            if (pauseConfig < 0)
                pauseConfig = configs - 1;
        } else if (pausePage == kPauseObjectives) {
            if (pauseObjectivesBrowse == 1 || glbMissionManager->objectives.size <= 1)
                return;
            pauseObjective--;
            PlayMenuSound("SFX_Toggle");
            if ((uint32_t)pauseObjective < 1)
                pauseObjective = glbMissionManager->objectives.size;
        }
        return;

    case kFrontEndDown:
        if (pausePage == kPauseMission || pausePage == kPauseFailed) {
            if (pauseConfirm == 0) {
                pauseSelection++;
                PlayMenuSound("SFX_Toggle");
                if (pauseSelection >= 3)
                    pauseSelection = pausePage == kPauseFailed ? 1 : 0;
            } else if (pauseConfirm == 1) {
                pauseConfirm = 2;
                PlayMenuSound("SFX_Toggle");
            }
        } else if (pausePage == kPauseControls) {
            int configs = InputConfigManager::Get()->GetNumConfigs(autoDrive);
            if ((unsigned)configs > 1) {
                pauseConfig++;
                PlayMenuSound("SFX_Toggle");
            }
            if (pauseConfig >= configs)
                pauseConfig = 0;
        } else if (pausePage == kPauseObjectives) {
            if (pauseObjectivesBrowse == 1 || glbMissionManager->objectives.size <= 1)
                return;
            pauseObjective++;
            PlayMenuSound("SFX_Toggle");
            // past the last, to 0 (Up wraps to the last)
            if ((uint32_t)pauseObjective > glbMissionManager->objectives.size)
                pauseObjective = 0;
        }
        return;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Cheats

// The sound plays for a match, and for an empty code.
// FUNC_AT(0x000d90e0)
bool GHud::CheckCheatInput(const int32_t *code) {
    int length = code[0];
    for (int i = 0; i < length; i++)
        if (cheatInput[16 - length + i] != code[1 + i])
            return false;
    PlayMenuSound("SFX_Toggle");
    return true;
}

// FUNC_AT(0x000d9140)
void GHud::CheatMsgHandler(int action) {
    if (action < kCheatDpadLeft || action > kCheatTriggerRightRelease)
        return;
    memmove(&cheatInput[0], &cheatInput[1], 15 * sizeof(cheatInput[0]));
    cheatInput[15] = action;

    if (CheckCheatInput(kCheatCheats1))
        Launch.cheats ^= kCheat1;
    if (CheckCheatInput(kCheatCheats2)) {
        if (Launch.cheats & kCheat2) {
            Launch.cheats &= ~kCheat2;
            fgScheduler->timeScale = 1.0f;
        } else {
            Launch.cheats |= kCheat2;
        }
    }
    if (strcmp(fgWorld->trackName, "snow2a_mis4") == 0) {
        if (CheckCheatInput(kCheatRace)) {
            Launch.flags = (Launch.flags & ~kLaunchFlag100) | kLaunchFlag80;
            Bond_SetExitToDriving(kRaceMission, "snow2a_race", true);
            Launch.unknown978 = kMissionEndRestart;
            exitRequested = true;
        }
        if (CheckCheatInput(kCheatRelaunch)) {
            Launch.flags |= kLaunchFlag100;
            Bond_SetExitToDriving(Launch.missionNum, Launch.missionName, true);
            Launch.unknown978 = kMissionEndRestart;
            exitRequested = true;
        }
    }
    if (CheckCheatInput(kCheatShield2))
        glbMissionManager->SetPlayerShieldTimes(2);
    if (CheckCheatInput(kCheatShield3))
        glbMissionManager->SetPlayerShieldTimes(3);
    if (CheckCheatInput(kCheatShield4))
        glbMissionManager->SetPlayerShieldTimes(4);
    if (CheckCheatInput(kCheatFlag1000))
        Launch.flags ^= kLaunchFlag1000;
    if (CheckCheatInput(kCheatAll)) {
        if (Launch.flags & kLaunchFlag2000) {
            Launch.cheats &= ~(kCheat1 | kCheat2);
            Launch.flags &= ~(kLaunchFlag2000 | kLaunchFlag1000 | kLaunchFlag6);
        } else {
            Launch.cheats |= kCheat1 | kCheat2;
            Launch.flags |= kLaunchFlag2000 | kLaunchFlag1000 | kLaunchFlag6;
        }
    }
}
