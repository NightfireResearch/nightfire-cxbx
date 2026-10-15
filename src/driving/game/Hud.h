#ifndef DRIVING_GAME_HUD_H_
#define DRIVING_GAME_HUD_H_

// ---------------------------------------------------------------------------------------------------------------
// The HUD (Ghidra: GHud, 0x4f0 bytes, one, from operator new in RRenderHigh's constructor; TheApp answers it). It
// owns the HUD gallery "hudNTSC.gal" and the puppets it draws through (the weapon, ammunition, health iris and
// armour, gadgets, crosshairs, radar, messages, the pause menu), the HUD's action queue "HUDQ" and its orthographic
// view, and the state of everything it shows: the letterbox, the black fade, the messages, the timer, the
// crosshairs' lock, the health display, the gadget, the blood curtain, the mission result and the pause menu.
// See Hud.cpp (construction, state, pause and cheats), HudDraw.cpp (the gauges and screen effects) and
// HudText.cpp (the fonts and messages); the large draws are in GFE_B's files.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "../data/CoordConvert.h"         // Coord3
#include "../world/Targeting.h"           // ScreenPos
#include "../../helpers.h"
#include "../../common/xbeClass.h"        // LAYOUT_32

class ATargeting;
class GHud;
class GGallery;
class RViewCamera;
class ActionQueue;
class UGroup;
struct GalleryKey;
namespace EAGL {
struct TAR;
}

// The warning beside a provisional port: the debug HUD's draws, which no shipped run shows. Said once.
inline void HudUntested(const char *what) {
    printf("[hud] WARNING: %s ran - a provisional port that no shipped run reaches, UNTESTED. Check what it "
           "draws against the original.\n", what);
    fflush(stdout);
}

#define HUD_UNTESTED(what) \
    do { \
        static bool warned_; \
        if (!warned_) { \
            warned_ = true; \
            HudUntested(what); \
        } \
    } while (0)

// ---- the HUD's colours: words the static initialisers convert (StaticInitTable.cpp); names ours, after use

extern uint32_t HudGadgetColour;
extern uint32_t HudHealthCriticalColour;        // the iris and border at a quarter of the health or less
extern uint32_t HudHealthLowColour;             // ... at a half or less
extern uint32_t HudHealthColour;                // ... above a half
extern uint32_t HudHealthCriticalDimColour;     // the iris's unlit segments, by the same health
extern uint32_t HudHealthLowDimColour;
extern uint32_t HudHealthDimColour;
extern uint32_t HudTextColour;
extern uint32_t HudObjectiveColour;
extern uint32_t HudColour4d8;
extern uint32_t HudShadowColour;
extern uint32_t HudColour4e0;
extern uint32_t HudCrossColour;
extern uint32_t HudCrossColour4e8;
extern uint32_t HudCrossColour4ec;
extern uint32_t HudCrossColour4f0;
extern uint32_t HudCrossColour4f4;
extern uint32_t HudCrossColour4f8;
extern uint32_t HudCrossColour4fc;
extern uint32_t HudCrossColour500;
extern uint32_t HudCrossColour504;
extern uint32_t HudCrossColour508;
extern uint32_t HudCrossColour50c;
extern uint32_t HudCrossColour510;
extern uint32_t HudHoleColour;
extern uint32_t HudBloodColour;
extern uint32_t HudBloodColour2;
extern uint32_t HudLetterBoxColour;
extern uint32_t HudTimerColour;
extern uint32_t HudTimerWarningColour;
extern uint32_t HudStylinColour;
extern float HudArrowFadeStep;                  // the blinking arrow's alpha step
extern uint32_t HudColour54c;
extern uint32_t HudColour550;
extern uint32_t HudMessageBoxColour;
extern uint32_t HudTimerBoxColour;
extern uint32_t HudCrossColour560;

// ---- the HUD's other globals

extern GHud *fHudApp;                           // TheApp's (Hud.cpp, as the two below)
extern ActionQueue *fHudActionQueue;            // "HUDQ"; Render reads it
extern RViewCamera *fHudView;                   // the GOrthoHudView; Render renders it
extern bool gHudOn;                             // clear: only the aiming crosshairs are drawn (HudFrame.cpp)
extern int16_t ScreenFade;                      // the black fade's direction, 1 in, -1 out (HudDraw.cpp); AFader reads it

