#pragma fp_contract(off)

#include "EventsC.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>              // _stricmp

#include <bit>

#include "../Scheduler.hpp"             // Scheduler, Schedule
#include "../anim/AnimEngine.h"         // RAnimEngine::Handle
#include "../audio/Bank.h"              // ABank, fgBanks
#include "../audio/Mix.h"               // AMix, AFX
#include "../audio/Sound.h"             // ABaseSound, AOneShotSound, AMenuSound
#include "../audio/SoundManager.h"      // ASoundManager
#include "../audio/ZoomObj.h"           // ASound, AZoomObj
#include "../camera/Camera.h"           // AISplinePath
#include "../camera/PlayerCamera.h"     // RPlayerCamera
#include "../data/Carp.h"               // CARP::Instance, CARP::AISpline, TagReference
#include "../eagl/anim/FnAnim.h"        // AnimPool_Init, AnimPool_Cleanup
#include "../engine/CoreContainers.h"   // GameVector
#include "../engine/Feedback.h"         // IFeedback
#include "../engine/GameInterfaces.hpp" // GHud, GFX
#include "../engine/GameLoop.h"         // LaunchPage
#include "../engine/UMemory.hpp"        // OperatorDelete
#include "../physics/PhysicsObject.h"
#include "../physics/RigidBody.h"       // RigidBody, DamageSourceSig
#include "../platform/RealMath.h"       // VU0
#include "../platform/X87.h"            // Truncate, Ftol
#include "../render/Colorize.h"         // Colorize
#include "../render/Fog.h"              // Fog
#include "../render/PathEngine.h"       // RPathEngine, RPathHandle
#include "../render/Renderer.h"         // fgRenderer
#include "../render/RenderHigh.h"       // fgRenderHigh
#include "../render/RSceneObj.hpp"      // RSceneObj, RandomShort
#include "../render/TimeData.h"         // VideoModeRate, GameTick
#include "../world/CollisionInstance.h" // WTrigger
#include "../world/CollisionManager.h"  // fgCollisionMgr
#include "../world/RoadNetwork.h"       // WRoadNetwork
#include "../world/Targeting.h"         // TargetPicker
#include "../world/Trigger.h"           // gEventDynamicData
#include "../world/TriggerManager.h"    // fgTriggerManager
#include "../world/World.h"             // fgWorld, ArticleEffect, ProcAnimState
#include "../../helpers.h"
#include "AIViews.h"
#include "EventsB.h"                    // PathSFXUpdateData
#include "Missile.h"                    // Missile::detonate
#include "MissionManager.h"             // glbMissionManager
#include "Sentry.h"                     // Sentry::AddMuzzleFlash
#include "Shell.h"                      // RayShell::Reset
#include "Simulation.h"                 // fgSimulation
#include "Vehicle.h"                    // PVehicle, CarPhysics
#include "VehicleSound.h"               // AVehicle
#include "VehicleSoundKinds.h"          // ASubmersible
#include "WeaponManager.h"

// ---------------------------------------------------------------------------------------------------------------
// The events of package C (0x00041850-0x0004ee80, with 0x0003da90 and 0x0003ec60): their MakeEvent and ResolveEvent
// callbacks, their destructors (where each does its work, in Process) and their scalar deleting destructors, and the
// path helper placed among them. Ported from the listing; the AI and front-end calls go to the originals through
// game/AIViews.h and engine/GameInterfaces.hpp.
// ---------------------------------------------------------------------------------------------------------------

// ---- the game's globals

#define fgScheduler (*(Scheduler **)0x001e520c)
#define gFeedbackAllowed BOOL8_AT(0x001b6f50)               // IFeedback's: clear, no force feedback
#define gSoundOption BOOL8_AT(0x001b7054)                   // ASoundManager::Init's second argument
#define gStreams BOOL8_AT(0x001b7056)
#define gSuperEasy BOOL8_AT(0x001e476c)
#define StreamHold I32_AT(0x00243a80)                       // audio/Stream.cpp's StreamHoldState
#define RealClock_Div2Ticks U32_AT(0x001e5208)              // the scheduler's
#define CullDistance FLOAT_AT(0x001c4624)
#define FrameAnimationTime FLOAT_AT(0x001c4668)             // a float: ESetFrameAnimationTime stores it with fstp

// The real clock ERenderFrame last drew a frame at (written only here; the name is ours)
// XBE_GLOBAL(0x001e2394, 0x4)
uint32_t LastRenderClock;

// ---- the vtables (the game's; each has the one slot, the scalar deleting destructor)

#define kECameraLockOnVtable ((vtable_Event *)0x0018c474)
#define kEPathSFXUpdateVtable ((vtable_Event *)0x0018c5cc)
#define kEPathSetAccelDelayDynamicVtable ((vtable_Event *)0x0018c5d0)
#define kEPathSetAccelDelayStaticVtable ((vtable_Event *)0x0018c5d4)
#define kEPathSetAccelerationDynamicVtable ((vtable_Event *)0x0018c5d8)
#define kEPathSetAccelerationStaticVtable ((vtable_Event *)0x0018c5dc)
#define kEPathSetDesiredThrottleDynamicVtable ((vtable_Event *)0x0018c5e0)
#define kEPathSetDesiredThrottleStaticVtable ((vtable_Event *)0x0018c5e4)
#define kEPathSetRunningDynamicVtable ((vtable_Event *)0x0018c5e8)
#define kEPathSetRunningStaticVtable ((vtable_Event *)0x0018c5ec)
#define kEPathSetThrottleDynamicVtable ((vtable_Event *)0x0018c5f0)
#define kEPathSetThrottleStaticVtable ((vtable_Event *)0x0018c5f4)
#define kEPathSetTimeDynamicVtable ((vtable_Event *)0x0018c5f8)
#define kEPathSetTimeStaticVtable ((vtable_Event *)0x0018c5fc)
#define kEPauseVtable ((vtable_Event *)0x0018c600)
#define kEPlayActorEffectVtable ((vtable_Event *)0x0018c604)
#define kEPlayCameraAnimVtable ((vtable_Event *)0x0018c608)
#define kEPlayCameraSplineVtable ((vtable_Event *)0x0018c60c)
#define kEPlayCarCameraAnimVtable ((vtable_Event *)0x0018c610)
#define kEPlayEffectVtable ((vtable_Event *)0x0018c614)
#define kEPlayGiottoEffectVtable ((vtable_Event *)0x0018c618)
#define kEPlayPOVDeathVtable ((vtable_Event *)0x0018c61c)
#define kEPlaySoundVtable ((vtable_Event *)0x0018c620)
#define kEPlaySoundPosVtable ((vtable_Event *)0x0018c630)
#define kEPlaySystemAnimVtable ((vtable_Event *)0x0018c634)
#define kEPlayerLoseVtable ((vtable_Event *)0x0018c638)
#define kEPlayerSetImmunityVtable ((vtable_Event *)0x0018c63c)
#define kEPlayerWinVtable ((vtable_Event *)0x0018c640)
#define kEPowerUpVtable ((vtable_Event *)0x0018c644)
#define kEPowerUpAmmoVtable ((vtable_Event *)0x0018c654)
#define kEPowerUpBrakingVtable ((vtable_Event *)0x0018c668)
#define kEPowerUpHealthVtable ((vtable_Event *)0x0018c66c)
#define kEPowerUpInvulnerableVtable ((vtable_Event *)0x0018c670)
#define kEPowerUpMultiDamageVtable ((vtable_Event *)0x0018c674)
#define kEPowerUpShieldVtable ((vtable_Event *)0x0018c678)
#define kEPowerUpTimerVtable ((vtable_Event *)0x0018c68c)
#define kEPowerUpTopSpeedVtable ((vtable_Event *)0x0018c690)
#define kEProfileMissionSectionVtable ((vtable_Event *)0x0018c694)
#define kERandomExplosionVtable ((vtable_Event *)0x0018c698)
#define kEReleaseStreamVtable ((vtable_Event *)0x0018c69c)
#define kERenderFrameVtable ((vtable_Event *)0x0018c6a0)
#define kEResetAudioMixVtable ((vtable_Event *)0x0018c6a4)
#define kEResetPlayerCarVtable ((vtable_Event *)0x0018c6a8)
#define kEResetPlayerCarPosVtable ((vtable_Event *)0x0018c6ac)
#define kERestartVtable ((vtable_Event *)0x0018c6b0)
#define kEResumeVtable ((vtable_Event *)0x0018c6b4)
#define kERevertAudioMixVtable ((vtable_Event *)0x0018c6b8)
#define kERollSubVtable ((vtable_Event *)0x0018c6bc)
#define kESapphirePoleVtable ((vtable_Event *)0x0018c6c0)
#define kEScaleFieldOfViewVtable ((vtable_Event *)0x0018c6c4)
#define kEScaleFogVtable ((vtable_Event *)0x0018c6c8)
#define kEScheduleEventVtable ((vtable_Event *)0x0018c6cc)
#define kESentryMuzzleFlashVtable ((vtable_Event *)0x0018c6d0)
#define kESetAFXModeVtable ((vtable_Event *)0x0018c6d4)
#define kESetAreaBrightnessVtable ((vtable_Event *)0x0018c6d8)
#define kESetAudioMixVtable ((vtable_Event *)0x0018c6dc)
#define kESetAutoDriveCamOffVtable ((vtable_Event *)0x0018c6e0)
#define kESetAutoDriveCamOffMovieVtable ((vtable_Event *)0x0018c6e4)
#define kESetAutoDriveCamOnVtable ((vtable_Event *)0x0018c6e8)
#define kESetAutoDriveCamOnMovieVtable ((vtable_Event *)0x0018c6f0)
#define kESetAvoidZoneVtable ((vtable_Event *)0x0018c6f4)
#define kESetBitMagicCounterVtable ((vtable_Event *)0x0018c6f8)
#define kESetCollisionGeometryVtable ((vtable_Event *)0x0018c6fc)
#define kESetCreakLevelVtable ((vtable_Event *)0x0018c700)
#define kESetCullDistanceFactorVtable ((vtable_Event *)0x0018c704)
#define kESetFrameAnimationTimeVtable ((vtable_Event *)0x0018c70c)
#define kESetJumpGravityVtable ((vtable_Event *)0x0018c710)
#define kESetMagicCounterVtable ((vtable_Event *)0x0018c714)
#define kESetMagicCounterThresholdVtable ((vtable_Event *)0x0018c718)
#define kESetMissionMessageVtable ((vtable_Event *)0x0018c71c)
#define kESetNextAISplineVtable ((vtable_Event *)0x0018c720)
#define kESetNextPathVtable ((vtable_Event *)0x0018c724)
#define kESetObjectiveVtable ((vtable_Event *)0x0018c728)
#define kESetRecSpeedOffsetVtable ((vtable_Event *)0x0018c72c)