// The package's own statics (names ours): DrawHealth's next grunt (HudDraw.cpp), DrawFlyByWire's frame (HudDraw.cpp),
// dbMessage's text and the step it goes at (Hud.cpp)
extern int32_t HudGruntIndex;
extern int32_t HudFlyByWireFrame;
extern float ScreenFadeLevel;                // DrawScreenFades' level (HudDraw.cpp)
extern char HudDebugMessage[33];
extern int32_t HudDebugMessageEndStep;

// ---- GHud

// letterBoxMode
enum HudLetterBoxMode : int32_t {
    kLetterBoxOn = 0,
    kLetterBoxOff = 1,
    kLetterBoxOpening = 2,              // the bars grow to kLetterBoxSteps, then on
    kLetterBoxClosing = 3,              // they shrink to 0, then off
};
constexpr int32_t kLetterBoxSteps = 30;

// The mission result texts (locale symbols)
constexpr int32_t kTextMissionPassed = 0x18576;
constexpr int32_t kTextMissionFailed = 0x18577;

// pausePage (names ours, after the pause canvas's puppets)
enum HudPausePage : int32_t {
    kPauseMission = 0,                  // continue, restart, quit
    kPauseObjectives = 1,
    kPauseControls = 2,
    kPauseScore = 3,
    kPausePageCount = 4,
    kPauseFailed = 5,                   // the mission failed: restart or quit (SetMissionFailed)
};

// TriggerObjectiveMsg's kind, as the objective events pass it (names ours)
enum ObjectiveMsgKind : int32_t {
    kObjectiveMsgCurrent = 1,           // EObjectiveSetCurrent's, and EObjectiveDisplayText's
    kObjectiveMsgPassed = 2,            // EObjectivePass's
    kObjectiveMsgFailed = 3,            // EObjectiveFail's
};