namespace {

constexpr float kOneSixtieth = 1.0f / 60.0f;
static_assert(std::bit_cast<uint32_t>(kOneSixtieth) == 0x3c888889, "1/60");
constexpr float kPercent = 0.01f;
static_assert(std::bit_cast<uint32_t>(kPercent) == 0x3c23d70a, "0.01");
constexpr float kDegrees = 1.0f / 360.0f;   // ESetAutoDriveCamOn's angles, in turns
static_assert(std::bit_cast<uint32_t>(kDegrees) == 0x3b360b61, "1/360");
constexpr float kCullDistanceScale = 400.0f;
constexpr float kOneIn65536 = 1.0f / 65536.0f;
static_assert(std::bit_cast<uint32_t>(kOneIn65536) == 0x37800000, "1/65536");

// The event GFX places with no offset: a zero offset and the identity rotation
const Coord3 kNoOffset = { 0.0f, 0.0f, 0.0f };
const Coord4 kNoRotation = { 0.0f, 0.0f, 0.0f, 1.0f };

// The path sounds' bank slot (EventsB.cpp's kBankSlotPath)
constexpr int kBankSlotPath = 0;

// AMenuSound::Trigger's view as these events pass it
constexpr int kMenuSoundView = 2;
// The menu sounds' bank numbers
constexpr int kPowerUpBank = 0;
constexpr int kSpeechBank = 7;
constexpr float kPowerUpMessageTime = 2.0f;
constexpr int kShieldMessage = 0x684;

// RAnimEngine::Handle::ProcessStimuli's mode as these events pass it (EventsA.cpp's kStimulusMode2), and the power-up
// scene object's stimulus (name ours)
constexpr int kStimulusMode2 = 2;
constexpr uint8_t kPowerUpStimulus = 10;

// RSceneObj::TriggerFX's type for an effect the scene object owns (name ours)
constexpr int kFXOwnEffect = 1;

// ERandomExplosion's explosion (Simulation::SpawnExplosion's first two arguments) and its force
constexpr uint32_t kRandomExplosionKind = 2;
constexpr float kRandomExplosionForce = 20.0f;
constexpr float kRandomExplosionLift = 1.5f;    // added to the height looked up from

// EScheduleEvent's task: event 0x52, once
constexpr int kScheduledEvent = 0x52;

// The death camera (RPlayerCamera::TriggerCarAnimationCamera's arguments; names ours)
constexpr uint32_t kDeathAnimation = 1;
constexpr int kDeathCameraSteps = 0x28;
constexpr int kDeathCameraFlags = 0x80;

constexpr float kDeathFaderVolume = 0.5f;

// ESetAreaBrightness's range and ERollSub's
constexpr int32_t kMaxBrightness = 100;
constexpr int32_t kMaxRoll = 80;

// The memory pool the restart gives the animations back (EAGLAnim's MemoryPoolManager)
constexpr uint32_t kAnimPoolSize = 0xc800;

// The launch page's end state (LaunchPage::unknown978) after a won and a lost mission (names ours)
constexpr int32_t kEndWon = 0;
constexpr int32_t kEndLostAlive = 1;
constexpr int32_t kEndLostDead = 4;

// audio/Stream.cpp's StreamHoldState
constexpr int32_t kHoldWaiting = 1;
constexpr int32_t kHoldReleased = 2;

PVehicle *PlayerCar() {
    return fgSimulation.cars.first[0];
}
RPlayerCamera *PlayerCamera() {
    return fgRenderHigh->views[0].camera;
}

// A time the scripts give in sixtieths of a second, in simulation steps (EventsA.cpp's)
int SixtiethsToSteps(uint32_t time) {
    return Truncate(float(double(time * uint32_t(fgSimulation.stepsPerSecond)) * kOneSixtieth));
}

// The path of the instance's scene object (the "Dynamic" path events'), NULL if either is missing
RPathHandle *InstancePath(const CARP::Instance *instance) {
    RSceneObj *object = fgWorld->GetSceneObjFromInstance(instance);
    return object != NULL ? object->pathHandle : NULL;
}

} // namespace

// =============================================================================================================
// The paths
// =============================================================================================================

// FUNC_AT(0x00048170)
void EPathSFXUpdate::Destruct() {
    vtable = kEPathSFXUpdateVtable;
    Process();
    vtable = kEventVtable;
}

// While the path runs, the sound follows it; once it stops, the end patch, and the task ends
void EPathSFXUpdate::Process() {
    if (!gSoundOption)
        return;
    if (path->running) {
        sound->position = *path->GetPosition();
        sound->velocity = path->velocity;
        return;
    }
    ABank *bank = fgBanks[kBankSlotPath];
    void *memory = ABaseSound::OperatorNew(sizeof(AOneShotSound), "AOneShotSound");
    AOneShotSound *shot = NULL;
    if (memory != NULL) {
        int patch = bank->index.Lookup(endPatch);
        shot = static_cast<AOneShotSound *>(memory)->ConstructAt(bank->handle, patch,
                                                                 path->GetPosition(), 1.0f, "Object Sounds", false);
    }
    // As in the original, a one-shot operator new could not make is written through regardless
    shot->minDistance = sound->minDistance;
    shot->maxDistance = sound->maxDistance;
    shot->maxDistanceSq = sound->maxDistance * sound->maxDistance;
    if (sound != NULL)
        sound->CallDelete(1);
    fgScheduler->s_SimRate->RemoveTask(taskNum);
    OperatorDelete(data);
}

// FUNC_AT(0x0004cda0)
EPathSFXUpdate* EPathSFXUpdate::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00041850)
void EPathSFXUpdate_MakeEvent_Callback(const EPathSFXUpdate::Record *record) {
    EPathSFXUpdate *event = new EPathSFXUpdate;
    if (event != NULL) {
        event->vtable = kEPathSFXUpdateVtable;
        event->data = record->self;
        event->path = record->path;
        event->sound = record->sound;
        event->endPatch = record->unknown10;
        event->taskNum = record->taskNum;
    }
}

// FUNC_AT(0x00041890)
void EPathSetAccelDelayDynamic::Destruct() {
    vtable = kEPathSetAccelDelayDynamicVtable;
    Process();
    vtable = kEventVtable;
}

void EPathSetAccelDelayDynamic::Process() {
    RPathHandle *path = InstancePath(instance);
    if (path != NULL)
        path->SetAccelDelay(accelDelay);
}

// FUNC_AT(0x000482c0)
EPathSetAccelDelayDynamic* EPathSetAccelDelayDynamic::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00041900)
void EPathSetAccelDelayDynamic_MakeEvent_Callback(const EPathSetAccelDelayDynamic::Record *record) {
    EPathSetAccelDelayDynamic *event = new EPathSetAccelDelayDynamic;
    if (event != NULL) {
        event->vtable = kEPathSetAccelDelayDynamicVtable;
        event->instance = gEventDynamicData.instance;
        event->accelDelay = record->accelDelay;
    }
}

// FUNC_AT(0x00041930)
void EPathSetAccelDelayStatic::Destruct() {
    vtable = kEPathSetAccelDelayStaticVtable;
    Process();
    vtable = kEventVtable;
}

void EPathSetAccelDelayStatic::Process() {
    RPathHandle *path = EPathUtil_GetFromInstanceOrAI(instance, element);
    if (path != NULL)
        path->SetAccelDelay(accelDelay);
}

// FUNC_AT(0x000482e0)
EPathSetAccelDelayStatic* EPathSetAccelDelayStatic::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00041990)
void EPathSetAccelDelayStatic_MakeEvent_Callback(const EPathSetAccelDelayStatic::Record *record) {
    EPathSetAccelDelayStatic *event = new EPathSetAccelDelayStatic;
    if (event != NULL) {
        event->vtable = kEPathSetAccelDelayStaticVtable;
        event->instance = record->instance;
        event->element = record->element;
        event->accelDelay = record->accelDelay;
    }
}

// FUNC_AT(0x000419c0)
void EPathSetAccelerationDynamic::Destruct() {
    vtable = kEPathSetAccelerationDynamicVtable;
    Process();
    vtable = kEventVtable;
}

void EPathSetAccelerationDynamic::Process() {
    RPathHandle *path = InstancePath(instance);
    if (path != NULL)
        path->SetAcceleration(acceleration);
}

// FUNC_AT(0x00048300)
EPathSetAccelerationDynamic* EPathSetAccelerationDynamic::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00041a30)
void EPathSetAccelerationDynamic_MakeEvent_Callback(const EPathSetAccelerationDynamic::Record *record) {
    EPathSetAccelerationDynamic *event = new EPathSetAccelerationDynamic;
    if (event != NULL) {
        event->vtable = kEPathSetAccelerationDynamicVtable;
        event->instance = gEventDynamicData.instance;
        event->acceleration = record->acceleration;
    }
}

// FUNC_AT(0x00041a60)
void EPathSetAccelerationStatic::Destruct() {
    vtable = kEPathSetAccelerationStaticVtable;
    Process();
    vtable = kEventVtable;
}

void EPathSetAccelerationStatic::Process() {
    RPathHandle *path = EPathUtil_GetFromInstanceOrAI(instance, element);
    if (path != NULL)
        path->SetAcceleration(acceleration);
}

// FUNC_AT(0x00048320)
EPathSetAccelerationStatic* EPathSetAccelerationStatic::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00041ac0)
void EPathSetAccelerationStatic_MakeEvent_Callback(const EPathSetAccelerationStatic::Record *record) {
    EPathSetAccelerationStatic *event = new EPathSetAccelerationStatic;
    if (event != NULL) {
        event->vtable = kEPathSetAccelerationStaticVtable;
        event->instance = record->instance;
        event->element = record->element;
        event->acceleration = record->acceleration;
    }
}

// FUNC_AT(0x00041af0)
void EPathSetDesiredThrottleDynamic::Destruct() {
    vtable = kEPathSetDesiredThrottleDynamicVtable;
    Process();
    vtable = kEventVtable;
}

void EPathSetDesiredThrottleDynamic::Process() {
    RPathHandle *path = InstancePath(instance);
    if (path != NULL)
        path->SetDesiredThrottle(throttle);
}

// FUNC_AT(0x00048340)
EPathSetDesiredThrottleDynamic* EPathSetDesiredThrottleDynamic::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00041b60)
void EPathSetDesiredThrottleDynamic_MakeEvent_Callback(const EPathSetDesiredThrottleDynamic::Record *record) {
    EPathSetDesiredThrottleDynamic *event = new EPathSetDesiredThrottleDynamic;
    if (event != NULL) {
        event->vtable = kEPathSetDesiredThrottleDynamicVtable;
        event->instance = gEventDynamicData.instance;
        event->throttle = record->throttle;
    }
}

// FUNC_AT(0x00041b90)
void EPathSetDesiredThrottleStatic::Destruct() {
    vtable = kEPathSetDesiredThrottleStaticVtable;
    Process();
    vtable = kEventVtable;
}

void EPathSetDesiredThrottleStatic::Process() {
    RPathHandle *path = EPathUtil_GetFromInstanceOrAI(instance, element);
    if (path != NULL)
        path->SetDesiredThrottle(throttle);
}

// FUNC_AT(0x00048360)
EPathSetDesiredThrottleStatic* EPathSetDesiredThrottleStatic::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00041bf0)
void EPathSetDesiredThrottleStatic_MakeEvent_Callback(const EPathSetDesiredThrottleStatic::Record *record) {
    EPathSetDesiredThrottleStatic *event = new EPathSetDesiredThrottleStatic;
    if (event != NULL) {
        event->vtable = kEPathSetDesiredThrottleStaticVtable;
        event->instance = record->instance;
        event->element = record->element;
        event->throttle = record->throttle;
    }
}

// FUNC_AT(0x00041c20)
void EPathSetRunningDynamic::Destruct() {
    vtable = kEPathSetRunningDynamicVtable;
    Process();
    vtable = kEventVtable;
}

void EPathSetRunningDynamic::Process() {
    RPathHandle *path = InstancePath(instance);
    if (path != NULL)
        path->SetRunning(running != 0);
}