class GHud {
public:
    const void *vtable;                 // +0x000 0x001a06c0: the scalar deleting destructor
    uint32_t unknown004[3];
    EAGL::TAR *bloodTar;                // +0x010 "data\render\blood.xsh"'s
    uint32_t unknown014;
    GGallery *gallery;                  // +0x018 "hudNTSC.gal"
    UGroup *hudGallery;                 // +0x01c
    UGroup *hudBaseCanvas;              // +0x020 "hud_base"
    GalleryKey *weaponPuppet;           // +0x024
    GalleryKey *ammoPuppet;             // +0x028
    GalleryKey *irisPuppets[7];         // +0x02c "iris1".."iris7": the health
    GalleryKey *armPuppets[8];          // +0x048 "a007", "arm1".."arm7": the shield
    GalleryKey *healthBorderPuppet;     // +0x068
    uint32_t unknown06C[6];
    GalleryKey *stylinPuppet;           // +0x084
    GalleryKey *gadgetPuppets[6];       // +0x088 "gadget0".."gadget5"
    GalleryKey *zoomLevelPuppet;        // +0x0a0
    GalleryKey *zoomCirclePuppet;       // +0x0a4
    GalleryKey *statusPuppet;           // +0x0a8 "hud_msg"'s from here to xboxRTPuppet
    GalleryKey *statusBackPuppet;       // +0x0ac
    GalleryKey *statusBackKeys[2];      // +0x0b0 its keys 1 and 2
    GalleryKey *statusBackTopPuppet;    // +0x0b8
    GalleryKey *statusBackTopKeys[2];   // +0x0bc
    GalleryKey *statusBackBotPuppet;    // +0x0c4
    GalleryKey *statusBackBotKeys[2];   // +0x0c8
    GalleryKey *buttonTxtPuppet;        // +0x0d0
    GalleryKey *triggerPuppet;          // +0x0d4 "XBOXRT", or the language's
    GalleryKey *xboxAPuppet;            // +0x0d8
    GalleryKey *xboxXPuppet;            // +0x0dc
    GalleryKey *xboxRTPuppet;           // +0x0e0 the same name as triggerPuppet
    UGroup *pauseErrCanvas;             // +0x0e4 "paus_err"
    GalleryKey *pauseErrTextPuppets[4]; // +0x0e8 "text1".."text4"
    GalleryKey *pauseErrQuadPuppets[2]; // +0x0f8 "quad1", "quad2"
    UGroup *pauseCanvas;                // +0x100 "pauseN", whose puppets follow
    GalleryKey *pausePagePuppets[3];    // +0x104 "00".."02"
    GalleryKey *continueMisPuppet;      // +0x110
    GalleryKey *restartMisPuppet;       // +0x114
    GalleryKey *quitMisPuppet;          // +0x118
    GalleryKey *missionPuppet;          // +0x11c
    GalleryKey *objectivesPuppet;       // +0x120
    uint32_t unknown124;
    GalleryKey *controlsPuppet;         // +0x128
    GalleryKey *scorePuppet;            // +0x12c
    GalleryKey *arrowPuppet;            // +0x130
    GalleryKey *controlsArrowLPuppet;   // +0x134
    GalleryKey *controlsArrowRPuppet;   // +0x138
    GalleryKey *controlsGelLPuppet;     // +0x13c
    GalleryKey *controlsGelMPuppet;     // +0x140
    GalleryKey *controlsGelRPuppet;     // +0x144
    GalleryKey *controlsHeaderPuppet;   // +0x148
    GalleryKey *controlsDetailsPuppet;  // +0x14c
    GalleryKey *controlsYAxisPuppet;    // +0x150
    GalleryKey *controlsInvertedPuppet; // +0x154
    uint32_t unknown158[4];
    GalleryKey *controlsXBOXAPuppet;    // +0x168
    GalleryKey *controlsXBOXYPuppet;    // +0x16c
    GalleryKey *controlsXBOXXPuppet;    // +0x170
    GalleryKey *controlsXBOXBPuppet;    // +0x174
    GalleryKey *controlsXBOXDHorPuppet; // +0x178
    GalleryKey *controlsXBOXDVertPuppet; // +0x17c
    GalleryKey *controlsXBOXLAPuppet;   // +0x180
    GalleryKey *controlsLeftTriggerPuppet;  // +0x184 "controlsXBOXLT", or the language's
    GalleryKey *controlsXBOXBlackPuppet; // +0x188
    GalleryKey *controlsXBOXRAPuppet;   // +0x18c
    GalleryKey *controlsRightTriggerPuppet; // +0x190 "controlsXBOXRT", or the language's
    GalleryKey *controlsXBOXWhitePuppet; // +0x194
    GalleryKey *hintTitlePuppet;        // +0x198
    GalleryKey *hintTextPuppet;         // +0x19c
    GalleryKey *hintQuitPuppet;         // +0x1a0
    uint32_t unknown1A4[8];
    GalleryKey *objCheckPuppet;         // +0x1c4
    GalleryKey *objBulletPuppet;        // +0x1c8
    GalleryKey *continuePuppet;         // +0x1cc
    GalleryKey *backPuppet;             // +0x1d0
    GalleryKey *invertPuppet;           // +0x1d4
    GalleryKey *selectPuppet;           // +0x1d8
    GalleryKey *hintPuppet;             // +0x1dc
    GalleryKey *changepagePuppet;       // +0x1e0
    GalleryKey *scrollPuppet;           // +0x1e4
    GalleryKey *scrollButXBOXPuppet;    // +0x1e8
    GalleryKey *continueButXBOXPuppet;  // +0x1ec
    GalleryKey *backButXBOXPuppet;      // +0x1f0
    GalleryKey *invertButXBOXPuppet;    // +0x1f4
    GalleryKey *selectButXBOXPuppet;    // +0x1f8
    GalleryKey *changepageButXBOXPuppet; // +0x1fc
    GalleryKey *scratchTxtPuppet;       // +0x200 also the timer's text
    GalleryKey *restartConfirmPuppet;   // +0x204
    GalleryKey *quitConfirmPuppet;      // +0x208
    GalleryKey *yesPuppet;              // +0x20c
    GalleryKey *noPuppet;               // +0x210
    GalleryKey *blipPuppet;             // +0x214 "hud_base"'s from here to holePuppet
    GalleryKey *blipBondPuppet;         // +0x218
    GalleryKey *blipEnemyPuppet;        // +0x21c
    GalleryKey *blipPowerupPuppet;      // +0x220
    GalleryKey *blipObjectivePuppet;    // +0x224
    GalleryKey *rechargeBarPuppet;      // +0x228
    GalleryKey *rdrarrowPuppet;         // +0x22c
    GalleryKey *rdrarrowKey;            // +0x230 its key 1
    GalleryKey *rdrarrowbrdrPuppet;     // +0x234
    GalleryKey *pulsePuppet;            // +0x238
    GalleryKey *radarPuppet;            // +0x23c
    GalleryKey *radarBorderPuppet;      // +0x240
    GalleryKey *radarStencilPuppet;     // +0x244
    GalleryKey *fbwPuppets[4];          // +0x248 "fbw1".."fbw4"
    GalleryKey *guidedTxtPuppet;        // +0x258
    GalleryKey *guidedDotPuppet;        // +0x25c
    GalleryKey *autoAimTextPuppet;      // +0x260
    GalleryKey *autoAimDotPuppet;       // +0x264
    UGroup *hudMsgCanvas;               // +0x268 "hud_msg"
    GalleryKey *txtPuppet;              // +0x26c
    GalleryKey *objCompTxtPuppet;       // +0x270
    GalleryKey *objNewTxtPuppet;        // +0x274
    GalleryKey *objObjTxtPuppet;        // +0x278
    GalleryKey *objBodyPuppet;          // +0x27c
    GalleryKey *crossNormPuppet;        // +0x280
    GalleryKey *crossZoomPuppet;        // +0x284
    GalleryKey *crossTorpNormPuppet;    // +0x288
    GalleryKey *crossTorpRoamPuppet;    // +0x28c
    GalleryKey *crossTorpOffscreenPuppet;   // +0x290
    GalleryKey *targetZonePuppet;       // +0x294
    GalleryKey *targetLockPuppet;       // +0x298
    GalleryKey *holePuppet;             // +0x29c
    uint32_t unknown2A0;
    GalleryKey *aredPuppet;             // +0x2a4
    GalleryKey *radioactivePuppet;      // +0x2a8
    GalleryKey *ayellowPuppet;          // +0x2ac

    bool paused;                        // +0x2b0 set: the fire buttons are ignored, the weapon manager holds its fire
    bool controllerUnplugged;           // +0x2b1
    bool autoDrive;                     // +0x2b2 SetAutoDrive's: picks the auto-drive control configurations
    uint8_t unknown2B3;                 // +0x2b3 Reset clears it
    int32_t letterBoxMode;              // +0x2b4 HudLetterBoxMode
    int32_t letterBoxSize;              // +0x2b8 0..kLetterBoxSteps
    // The pause menu's blinking arrow
    float arrowAlpha;                   // +0x2bc
    float arrowTick;                    // +0x2c0 TIMER_gettick at the last draw
    float arrowTime;                    // +0x2c4 time towards the next alpha step
    int32_t arrowState;                 // +0x2c8 0: start, 1: fading out, 2: fading in
    int32_t timerLastSecond;            // +0x2cc the timer's second at the last beep
    // The health display: shown at full alpha for showTime, then fading (fadeTime); blinkTime when low
    float healthShowTime;               // +0x2d0
    float healthFadeTime;               // +0x2d4
    float healthBlinkTime;              // +0x2d8
    // Written as the health's, by Reset, PauseOn and the weapon selection actions; not read
    float weaponShowTime;               // +0x2dc
    float unknown2E0;                   // +0x2e0
    float unknown2E4;                   // +0x2e4
    float weaponFadeTime;               // +0x2e8
    // Stylin: the "stylin" puppet zooms in, then fades
    float stylinTime;                   // +0x2ec
    int32_t stylinState;                // +0x2f0 0: off, 1: zooming, 2: fading
    float stylinScale;                  // +0x2f4
    float stylinFade;                   // +0x2f8
    // The button message (TriggerButtonMsg)
    int32_t buttonMsg;                  // +0x2fc a locale symbol
    int32_t buttonMsgUnknown;           // +0x300 TriggerButtonMsg's fourth argument
    float buttonMsgTime;                // +0x304
    float buttonMsgFade;                // +0x308
    bool objectiveMsgFlag;              // +0x30c TriggerObjectiveMsg's third argument
    bool zoomCrossHairsOn;              // +0x30d DrawZoomingCrossHairs sets it, Reset clears it
    uint8_t unknown30E[2];
    // The mission result (SetMissionPassed, SetMissionFailed)
    int32_t missionResultText;          // +0x310 kTextMissionPassed or kTextMissionFailed; -1 for none
    int32_t missionResultState;         // +0x314 HudResultState (HudMenus.h); 0: none
    int32_t missionResultStep;          // +0x318 frames into the box's opening (states 1 and 2)
    float missionResultFadeIn;          // +0x31c the text's fade in (state 3)
    float missionResultHold;            // +0x320 the text held (state 4)
    float missionResultDelay;           // +0x324 the fade (state 5); Render ends the mission when it is over
    // The damage flash: TriggerDamageFlash sets both, DrawScreenHits counts the first down
    int32_t damageFlash;                // +0x328
    int32_t damageFlashLength;          // +0x32c up to 20
    int32_t lavaDamageStep;             // +0x330 the step after EnableLavaDamageEffect; 0: off
    int32_t unknown334;                 // +0x334 Reset clears these
    int32_t unknown338;                 // +0x338
    int32_t unknown33C;                 // +0x33c
    int32_t unknown340;                 // +0x340 Reset: 26
    int32_t unknown344;                 // +0x344 Reset: 13
    int32_t unknown348;                 // +0x348 Reset: 0
    // The black fade: pct = start + rate * (steps since startStep), clamped to 0..1
    float blackFadePct;                 // +0x34c
    bool blackFadeAnimating;            // +0x350
    uint8_t unknown351[3];
    int32_t blackFadeStartStep;         // +0x354
    float blackFadeStart;               // +0x358
    float blackFadeRate;                // +0x35c per step
    // The frame's time, from Render
    int32_t lastStep;                   // +0x360 the simulation step at the last Render (TriggerMissionMsg sets it too)
    int32_t steps;                      // +0x364 steps since then
    float elapsed;                      // +0x368 ... in seconds
    // The health shown, as fractions of the maxima
    float shownHealth;                  // +0x36c
    float shownShield;                  // +0x370
    int32_t unknown374;                 // +0x374 Reset clears these
    int32_t unknown378;                 // +0x378
    uint32_t unknown37C;
    // The crosshairs' target (SetTarget, SetTargetLockState)
    ScreenPos target;                   // +0x380 in the HUD's frame
    int32_t targetLockState;            // +0x388 TargetLockState
    int32_t lockFrames;                 // +0x38c 0..7: the zooming crosshairs count up, the fading ones down
    int32_t lockFramesShown;            // +0x390 the count the zoom reached
    float lockX;                        // +0x394 where the zooming crosshairs were (-5000 when a lock starts)
    float lockY;                        // +0x398
    ATargeting *targeting;              // +0x39c the lock's sounds, on the missions that have them
    int32_t lockStep;                   // +0x3a0 the step a lock started, plus 15
    int32_t unknown3A4;                 // +0x3a4 Reset clears it
    // The objective message (TriggerObjectiveMsg), and the next one, kept while one shows
    int32_t objectivePulses;            // +0x3a8 the title's fades in; the body shows from the second
    float objectivePulseTime;           // +0x3ac
    float objectiveMsgTime;             // +0x3b0
    int32_t objectiveMsgId;             // +0x3b4
    int32_t nextObjectiveMsgId;         // +0x3b8 -1 for none
    int32_t objectiveMsgKind;           // +0x3bc
    int32_t nextObjectiveMsgKind;       // +0x3c0 -1 for none
    // The targets on screen (UpdateTargets)
    bool instanceTargetShown;           // +0x3c4
    uint8_t unknown3C5[3];
    float instanceTargetDescription;    // +0x3c8 its article's PhysicsData DESCRIPTION
    uint32_t unknown3CC;
    ScreenPos instanceTarget;           // +0x3d0
    int32_t unknown3D8;                 // +0x3d8 Reset clears it
    bool objectiveTargetShown;          // +0x3dc
    uint8_t unknown3DD[3];
    uint32_t objectiveTargetComment;    // +0x3e0 SMissionManager::GetObjectiveComment's
    uint32_t unknown3E4[3];
    ScreenPos objectiveTarget;          // +0x3f0
    int32_t unknown3F8;
    int32_t unknown3FC;                 // +0x3fc Reset clears these
    int32_t unknown400;                 // +0x400
    // The mission message (TriggerMissionMsg)
    int32_t missionMsg;                 // +0x404 a locale symbol; -1 from Reset
    float missionMsgTime;               // +0x408
    float missionMsgFade;               // +0x40c
    bool missionMsgLong;                // +0x410 more than 32 characters: the weapon and health are not drawn
    uint8_t unknown411[3];
    int32_t language;                   // +0x414 GLocale::GetLanguage at Reset
    bool aimOn;                         // +0x418 AimOn, AimOff
    uint8_t unknown419[3];
    // The gadget (GadgetOn)
    int32_t gadget;                     // +0x41c the gadget puppet; out of range: gadgetPuppets[3]
    float gadgetTime;                   // +0x420 GadgetOn's value; shown while above 0
    float gadgetGrow;                   // +0x424 0.5 down to 0 once the pulses are over
    float gadgetPulse;                  // +0x428 0.5 down to 0, for each pulse
    int32_t gadgetPulses;               // +0x42c
    bool levelLoadText;                 // +0x430 SetLevelLoadText's
    bool unknown431;                    // +0x431 the launch page's unknown998, at Reset: no health flash
    bool unknown432;                    // +0x432 the launch page's unknown99c, at Reset
    bool restartRequested;              // +0x433 GOrthoHudView::PostRender sends ERestart
    bool exitRequested;                 // +0x434 ... and EExit
    uint8_t unknown435[3];
    int32_t pauseConfig;                // +0x438 the control configuration chosen in the pause menu
    float bloodCurtainTime;             // +0x43c
    float screenWidth;                  // +0x440 the renderer's, at Reset
    float screenHeight;                 // +0x444
    float centerX;                      // +0x448
    float centerY;                      // +0x44c
    float zoomPercent;                  // +0x450 the player camera's zoom as the crosshairs last saw it (auto-drive)
    bool superEasy;                     // +0x454 the mission's programmer event 0x18 at Reset (MissionManager.h's
                                        //        kEventSuperEasy)
    uint8_t unknown455[3];
    int32_t timer;                      // +0x458 the mission timer shown; -1 for none
    float targetScaleX;                 // +0x45c the targets' frame: (screen - offset) / scale
    float targetScaleY;                 // +0x460
    float targetOffsetX;                // +0x464
    float targetOffsetY;                // +0x468
    uint32_t unknown46C;
    // The pause menu
    int32_t pausePage;                  // +0x470 HudPausePage
    int32_t pauseSelection;             // +0x474
    int32_t pauseObjective;             // +0x478 the objective shown
    int32_t unknown47C;                 // +0x47c
    uint32_t unknown480;
    int32_t pauseConfirm;               // +0x484 0: none; 1, 2: a yes/no question (2 answers no)
    int32_t pauseObjectivesBrowse;      // +0x488 toggled on the objectives page; set, left and right do not turn
    int32_t pauseError;                 // +0x48c 1: the controller is unplugged (DrawPauseErrors)
    uint32_t unknown490;
    int32_t unknown494;                 // +0x494 Reset clears these
    int32_t unknown498;                 // +0x498
    int32_t cheatInput[16];             // +0x49c the last cheat actions, the newest last
    uint32_t unknown4DC;
    float frameRate;                    // +0x4e0 25, 30 or 60 by the video mode
    uint32_t unknown4E4[3];