// FUNC_AT(0x00048380)
EPathSetRunningDynamic* EPathSetRunningDynamic::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00041c90)
void EPathSetRunningDynamic_MakeEvent_Callback(const EPathSetRunningDynamic::Record *record) {
    EPathSetRunningDynamic *event = new EPathSetRunningDynamic;
    if (event != NULL) {
        event->vtable = kEPathSetRunningDynamicVtable;
        event->instance = gEventDynamicData.instance;
        event->running = record->running;
    }
}

// FUNC_AT(0x00041cc0)
void EPathSetRunningStatic::Destruct() {
    vtable = kEPathSetRunningStaticVtable;
    Process();
    vtable = kEventVtable;
}

void EPathSetRunningStatic::Process() {
    RPathHandle *path = EPathUtil_GetFromInstanceOrAI(instance, element);
    if (path != NULL)
        path->SetRunning(running != 0);
}

// FUNC_AT(0x000483a0)
EPathSetRunningStatic* EPathSetRunningStatic::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00041d30)
void EPathSetRunningStatic_MakeEvent_Callback(const EPathSetRunningStatic::Record *record) {
    EPathSetRunningStatic *event = new EPathSetRunningStatic;
    if (event != NULL) {
        event->vtable = kEPathSetRunningStaticVtable;
        event->instance = record->instance;
        event->element = record->element;
        event->running = record->running;
    }
}

// FUNC_AT(0x00041d60)
void EPathSetThrottleDynamic::Destruct() {
    vtable = kEPathSetThrottleDynamicVtable;
    Process();
    vtable = kEventVtable;
}

void EPathSetThrottleDynamic::Process() {
    RPathHandle *path = InstancePath(instance);
    if (path != NULL)
        path->SetThrottle(throttle);
}

// FUNC_AT(0x000483c0)
EPathSetThrottleDynamic* EPathSetThrottleDynamic::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00041dd0)
void EPathSetThrottleDynamic_MakeEvent_Callback(const EPathSetThrottleDynamic::Record *record) {
    EPathSetThrottleDynamic *event = new EPathSetThrottleDynamic;
    if (event != NULL) {
        event->vtable = kEPathSetThrottleDynamicVtable;
        event->instance = gEventDynamicData.instance;
        event->throttle = record->throttle;
    }
}

// FUNC_AT(0x00041e00)
void EPathSetThrottleStatic::Destruct() {
    vtable = kEPathSetThrottleStaticVtable;
    Process();
    vtable = kEventVtable;
}

void EPathSetThrottleStatic::Process() {
    RPathHandle *path = EPathUtil_GetFromInstanceOrAI(instance, element);
    if (path != NULL)
        path->SetThrottle(throttle);
}

// FUNC_AT(0x000483e0)
EPathSetThrottleStatic* EPathSetThrottleStatic::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00041e60)
void EPathSetThrottleStatic_MakeEvent_Callback(const EPathSetThrottleStatic::Record *record) {
    EPathSetThrottleStatic *event = new EPathSetThrottleStatic;
    if (event != NULL) {
        event->vtable = kEPathSetThrottleStaticVtable;
        event->instance = record->instance;
        event->element = record->element;
        event->throttle = record->throttle;
    }
}

// FUNC_AT(0x00041e90)
void EPathSetTimeDynamic::Destruct() {
    vtable = kEPathSetTimeDynamicVtable;
    Process();
    vtable = kEventVtable;
}

void EPathSetTimeDynamic::Process() {
    RPathHandle *path = InstancePath(instance);
    if (path != NULL)
        path->SetParametricTime(path->GetParametricDuration() * time);
}

// FUNC_AT(0x00048400)
EPathSetTimeDynamic* EPathSetTimeDynamic::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00041f10)
void EPathSetTimeDynamic_MakeEvent_Callback(const EPathSetTimeDynamic::Record *record) {
    EPathSetTimeDynamic *event = new EPathSetTimeDynamic;
    if (event != NULL) {
        event->vtable = kEPathSetTimeDynamicVtable;
        event->instance = gEventDynamicData.instance;
        event->time = record->time;
    }
}

// FUNC_AT(0x00041f40)
void EPathSetTimeStatic::Destruct() {
    vtable = kEPathSetTimeStaticVtable;
    Process();
    vtable = kEventVtable;
}

void EPathSetTimeStatic::Process() {
    RPathHandle *path = EPathUtil_GetFromInstanceOrAI(instance, element);
    if (path != NULL)
        path->SetParametricTime(path->GetParametricDuration() * time);
}

// FUNC_AT(0x00048420)
EPathSetTimeStatic* EPathSetTimeStatic::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00041fb0)
void EPathSetTimeStatic_MakeEvent_Callback(const EPathSetTimeStatic::Record *record) {
    EPathSetTimeStatic *event = new EPathSetTimeStatic;
    if (event != NULL) {
        event->vtable = kEPathSetTimeStaticVtable;
        event->instance = record->instance;
        event->element = record->element;
        event->time = record->time;
    }
}

// FUNC_AT(0x00041fe0)
void EPathSetTimeStatic_ResolveEvent_Callback(EPathSetTimeStatic::Record *record, UGroup *shared, UGroup *parent) {
    reinterpret_cast<CARP::TagReference *>(&record->instance)->Construct(shared, parent);
    reinterpret_cast<CARP::TagReference *>(&record->element)->Construct(shared, parent);
}

// FUNC_AT(0x00044250)
void ESetNextPath::Destruct() {
    vtable = kESetNextPathVtable;
    Process();
    vtable = kEventVtable;
}

void ESetNextPath::Process() {
    if (instance == NULL)
        return;
    RPathHandle *handle = RPathEngine::GetPathHandle(instance);
    if (handle != NULL)
        handle->SetNextPath(path);
}

// FUNC_AT(0x00048a80)
ESetNextPath* ESetNextPath::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x000442b0)
void ESetNextPath_MakeEvent_Callback(const ESetNextPath::Record *record) {
    ESetNextPath *event = new ESetNextPath;
    if (event != NULL) {
        event->vtable = kESetNextPathVtable;
        event->instance = record->instance;
        event->path = record->path;
    }
}

// FUNC_AT(0x0004d9d0)
void ESetNextAISpline::Destruct() {
    vtable = kESetNextAISplineVtable;
    Process();
    vtable = kEventVtable;
}

void ESetNextAISpline::Process() {
    if (spline == NULL || spline->path.value == 0)
        return;
    AIVehicle *vehicle;
    if (element == NULL) {
        vehicle = PlayerCar()->GetAIGroundVehiclePtr();
    } else {
        CARP::AIElement *redirected = fgAIElementController->RedirectCarpElement(element);
        if (redirected->unknown48 != 1 || redirected->type != CARP::kAIElementVehicle)
            return;
        vehicle = redirected->vehicle;
    }
    vehicle->SetNextSplinePath(spline);
}

// FUNC_AT(0x0004ee40)
ESetNextAISpline* ESetNextAISpline::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00044220)
void ESetNextAISpline_MakeEvent_Callback(const ESetNextAISpline::Record *record) {
    ESetNextAISpline *event = new ESetNextAISpline;
    if (event != NULL) {
        event->vtable = kESetNextAISplineVtable;
        event->element = record->element;
        event->spline = record->spline;
    }
}

// FUNC_AT(0x0004da70)
void ESetRecSpeedOffset::Destruct() {
    vtable = kESetRecSpeedOffsetVtable;
    Process();
    vtable = kEventVtable;
}

void ESetRecSpeedOffset::Process() {
    if (element == NULL) {
        PlayerCar()->GetAIGroundVehiclePtr()->autoDriveSpeed = speed;
        return;
    }
    CARP::AIElement *redirected = fgAIElementController->RedirectCarpElement(element);
    if (redirected->unknown48 == 1 && redirected->type == CARP::kAIElementVehicle)
        static_cast<AIGroundVehicle *>(redirected->vehicle)->autoDriveSpeed = speed;
}

// FUNC_AT(0x0004ee60)
ESetRecSpeedOffset* ESetRecSpeedOffset::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x000443d0)
void ESetRecSpeedOffset_MakeEvent_Callback(const ESetRecSpeedOffset::Record *record) {
    ESetRecSpeedOffset *event = new ESetRecSpeedOffset;
    if (event != NULL) {
        event->vtable = kESetRecSpeedOffsetVtable;
        event->element = record->element;
        event->speed = record->speed;
    }
}

// =============================================================================================================
// Effects and sounds
// =============================================================================================================

// FUNC_AT(0x000420f0)
void EPlayActorEffect::Destruct() {
    vtable = kEPlayActorEffectVtable;
    Process();
    vtable = kEventVtable;
}

// No check of the effect or the scene object, as in the original
void EPlayActorEffect::Process() {
    Coord3 position = effect->position;
    Coord4 rotation = effect->gfx.rotation;
    GFX::Trigger(effect->gfx.reference, &kNoOffset, &kNoRotation, &position, &rotation, sceneObj,
                 effect->instance, sceneObj->GetVelocityVirtual());
}

// FUNC_AT(0x00048460)
EPlayActorEffect* EPlayActorEffect::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x000421b0)
void EPlayActorEffect_MakeEvent_Callback(const EPlayActorEffect::Record *record) {
    EPlayActorEffect *event = new EPlayActorEffect;
    if (event != NULL) {
        event->vtable = kEPlayActorEffectVtable;
        event->effect = record->effect;
        event->sceneObj = gEventDynamicData.sceneObj;
    }
}

// FUNC_AT(0x00042500)
void EPlayEffect::Destruct() {
    vtable = kEPlayEffectVtable;
    Process();
    vtable = kEventVtable;
}

void EPlayEffect::Process() {
    if (sceneObj == NULL && instance != NULL && procAnimState != NULL)
        sceneObj = procAnimState->sceneObj;
    if (effect == NULL || effect->type != ArticleEffect::kTypeGfx || effect->gfx.reference == NULL)
        return;
    if (effect->flags & (ArticleEffect::kFlag02 | ArticleEffect::kFollowsSystem)) {
        if (sceneObj != NULL)
            sceneObj->TriggerFXVirtual(kFXOwnEffect, effect->id, 0, &kNoOffset, &kNoOffset, -1.0f);
        return;
    }

    Coord3 position = effect->position;
    Coord4 rotation = effect->gfx.rotation;
    if (sceneObj == NULL) {
        // In the instance's frame
        alignas(16) MATRIX4 frame;
        if (instance != NULL)
            instance->GetMatrix4(&frame);
        else
            VU0_MATRIX4Init(&frame);
        alignas(16) Coord4 placement;
        VU0_m4toquat(&placement, &frame);
        D3DXQuaternionMultiply(&rotation, &rotation, &placement);
        VU0_v3add(&position, MatrixRow(&frame, 3), &position);
        GFX::Trigger(effect->gfx.reference, &position, &rotation, &kNoOffset, &kNoRotation, NULL, 0, NULL);
        return;
    }
    // Mirrored in x for another instance
    if ((effect->flags & ArticleEffect::kFlag200) && effect->instance != instanceIndex) {
        alignas(16) MATRIX4 frame;
        sceneObj->GetInstancePosition(instanceIndex, &frame, false);
        rotation.z = -rotation.z;
        rotation.y = -rotation.y;
        position.x = float(double(frame.mtx[3][0]) + frame.mtx[3][0] - position.x);
    }
    GFX::Trigger(effect->gfx.reference, &kNoOffset, &kNoRotation, &position, &rotation, sceneObj,
                 effect->instance, sceneObj->GetVelocityVirtual());
}