    // ---- construction (Hud.cpp)
    GHud* Construct();                                                                          // 0x000dab50
    void Destruct();                                                                            // 0x000dc420
    GHud* Delete(unsigned flags);       // the scalar deleting destructor, vtable slot 0        // 0x000e00d0
    // The one HUD, or null before it exists
    static GHud* TheApp();                                                                      // 0x000d7c10
    void Reset();                                                                               // 0x000d7da0

    // ---- state (Hud.cpp)
    void SetLevelLoadText();                                                                    // 0x000d7c20
    void EnableLavaDamageEffect(bool on);                                                       // 0x000d7c30
    bool LavaDamageEffectEnabled();                                                             // 0x000d7c60
    void FadeInFromBlack(int steps);                                                            // 0x000d7c70
    void FadeOutToBlack(int steps);                                                             // 0x000d7cd0
    bool GetBlackFadeAnimating();                                                               // 0x000d7d40
    float GetBlackFadePct();                                                                    // 0x000d7d50
    void Stylin();                                                                              // 0x000d7d60
    float RadarRadiusInMeters();                                                                // 0x000d7d90
    void TriggerDamageFlash(float damage);                                                      // 0x000d87a0
    void TriggerBloodCurtain();                                                                 // 0x000d87f0
    void TriggerTimerOn(int timer);                                                             // 0x000d8800
    void TriggerTimerOff(int timer);    // the argument is not read                             // 0x000d8810
    void SetAutoDrive(bool on);                                                                 // 0x000d9040
    void LetterBoxOn(bool immediate);                                                           // 0x000d9b30
    void LetterBoxOff(bool immediate);                                                          // 0x000d9b70
    int GetLetterBoxMode();                                                                     // 0x000d9b90
    void AimOn();                                                                               // 0x000d9ba0
    void AimOff();                                                                              // 0x000d9bd0
    void GadgetOn(int gadget, float time);                                                      // 0x000d9bf0
    void GadgetOff();                                                                           // 0x000d9c30
    void SetTarget(const ScreenPos *cursor);                                                    // 0x000d9f50
    void SetTargetLockState(int state);                                                         // 0x000d9fb0
    void TriggerMissionMsg(int message, float time);                                            // 0x000da030
    void TriggerObjectiveMsg(int kind, int id, bool flag);                                      // 0x000da0a0
    void TriggerButtonMsg(int message, int unused2, int unused3, int unknown, float time);      // 0x000da180
    void KillButtonMessage();                                                                   // 0x000da260
    // Copies at most 32 characters of `text` for the debug message, shown for `steps` (does not read `this`)
    static void dbMessage(const char *text, int steps);                                         // 0x000da280
    // The index of the first button marker ("*A*" ...) in `text`, -1 for none; the text before it is copied
    // into `before` (not terminated), the text after it into `after`
    int GetButton(const char *text, char *before, char *after);                                 // 0x000da1d0
    bool IsTargetPointOnScreen(const ScreenPos *point);                                         // 0x000d8170
    void GetScreenCoords(const Coord3 *position, bool *onScreen, ScreenPos *screen);            // 0x000dc4a0

    // ---- the pause menu and cheats (Hud.cpp)
    void PauseOn();                                                                             // 0x000dcae0
    void PauseOff();                                                                            // 0x000d9050
    void PauseMenuMsgHandler(int action);                                                       // 0x000dcc10
    // Whether the last cheat actions end with `code` (its length, then its actions)
    bool CheckCheatInput(const int32_t *code);                                                  // 0x000d90e0
    void CheatMsgHandler(int action);                                                           // 0x000d9140

    // ---- the gauges and screen effects (HudDraw.cpp)
    void DrawHealth();                                                                          // 0x000d81d0
    void DrawScreenHits();                                                                      // 0x000d8820
    void DrawScreenFades();                                                                     // 0x000d8970
    void DrawFlyByWire();                                                                       // 0x000d8ae0
    // Answers the colour drawn
    uint32_t DrawZoomingCrossHairs(GalleryKey *puppet);                                         // 0x000d8c00
    void DrawFadingCrossHairs(GalleryKey *puppet);                                              // 0x000d8ea0
    void DrawPauseErrors();                                                                     // 0x000d93d0
    void DrawBlinkingArrow(float x, float y);                                                   // 0x000d9570
    void DrawGadget();                                                                          // 0x000d9c50
    void DrawLetterBox();                                                                       // 0x000d9e30
    void DrawStylin();                                                                          // 0x000da540
    void DrawBloodCurtain();                                                                    // 0x000dc590
    void DrawTimer();                                                                           // 0x000dc7d0
    void DrawWeapon();                                                                          // 0x000dc990