// FUNC_AT(0x000484c0)
EPlayEffect* EPlayEffect::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00042700)
void EPlayEffect_MakeEvent_Callback(const EPlayEffect::Record *record) {
    EPlayEffect *event = new EPlayEffect;
    if (event != NULL) {
        event->vtable = kEPlayEffectVtable;
        event->effect = record->effect;
        event->instance = record->instance;
        event->procAnimState = record->procAnimState;
        event->sceneObj = gEventDynamicData.sceneObj;
        event->instanceIndex = gEventDynamicData.instanceIndex;
    }
}

// FUNC_AT(0x00042750)
void EPlayGiottoEffect::Destruct() {
    vtable = kEPlayGiottoEffectVtable;
    Process();
    vtable = kEventVtable;
}

void EPlayGiottoEffect::Process() {
    Coord4 position = place->position;
    position.w = 1.0f;
    Coord4 direction = place->direction;
    direction.w = 1.0f;
    GFX::TriggerByName(effect, Xyz(&position), &direction, &kNoOffset, &kNoRotation, NULL, 0, NULL);
}

// FUNC_AT(0x000484e0)
EPlayGiottoEffect* EPlayGiottoEffect::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00042810)
void EPlayGiottoEffect_MakeEvent_Callback(const EPlayGiottoEffect::Record *record) {
    EPlayGiottoEffect *event = new EPlayGiottoEffect;
    if (event != NULL) {
        event->vtable = kEPlayGiottoEffectVtable;
        event->place = record->place;
        event->effect = record->effect;
    }
}

// FUNC_AT(0x000437d0)
void ESentryMuzzleFlash::Destruct() {
    vtable = kESentryMuzzleFlashVtable;
    Process();
    vtable = kEventVtable;
}

void ESentryMuzzleFlash::Process() {
    if (effect == NULL || sceneObj == NULL)
        return;
    alignas(16) MATRIX4 frame;
    sceneObj->LocateFX(sceneObj->animHandle->FindEffectByID(effect->id), &frame, true);
    alignas(16) Coord4 direction = { 0.0f, 1.0f, 0.0f, 0.0f };
    VU0_MATRIX4_vect4mult(&direction, &frame, &direction);
    Sentry::AddMuzzleFlash(MatrixRow(&frame, 3), Xyz(&direction));
}

// FUNC_AT(0x00048880)
ESentryMuzzleFlash* ESentryMuzzleFlash::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00043890)
void ESentryMuzzleFlash_MakeEvent_Callback(const ESentryMuzzleFlash::Record *record) {
    ESentryMuzzleFlash *event = new ESentryMuzzleFlash;
    if (event != NULL) {
        event->vtable = kESentryMuzzleFlashVtable;
        event->effect = record->effect;
        event->sceneObj = gEventDynamicData.sceneObj;
        event->instanceIndex = gEventDynamicData.instanceIndex;
    }
}

// FUNC_AT(0x000428d0)
void EPlaySound::Destruct() {
    vtable = kEPlaySoundVtable;
    Process();
    vtable = kEventVtable;
}

void EPlaySound::Process() {
    if (bank != NULL && patch != NULL)
        AMenuSound::Trigger(bank, patch, "Power Ups", kMenuSoundView);
}

// FUNC_AT(0x00048520)
EPlaySound* EPlaySound::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00042950)
void EPlaySound_MakeEvent_Callback(const EPlaySound::Record *record) {
    EPlaySound *event = new EPlaySound;
    if (event != NULL) {
        event->vtable = kEPlaySoundVtable;
        event->bank = record->bank;
        event->patch = record->patch;
    }
}

// FUNC_AT(0x00048540)
void EPlaySoundPos::Destruct() {
    vtable = kEPlaySoundPosVtable;
    Process();
    vtable = kEventVtable;
}

// The one-shot is written to without a check that it was made, as in the original
void EPlaySoundPos::Process() {
    if (bank == NULL || patch == NULL || position == NULL)
        return;
    ABank *found = ABank::Get(bank);
    void *memory = ABaseSound::OperatorNew(sizeof(AOneShotSound), "AOneShotSound");
    AOneShotSound *shot = NULL;
    if (memory != NULL) {
        int index = found->index.Lookup(patch);
        shot = static_cast<AOneShotSound *>(memory)->ConstructAt(found->handle, index, position, 1.0f,
                                                                 "World Sounds", loop != 0);
    }
    shot->minDistance = minDistance;
    shot->maxDistance = maxDistance;
    shot->maxDistanceSq = maxDistance * maxDistance;
}

// FUNC_AT(0x0004cf30)
EPlaySoundPos* EPlaySoundPos::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00042980)
void EPlaySoundPos_MakeEvent_Callback(const EPlaySoundPos::Record *record) {
    EPlaySoundPos *event = new EPlaySoundPos;
    if (event != NULL) {
        event->vtable = kEPlaySoundPosVtable;
        event->position = record->position;
        event->bank = record->bank;
        event->patch = record->patch;
        event->loop = record->loop;
        event->minDistance = record->minDistance;
        event->maxDistance = record->maxDistance;
    }
}

// FUNC_AT(0x000429d0)
void EPlaySystemAnim::Destruct() {
    vtable = kEPlaySystemAnimVtable;
    Process();
    vtable = kEventVtable;
}

void EPlaySystemAnim::Process() {
    if (sceneObj == NULL) {
        if (instance != NULL && procAnimState != NULL) {
            sceneObj = procAnimState->sceneObj;
        } else if (bodySlot != -1) {
            PhysicsObject *owner = bySimpleBody ? fgSimulation.simpleRigidBodyOwners[bodySlot]
                                                : fgSimulation.rigidBodyOwners[bodySlot];
            if (owner != NULL)
                sceneObj = owner->renderObject;
        }
    }
    if (sceneObj != NULL)
        sceneObj->animHandle->ProcessStimuli(system, uint8_t(stimulus), GameTick, kStimulusMode2);
}

// FUNC_AT(0x00048620)
EPlaySystemAnim* EPlaySystemAnim::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00042a80)
void EPlaySystemAnim_MakeEvent_Callback(const EPlaySystemAnim::Record *record) {
    EPlaySystemAnim *event = new EPlaySystemAnim;
    if (event != NULL) {
        event->vtable = kEPlaySystemAnimVtable;
        event->system = record->system;
        event->stimulus = record->stimulus;
        event->instance = record->instance;
        event->procAnimState = record->procAnimState;
        event->bySimpleBody = gEventDynamicData.flag;
        event->bodySlot = gEventDynamicData.index;
        event->sceneObj = gEventDynamicData.sceneObj;
    }
}

// FUNC_AT(0x00042ae0)
void EPlaySystemAnim_ResolveEvent_Callback(EPlaySystemAnim::Record *record, UGroup *shared, UGroup *parent) {
    reinterpret_cast<CARP::TagReference *>(&record->instance)->Construct(shared, parent);
    reinterpret_cast<CARP::TagReference *>(&record->procAnimState)->Construct(shared, parent);
}

// FUNC_AT(0x000430e0)
void ERandomExplosion::Destruct() {
    vtable = kERandomExplosionVtable;
    Process();
    vtable = kEventVtable;
}

// The x and z offsets: a random 0..1 of the radius, less half of it
void ERandomExplosion::Process() {
    if ((RandomShort() & 0x7f) != 0)
        return;
    Coord4 point = *reinterpret_cast<const Coord4 *>(trigger);     // the position and the radius
    double radius = trigger->radius;
    double half = 0.5f * radius;
    uint32_t randomX = RandomShort();
    uint32_t randomZ = RandomShort();
    point.x = float(double(int32_t(randomX)) * radius * kOneIn65536 - half + point.x);
    point.z = float(double(int32_t(randomZ)) * radius * kOneIn65536 - half + point.z);
    point.y += kRandomExplosionLift;
    if (fgCollisionMgr->GetWorldHeightAtPoint(Xyz(&point), &point.y, false)) {
        point.y += 1.0f;
        const Coord3 direction = { 0.0f, 0.0f, 1.0f };
        fgSimulation.SpawnExplosion(kRandomExplosionKind, kRandomExplosionKind, &direction, Xyz(&point),
                                    kRandomExplosionForce, 0, NULL, DamageSourceSig);
    }
}

// FUNC_AT(0x00048740)
ERandomExplosion* ERandomExplosion::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00043260)
void ERandomExplosion_MakeEvent_Callback(const ERandomExplosion::Record *record) {
    ERandomExplosion *event = new ERandomExplosion;
    if (event != NULL) {
        event->vtable = kERandomExplosionVtable;
        event->unknown04 = record->unknown04;
        event->unknown08 = record->unknown08;
        event->trigger = gEventDynamicData.trigger;
    }
}

// =============================================================================================================
// The audio mixes and FX
// =============================================================================================================

// FUNC_AT(0x00043290)
void EReleaseStream::Destruct() {
    vtable = kEReleaseStreamVtable;
    Process();
    vtable = kEventVtable;
}

void EReleaseStream::Process() {
    if (gStreams && StreamHold == kHoldWaiting)
        StreamHold = kHoldReleased;
}

// FUNC_AT(0x00048760)
EReleaseStream* EReleaseStream::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x000432c0)
void EReleaseStream_MakeEvent_Callback(const void *) {
    EReleaseStream *event = new EReleaseStream;
    if (event != NULL)
        event->vtable = kEReleaseStreamVtable;
}

// FUNC_AT(0x00043390)
void EResetAudioMix::Destruct() {
    vtable = kEResetAudioMixVtable;
    Process();
    vtable = kEventVtable;
}

void EResetAudioMix::Process() {
    if (gSoundOption)
        AMix::Reset(steps);
}

// FUNC_AT(0x000487a0)
EResetAudioMix* EResetAudioMix::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x000433f0)
void EResetAudioMix_MakeEvent_Callback(const EResetAudioMix::Record *record) {
    EResetAudioMix *event = new EResetAudioMix;
    if (event != NULL) {
        event->vtable = kEResetAudioMixVtable;
        event->steps = record->steps;
    }
}

// FUNC_AT(0x000434c0)
void ERevertAudioMix::Destruct() {
    vtable = kERevertAudioMixVtable;
    Process();
    vtable = kEventVtable;
}

void ERevertAudioMix::Process() {
    if (gSoundOption)
        AMix::Get(mix)->Revert(steps);
}

// FUNC_AT(0x000487c0)
ERevertAudioMix* ERevertAudioMix::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00043530)
void ERevertAudioMix_MakeEvent_Callback(const ERevertAudioMix::Record *record) {
    ERevertAudioMix *event = new ERevertAudioMix;
    if (event != NULL) {
        event->vtable = kERevertAudioMixVtable;
        event->mix = record->mix;
        event->steps = record->steps;
    }
}

// FUNC_AT(0x00043a00)
void ESetAudioMix::Destruct() {
    vtable = kESetAudioMixVtable;
    Process();
    vtable = kEventVtable;
}

// The test against 1 is of the unrounded product, the test against 0 of the rounded volume
void ESetAudioMix::Process() {
    if (!gSoundOption)
        return;
    AMix *found = AMix::Get(mix);
    double scaled = double(found->GetPresetVolume()) * (double(percent) * kPercent + 1.0f);
    float volume = float(scaled);
    if (scaled > 1.0)
        volume = 1.0f;
    else if (volume < 0.0f)
        volume = 0.0f;
    found->SetTransition(volume, steps);
}

// FUNC_AT(0x000488e0)
ESetAudioMix* ESetAudioMix::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00043ac0)
void ESetAudioMix_MakeEvent_Callback(const ESetAudioMix::Record *record) {
    ESetAudioMix *event = new ESetAudioMix;
    if (event != NULL) {
        event->vtable = kESetAudioMixVtable;
        event->mix = record->mix;
        event->steps = record->steps;
        event->percent = record->percent;
    }
}