    // ---- the fonts and messages (HudText.cpp)
    // The x that centres `text` (the puppet's own text when null) at `scale` on the screen, answered unrounded
    double GetCenter(GalleryKey *puppet, float scale, const char *text);                        // 0x000d9370
    // The text (the puppet's own when null) at x + 1, y + 1 in shadowColour, then at x, y in colour
    void DrawFontAndShadow(GalleryKey *puppet, float x, float y, uint32_t shadowColour, uint32_t colour,
                           float scale, const char *text);                                      // 0x000d98d0
    // Lines centred in a box, one under another
    void DrawFontCentered(GalleryKey *puppet, float left, float top, float right, float bottom, float scale,
                          uint32_t colour, unsigned count, char **lines);                       // 0x000d9970
    void DrawFont(GalleryKey *puppet, float x, float y, uint32_t colour, float scale, char **lines,
                  int count);                                                                   // 0x000d9a50
    void DrawFont(GalleryKey *puppet, float x, float y, uint32_t colour, float scale, const char *text); // 0x000d9ad0
    void DrawMessage();                                                                         // 0x000da2e0
    // A debug target: its texts beside a bracket and a blip (the third argument is not read)
    void dbDrawTarget(const ScreenPos *position, const char *text, float unused, uint32_t colour,
                      const char *text2);                                                       // 0x000da8e0