// FUNC_AT(0x000438d0)
void ESetAFXMode::Destruct() {
    vtable = kESetAFXModeVtable;
    Process();
    vtable = kEventVtable;
}

void ESetAFXMode::Process() {
    if (gSoundOption)
        AFX::SetMode(mode, slot, priority);
}

// FUNC_AT(0x000488a0)
ESetAFXMode* ESetAFXMode::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00043930)
void ESetAFXMode_MakeEvent_Callback(const ESetAFXMode::Record *record) {
    ESetAFXMode *event = new ESetAFXMode;
    if (event != NULL) {
        event->vtable = kESetAFXModeVtable;
        event->mode = record->mode;
        event->slot = record->slot;
        event->priority = record->priority;
    }
}

// FUNC_AT(0x0004d930)
void ESetCreakLevel::Destruct() {
    vtable = kESetCreakLevelVtable;
    Process();
    vtable = kEventVtable;
}

// The car's physics are copied out whole, as in the original
void ESetCreakLevel::Process() {
    CarPhysics physics = *PlayerCar()->GetPhysics();
    if (physics.isSub > 0)
        static_cast<ASubmersible *>(PlayerCar()->GetAudio())->CallSetCreakLevel(uint16_t(level), period);
}

// FUNC_AT(0x0004ee20)
ESetCreakLevel* ESetCreakLevel::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00043f70)
void ESetCreakLevel_MakeEvent_Callback(const ESetCreakLevel::Record *record) {
    ESetCreakLevel *event = new ESetCreakLevel;
    if (event != NULL) {
        event->vtable = kESetCreakLevelVtable;
        event->level = record->level;
        event->period = record->period;
    }
}

// =============================================================================================================
// The cameras and auto-drive
// =============================================================================================================

// FUNC_AT(0x000421e0)
void EPlayCameraAnim::Destruct() {
    vtable = kEPlayCameraAnimVtable;
    Process();
    vtable = kEventVtable;
}

void EPlayCameraAnim::Process() {
    if (delay > 1)
        delay = SixtiethsToSteps(delay);
    if (b > 1)
        b = SixtiethsToSteps(b);
    else
        b = 1;
    PlayerCamera()->TriggerAnimationCamera(animation, b, uint16_t(delay), timePath != 0, fov);
    glbMissionManager->SetCinematicMode(kCinematicCameraAnim);
}

// FUNC_AT(0x00048480)
EPlayCameraAnim* EPlayCameraAnim::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00042310)
void EPlayCameraAnim_MakeEvent_Callback(const EPlayCameraAnim::Record *record) {
    EPlayCameraAnim *event = new EPlayCameraAnim;
    if (event != NULL) {
        event->vtable = kEPlayCameraAnimVtable;
        event->animation = record->animation;
        event->b = record->b;
        event->delay = record->delay;
        event->timePath = record->timePath;
        event->fov = record->fov;
    }
}

// FUNC_AT(0x00042350)
void EPlayCameraSpline::Destruct() {
    vtable = kEPlayCameraSplineVtable;
    Process();
    vtable = kEventVtable;
}

void EPlayCameraSpline::Process() {
    if (delay > 1)
        delay = SixtiethsToSteps(delay);
    if (b > 1)
        b = SixtiethsToSteps(b);
    else
        b = 1;
    PlayerCamera()->TriggerAIPathAnimationCamera(path, b, uint16_t(delay), fov);
    glbMissionManager->SetCinematicMode(kCinematicCameraAnim);
}

// FUNC_AT(0x000484a0)
EPlayCameraSpline* EPlayCameraSpline::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00042470)
void EPlayCameraSpline_MakeEvent_Callback(const EPlayCameraSpline::Record *record) {
    EPlayCameraSpline *event = new EPlayCameraSpline;
    if (event != NULL) {
        event->vtable = kEPlayCameraSplineVtable;
        event->path = record->path;
        event->b = record->b;
        event->delay = record->delay;
        event->fov = record->fov;
    }
}

// FUNC_AT(0x0004cdc0)
void EPlayCarCameraAnim::Destruct() {
    vtable = kEPlayCarCameraAnimVtable;
    Process();
    vtable = kEventVtable;
}

void EPlayCarCameraAnim::Process() {
    RPlayerCamera *camera = PlayerCamera();
    if (delay > 1)
        delay = SixtiethsToSteps(delay);
    if (b > 1)
        b = SixtiethsToSteps(b);
    else
        b = 1;
    PhysicsObject *anchor = NULL;
    if (element != NULL) {
        CARP::AIElement *redirected = fgAIElementController->RedirectCarpElement(element);
        if (redirected->type == CARP::kAIElementVehicle)
            anchor = redirected->object;
    }
    if (anchor == NULL) {
        if (element == NULL)
            anchor = PlayerCar();
        else if (!bySimpleBody)
            anchor = fgSimulation.rigidBodyOwners[bodySlot];
        else
            anchor = fgSimulation.simpleRigidBodyOwners[bodySlot];
    }
    camera->TriggerCarAnimationCamera(animationId, b, uint16_t(delay), anchor, 0);
}

// FUNC_AT(0x0004ece0)
EPlayCarCameraAnim* EPlayCarCameraAnim::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x000424b0)
void EPlayCarCameraAnim_MakeEvent_Callback(const EPlayCarCameraAnim::Record *record) {
    EPlayCarCameraAnim *event = new EPlayCarCameraAnim;
    if (event != NULL) {
        event->vtable = kEPlayCarCameraAnimVtable;
        event->element = record->element;
        event->b = record->b;
        event->delay = record->delay;
        event->animationId = record->animationId;
        event->bySimpleBody = gEventDynamicData.flag;
        event->bodySlot = gEventDynamicData.index;
    }
}

// FUNC_AT(0x00042840)
void EPlayPOVDeath::Destruct() {
    vtable = kEPlayPOVDeathVtable;
    Process();
    vtable = kEventVtable;
}

void EPlayPOVDeath::Process() {
    PlayerCamera()->TriggerCarAnimationCamera(kDeathAnimation, kDeathCameraSteps, 0, NULL, kDeathCameraFlags);
    if (fgHandObj != NULL)
        fgHandObj->HideHands();
}

// FUNC_AT(0x00048500)
EPlayPOVDeath* EPlayPOVDeath::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x000428b0)
void EPlayPOVDeath_MakeEvent_Callback(const void *) {
    EPlayPOVDeath *event = new EPlayPOVDeath;
    if (event != NULL)
        event->vtable = kEPlayPOVDeathVtable;
}

// FUNC_AT(0x00043670)
void EScaleFieldOfView::Destruct() {
    vtable = kEScaleFieldOfViewVtable;
    Process();
    vtable = kEventVtable;
}

void EScaleFieldOfView::Process() {
    fgRenderer->fieldOfViewScale = scale;
}

// FUNC_AT(0x00048820)
EScaleFieldOfView* EScaleFieldOfView::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00043690)
void EScaleFieldOfView_MakeEvent_Callback(const EScaleFieldOfView::Record *record) {
    EScaleFieldOfView *event = new EScaleFieldOfView;
    if (event != NULL) {
        event->vtable = kEScaleFieldOfViewVtable;
        event->scale = record->scale;
    }
}

// FUNC_AT(0x000436b0)
void EScaleFog::Destruct() {
    vtable = kEScaleFogVtable;
    Process();
    vtable = kEventVtable;
}

void EScaleFog::Process() {
    Fog->FadeScale(scale, seconds);
}

// FUNC_AT(0x00048840)
EScaleFog* EScaleFog::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00043710)
void EScaleFog_MakeEvent_Callback(const EScaleFog::Record *record) {
    EScaleFog *event = new EScaleFog;
    if (event != NULL) {
        event->vtable = kEScaleFogVtable;
        event->scale = record->scale;
        event->seconds = record->seconds;
    }
}

// FUNC_AT(0x00043af0)
void ESetAutoDriveCamOff::Destruct() {
    vtable = kESetAutoDriveCamOffVtable;
    Process();
    vtable = kEventVtable;
}

void ESetAutoDriveCamOff::Process() {
    PlayerCamera()->SetLastSelectableCameraMode(0, 0, false, false);
    glbMissionManager->SetAutoDrive(false, true);
}

// FUNC_AT(0x00048900)
ESetAutoDriveCamOff* ESetAutoDriveCamOff::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00043b60)
void ESetAutoDriveCamOff_MakeEvent_Callback(const void *) {
    ESetAutoDriveCamOff *event = new ESetAutoDriveCamOff;
    if (event != NULL)
        event->vtable = kESetAutoDriveCamOffVtable;
}

// FUNC_AT(0x0004d850)
void ESetAutoDriveCamOffMovie::Destruct() {
    vtable = kESetAutoDriveCamOffMovieVtable;
    Process();
    vtable = kEventVtable;
}

// In "paris_mis01" on super easy, a helicopter's zoom is left
void ESetAutoDriveCamOffMovie::Process() {
    if (_stricmp(Launch.missionName, "paris_mis01") == 0 && PlayerCar()->GetAudio()->vehicleType == kVehicleSoundHeli &&
        gSuperEasy)
        PlayerCar()->GetAudio()->zoom->InTheZone(false);
    PlayerCamera()->SetLastSelectableCameraMode(0, 0, false, true);
    PlayerCamera()->unknown145 = true;
    if (fgHandObj != NULL)
        fgHandObj->HideHands();
    TargetPicker.DeactivateTargeting();
    glbMissionManager->SetAutoDrive(false, false);
}

// FUNC_AT(0x0004ee00)
ESetAutoDriveCamOffMovie* ESetAutoDriveCamOffMovie::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00043b80)
void ESetAutoDriveCamOffMovie_MakeEvent_Callback(const void *) {
    ESetAutoDriveCamOffMovie *event = new ESetAutoDriveCamOffMovie;
    if (event != NULL)
        event->vtable = kESetAutoDriveCamOffMovieVtable;
}

// FUNC_AT(0x00043ba0)
void ESetAutoDriveCamOn::Destruct() {
    vtable = kESetAutoDriveCamOnVtable;
    Process();
    vtable = kEventVtable;
}

void ESetAutoDriveCamOn::Process() {
    if (glbMissionManager->autoDrive)
        return;
    PlayerCamera()->SetAutoDriveCamera(x * kDegrees, y * kDegrees, 1);
    glbMissionManager->SetAutoDrive(true, true);
}

// FUNC_AT(0x00048920)
ESetAutoDriveCamOn* ESetAutoDriveCamOn::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00043c30)
void ESetAutoDriveCamOn_MakeEvent_Callback(const ESetAutoDriveCamOn::Record *record) {
    ESetAutoDriveCamOn *event = new ESetAutoDriveCamOn;
    if (event != NULL) {
        event->vtable = kESetAutoDriveCamOnVtable;
        event->x = record->x;
        event->y = record->y;
    }
}

// FUNC_AT(0x00043c60)
void ESetAutoDriveCamOnMovie::Destruct() {
    vtable = kESetAutoDriveCamOnMovieVtable;
    Process();
    vtable = kEventVtable;
}

void ESetAutoDriveCamOnMovie::Process() {
    if (glbMissionManager->autoDrive)
        return;
    PlayerCamera()->SetAutoDriveCamera(x * kDegrees, y * kDegrees, 1);
    PlayerCamera()->unknown145 = true;
    if (fgHandObj != NULL)
        fgHandObj->ShowHands();
    TargetPicker.ActivateTargeting(1);
    glbMissionManager->SetAutoDrive(true, false);
}

// FUNC_AT(0x00048940)
ESetAutoDriveCamOnMovie* ESetAutoDriveCamOnMovie::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00043d20)
void ESetAutoDriveCamOnMovie_MakeEvent_Callback(const ESetAutoDriveCamOnMovie::Record *record) {
    ESetAutoDriveCamOnMovie *event = new ESetAutoDriveCamOnMovie;
    if (event != NULL) {
        event->vtable = kESetAutoDriveCamOnMovieVtable;
        event->x = record->x;
        event->y = record->y;
    }
}

// =============================================================================================================
// The render settings
// =============================================================================================================

// FUNC_AT(0x000432e0)
void ERenderFrame::Destruct() {
    vtable = kERenderFrameVtable;
    Process();
    vtable = kEventVtable;
}

void ERenderFrame::Process() {
    RPlayerCamera *camera = PlayerCamera();
    uint32_t clock = RealClock_Div2Ticks;
    if (camera->unknown145) {
        camera->unknown145 = false;
    } else if (!glbMissionManager->IsSkippingCinematic() || fgSimulation.simState == kSimPaused) {
        fgRenderHigh->Render();
        LastRenderClock = clock;
    }
}

// FUNC_AT(0x00048780)
ERenderFrame* ERenderFrame::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00043370)
void ERenderFrame_MakeEvent_Callback(const void *) {
    ERenderFrame *event = new ERenderFrame;
    if (event != NULL) {
        event->vtable = kERenderFrameVtable;
        event->unknown04 = 0;
    }
}

// FUNC_AT(0x00043960)
void ESetAreaBrightness::Destruct() {
    vtable = kESetAreaBrightnessVtable;
    Process();
    vtable = kEventVtable;
}

void ESetAreaBrightness::Process() {
    if (percent > kMaxBrightness)
        percent = kMaxBrightness;
    else if (percent < 0)
        percent = 0;
    Colorize->SetAreaBrightness(float(double(percent) * kPercent));
}

// FUNC_AT(0x000488c0)
ESetAreaBrightness* ESetAreaBrightness::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x000439e0)
void ESetAreaBrightness_MakeEvent_Callback(const ESetAreaBrightness::Record *record) {
    ESetAreaBrightness *event = new ESetAreaBrightness;
    if (event != NULL) {
        event->vtable = kESetAreaBrightnessVtable;
        event->percent = record->percent;
    }
}

// FUNC_AT(0x00043fa0)
void ESetCullDistanceFactor::Destruct() {
    vtable = kESetCullDistanceFactorVtable;
    Process();
    vtable = kEventVtable;
}

void ESetCullDistanceFactor::Process() {
    CullDistance = factor * kCullDistanceScale;
}

// FUNC_AT(0x000489c0)
ESetCullDistanceFactor* ESetCullDistanceFactor::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00043fc0)
void ESetCullDistanceFactor_MakeEvent_Callback(const ESetCullDistanceFactor::Record *record) {
    ESetCullDistanceFactor *event = new ESetCullDistanceFactor;
    if (event != NULL) {
        event->vtable = kESetCullDistanceFactorVtable;
        event->factor = record->factor;
    }
}

// FUNC_AT(0x00043fe0)
void ESetFrameAnimationTime::Destruct() {
    vtable = kESetFrameAnimationTimeVtable;
    Process();
    vtable = kEventVtable;
}

void ESetFrameAnimationTime::Process() {
    FrameAnimationTime = float(int32_t(Ftol(double(VideoModeRate) * seconds) + GameTick));
}

// FUNC_AT(0x000489e0)
ESetFrameAnimationTime* ESetFrameAnimationTime::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00044020)
void ESetFrameAnimationTime_MakeEvent_Callback(const ESetFrameAnimationTime::Record *record) {
    ESetFrameAnimationTime *event = new ESetFrameAnimationTime;
    if (event != NULL) {
        event->vtable = kESetFrameAnimationTimeVtable;
        event->seconds = record->seconds;
    }
}

// FUNC_AT(0x00044040)
void ESetJumpGravity::Destruct() {
    vtable = kESetJumpGravityVtable;
    Process();
    vtable = kEventVtable;
}

void ESetJumpGravity::Process() {
    RigidPlayerGravity = gravity;
}

// FUNC_AT(0x00048a00)
ESetJumpGravity* ESetJumpGravity::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00044050)
void ESetJumpGravity_MakeEvent_Callback(const ESetJumpGravity::Record *record) {
    ESetJumpGravity *event = new ESetJumpGravity;
    if (event != NULL) {
        event->vtable = kESetJumpGravityVtable;
        event->gravity = record->gravity;
    }
}

// FUNC_AT(0x00043ec0)
void ESetCollisionGeometry::Destruct() {
    vtable = kESetCollisionGeometryVtable;
    Process();
    vtable = kEventVtable;
}

void ESetCollisionGeometry::Process() {
    if (sceneObj == NULL)
        sceneObj = fgWorld->GetSceneObjFromInstance(instance);
    if (sceneObj != NULL)
        fgCollisionMgr->SetCollisionArticle(static_cast<CARP::Instance *>(sceneObj->sourceInstance), article);
}

// FUNC_AT(0x000489a0)
ESetCollisionGeometry* ESetCollisionGeometry::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00043f40)
void ESetCollisionGeometry_MakeEvent_Callback(const ESetCollisionGeometry::Record *record) {
    ESetCollisionGeometry *event = new ESetCollisionGeometry;
    if (event != NULL) {
        event->vtable = kESetCollisionGeometryVtable;
        event->instance = record->instance;
        event->article = record->article;
        event->sceneObj = gEventDynamicData.sceneObj;
    }
}

// =============================================================================================================
// The AI
// =============================================================================================================

// FUNC_AT(0x00043d50)
void ESetAvoidZone::Destruct() {
    vtable = kESetAvoidZoneVtable;
    Process();
    vtable = kEventVtable;
}

// No check of the element or the character, as in the original
void ESetAvoidZone::Process() {
    CARP::AIElement *redirected = fgAIElementController->RedirectCarpElement(element);
    if (redirected->unknown48 != 1 || redirected->type != CARP::kAIElementCharacter)
        return;
    AICharacter *character =
        reinterpret_cast<AICharacter *>(redirected->character);
    character->avoiding = 1;
    character->avoidCentre = trigger->position;
    character->avoidRadius = trigger->radius;
}

// FUNC_AT(0x00048960)
ESetAvoidZone* ESetAvoidZone::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00043df0)
void ESetAvoidZone_MakeEvent_Callback(const ESetAvoidZone::Record *record) {
    ESetAvoidZone *event = new ESetAvoidZone;
    if (event != NULL) {
        event->vtable = kESetAvoidZoneVtable;
        event->element = record->element;
        event->trigger = gEventDynamicData.trigger;
    }
}

// FUNC_AT(0x00043560)
void ERollSub::Destruct() {
    vtable = kERollSubVtable;
    Process();
    vtable = kEventVtable;
}

void ERollSub::Process() {
    if (element == NULL)
        return;
    CARP::AIElement *redirected = fgAIElementController->RedirectCarpElement(element);
    if (redirected->unknown48 != 1 || redirected->type != CARP::kAIElementVehicle || redirected->object == NULL)
        return;
    if (degrees > kMaxRoll)
        degrees = kMaxRoll;
    else if (degrees < -kMaxRoll)
        degrees = -kMaxRoll;
    (static_cast<PVehicle *>(redirected->object))->RollSub(uint8_t(degrees), uint8_t(subRolling));
}

// FUNC_AT(0x000487e0)
ERollSub* ERollSub::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00043610)
void ERollSub_MakeEvent_Callback(const ERollSub::Record *record) {
    ERollSub *event = new ERollSub;
    if (event != NULL) {
        event->vtable = kERollSubVtable;
        event->element = record->element;
        event->degrees = record->degrees;
        event->subRolling = record->subRolling;
    }
}

// =============================================================================================================
// The player's car
// =============================================================================================================

// FUNC_AT(0x0004d1e0)
void EPlayerSetImmunity::Destruct() {
    vtable = kEPlayerSetImmunityVtable;
    Process();
    vtable = kEventVtable;
}

void EPlayerSetImmunity::Process() {
    PlayerCar()->SetImmunity(on != 0);
}

// FUNC_AT(0x0004ed20)
EPlayerSetImmunity* EPlayerSetImmunity::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00042b90)
void EPlayerSetImmunity_MakeEvent_Callback(const EPlayerSetImmunity::Record *record) {
    EPlayerSetImmunity *event = new EPlayerSetImmunity;
    if (event != NULL) {
        event->vtable = kEPlayerSetImmunityVtable;
        event->on = record->on;
    }
}

// FUNC_AT(0x0004d460)
void EResetPlayerCar::Destruct() {
    vtable = kEResetPlayerCarVtable;
    Process();
    vtable = kEventVtable;
}

void EResetPlayerCar::Process() {
    PlayerCar()->ResetCar(false);
}

// FUNC_AT(0x0004ed80)
EResetPlayerCar* EResetPlayerCar::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00043410)
void EResetPlayerCar_MakeEvent_Callback(const void *) {
    EResetPlayerCar *event = new EResetPlayerCar;
    if (event != NULL)
        event->vtable = kEResetPlayerCarVtable;
}

// FUNC_AT(0x0004d4c0)
void EResetPlayerCarPos::Destruct() {
    vtable = kEResetPlayerCarPosVtable;
    Process();
    vtable = kEventVtable;
}

// The momentum: along the direction, the car's mass times the speed
void EResetPlayerCarPos::Process() {
    Coord4 position = place->position;
    position.w = 1.0f;
    Coord4 direction = place->direction;
    direction.w = 1.0f;
    fgSimulation.SetStartConditions(Xyz(&position), Xyz(&direction));
    PlayerCar()->ResetCar(Xyz(&position), Xyz(&direction));

    alignas(16) Coord4 momentum = { direction.x, direction.y, direction.z, 0.0f };
    float push = fgSimulation.GetRigidBody(PlayerCar()->rigidBodySlot)->mass * speed;
    VU0_v4unitxyz(&momentum, &momentum);
    VU0_v4scale(&momentum, push, &momentum);
    fgSimulation.GetRigidBody(PlayerCar()->rigidBodySlot)->momentum = *Xyz(&momentum);
}

// FUNC_AT(0x0004eda0)
EResetPlayerCarPos* EResetPlayerCarPos::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00043430)
void EResetPlayerCarPos_MakeEvent_Callback(const EResetPlayerCarPos::Record *record) {
    EResetPlayerCarPos *event = new EResetPlayerCarPos;
    if (event != NULL) {
        event->vtable = kEResetPlayerCarPosVtable;
        event->place = record->place;
        event->speed = record->speed;
    }
}

// =============================================================================================================
// The power-ups
// =============================================================================================================

// FUNC_AT(0x00042be0)
void EPowerUp::Destruct() {
    vtable = kEPowerUpVtable;
    Process();
    vtable = kEventVtable;
}

void EPowerUp::Process() {
    alignas(16) Coord4 rotation;
    VU0_v4Init(&rotation);
    Coord3 at = { position.x, position.y + 1.0f, position.z };
    GFX::TriggerByName("Pr_Dstry", &at, &rotation, &kNoOffset, &kNoRotation, NULL, 0, NULL);
    if (procAnimState != NULL && procAnimState->sceneObj != NULL) {
        procAnimState->sceneObj->Hide();
        if (procAnimState->sceneObj->animHandle != NULL)
            procAnimState->sceneObj->animHandle->ProcessStimuli(kPowerUpStimulus, fgSimulation.stepCount,
                                                                kStimulusMode2);
    }
    fgSimulation.UntrackInstance(instance);
}