    // ---- the frame, menus, messages and crosshairs (HudFrame.cpp, HudMenus.cpp, HudCrossHairs.cpp)
    void DrawPauseMenu();                                                                       // 0x000dd340
    void SetMissionPassed(bool unused);                                                         // 0x000dee80
    // The result box opens unless `immediate` (or the blood curtain is running, whose time it waits)
    void SetMissionFailed(bool immediate);                                                      // 0x000deee0
    void DrawButtonMessage();                                                                   // 0x000def90
    void DrawMissionResult();                                                                   // 0x000df2e0
    void DrawObjectiveMessage();                                                                // 0x000dfc30
    // Shows or clears the "please reconnect the controller" state; showing it pauses the game
    void SetControllerUnplugged(bool unplugged);                                                // 0x000e00f0
    int Render();                       // answers 0                                            // 0x000e0140
    void DrawCrossHairs();                                                                      // 0x000e0440
    void Draw();                                                                                // 0x000e1970
    void UpdateTargets();                                                                       // 0x000e1a90
};
LAYOUT_32(sizeof(GHud) == 0x4f0, "GHud is 0x4f0 bytes");
LAYOUT_32(offsetof(GHud, gallery) == 0x18 && offsetof(GHud, healthBorderPuppet) == 0x68 &&
          offsetof(GHud, stylinPuppet) == 0x84 && offsetof(GHud, pauseCanvas) == 0x100 &&
          offsetof(GHud, controlsPuppet) == 0x128 && offsetof(GHud, controlsXBOXAPuppet) == 0x168 &&
          offsetof(GHud, objCheckPuppet) == 0x1c4 && offsetof(GHud, hudMsgCanvas) == 0x268 &&
          offsetof(GHud, aredPuppet) == 0x2a4 && offsetof(GHud, paused) == 0x2b0 &&
          offsetof(GHud, letterBoxMode) == 0x2b4 && offsetof(GHud, healthShowTime) == 0x2d0 &&
          offsetof(GHud, buttonMsg) == 0x2fc && offsetof(GHud, missionResultText) == 0x310 &&
          offsetof(GHud, blackFadePct) == 0x34c && offsetof(GHud, lastStep) == 0x360 &&
          offsetof(GHud, target) == 0x380 && offsetof(GHud, targeting) == 0x39c &&
          offsetof(GHud, objectiveMsgTime) == 0x3b0 && offsetof(GHud, instanceTargetShown) == 0x3c4 &&
          offsetof(GHud, objectiveTargetShown) == 0x3dc && offsetof(GHud, objectiveTarget) == 0x3f0 &&
          offsetof(GHud, missionMsg) == 0x404 && offsetof(GHud, gadget) == 0x41c &&
          offsetof(GHud, levelLoadText) == 0x430 && offsetof(GHud, pauseConfig) == 0x438 &&
          offsetof(GHud, screenWidth) == 0x440 && offsetof(GHud, superEasy) == 0x454 &&
          offsetof(GHud, timer) == 0x458 && offsetof(GHud, pausePage) == 0x470 &&
          offsetof(GHud, pauseConfirm) == 0x484 && offsetof(GHud, cheatInput) == 0x49c &&
          offsetof(GHud, frameRate) == 0x4e0, "GHud layout");

// Splits `text` at spaces into lines of at most `width` characters (at most five lines, the last cut short);
// *lines gets an array of five, from operator new[], each line from operator new[] too. Answers the count.
int WordWrap(const char *text, unsigned width, char ***lines);                                  // 0x000d9740

#endif // DRIVING_GAME_HUD_H_