// FUNC_AT(0x00048640)
EPowerUp* EPowerUp::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00042cc0)
void EPowerUp_MakeEvent_Callback(const EPowerUp::Record *record) {
    EPowerUp *event = new EPowerUp;
    if (event != NULL) {
        event->vtable = kEPowerUpVtable;
        event->instance = record->instance;
        event->procAnimState = record->procAnimState;
        event->position = gEventDynamicData.position;
    }
}

// FUNC_AT(0x00042d20)
void EPowerUpAmmo::Destruct() {
    vtable = kEPowerUpAmmoVtable;
    Process();
    vtable = kEventVtable;
}

// The HUD's message: the ammunition's for a loaded weapon with ammunition, else the pick-up's
void EPowerUpAmmo::Process() {
    if (ammo <= 0)
        return;
    const WeaponRecord *record = &WeaponManager->weapons[weapon];
    if (GHud::TheApp() != NULL) {
        int message = (record->flags & kWeaponLoaded) && record->ammo > 0 ? record->ammoText : record->pickupText;
        GHud::TheApp()->TriggerMissionMsg(message, kPowerUpMessageTime);
    }
    WeaponManager->LoadWeapon(weapon, ammo, 0, 0);
    AMenuSound::Trigger(kPowerUpBank, "SFX_PowerupWeap", "Power Ups", kMenuSoundView);
}

// FUNC_AT(0x00048660)
EPowerUpAmmo* EPowerUpAmmo::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00042df0)
void EPowerUpAmmo_MakeEvent_Callback(const EPowerUpAmmo::Record *record) {
    EPowerUpAmmo *event = new EPowerUpAmmo;
    if (event != NULL) {
        event->vtable = kEPowerUpAmmoVtable;
        event->weapon = record->weapon;
        event->ammo = record->ammo;
    }
}

// FUNC_AT(0x00042e20)
void EPowerUpBraking_MakeEvent_Callback(const EPowerUpBraking::Record *record) {
    EPowerUpBraking *event = new EPowerUpBraking;
    if (event != NULL) {
        event->vtable = kEPowerUpBrakingVtable;
        event->unknown04 = record->unknown04;
        event->unknown08 = record->unknown08;
    }
}

// FUNC_AT(0x0004d3d0)
void EPowerUpHealth::Destruct() {
    vtable = kEPowerUpHealthVtable;
    Process();
    vtable = kEventVtable;
}

void EPowerUpHealth::Process() {
    glbMissionManager->BoostPlayerHealth(float(double(percent) * kPercent));
    PlayerCar()->ResetDamage();
    AMenuSound::Trigger(kPowerUpBank, "SFX_pickuparmor", "Power Ups", kMenuSoundView);
}

// FUNC_AT(0x0004ed60)
EPowerUpHealth* EPowerUpHealth::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00042e50)
void EPowerUpHealth_MakeEvent_Callback(const EPowerUpHealth::Record *record) {
    EPowerUpHealth *event = new EPowerUpHealth;
    if (event != NULL) {
        event->vtable = kEPowerUpHealthVtable;
        event->percent = record->percent;
    }
}

// FUNC_AT(0x00042e70)
void EPowerUpInvulnerable_MakeEvent_Callback(const EPowerUpInvulnerable::Record *record) {
    EPowerUpInvulnerable *event = new EPowerUpInvulnerable;
    if (event != NULL) {
        event->vtable = kEPowerUpInvulnerableVtable;
        event->unknown04 = record->unknown04;
    }
}

// FUNC_AT(0x00042e90)
void EPowerUpMultiDamage::Destruct() {
    vtable = kEPowerUpMultiDamageVtable;
    Process();
    vtable = kEventVtable;
}

void EPowerUpMultiDamage::Process() {
    WeaponManager->doubleShot = 1;
}

// FUNC_AT(0x000486a0)
EPowerUpMultiDamage* EPowerUpMultiDamage::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00042eb0)
void EPowerUpMultiDamage_MakeEvent_Callback(const EPowerUpMultiDamage::Record *record) {
    EPowerUpMultiDamage *event = new EPowerUpMultiDamage;
    if (event != NULL) {
        event->vtable = kEPowerUpMultiDamageVtable;
        event->unknown04 = record->unknown04;
        event->unknown08 = record->unknown08;
    }
}

// FUNC_AT(0x00042ee0)
void EPowerUpShield::Destruct() {
    vtable = kEPowerUpShieldVtable;
    Process();
    vtable = kEventVtable;
}

void EPowerUpShield::Process() {
    glbMissionManager->BoostPlayerShield(float(double(percent) * kPercent));
    AMenuSound::Trigger(kPowerUpBank, "SFX_pickuparmor", "Power Ups", kMenuSoundView);
    if (GHud::TheApp() != NULL)
        GHud::TheApp()->TriggerMissionMsg(kShieldMessage, kPowerUpMessageTime);
}

// FUNC_AT(0x000486c0)
EPowerUpShield* EPowerUpShield::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00042f80)
void EPowerUpShield_MakeEvent_Callback(const EPowerUpShield::Record *record) {
    EPowerUpShield *event = new EPowerUpShield;
    if (event != NULL) {
        event->vtable = kEPowerUpShieldVtable;
        event->percent = record->percent;
    }
}

// FUNC_AT(0x00042fa0)
void EPowerUpTimer::Destruct() {
    vtable = kEPowerUpTimerVtable;
    Process();
    vtable = kEventVtable;
}

void EPowerUpTimer::Process() {
    glbMissionManager->timers[timer].Increment(Ftol(double(fgSimulation.stepsPerSecond) * seconds));
}

// FUNC_AT(0x000486e0)
EPowerUpTimer* EPowerUpTimer::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00043010)
void EPowerUpTimer_MakeEvent_Callback(const EPowerUpTimer::Record *record) {
    EPowerUpTimer *event = new EPowerUpTimer;
    if (event != NULL) {
        event->vtable = kEPowerUpTimerVtable;
        event->timer = record->timer;
        event->seconds = record->seconds;
    }
}

// FUNC_AT(0x00043040)
void EPowerUpTopSpeed_MakeEvent_Callback(const EPowerUpTopSpeed::Record *record) {
    EPowerUpTopSpeed *event = new EPowerUpTopSpeed;
    if (event != NULL) {
        event->vtable = kEPowerUpTopSpeedVtable;
        event->unknown04 = record->unknown04;
        event->unknown08 = record->unknown08;
    }
}

// =============================================================================================================
// The mission
// =============================================================================================================

// FUNC_AT(0x00042050)
EPause* EPause::Construct() {
    vtable = kEPauseVtable;
    return this;
}

// FUNC_AT(0x00042060)
void EPause::Destruct() {
    vtable = kEPauseVtable;
    Process();
    vtable = kEventVtable;
}

void EPause::Process() {
    if (fgSimulation.simState != kSimPaused) {
        fgSimulation.PauseSimState();
        glbMissionManager->Stop();
        gFeedbackAllowed = false;
    }
    ASoundManager::Pause();
}

// FUNC_AT(0x00048440)
EPause* EPause::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x000420d0)
void EPause_MakeEvent_Callback(const void *) {
    EPause *event = new EPause;
    if (event != NULL)
        event->vtable = kEPauseVtable;
}

// FUNC_AT(0x00043490)
EResume* EResume::Construct() {
    vtable = kEResumeVtable;
    return this;
}

// FUNC_AT(0x0004d7c0)
void EResume::Destruct() {
    vtable = kEResumeVtable;
    Process();
    vtable = kEventVtable;
}

void EResume::Process() {
    if (fgSimulation.simState != kSimPaused)
        return;
    fgSimulation.UnPauseSimState();
    if (glbMissionManager->missionState == kMissionStopped)
        glbMissionManager->Start();
    ASoundManager::Resume();
    gFeedbackAllowed = true;
    PlayerCamera()->PauseOff();
    PlayerCar()->FlushCarControls();
}

// FUNC_AT(0x0004ede0)
EResume* EResume::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x000434a0)
void EResume_MakeEvent_Callback(const void *) {
    EResume *event = new EResume;
    if (event != NULL)
        event->vtable = kEResumeVtable;
}

// FUNC_AT(0x00043460)
ERestart* ERestart::Construct() {
    vtable = kERestartVtable;
    return this;
}

// FUNC_AT(0x0004d620)
void ERestart::Destruct() {
    vtable = kERestartVtable;
    Process();
    vtable = kEventVtable;
}

// Everything taken down and set up again, the Simulation reset twice; the car keeps its colour
void ERestart::Process() {
    uint32_t colour = PlayerCar()->GetCarColour();
    glbMissionManager->SetAutoDrive(false, true);
    glbMissionManager->SetDriveMissile(NULL);
    AIElementController::Destruct();
    AICharacter::Shutdown();
    RayShell::Reset();
    fgSimulation.Reset();
    GFX::PurgeAllEffects();
    GHud::TheApp()->Reset();
    fgSimulation.Reset();
    AnimPool_Cleanup();
    AnimPool_Init(kAnimPoolSize);
    fgWorld->Reset();

    const Coord3 direction = { 0.0f, 0.0f, 1.0f };
    const Coord3 position = { 0.0f, 0.0f, 0.0f };
    fgSimulation.SpawnCarObject(0, glbMissionManager->GetCarType(), colour, &direction, &position);
    WeaponManager->SetPlayerCar(PlayerCar()->renderObject);
    PlayerCar()->SetHitPointLoc(&glbMissionManager->playerHealth);
    WRoadNetwork::Get()->Restart();
    AIVehicleController::Get()->Reset();
    AIVehicleController::Get()->CreateSimTrafficCars(false);
    AIRoadSpawn::Get()->Restart();
    AIVehicleController::Get()->RegisterSimCarList();
    RRenderHigh::RestartTrackRender();
    AICharacter::Init();
    Scheduler::Reset();
    gFeedbackAllowed = true;
    ASoundManager::Restart();
}

// FUNC_AT(0x0004edc0)
ERestart* ERestart::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00043470)
void ERestart_MakeEvent_Callback(const void *) {
    ERestart *event = new ERestart;
    if (event != NULL)
        event->vtable = kERestartVtable;
}

// FUNC_AT(0x0004cf50)
void EPlayerLose::Destruct() {
    vtable = kEPlayerLoseVtable;
    Process();
    vtable = kEventVtable;
}

// The death sound and the blood curtain when asked, or in auto-drive with no health left
void EPlayerLose::Process() {
    if (glbMissionManager->missionState == kMissionWon)
        return;
    if (Launch.restartLost) {
        Launch.restartCount++;
        ERestart *restart = new ERestart;
        if (restart != NULL)
            restart->Construct();
        return;
    }
    if (gPlayerCanNeverLose)
        return;

    glbMissionManager->Lose();
    fgAIElementController->EveryoneChill();
    PlayerCar()->SetCarControlGas(0.0f);
    PlayerCar()->SetCarControlBrake(0.0f);
    if (PlayerCar()->GetPhysics()->isSub == 0) {
        PlayerCar()->SetCarControlHandBrake(true);
    } else {
        PlayerCar()->SetCarControlSteering(0.0f);
        PlayerCar()->SetCarControlSteeringVertical(0.0f);
        if (glbMissionManager->driveMissile != NULL)
            glbMissionManager->driveMissile->detonate = 1;
    }
    PlayerCar()->SetCarControlFirePrimary(false);
    PlayerCar()->LockCarControlForever();
    IFeedback::Pause();
    fgTriggerManager->active = false;
    ASoundManager::SetMissionOver();
    glbMissionManager->SetDriveMissile(NULL);
    if (glbMissionManager->autoDrive) {
        if (fgHandObj != NULL)
            fgHandObj->HideHands();
        const Coord4 target = { 0.0f, 0.0f, 0.0f, 0.0f };
        PlayerCamera()->CameraLockOn(NULL, &target, 0, 0);
    } else if (keepCamera == 0) {
        PlayerCamera()->SetPauseCamera();
    }

    if (GHud::TheApp() != NULL) {
        if (bloodCurtain != 0 || (glbMissionManager->autoDrive && glbMissionManager->GetPlayerHealth(false) <= 0.0)) {
            AMix *fader = AMix::Get("0:Fader0");
            fader->previous = fader->volume;
            fader->volume = kDeathFaderVolume;
            AMenuSound::Trigger(kSpeechBank, "SFX_BondDeath", "Speech", kMenuSoundView);
            GHud::TheApp()->TriggerBloodCurtain();
        }
        GHud::TheApp()->SetMissionFailed(failedFlag != 0);
    }
    Launch.unknown978 = glbMissionManager->GetPlayerHealth(false) <= 0.0 ? kEndLostDead : kEndLostAlive;
}

// FUNC_AT(0x0004ed00)
EPlayerLose* EPlayerLose::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00042b50)
void EPlayerLose_MakeEvent_Callback(const EPlayerLose::Record *record) {
    EPlayerLose *event = new EPlayerLose;
    if (event != NULL) {
        event->vtable = kEPlayerLoseVtable;
        event->keepCamera = record->keepCamera;
        event->unknown08 = record->unknown08;
        event->failedFlag = record->failedFlag;
        event->bloodCurtain = record->bloodCurtain;
    }
}

// FUNC_AT(0x0004d240)
void EPlayerWin::Destruct() {
    vtable = kEPlayerWinVtable;
    Process();
    vtable = kEventVtable;
}

void EPlayerWin::Process() {
    if (glbMissionManager->missionState == kMissionLost)
        return;
    ASoundManager::SetMissionOver();
    glbMissionManager->Win();
    fgAIElementController->EveryoneChill();
    PlayerCar()->GetAIGroundVehiclePtr()->autoDriveSpeed = 0.0f;
    PlayerCar()->SetCarControlGas(0.0f);
    PlayerCar()->SetCarControlBrake(0.0f);
    PlayerCar()->SetCarControlFirePrimary(false);
    PlayerCar()->LockCarControlForever();
    if (PlayerCar()->GetPhysics()->isSub == 0)
        PlayerCar()->SetCarControlHandBrake(true);
    else if (glbMissionManager->driveMissile != NULL)
        glbMissionManager->driveMissile->detonate = 1;
    fgTriggerManager->active = false;
    IFeedback::Pause();
    if (glbMissionManager->autoDrive) {
        const Coord4 target = { 0.0f, 0.0f, 0.0f, 0.0f };
        PlayerCamera()->CameraLockOn(NULL, &target, 0, 0);
    } else if (keepCamera == 0) {
        PlayerCamera()->SetPauseCamera();
    }
    if (GHud::TheApp() != NULL)
        GHud::TheApp()->SetMissionPassed(passedFlag != 0);
    Launch.unknown978 = kEndWon;
}

// FUNC_AT(0x0004ed40)
EPlayerWin* EPlayerWin::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00042bb0)
void EPlayerWin_MakeEvent_Callback(const EPlayerWin::Record *record) {
    EPlayerWin *event = new EPlayerWin;
    if (event != NULL) {
        event->vtable = kEPlayerWinVtable;
        event->keepCamera = record->keepCamera;
        event->passedFlag = record->passedFlag;
    }
}

// FUNC_AT(0x00043070)
void EProfileMissionSection::Destruct() {
    vtable = kEProfileMissionSectionVtable;
    Process();
    vtable = kEventVtable;
}

void EProfileMissionSection::Process() {
    glbMissionManager->StartSectionProfile(section);
}

// FUNC_AT(0x00048720)
EProfileMissionSection* EProfileMissionSection::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x000430c0)
void EProfileMissionSection_MakeEvent_Callback(const EProfileMissionSection::Record *record) {
    EProfileMissionSection *event = new EProfileMissionSection;
    if (event != NULL) {
        event->vtable = kEProfileMissionSectionVtable;
        event->section = record->section;
    }
}

// FUNC_AT(0x00048800)
ESapphirePole* ESapphirePole::Delete(unsigned flags) {
    Event::Destruct();
    if (flags & 1)
        Event::operator delete(this, sizeof(ESapphirePole));
    return this;
}

// FUNC_AT(0x00043650)
void ESapphirePole_MakeEvent_Callback(const void *) {
    ESapphirePole *event = new ESapphirePole;
    if (event != NULL)
        event->vtable = kESapphirePoleVtable;
}

// FUNC_AT(0x00043740)
void EScheduleEvent::Destruct() {
    vtable = kEScheduleEventVtable;
    Process();
    vtable = kEventVtable;
}

void EScheduleEvent::Process() {
    fgScheduler->s_oncePerGameLoop->AddTask(kScheduledEvent, staticData, 0, true, delayTicks, 1);
}

// FUNC_AT(0x00048860)
EScheduleEvent* EScheduleEvent::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x000437a0)
void EScheduleEvent_MakeEvent_Callback(const EScheduleEvent::Record *record) {
    EScheduleEvent *event = new EScheduleEvent;
    if (event != NULL) {
        event->vtable = kEScheduleEventVtable;
        event->staticData = record->staticData;
        event->delayTicks = record->delayTicks;
        event->unknown0c = record->unknown0c;
    }
}

// FUNC_AT(0x00043e20)
void ESetBitMagicCounter::Destruct() {
    vtable = kESetBitMagicCounterVtable;
    Process();
    vtable = kEventVtable;
}

void ESetBitMagicCounter::Process() {
    glbMissionManager->SetMagicCounter(counter, glbMissionManager->GetMagicCounter(counter) | bits);
}

// FUNC_AT(0x00048980)
ESetBitMagicCounter* ESetBitMagicCounter::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00043e90)
void ESetBitMagicCounter_MakeEvent_Callback(const ESetBitMagicCounter::Record *record) {
    ESetBitMagicCounter *event = new ESetBitMagicCounter;
    if (event != NULL) {
        event->vtable = kESetBitMagicCounterVtable;
        event->counter = record->counter;
        event->bits = record->bits;
    }
}

// FUNC_AT(0x00044070)
void ESetMagicCounter::Destruct() {
    vtable = kESetMagicCounterVtable;
    Process();
    vtable = kEventVtable;
}

void ESetMagicCounter::Process() {
    glbMissionManager->SetMagicCounter(counter, value);
}

// FUNC_AT(0x00048a20)
ESetMagicCounter* ESetMagicCounter::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x000440d0)
void ESetMagicCounter_MakeEvent_Callback(const ESetMagicCounter::Record *record) {
    ESetMagicCounter *event = new ESetMagicCounter;
    if (event != NULL) {
        event->vtable = kESetMagicCounterVtable;
        event->counter = record->counter;
        event->value = record->value;
    }
}

// FUNC_AT(0x00044100)
void ESetMagicCounterThreshold::Destruct() {
    vtable = kESetMagicCounterThresholdVtable;
    Process();
    vtable = kEventVtable;
}

void ESetMagicCounterThreshold::Process() {
    glbMissionManager->SetMagicCounterThreshold(counter, threshold);
}

// FUNC_AT(0x00048a40)
ESetMagicCounterThreshold* ESetMagicCounterThreshold::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00044160)
void ESetMagicCounterThreshold_MakeEvent_Callback(const ESetMagicCounterThreshold::Record *record) {
    ESetMagicCounterThreshold *event = new ESetMagicCounterThreshold;
    if (event != NULL) {
        event->vtable = kESetMagicCounterThresholdVtable;
        event->counter = record->counter;
        event->threshold = record->threshold;
    }
}

// FUNC_AT(0x00044190)
void ESetMissionMessage::Destruct() {
    vtable = kESetMissionMessageVtable;
    Process();
    vtable = kEventVtable;
}

void ESetMissionMessage::Process() {
    glbMissionManager->AddMessage(unknown04, unknown08, unknown0c);
}

// FUNC_AT(0x00048a60)
ESetMissionMessage* ESetMissionMessage::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x000441f0)
void ESetMissionMessage_MakeEvent_Callback(const ESetMissionMessage::Record *record) {
    ESetMissionMessage *event = new ESetMissionMessage;
    if (event != NULL) {
        event->vtable = kESetMissionMessageVtable;
        event->unknown04 = record->unknown04;
        event->unknown08 = record->unknown08;
        event->unknown0c = record->unknown0c;
    }
}

// FUNC_AT(0x000442e0)
void ESetObjective::Destruct() {
    vtable = kESetObjectiveVtable;
    Process();
    vtable = kEventVtable;
}

void ESetObjective::Process() {
    glbMissionManager->SetObjectiveComment(0, comment);
    if (marker != NULL) {
        glbMissionManager->SetObjective(0, marker);
    } else if (target != NULL) {
        glbMissionManager->SetObjective(0, target);
    } else if (element != NULL) {
        glbMissionManager->SetObjective(0, element);
    } else {
        glbMissionManager->ClearObjective(0);
        glbMissionManager->SetObjectiveComment(0, 0);
    }
}

// FUNC_AT(0x00048aa0)
ESetObjective* ESetObjective::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00044390)
void ESetObjective_MakeEvent_Callback(const ESetObjective::Record *record) {
    ESetObjective *event = new ESetObjective;
    if (event != NULL) {
        event->vtable = kESetObjectiveVtable;
        event->marker = record->marker;
        event->target = record->target;
        event->element = record->element;
        event->comment = record->comment;
    }
}

// =============================================================================================================
// ECameraLockOn's MakeEvent (the class is EventsA.h's)
// =============================================================================================================

// FUNC_AT(0x0003ec60)
void ECameraLockOn_MakeEvent_Callback(const ECameraLockOn::Record *record) {
    ECameraLockOn *event = new ECameraLockOn;
    if (event != NULL) {
        event->vtable = kECameraLockOnVtable;
        event->element = record->element;
        event->point = record->point;
        event->wheel = record->wheel;
        event->member = record->member;
        event->targetY = record->targetY;
        event->targetZ = record->targetZ;
        event->targetX = record->targetX;
        event->param = record->param;
        event->quiet = record->quiet;
    }
}

// =============================================================================================================
// The path events' helper and the other functions placed here
// =============================================================================================================

// FUNC_AT(0x0003da90)
RPathHandle* EPathUtil_GetFromInstanceOrAI(const CARP::Instance *instance, CARP::AIElement *element) {
    if (instance != NULL)
        return InstancePath(instance);
    AIVehicle *vehicle;
    if (element != NULL) {
        CARP::AIElement *redirected = fgAIElementController->RedirectCarpElement(element);
        if (redirected->unknown48 != 1 || redirected->type != CARP::kAIElementVehicle)
            return NULL;
        vehicle = redirected->vehicle;
    } else {
        vehicle = PlayerCar()->GetAIGroundVehiclePtr();
    }
    AISplinePath *spline = vehicle->GetSplinePath();
    return spline != NULL ? spline->path : NULL;
}

