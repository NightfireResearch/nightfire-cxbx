#pragma fp_contract(off)

#include "EventsA.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <bit>

#include "../anim/AnimEngine.h"         // RAnimEngine::Update, Handle
#include "../anim/Character.h"          // MinFloat
#include "../anim/Weapon.h"             // ActWeapon
#include "../audio/Fader.h"             // AFader
#include "../audio/Mix.h"               // AMix, AFX, AListener
#include "../audio/SoundManager.h"      // ASoundManager::BuildPaths
#include "../audio/Stream.h"            // AStream
#include "../audio/Voice.h"             // AVoice::PlayVoices
#include "../camera/PlayerCamera.h"     // RPlayerCamera
#include "../data/AttributeSet.h"
#include "../data/Carp.h"               // CARP::Instance, CARP::AISpline, TagReference
#include "../engine/CoreContainers.h"   // GameVector
#include "../engine/CoreFoundation.h"   // NullFunction
#include "../engine/Feedback.h"         // IFeedback
#include "../engine/GameLoop.h"         // LaunchPage
#include "../engine/IOModule.hpp"
#include "../engine/UMemory.hpp"        // OperatorNew, OperatorDelete
#include "../physics/RigidBody.h"
#include "../platform/RealMath.h"
#include "../platform/RealMemory.h"     // MEM_totalunused
#include "../platform/X87.h"            // Truncate
#include "../render/PathEngine.h"       // RPathEngine
#include "../render/RenderHigh.h"       // fgRenderHigh
#include "../render/RSceneObj.hpp"
#include "../world/CollisionInstance.h" // WTrigger
#include "../world/CollisionTypes.h"    // MatrixRow
#include "../world/RoadNav.h"           // WRoadNav
#include "../world/SoundGroup.h"        // WSoundGroup
#include "../world/Trigger.h"           // gEventDynamicData
#include "../world/World.h"             // fgWorld, ArticleOf, kUntrackedModel
#include "../world/WorldPos.h"
#include "../../helpers.h"
#include "AICharacter.h"
#include "AIElements.h"
#include "AIHelicopter.h"
#include "AIPedestrian.h"
#include "AIRoadSpawn.h"
#include "AIVehicleController.h"
#include "BondCarState.h"               // CarSurface
#include "EventsD.h"                    // StingerChannel
#include "Gfx.h"
#include "Helicopter.h"                 // PHelicopter::scoreable
#include "Hud.h"
#include "Human.h"
#include "MissionManager.h"             // glbMissionManager
#include "Simulation.h"                 // fgSimulation
#include "VehicleSound.h"               // AVehicle

// ---------------------------------------------------------------------------------------------------------------
// The events of package A (0x0003db80-0x0004ec40): their MakeEvent and ResolveEvent callbacks, their destructors
// (where each does its work, in Process) and their scalar deleting destructors. Ported from the listing.
// ---------------------------------------------------------------------------------------------------------------

// ---- the game's globals

#define gStreams BOOL8_AT(0x001b7056)
#define gSuperEasy BOOL8_AT(0x001e476c)
#define gFeedbackAllowed BOOL8_AT(0x001b6f50)
// The game's zero vector and (0, 0, 0, 1), whose addresses TriggerIncidental is handed
// XBE_GLOBAL(0x00243030, 0x10)
#define GameZeroVector ((const Coord3 *)0x00243030)
#define GameDefaultVector ((const Coord4 *)0x001d4c00)

// The C runtime's printf (the game's)
#define CRT_printf ((int (*)(const char *, ...))0x00132192)

// XBE_GLOBAL(0x001e23a0, 0x4)
uint32_t LastIncidentalStep;
// XBE_GLOBAL(0x001e23a4, 0x4)
uint32_t LastPlayerIncidentalStep;

// ---- the vtables (the game's; each has the one slot, the scalar deleting destructor)

#define kE007LogoVtable ((vtable_Event *)0x0018c3b0)
#define kEAICommandVtable ((vtable_Event *)0x0018c3d0)
#define kEAIElementFireOffVtable ((vtable_Event *)0x0018c3d4)
#define kEAIElementFireOnVtable ((vtable_Event *)0x0018c3d8)
#define kEAIElementSetAccuracyVtable ((vtable_Event *)0x0018c3dc)
#define kEAIElementSetCharParamsVtable ((vtable_Event *)0x0018c3e0)
#define kEAIElementSetNonScoreableVtable ((vtable_Event *)0x0018c3e4)
#define kEAIElementSetVehicleParamsVtable ((vtable_Event *)0x0018c3e8)
#define kEAIElementSetWakeRangeVtable ((vtable_Event *)0x0018c3ec)
#define kEAIUpdateVtable ((vtable_Event *)0x0018c3f0)
#define kEAbortCinematicVtable ((vtable_Event *)0x0018c3f4)
#define kEActivateAIElementVtable ((vtable_Event *)0x0018c3f8)
#define kEActorMuzzleFlashVtable ((vtable_Event *)0x0018c40c)
#define kEAddObjectiveVtable ((vtable_Event *)0x0018c410)
#define kEAddScoreActionVtable ((vtable_Event *)0x0018c414)
#define kEAddScoreObjectiveVtable ((vtable_Event *)0x0018c434)
#define kEAnimUpdateVtable ((vtable_Event *)0x0018c438)
#define kEAudioUpdateVtable ((vtable_Event *)0x0018c444)
#define kEAutoDriveVtable ((vtable_Event *)0x0018c448)
#define kEAutoDriveSpeedChangeVtable ((vtable_Event *)0x0018c44c)
#define kEAutoDriveSteeringVtable ((vtable_Event *)0x0018c450)
#define kEAwardHitToPlayerVtable ((vtable_Event *)0x0018c454)
#define kEAwardKillToPlayerVtable ((vtable_Event *)0x0018c458)
#define kEBashCarAlongObjectAxisVtable ((vtable_Event *)0x0018c45c)
#define kEBeginDownloadCodeVtable ((vtable_Event *)0x0018c460)
#define kEButtonMsgOffVtable ((vtable_Event *)0x0018c464)
#define kEButtonMsgOnVtable ((vtable_Event *)0x0018c468)
#define kECall911Vtable ((vtable_Event *)0x0018c46c)
#define kECallStageVtable ((vtable_Event *)0x0018c470)
#define kECameraLockOnVtable ((vtable_Event *)0x0018c474)
#define kECameraShakeVtable ((vtable_Event *)0x0018c478)
#define kECameraUpdateVtable ((vtable_Event *)0x0018c47c)
#define kECameraZoomVtable ((vtable_Event *)0x0018c480)
#define kEChangeCarCameraViewVtable ((vtable_Event *)0x0018c484)
#define kEChangeMaxTrafficVtable ((vtable_Event *)0x0018c488)
#define kEChangeStageVtable ((vtable_Event *)0x0018c48c)
#define kECinematicCameraVtable ((vtable_Event *)0x0018c490)
#define kEClearBitMagicCounterVtable ((vtable_Event *)0x0018c494)
#define kEClearProgrammerEventVtable ((vtable_Event *)0x0018c498)
#define kECollisionVtable ((vtable_Event *)0x0018c49c)
#define kEControlToCPUVtable ((vtable_Event *)0x0018c4a0)
#define kEControlToPlayerVtable ((vtable_Event *)0x0018c4a4)
#define kEControlToSplineVtable ((vtable_Event *)0x0018c4a8)
#define kECustomTransitionVtable ((vtable_Event *)0x0018c4bc)
#define kEDamagePlayerVtable ((vtable_Event *)0x0018c4c0)
#define kEDeactivateAIElementVtable ((vtable_Event *)0x0018c4c4)
#define kEDisablePowerUpVtable ((vtable_Event *)0x0018c4c8)
#define kEDisableTriggerVtable ((vtable_Event *)0x0018c4cc)
#define kEDisplayObjectiveTextVtable ((vtable_Event *)0x0018c4d0)
#define kEDisplayTextVtable ((vtable_Event *)0x0018c4d4)
#define kEDropMineVtable ((vtable_Event *)0x0018c4d8)
#define kEEnablePowerUpVtable ((vtable_Event *)0x0018c4dc)
#define kEEnableTriggerVtable ((vtable_Event *)0x0018c4e0)
#define kEEndCameraAnimVtable ((vtable_Event *)0x0018c4e4)
#define kEEndCarStopVtable ((vtable_Event *)0x0018c4e8)
#define kEEndControlToSplineVtable ((vtable_Event *)0x0018c4ec)
#define kEEndMissionVtable ((vtable_Event *)0x0018c4f0)

namespace {


const char *const kSpeechFader = "SpeechVsAmbience";

// The listener EAudioUpdate makes: from operator new, eight bytes more than an AListener, in the view that hears a
// sound at its azimuth (AListener::view)
constexpr unsigned kListenerBytes = 0x80;
constexpr int kListenerView = 2;

constexpr float kOneSixtieth = 1.0f / 60.0f;
static_assert(std::bit_cast<uint32_t>(kOneSixtieth) == 0x3c888889, "1/60");
constexpr float kPercent = 0.01f;
static_assert(std::bit_cast<uint32_t>(kPercent) == 0x3c23d70a, "0.01");

// RPlayerCamera::SetCameraZoom's kind ECameraZoom uses (camera/PlayerCameraB.cpp's kZoomBetween)
constexpr int kZoomBetween = 2;

// EDropMine's animation system, its two stimuli and RAnimEngine::Handle::ProcessStimuli's mode (names ours)
constexpr uint32_t kMineSystem = 0x41;
constexpr uint8_t kMineStimulus1 = 8;
constexpr uint8_t kMineStimulus2 = 10;
constexpr int kStimulusMode2 = 2;

// ApplyDamage's kinds as the events pass them
constexpr int kDamageKind1 = 1;
constexpr int kDamageKind3 = 3;

// CARP::Instance::flags bit 0: EDisablePowerUp sets it, EEnablePowerUp clears it (name ours)
constexpr uint8_t kInstanceDisabled = 0x01;

// The player's car and camera
PVehicle *PlayerCar() {
    return fgSimulation.cars.first[0];
}
RPlayerCamera *PlayerCamera() {
    return fgRenderHigh->views[0].camera;
}

// A time the scripts give in sixtieths of a second, in simulation steps
int SixtiethsToSteps(uint32_t time) {
    return Truncate(float(double(time * uint32_t(fgSimulation.stepsPerSecond)) * kOneSixtieth));
}
int SixtiethsToSteps(int32_t time) {
    return Truncate(float(double(time * fgSimulation.stepsPerSecond) * kOneSixtieth));
}

// The C runtime's arc cosine (_CIacos at 0x001328d0, the game's own), which takes and answers on the x87 stack
__declspec(naked) double CrtAcos(float) {
    __asm {
        fld dword ptr [esp + 4]
        mov eax, 0x001328d0
        call eax
        ret
    }
}

} // namespace

// =============================================================================================================
// The per-tick events
// =============================================================================================================

// FUNC_AT(0x0003e2b0)
void EAIUpdate::Destruct() {
    vtable = kEAIUpdateVtable;
    Process();
    vtable = kEventVtable;
}

void EAIUpdate::Process() {
    if (fgSimulation.simState != kSimPaused) {
        AIVehicleController::Get()->UpdateVehicles();
        AIZoneController::Get()->Update();
        AICharacterPedestrian::Manage();
    }
}

// FUNC_AT(0x00047470)
EAIUpdate* EAIUpdate::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x0003e320)
void EAIUpdate_MakeEvent_Callback(const void *) {
    EAIUpdate *event = new EAIUpdate;
    if (event != NULL)
        event->vtable = kEAIUpdateVtable;
}

// FUNC_AT(0x0003e620)
void EAnimUpdate::Destruct() {
    vtable = kEAnimUpdateVtable;
    Process();
    vtable = kEventVtable;
}

void EAnimUpdate::Process() {
    if (fgSimulation.simState != kSimPaused) {
        RAnimEngine::Update(fgSimulation.stepCount);
        RPathEngine::Update(float(int32_t(fgSimulation.stepCount)));
    }
}

// FUNC_AT(0x00047510)
EAnimUpdate* EAnimUpdate::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x0003e690)
void EAnimUpdate_MakeEvent_Callback(const void *) {
    EAnimUpdate *event = new EAnimUpdate;
    if (event != NULL)
        event->vtable = kEAnimUpdateVtable;
}

// FUNC_AT(0x00049790)
void EAudioUpdate::Destruct() {
    vtable = kEAudioUpdateVtable;
    Process();
    vtable = kEventVtable;
}

// The world's sound groups, then the frame's listener - the first view's camera, or the player's car without the
// renderer - with the ambience of the environment under the car, then every sound for that listener.
void EAudioUpdate::Process() {
    if (fgWorld != NULL)
        fgWorld->soundGroup->Update();

    AListener *listener;
    if (fgRenderHigh != NULL) {
        RCamera *camera = fgRenderHigh->views[0].view->camera;
        listener = static_cast<AListener *>(OperatorNew(kListenerBytes));
        if (listener != NULL)
            listener = listener->Construct(&camera->matrix, reinterpret_cast<const Coord3 *>(&camera->unknown50),
                                           kListenerView);
    } else {
        RigidBody *body = fgSimulation.GetRigidBody(PlayerCar()->rigidBodySlot);
        alignas(16) MATRIX4 frame = body->info->orientation;
        const Coord3 *position = PlayerCar()->GetPosition();
        frame.mtx[3][0] = position->x;
        frame.mtx[3][1] = position->y;
        frame.mtx[3][2] = position->z;
        listener = static_cast<AListener *>(OperatorNew(kListenerBytes));
        if (listener != NULL)
            listener = listener->Construct(&frame, PlayerCar()->GetLinearVelocity(), kListenerView);
    }
    // As in the original, a listener operator new could not make is written through regardless
    const Coord3 *carPosition = PlayerCar()->GetPosition();
    Coord4 *hearing = reinterpret_cast<Coord4 *>(listener->unknown60);
    hearing->x = carPosition->x;
    hearing->y = carPosition->y;
    hearing->z = carPosition->z;
    hearing->w = 1.0f;

    if (fgWorld != NULL) {
        PhysicsObject *car = PlayerCar();
        WWorldPos *worldPos = &car->worldPos;
        if (worldPos->lastQueryStep != int32_t(fgSimulation.stepCount))
            worldPos->FindClosestFace(car->GetPosition(), true);
        if (worldPos->valid && worldPos->instance != NULL) {
            const EnviroDesc *enviro = fgWorld->GetEnviroDesc(worldPos);
            if (enviro->ambience[0] != 0) {
                AFX::SetMode(enviro->fxMode, 0, 0);
                AFader *fader = AFader::Get(kSpeechFader);
                if (gStreams)
                    fader->SetSecondary(enviro->ambience);
                if (enviro->bandWidth > 0.0f) {
                    listener->level = enviro->level;
                    listener->bandWidth = enviro->bandWidth;
                }
                // 0 below the band, 1 above the level, and between them (bandWidth + level + y) / 10, as the
                // original sums it
                float filter;
                if (double(enviro->level) - enviro->bandWidth > listener->position.y)
                    filter = 0.0f;
                else if (listener->position.y > enviro->level)
                    filter = 1.0f;
                else
                    filter = float((double(enviro->bandWidth) + enviro->level + listener->position.y) * 0.1f);
                fader->SetFilter(filter);
            }
        }
    }

    NullFunction();
    ASoundManager::BuildPaths(listener);
    NullFunction();
    OperatorDelete(listener);

    AFader::Get(kSpeechFader)->Update();
    AMix *fader2 = AMix::Get("0:Fader2");
    fader2->previous = fader2->volume;
    if (fader2->volume < 1.0f) {
        double raised = double(fader2->volume) + kPercent;
        fader2->volume = float(raised);
        if (raised > 1.0)
            fader2->volume = 1.0f;
    }
    AVoice::PlayVoices();
    FramesSinceAudioUpdate = 0;
}

// FUNC_AT(0x0004eaa0)
EAudioUpdate* EAudioUpdate::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x0003e6d0)
void EAudioUpdate_MakeEvent_Callback(const void *) {
    EAudioUpdate *event = new EAudioUpdate;
    if (event != NULL)
        event->vtable = kEAudioUpdateVtable;
}

// FUNC_AT(0x0003ed70)
void ECameraUpdate::Destruct() {
    vtable = kECameraUpdateVtable;
    Process();
    vtable = kEventVtable;
}

// Every view's camera, the last first
void ECameraUpdate::Process() {
    for (int i = int(fgRenderHigh->viewCount) - 1; i >= 0; i--) {
        RPlayerCamera *camera = fgRenderHigh->views[i].camera;
        (camera->*XbeVirtual<void (RPlayerCamera::*)()>(camera, 3))();     // UpdateCamera
    }
}

// FUNC_AT(0x00047650)
ECameraUpdate* ECameraUpdate::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x0003ede0)
void ECameraUpdate_MakeEvent_Callback(const void *) {
    ECameraUpdate *event = new ECameraUpdate;
    if (event != NULL)
        event->vtable = kECameraUpdateVtable;
}

// =============================================================================================================
// The AI
// =============================================================================================================

// FUNC_AT(0x0003dcd0)
EAICommand* EAICommand::Construct(AICommand *command, CARP::AIElement *element, CARP::AIElement *target,
                                  AIVehicle *vehicle, WTrigger *trigger, uint8_t bySimpleBody, int bodySlot) {
    this->command = command;
    this->element = element;
    this->target = target;
    this->vehicle = vehicle;
    this->trigger = trigger;
    vtable = kEAICommandVtable;
    this->bySimpleBody = bySimpleBody;
    this->bodySlot = bodySlot;
    return this;
}

// FUNC_AT(0x00048ec0)
void EAICommand::Destruct() {
    vtable = kEAICommandVtable;
    Process();
    vtable = kEventVtable;
}

void EAICommand::Process() {
    if (command == NULL)
        return;

    // With a trigger, the toucher must be the element's vehicle (by its kind of body), or the player's car
    if (trigger != NULL) {
        if (element != NULL) {
            CARP::AIElement *toucherElement = fgAIElementController->RedirectCarpElement(element);
            if (toucherElement->state != kElementAwake)
                return;
            AIVehicle *toucher = toucherElement->vehicle;
            if (bySimpleBody) {
                SimpleRigidBody *body = fgSimulation.GetSimpleRigidBody(bodySlot);
                if (toucher->vehicleType == kAIHelicopter && toucher->active) {
                    if (fgSimulation.GetSimpleRigidBody(toucher->GetPhysicsObject()->rigidBodySlot) != body)
                        return;
                    trigger->flags &= ~WTrigger::kEnabled;
                }
            } else {
                RigidBody *body = fgSimulation.GetRigidBody(bodySlot);
                if (toucher->vehicleType == kAIGroundVehicle && toucher->active) {
                    if (fgSimulation.GetRigidBody(toucher->GetPhysicsObject()->rigidBodySlot) != body)
                        return;
                    trigger->flags &= ~WTrigger::kEnabled;
                }
            }
        } else if (!(trigger->flags & WTrigger::kRigidType1)) {
            trigger->flags &= ~WTrigger::kEnabled;
        } else {
            if (bySimpleBody)
                return;
            RigidBody *body = fgSimulation.GetRigidBody(bodySlot);
            if (fgSimulation.GetRigidBody(PlayerCar()->rigidBodySlot) != body)
                return;
            trigger->flags &= ~WTrigger::kEnabled;
        }
    }

    // The vehicle ordered: the event's, the target element's (whose character takes the command's position
    // instead), or the player's AI vehicle
    AIVehicle *ordered = vehicle;
    if (ordered == NULL) {
        if (target != NULL) {
            CARP::AIElement *targetElement = fgAIElementController->RedirectCarpElement(target);
            AICharacter *character = NULL;
            if (targetElement->state == kElementAwake) {
                if (targetElement->type == CARP::kAIElementVehicle)
                    ordered = targetElement->vehicle;
                else if (targetElement->type == CARP::kAIElementCharacter)
                    character = targetElement->character;
            }
            if (command->attackMode != kGroundAttackGuns && command->attackMode != kGroundAttackRockets)
                targetElement->command = command;
            if (character != NULL) {
                if (command->targetMode == 0) {
                    character->hasCommandTarget = 0;
                } else {
                    character->hasCommandTarget = 1;
                    character->commandTarget = command->position;
                }
                return;
            }
        } else {
            ordered = AIVehicleController::Get()->GetPlayerGroundVehiclePtr(0);
        }
    }
    if (ordered == NULL)
        return;

    if (ordered->vehicleType == kAIGroundVehicle) {
        AIGroundVehicle *car = static_cast<AIGroundVehicle *>(ordered);
        if (!car->active || car->command == command)
            return;
        CommandGroundVehicle(car);
        car->commanded = 1;
    } else if (ordered->vehicleType == kAIHelicopter && ordered->active) {
        static_cast<AIHelicopter *>(ordered)->NewState(command);
        ordered->commanded = 1;
    }
}

void EAICommand::CommandGroundVehicle(AIGroundVehicle *car) {
    car->command = command;
    Coord3 position = command->position;

    // Put at the command's position, after unspawning the class-3 cars that would overlap it
    if (command->resetPosition) {
        for (PVehicle **other = fgSimulation.cars.first; other != fgSimulation.cars.last; other++) {
            PVehicle *otherCar = *other;
            if (otherCar->GetCarClass() != kCarClass3)
                continue;
            RigidBody *otherBody = fgSimulation.GetRigidBody((*other)->rigidBodySlot);
            Coord3 otherPosition = otherBody->position;
            RigidBodyInfo *otherInfo = otherBody->info;
            RigidBodyInfo *info = fgSimulation.GetRigidBody(car->GetPhysicsObject()->rigidBodySlot)->info;
            float distance = vec3distance(&otherPosition, &position);
            if (double(info->halfExtents.z) + otherInfo->halfExtents.z >= distance)
                otherCar->GetAIGroundVehiclePtr()->UnSpawn();
        }
        MATRIX4 orientation = command->orientation;
        static_cast<PVehicle *>(car->GetPhysicsObject())
            ->ResetCar(&position, reinterpret_cast<const Coord3 *>(orientation.mtx[2]));
    }

    car->useLanes = command->unknown7c == 0;
    if (car != AIVehicleController::Get()->GetPlayerGroundVehiclePtr(0))
        car->SetTargetMode(command->targetMode, &command->position);
    car->driveToNav->unknown04 = command->unknown7f;
    if (command->unknown7e >= 0)
        car->driveToNav->nextChoice = command->unknown7e;
    car->driveToNav->unknownB1 = command->unknown84;
    car->driveToNav->unknownB2 = command->unknown85;
    car->SetNavigateMode(command->navigateMode, command->navigateTarget);
    car->SetAttackMode(command->attackMode);
    car->attackRange = command->range;
    car->maxSpeed = command->maxSpeed;
    car->speedScale = command->speedScale;
    car->chargeChance = command->shotCount;
    car->unknown140 = command->unknown6c;
    car->fireDelay = command->fireDelay;

    // A push along the car's heading, scaled by its mass (none for 0 or 1.5)
    if (command->pushForward != 0.0f && command->pushForward != 1.5f) {
        Coord4 heading;
        const Coord4 *forward =
            MatrixRow(&fgSimulation.GetRigidBody(car->GetPhysicsObject()->rigidBodySlot)->info->orientation, 2);
        heading.x = forward->x;
        heading.y = forward->y;
        heading.z = forward->z;
        heading.w = 0.0f;
        float push = fgSimulation.GetRigidBody(car->GetPhysicsObject()->rigidBodySlot)->mass * command->pushForward;
        VU0_v4unitxyz(&heading, &heading);
        VU0_v4scale(&heading, push, &heading);
        RigidBody *body = fgSimulation.GetRigidBody(car->GetPhysicsObject()->rigidBodySlot);
        body->momentum.x = heading.x;
        body->momentum.y = heading.y;
        body->momentum.z = heading.z;
    }
    // A spin about y, scaled by its mass
    if (command->pushSpin != 0.0f) {
        Coord4 spin = { 0.0f, 1.0f, 0.0f, 0.0f };
        float push = fgSimulation.GetRigidBody(car->GetPhysicsObject()->rigidBodySlot)->mass * command->pushSpin;
        VU0_v4scale(&spin, push, &spin);
        fgSimulation.GetRigidBody(car->GetPhysicsObject()->rigidBodySlot)
            ->SetAngularMomentum(reinterpret_cast<const Coord3 *>(&spin));
    }

    car->SetSecondaryTarget(command->secondaryTarget);
    car->unknown145 = command->unknown7d != 0;
    car->followTargetHeight = command->followTargetHeight;
    car->maxHeight = command->maxHeight;
    car->minHeight = command->minHeight;
    if (car->GetPhysicsObject()->attributes.LookupBool("HAS_MACHINEGUNS", NULL) ||
        car->GetPhysicsObject()->attributes.LookupBool("HAS_CANNONS", NULL))
        car->useGuns = command->unknown87 || command->unknown92;
    if (car->GetPhysicsObject()->attributes.LookupBool("HAS_ROCKETS", NULL))
        car->useRockets = command->rockets;
    if (car->GetPhysicsObject()->attributes.LookupBool("HAS_MISSLES", NULL) ||
        car->GetPhysicsObject()->attributes.LookupBool("HAS_TORPEDOS", NULL))
        car->useMissiles = command->missiles;
    car->aimScale = command->aimScale;
    car->accuracy = command->accuracy;
    car->newCommand = 1;
}

// FUNC_AT(0x0004ea60)
EAICommand* EAICommand::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x0003dd10)
void EAICommand_MakeEvent_Callback(const EAICommand::Record *record) {
    EAICommand *event = new EAICommand;
    if (event != NULL)
        event->Construct(record->command, record->element, record->target, record->vehicle, gEventDynamicData.trigger,
                         gEventDynamicData.flag, gEventDynamicData.index);
}

// FUNC_AT(0x000472e0)
void EAICommand_ResolveEvent_Callback(EAICommand::Record *record, UGroup *shared, UGroup *parent) {
    reinterpret_cast<CARP::TagReference *>(&record->command)->Construct(shared, parent);
    reinterpret_cast<CARP::TagReference *>(&record->element)->Construct(shared, parent);
    reinterpret_cast<CARP::TagReference *>(&record->target)->Construct(shared, parent);
    reinterpret_cast<CARP::TagReference *>(&record->vehicle)->Construct(shared, parent);
}

// FUNC_AT(0x0003dd70)
void EAIElementFireOff::Destruct() {
    vtable = kEAIElementFireOffVtable;
    Process();
    vtable = kEventVtable;
}

void EAIElementFireOff::Process() {
    if (element == NULL)
        return;
    CARP::AIElement *redirected = fgAIElementController->RedirectCarpElement(element);
    if (redirected->type == CARP::kAIElementCharacter) {
        if (redirected->character != NULL)
            redirected->character->firing = 0;
    } else if (redirected->type == CARP::kAIElementVehicle) {
        for (int i = 0; i < redirected->memberCount; i++) {
            if (redirected->memberCharacters[i] != NULL)
                redirected->memberCharacters[i]->firing = 0;
        }
    }
}

// FUNC_AT(0x00047390)
EAIElementFireOff* EAIElementFireOff::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x0003de10)
void EAIElementFireOff_MakeEvent_Callback(const EAIElementFireOff::Record *record) {
    EAIElementFireOff *event = new EAIElementFireOff;
    if (event != NULL) {
        event->vtable = kEAIElementFireOffVtable;
        event->element = record->element;
    }
}

// FUNC_AT(0x0003de30)
void EAIElementFireOn::Destruct() {
    vtable = kEAIElementFireOnVtable;
    Process();
    vtable = kEventVtable;
}

void EAIElementFireOn::Process() {
    if (element == NULL)
        return;
    CARP::AIElement *redirected = fgAIElementController->RedirectCarpElement(element);
    if (redirected->type == CARP::kAIElementCharacter) {
        if (redirected->character != NULL)
            redirected->character->firing = 1;
    } else if (redirected->type == CARP::kAIElementVehicle) {
        for (int i = 0; i < redirected->memberCount; i++) {
            if (redirected->memberCharacters[i] != NULL)
                redirected->memberCharacters[i]->firing = 1;
        }
    }
}

// FUNC_AT(0x000473b0)
EAIElementFireOn* EAIElementFireOn::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x0003ded0)
void EAIElementFireOn_MakeEvent_Callback(const EAIElementFireOn::Record *record) {
    EAIElementFireOn *event = new EAIElementFireOn;
    if (event != NULL) {
        event->vtable = kEAIElementFireOnVtable;
        event->element = record->element;
    }
}

// FUNC_AT(0x0003def0)
void EAIElementSetAccuracy::Destruct() {
    vtable = kEAIElementSetAccuracyVtable;
    Process();
    vtable = kEventVtable;
}

void EAIElementSetAccuracy::Process() {
    if (element == NULL)
        return;
    CARP::AIElement *redirected = fgAIElementController->RedirectCarpElement(element);
    if (redirected->type == CARP::kAIElementCharacter) {
        redirected->person.info.accuracy = accuracy;
    } else if (redirected->type == CARP::kAIElementVehicle) {
        for (int i = 0; i < redirected->memberCount; i++)
            redirected->members[i].accuracy = accuracy;
    }
}

// FUNC_AT(0x000473d0)
EAIElementSetAccuracy* EAIElementSetAccuracy::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x0003df80)
void EAIElementSetAccuracy_MakeEvent_Callback(const EAIElementSetAccuracy::Record *record) {
    EAIElementSetAccuracy *event = new EAIElementSetAccuracy;
    if (event != NULL) {
        event->vtable = kEAIElementSetAccuracyVtable;
        event->element = record->element;
        event->accuracy = record->accuracy;
    }
}

// FUNC_AT(0x0003dfb0)
void EAIElementSetCharParams::Destruct() {
    vtable = kEAIElementSetCharParamsVtable;
    Process();
    vtable = kEventVtable;
}

void EAIElementSetCharParams::Process() {
    if (element == NULL)
        return;
    CARP::AIElement *redirected = fgAIElementController->RedirectCarpElement(element);
    if (redirected->type == CARP::kAIElementCharacter) {
        redirected->person.info.aimScale = unknown08;
        redirected->person.info.accuracy = accuracy;
        redirected->person.info.hitPoints = unknown10;
        redirected->hitPoints = unknown10;
    } else if (redirected->type == CARP::kAIElementVehicle) {
        for (int i = 0; i < redirected->memberCount; i++) {
            redirected->members[i].aimScale = unknown08;
            redirected->members[i].accuracy = accuracy;
            redirected->members[i].hitPoints = unknown10;
        }
    }
}

// FUNC_AT(0x000473f0)
EAIElementSetCharParams* EAIElementSetCharParams::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x0003e060)
void EAIElementSetCharParams_MakeEvent_Callback(const EAIElementSetCharParams::Record *record) {
    EAIElementSetCharParams *event = new EAIElementSetCharParams;
    if (event != NULL) {
        event->unknown10 = record->unknown10;
        event->unknown08 = record->unknown08;
        event->vtable = kEAIElementSetCharParamsVtable;
        event->element = record->element;
        event->accuracy = record->accuracy;
    }
}

// FUNC_AT(0x0003e0a0)
void EAIElementSetNonScoreable::Destruct() {
    vtable = kEAIElementSetNonScoreableVtable;
    Process();
    vtable = kEventVtable;
}

void EAIElementSetNonScoreable::Process() {
    if (element == NULL)
        return;
    CARP::AIElement *redirected = fgAIElementController->RedirectCarpElement(element);
    fgAIElementController->GetCarpElementTracking(element);     // the answer is not used
    switch (redirected->type) {
    case CARP::kAIElementCharacter:
        if (redirected->person.human != NULL)
            redirected->person.human->character->scoreable = 0;
        break;
    case CARP::kAIElementVehicle:
        // The test the wrong way round, as the original has it: a helicopter is handled as a car, a car as a
        // helicopter
        if (redirected->car.helicopter) {
            if (redirected->object != NULL)
                (static_cast<PVehicle *>(redirected->object))->SetScoreable(false);
        } else {
            PHelicopter *helicopter = static_cast<PHelicopter *>(redirected->object);
            if (helicopter != NULL)
                helicopter->scoreable = false;
        }
        break;
    }
}

// FUNC_AT(0x00047410)
EAIElementSetNonScoreable* EAIElementSetNonScoreable::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x0003e160)
void EAIElementSetNonScoreable_MakeEvent_Callback(const EAIElementSetNonScoreable::Record *record) {
    EAIElementSetNonScoreable *event = new EAIElementSetNonScoreable;
    if (event != NULL) {
        event->vtable = kEAIElementSetNonScoreableVtable;
        event->element = record->element;
    }
}

// FUNC_AT(0x0003e180)
void EAIElementSetVehicleParams::Destruct() {
    vtable = kEAIElementSetVehicleParamsVtable;
    Process();
    vtable = kEventVtable;
}

void EAIElementSetVehicleParams::Process() {
    if (element != NULL)
        fgAIElementController->RedirectCarpElement(element)->hitPoints = unknown08;
}

// FUNC_AT(0x00047430)
EAIElementSetVehicleParams* EAIElementSetVehicleParams::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x0003e1e0)
void EAIElementSetVehicleParams_MakeEvent_Callback(const EAIElementSetVehicleParams::Record *record) {
    EAIElementSetVehicleParams *event = new EAIElementSetVehicleParams;
    if (event != NULL) {
        event->unknown08 = record->unknown08;
        event->vtable = kEAIElementSetVehicleParamsVtable;
        event->element = record->element;
    }
}

// FUNC_AT(0x0003e210)
void EAIElementSetWakeRange::Destruct() {
    vtable = kEAIElementSetWakeRangeVtable;
    Process();
    vtable = kEventVtable;
}

void EAIElementSetWakeRange::Process() {
    fgAIElementController->RedirectCarpElement(element);    // the answer is not used
    fgAIElementController->GetCarpElementTracking(element)->SetWakeRange(wakeRange);
}

// FUNC_AT(0x00047450)
EAIElementSetWakeRange* EAIElementSetWakeRange::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x0003e280)
void EAIElementSetWakeRange_MakeEvent_Callback(const EAIElementSetWakeRange::Record *record) {
    EAIElementSetWakeRange *event = new EAIElementSetWakeRange;
    if (event != NULL) {
        event->wakeRange = record->wakeRange;
        event->vtable = kEAIElementSetWakeRangeVtable;
        event->element = record->element;
    }
}

// FUNC_AT(0x000495d0)
void EActivateAIElement::Destruct() {
    vtable = kEActivateAIElementVtable;
    Process();
    vtable = kEventVtable;
}

void EActivateAIElement::Process() {
    // With a trigger, the toucher must be the toucher element's vehicle (by its kind of body), or the player's car
    // when the trigger is one rigid bodies touch
    if (trigger != NULL) {
        if (toucherElement != NULL) {
            CARP::AIElement *redirected = fgAIElementController->RedirectCarpElement(toucherElement);
            if (redirected->state == kElementAwake) {
                AIVehicle *toucher = redirected->vehicle;
                if (bySimpleBody) {
                    SimpleRigidBody *body = fgSimulation.GetSimpleRigidBody(bodySlot);
                    if (toucher->vehicleType == kAIHelicopter && toucher->active) {
                        if (fgSimulation.GetSimpleRigidBody(toucher->GetPhysicsObject()->rigidBodySlot) != body)
                            return;
                        trigger->flags &= ~WTrigger::kEnabled;
                        CRT_printf("The Helicopter hit the Trigger.\n");
                    }
                } else {
                    RigidBody *body = fgSimulation.GetRigidBody(bodySlot);
                    if (toucher->vehicleType == kAIGroundVehicle && toucher->active) {
                        if (fgSimulation.GetRigidBody(toucher->GetPhysicsObject()->rigidBodySlot) != body)
                            return;
                        trigger->flags &= ~WTrigger::kEnabled;
                    }
                }
            }
        } else if (trigger->flags & WTrigger::kRigidType1) {
            if (bySimpleBody)
                return;
            RigidBody *body = fgSimulation.GetRigidBody(bodySlot);
            if (fgSimulation.GetRigidBody(PlayerCar()->rigidBodySlot) != body)
                return;
            trigger->flags &= ~WTrigger::kEnabled;
        }
    }

    if (element != NULL) {
        CARP::AIElement *redirected = fgAIElementController->RedirectCarpElement(element);
        if (!gLimitHenchSpawns || redirected->type != CARP::kAIElementVehicle ||
            (redirected->flags & kElementForceAwake) || fgAIElementController->OkToSpawnHench())
            redirected->flags &= ~kElementDeactivated;
    }
}

// FUNC_AT(0x0004ea80)
EActivateAIElement* EActivateAIElement::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x0003e3b0)
void EActivateAIElement_MakeEvent_Callback(const EActivateAIElement::Record *record) {
    EActivateAIElement *event = new EActivateAIElement;
    if (event != NULL) {
        event->element = record->element;
        event->trigger = gEventDynamicData.trigger;
        event->toucherElement = record->toucherElement;
        event->vtable = kEActivateAIElementVtable;
        event->bySimpleBody = gEventDynamicData.flag;
        event->bodySlot = gEventDynamicData.index;
    }
}

// FUNC_AT(0x0003f470)
void EDeactivateAIElement::Destruct() {
    vtable = kEDeactivateAIElementVtable;
    Process();
    vtable = kEventVtable;
}

void EDeactivateAIElement::Process() {
    if (element == NULL)
        return;
    CARP::AIElement *redirected = fgAIElementController->RedirectCarpElement(element);
    redirected->flags |= kElementDeactivated;
    if (redirected->type == CARP::kAIElementVehicle)
        fgAIElementController->ForceVehicleToSleep(redirected->vehicle);
}

// FUNC_AT(0x00047930)
EDeactivateAIElement* EDeactivateAIElement::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x0003f4e0)
void EDeactivateAIElement_MakeEvent_Callback(const EDeactivateAIElement::Record *record) {
    EDeactivateAIElement *event = new EDeactivateAIElement;
    if (event != NULL) {
        event->vtable = kEDeactivateAIElementVtable;
        event->element = record->element;
    }
}

// FUNC_AT(0x0003f500)
void EDeactivateAIElement_ResolveEvent_Callback(EDeactivateAIElement::Record *record, UGroup *shared,
                                                UGroup *parent) {
    reinterpret_cast<CARP::TagReference *>(&record->element)->Construct(shared, parent);
}

// FUNC_AT(0x0003f720)
void EDropMine::Destruct() {
    vtable = kEDropMineVtable;
    Process();
    vtable = kEventVtable;
}

void EDropMine::Process() {
    CARP::AIElement *redirected = fgAIElementController->RedirectCarpElement(element);
    if (redirected->type != CARP::kAIElementVehicle)
        return;
    PhysicsObject *object = redirected->object;
    if (object != NULL) {
        object->renderObject->animHandle->ProcessStimuli(kMineSystem, kMineStimulus1, fgSimulation.stepCount,
                                                         kStimulusMode2);
        object->renderObject->animHandle->ProcessStimuli(kMineSystem, kMineStimulus2, fgSimulation.stepCount,
                                                         kStimulusMode2);
    }
}

// FUNC_AT(0x000479d0)
EDropMine* EDropMine::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x0003f7c0)
void EDropMine_MakeEvent_Callback(const EDropMine::Record *record) {
    EDropMine *event = new EDropMine;
    if (event != NULL) {
        event->vtable = kEDropMineVtable;
        event->element = record->element;
        event->unknown08 = record->unknown08;
    }
}

// FUNC_AT(0x0003e410)
void EActorMuzzleFlash::Destruct() {
    vtable = kEActorMuzzleFlashVtable;
    Process();
    vtable = kEventVtable;
}

void EActorMuzzleFlash::Process() {
    if (point == NULL || sceneObj == NULL)
        return;
    alignas(16) MATRIX4 transform;
    alignas(16) Coord4 position;
    sceneObj->GetInstancePosition(point->instance, &transform, false);
    MATRIX4_TransformPoint(&transform, &point->position, &position);
    weapon->StartMuzzleFlash(reinterpret_cast<const Coord3 *>(&position));
}

// FUNC_AT(0x000474b0)
EActorMuzzleFlash* EActorMuzzleFlash::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x0003e4a0)
void EActorMuzzleFlash_MakeEvent_Callback(const EActorMuzzleFlash::Record *record) {
    EActorMuzzleFlash *event = new EActorMuzzleFlash;
    if (event != NULL) {
        event->point = record->point;
        event->vtable = kEActorMuzzleFlashVtable;
        event->weapon = gEventDynamicData.weapon;
        event->sceneObj = gEventDynamicData.sceneObj;
    }
}

// =============================================================================================================
// The player's car and its controls
// =============================================================================================================

// FUNC_AT(0x0003e730)
void EAutoDrive::Destruct() {
    vtable = kEAutoDriveVtable;
    Process();
    vtable = kEventVtable;
}

void EAutoDrive::Process() {
    glbMissionManager->SetAutoDrive(on != 0, true);
    GHud::TheApp()->SetAutoDrive(on != 0);
}

// FUNC_AT(0x00047530)
EAutoDrive* EAutoDrive::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x0003e7a0)
void EAutoDrive_MakeEvent_Callback(const EAutoDrive::Record *record) {
    EAutoDrive *event = new EAutoDrive;
    if (event != NULL) {
        event->vtable = kEAutoDriveVtable;
        event->on = record->on;
    }
}

// FUNC_AT(0x00049a80)
void EAutoDriveSpeedChange::Destruct() {
    vtable = kEAutoDriveSpeedChangeVtable;
    Process();
    vtable = kEventVtable;
}

void EAutoDriveSpeedChange::Process() {
    PlayerCar()->GetAIGroundVehiclePtr()->autoDriveSpeed = speed;
}

// FUNC_AT(0x0004eac0)
EAutoDriveSpeedChange* EAutoDriveSpeedChange::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x0003e7c0)
void EAutoDriveSpeedChange_MakeEvent_Callback(const EAutoDriveSpeedChange::Record *record) {
    EAutoDriveSpeedChange *event = new EAutoDriveSpeedChange;
    if (event != NULL) {
        event->vtable = kEAutoDriveSpeedChangeVtable;
        event->speed = record->speed;
    }
}

// FUNC_AT(0x00049ae0)
void EAutoDriveSteering::Destruct() {
    vtable = kEAutoDriveSteeringVtable;
    Process();
    vtable = kEventVtable;
}

void EAutoDriveSteering::Process() {
    AICharacterHands *hands = fgHandObj;
    if (on) {
        if (hands != NULL)
            hands->ShowHands();
        PlayerCamera()->SetAutoDriveForwardLock(false);
        PlayerCar()->GetAIGroundVehiclePtr()->autoDriveSteering = 0;
    } else {
        if (hands != NULL)
            hands->HideHands();
        PlayerCamera()->SetAutoDriveForwardLock(true);
        PlayerCar()->GetAIGroundVehiclePtr()->autoDriveSteering = 1;
    }
}

// FUNC_AT(0x0004eae0)
EAutoDriveSteering* EAutoDriveSteering::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x0003e7e0)
void EAutoDriveSteering_MakeEvent_Callback(const EAutoDriveSteering::Record *record) {
    EAutoDriveSteering *event = new EAutoDriveSteering;
    if (event != NULL) {
        event->vtable = kEAutoDriveSteeringVtable;
        event->on = record->on;
    }
}

// FUNC_AT(0x00049b90)
void EBashCarAlongObjectAxis::Destruct() {
    vtable = kEBashCarAlongObjectAxisVtable;
    Process();
    vtable = kEventVtable;
}

// An impulse on the player's car at its position plus twice the scene object's instance's y axis, along twice that
// axis; then the damage.
void EBashCarAlongObjectAxis::Process() {
    alignas(16) MATRIX4 axes = sceneObj->Instance()->Matrix();
    axes.mtx[1][3] = 0.0f;
    RigidBody *body = fgSimulation.GetRigidBody(PlayerCar()->rigidBodySlot);
    const Coord3 *carPosition = PlayerCar()->GetPosition();
    alignas(16) Coord4 position = { carPosition->x, carPosition->y, carPosition->z, 1.0f };
    alignas(16) CollisionImpact impact;
    impact.Construct();
    alignas(16) Coord4 offset = *MatrixRow(&axes, 1);
    VU0_v4scale(&offset, 2.0f, &offset);
    alignas(16) Coord4 point = { 0.0f, 0.0f, 0.0f, 0.0f };
    VU0_v3add(&position, &offset, &point);
    alignas(16) Coord4 normal = *MatrixRow(&axes, 1);
    VU0_v4scale(&normal, 2.0f, &normal);
    body->GenerateImpulse(&impact, &normal, &point, 0.025f, false, 0, 1.0f);
    fgSimulation.rigidBodyOwners[PlayerCar()->rigidBodySlot]->ApplyDamageVirtual(&body->position, &body->position,
                                                                                 damage, 0.05f, kDamageKind1,
                                                                                 DamageSourceSig);
}

// FUNC_AT(0x0004eb00)
EBashCarAlongObjectAxis* EBashCarAlongObjectAxis::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x0003e8f0)
void EBashCarAlongObjectAxis_MakeEvent_Callback(const EBashCarAlongObjectAxis::Record *record) {
    EBashCarAlongObjectAxis *event = new EBashCarAlongObjectAxis;
    if (event != NULL) {
        event->damage = record->damage;
        event->vtable = kEBashCarAlongObjectAxisVtable;
        event->sceneObj = gEventDynamicData.sceneObj;
    }
}

// FUNC_AT(0x0004a820)
void EControlToCPU::Destruct() {
    vtable = kEControlToCPUVtable;
    Process();
    vtable = kEventVtable;
}

void EControlToCPU::Process() {
    PlayerCar()->GetAIGroundVehiclePtr()->EngageAutoDrive();
    PlayerCamera()->SetControlToCPU(true);
}

// FUNC_AT(0x0004eb60)
EControlToCPU* EControlToCPU::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x0003f390)
void EControlToCPU_MakeEvent_Callback(const void *) {
    EControlToCPU *event = new EControlToCPU;
    if (event != NULL)
        event->vtable = kEControlToCPUVtable;
}

// FUNC_AT(0x0004a890)
void EControlToPlayer::Destruct() {
    vtable = kEControlToPlayerVtable;
    Process();
    vtable = kEventVtable;
}

void EControlToPlayer::Process() {
    PlayerCar()->GetAIGroundVehiclePtr()->DisengageAutoDrive();
    PlayerCar()->SetTargetGas(0.0f);
    PlayerCar()->SetTargetBrake(0.0f);
    IOModule::GetIOModule()->resendAll = true;
    PlayerCamera()->SetControlToCPU(false);
}

// FUNC_AT(0x0004eb80)
EControlToPlayer* EControlToPlayer::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x0003f3b0)
void EControlToPlayer_MakeEvent_Callback(const void *) {
    EControlToPlayer *event = new EControlToPlayer;
    if (event != NULL)
        event->vtable = kEControlToPlayerVtable;
}

// FUNC_AT(0x0004a920)
void EControlToSpline::Destruct() {
    vtable = kEControlToSplineVtable;
    Process();
    vtable = kEventVtable;
}

void EControlToSpline::Process() {
    if (spline == NULL || spline->path.value == 0)
        return;
    if (element == NULL) {
        PlayerCar()->GetAIGroundVehiclePtr()->CallEngageSplinePath(spline);
        PlayerCamera()->SetControlToCPU(true);
    } else {
        CARP::AIElement *redirected = fgAIElementController->RedirectCarpElement(element);
        if (redirected->state == kElementAwake && redirected->type == CARP::kAIElementVehicle)
            redirected->vehicle->CallEngageSplinePath(spline);
    }
}

// FUNC_AT(0x0004eba0)
EControlToSpline* EControlToSpline::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x0003f3d0)
void EControlToSpline_MakeEvent_Callback(const EControlToSpline::Record *record) {
    EControlToSpline *event = new EControlToSpline;
    if (event != NULL) {
        event->vtable = kEControlToSplineVtable;
        event->element = record->element;
        event->spline = record->spline;
    }
}

// FUNC_AT(0x0004abc0)
void EEndControlToSpline::Destruct() {
    vtable = kEEndControlToSplineVtable;
    Process();
    vtable = kEventVtable;
}

void EEndControlToSpline::Process() {
    if (element == NULL) {
        PlayerCar()->GetAIGroundVehiclePtr()->CallDisengageSplinePath();
        PlayerCamera()->SetControlToCPU(false);
    } else {
        fgAIElementController->RedirectCarpElement(element)->vehicle->CallDisengageSplinePath();
    }
}

// FUNC_AT(0x0004ec20)
EEndControlToSpline* EEndControlToSpline::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x0003fa90)
void EEndControlToSpline_MakeEvent_Callback(const EEndControlToSpline::Record *record) {
    EEndControlToSpline *event = new EEndControlToSpline;
    if (event != NULL) {
        event->vtable = kEEndControlToSplineVtable;
        event->element = record->element;
    }
}

// FUNC_AT(0x0004ab60)
void EEndCarStop::Destruct() {
    vtable = kEEndCarStopVtable;
    Process();
    vtable = kEventVtable;
}

void EEndCarStop::Process() {
    PlayerCar()->ForceStopOff(3);
}

// FUNC_AT(0x0004ec00)
EEndCarStop* EEndCarStop::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x0003fa70)
void EEndCarStop_MakeEvent_Callback(const void *) {
    EEndCarStop *event = new EEndCarStop;
    if (event != NULL)
        event->vtable = kEEndCarStopVtable;
}

// FUNC_AT(0x0004aad0)
void EDamagePlayer::Destruct() {
    vtable = kEDamagePlayerVtable;
    Process();
    vtable = kEventVtable;
}

void EDamagePlayer::Process() {
    Coord3 direction = { 0.0f, 0.0f, 1.0f };
    PVehicle *car = PlayerCar();
    car->ApplyDamageVirtual(car->GetPosition(), &direction, damage, unknown08, kDamageKind3, DamageSourceSig);
}

// FUNC_AT(0x0004ebe0)
EDamagePlayer* EDamagePlayer::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x0003f440)
void EDamagePlayer_MakeEvent_Callback(const EDamagePlayer::Record *record) {
    EDamagePlayer *event = new EDamagePlayer;
    if (event != NULL) {
        event->vtable = kEDamagePlayerVtable;
        event->damage = record->damage;
        event->unknown08 = record->unknown08;
    }
}

// FUNC_AT(0x0004a9d0)
void ECustomTransition::Destruct() {
    vtable = kECustomTransitionVtable;
    Process();
    vtable = kEventVtable;
}

// The car changes to its secondary type, and the weapons are reloaded from the mission set with weapons 23 to 31
// taken away and the first given 500 rounds (super easy: the third emptied), the set put back after.
void ECustomTransition::Process() {
    if (_stricmp(transition, "paris") != 0)
        return;
    MEM_totalunused(0);
    const char *secondary = PlayerCar()->GetSecondaryType();
    PlayerCar()->ChangeCarType(secondary, false);
    MEM_totalunused(0);

    MissionSet *set = glbMissionManager->missionSet;
    MissionSet saved = *set;
    for (int weapon = 23; weapon < 32; weapon++)
        set->weaponEnabled[weapon] = 0;
    if (gSuperEasy) {
        set->weaponAmmo[2] = 0;
    } else {
        set->weaponAmmo[0] = 500;
        set->weaponEnabled[0] = 1;
    }
    glbMissionManager->LoadWeaponSet();
    AICharacterPassenger::Init();
    AICharacterBond::Init();
    *set = saved;
}

// FUNC_AT(0x0004ebc0)
ECustomTransition* ECustomTransition::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x0003f420)
void ECustomTransition_MakeEvent_Callback(const ECustomTransition::Record *record) {
    ECustomTransition *event = new ECustomTransition;
    if (event != NULL) {
        event->vtable = kECustomTransitionVtable;
        event->transition = record->transition;
    }
}

// =============================================================================================================
// The collision event
// =============================================================================================================

namespace {

// The rotation a box's corner gives a collision there: x, y and z scale the lever, w the angle's sign
const Coord4 kCornerTwist[8] = {
    { 0.42f, 1.0f, 1.2f, 1.0f }, { 0.42f, 1.0f, 1.2f, -1.0f }, { 0.4f, 1.0f, 1.2f, 1.0f }, { 0.4f, 1.0f, 1.2f, -1.0f },
    { 0.4f, 1.0f, 1.2f, -1.0f }, { 0.4f, 1.0f, 1.2f, 1.0f }, { 0.42f, 1.0f, 1.2f, -1.0f }, { 0.42f, 1.0f, 1.2f, 1.0f },
};

constexpr float kQuarterPi = 0.7853982f;
static_assert(std::bit_cast<uint32_t>(kQuarterPi) == 0x3f490fdb, "pi/4");
constexpr float kRadiansToDegrees = 57.2957763671875f;
static_assert(std::bit_cast<uint32_t>(kRadiansToDegrees) == 0x42652ee0, "180/pi, as the game has it");

// The impact kinds (CollisionImpact::kindA and kindB) and surface tags (tag, unknown4a) the event tests
constexpr uint8_t kImpactKindPlayer = 1;
constexpr uint8_t kImpactKind4 = 4;
constexpr uint8_t kImpactKind7 = 7;
constexpr uint16_t kTagLanding = 0x0e;      // a landing: PlayLanding, no effects

// The sound's surface (CarSurface) for a tag, as the first side's collision sound picks it
int CollisionSurface(uint16_t tag) {
    switch (int16_t(tag)) {
    case 1: return kPAVED;
    case 2: return kGRAVEL;
    case 3: return kGRASS;
    case 4: return kCOBBLE;
    case 5: return kDIRT;
    case 10: return kPAVED_ROUGH;
    case 12: return kRAILROAD;
    case 13: return kMETAL;
    case 16: return kMETAL;
    case 48: return kSNOW;
    case 64: return kICE;
    case 80: return kDIRT;
    case 96: return kSurface15;
    default: return kNODRIVE;
    }
}

// Whether a side's render object gets the collision effect: hard enough, and not of the tags that have none
bool HardEnough(float strength, const Coord4 *velocity) {
    return strength > 0.1f || VU0_v3lengthsquare(velocity) > 15.0f;
}

} // namespace

// FUNC_AT(0x0003f370)
ECollision* ECollision::Construct(CollisionImpact impact) {
    this->impact = impact;
    vtable = kECollisionVtable;
    return this;
}

// FUNC_AT(0x00049eb0)
void ECollision::Destruct() {
    vtable = kECollisionVtable;
    Process();
    vtable = kEventVtable;
}

void ECollision::Process() {
    float strength = fabsf(impact.strength);
    Zone zone;
    zone.position.x = impact.point.x;
    zone.position.y = impact.point.y;
    zone.position.z = impact.point.z;
    zone.position.w = 0.0f;
    zone.velocity.x = impact.relativeVelocity.x;
    zone.velocity.y = impact.relativeVelocity.y;
    zone.velocity.z = impact.relativeVelocity.z;
    zone.velocity.w = 0.0f;
    zone.unknown20 = Coord4{ 0.0f, 0.0f, 0.0f, 0.0f };
    zone.normal.x = impact.normal.x;
    zone.normal.y = impact.normal.y;
    zone.normal.z = impact.normal.z;
    zone.normal.w = 0.0f;
    AIZoneController::Get()->RegisterZone(1, Xyz(&zone.position));

    ProcessSideA(&zone, strength);
    ProcessSideB(&zone, strength);
    if (impact.unknown4a != kTagLanding && impact.tag != kTagLanding)
        TriggerIncidental(&zone);
}

void ECollision::ProcessSideA(Zone *zone, float strength) {
    if (impact.kindA > 5 || fgSimulation.rigidBodyOwners[impact.ownerA] == NULL)
        return;
    RigidBody *body = fgSimulation.GetRigidBody(impact.ownerA);
    PhysicsObject *owner = body->GetOwner();
    const Coord3 *position = reinterpret_cast<const Coord3 *>(&zone->position);
    const Coord3 *velocity = reinterpret_cast<const Coord3 *>(&zone->velocity);

    ABaseSound *sound = owner->audioObject;
    if (sound != NULL) {
        if (impact.unknown4a == kTagLanding) {
            static_cast<AVehicle *>(sound)->CallPlayLanding(strength, position, velocity);
        } else if (impact.kindA == kImpactKind4) {
            if (impact.kindB == kImpactKind7)
                static_cast<AVehicle *>(sound)->CallPlayLanding(strength, position, velocity);
        } else if (impact.kindA < kImpactKind4) {
            if (impact.kindB != kImpactKind4)
                static_cast<AVehicle *>(sound)->CallPlayCollision(strength, position, velocity,
                                                                  CollisionSurface(impact.tag));
        }
    }
    if (owner->feedbackObject != NULL)
        owner->feedbackObject->AddCollisionInfo(&impact);

    if (owner->type == 1 && owner->renderObject != NULL) {
        if (HardEnough(strength, &zone->velocity) && impact.unknown4a != kTagLanding && impact.kindB <= 6 &&
            impact.kindB != kImpactKind4 && impact.tag != 5 && impact.tag != 0x50 && impact.tag != 0x30 &&
            impact.tag != 0x40) {
            // A box hit at a corner: the effect moved round from the corner, by the angle between the collision's
            // normal and the box's z axis
            int corner = impact.unknown50;
            if (corner >= 0) {
                RigidBodyInfo *info = body->info;
                float alignment = fabsf(v3dotprod(&zone->normal, MatrixRow(&info->orientation, 2)));
                const float one = 1.0f;
                const float minusOne = -1.0f;
                const float *cosine = MinFloat(alignment < -1.0f ? &minusOne : &alignment, &one);
                float angle = float((CrtAcos(*cosine) - kQuarterPi) * kCornerTwist[corner].w);
                alignas(16) Coord4 lever;
                VU0_v4sub(&zone->position, &body->position, &lever);
                const Coord4 *axis = MatrixRow(&info->orientation, 1);
                alignas(16) MATRIX4 rotation;
                BuildRotate(&rotation, angle * kRadiansToDegrees, axis->x, axis->y, axis->z);
                MATRIX4_TransformPoint(&rotation, &lever, &lever);
                alignas(16) MATRIX4 inverse = info->orientation;
                OrthoInverse(&inverse);
                VU0_MATRIX4_vect3rotate(&lever, &inverse, &lever);
                lever.x = lever.x * kCornerTwist[corner].x;
                lever.y = lever.y * kCornerTwist[corner].y;
                lever.z = lever.z * kCornerTwist[corner].z;
                VU0_MATRIX4_vect3rotate(&lever, &info->orientation, &lever);
                VU0_v3add(&lever, &body->position, &zone->position);
            }
            owner->renderObject->TriggerFXVirtual(6, 8, 0, &zone->position, &zone->velocity, -1.0f);
            if (impact.kindA == kImpactKindPlayer || impact.kindB == kImpactKindPlayer) {
                IFeedback *feedback = PlayerCar()->feedbackObject;
                if (feedback != NULL)
                    feedback->SetVibrate(0.2f, 0.2f);
            }
        }
        owner->renderObject->AddCollisionInfo(reinterpret_cast<const Coord3 *>(&impact.point),
                                              reinterpret_cast<const Coord3 *>(&impact.relativeVelocity));
    }

    if (body->kind < 4 && impact.kindA <= 2) {
        PVehicle *vehicle = static_cast<PVehicle *>(owner);
        if (vehicle->GetAIGroundVehiclePtr() != NULL && vehicle->GetAIGroundVehiclePtr()->active) {
            uint8_t otherKind = impact.kindB;
            AIGroundVehicle *ai = vehicle->GetAIGroundVehiclePtr();
            ai->hitByCollision = 1;
            ai->collisionKind = otherKind;
        }
    }
}

// As the first side, but the sound's surface from the other side's kind and tag, no corner, no rumble, and the
// tags of the effect test mixed (as in the original)
void ECollision::ProcessSideB(Zone *zone, float strength) {
    if (impact.kindB > 5 || fgSimulation.rigidBodyOwners[impact.ownerB] == NULL)
        return;
    RigidBody *body = fgSimulation.GetRigidBody(impact.ownerB);
    PhysicsObject *owner = body->GetOwner();
    const Coord3 *position = reinterpret_cast<const Coord3 *>(&zone->position);
    const Coord3 *velocity = reinterpret_cast<const Coord3 *>(&zone->velocity);

    ABaseSound *sound = owner->audioObject;
    if (sound != NULL) {
        if (impact.tag == kTagLanding) {
            static_cast<AVehicle *>(sound)->CallPlayLanding(strength, position, velocity);
        } else if (impact.kindB == kImpactKind4) {
            if (impact.kindA == kImpactKind7)
                static_cast<AVehicle *>(sound)->CallPlayLanding(strength, position, velocity);
        } else if (impact.kindB < kImpactKind4 && impact.kindA != kImpactKind4) {
            int surface;
            if (impact.kindA == kImpactKind7)
                surface = kPAVED;
            else if (impact.unknown4a == 0x10 || impact.unknown4a == 0xd)
                surface = kMETAL;
            else
                surface = kSurface15;
            static_cast<AVehicle *>(sound)->CallPlayCollision(strength, position, velocity, surface);
        }
    }
    if (owner->feedbackObject != NULL)
        owner->feedbackObject->AddCollisionInfo(&impact);

    if (owner->type == 1 && owner->renderObject != NULL) {
        if (HardEnough(strength, &zone->velocity) && impact.tag != kTagLanding && impact.kindA <= 6 &&
            impact.kindA != kImpactKind4 && impact.unknown4a != 5 && impact.unknown4a != 0x50 &&
            impact.unknown4a != 0x30 && impact.tag != 0x40)
            owner->renderObject->TriggerFXVirtual(6, 8, 0, &zone->position, &zone->velocity, -1.0f);
        owner->renderObject->AddCollisionInfo(reinterpret_cast<const Coord3 *>(&impact.point),
                                              reinterpret_cast<const Coord3 *>(&impact.relativeVelocity));
    }

    if (body->kind < 4 && impact.kindB <= 2) {
        PVehicle *vehicle = static_cast<PVehicle *>(owner);
        if (vehicle->GetAIGroundVehiclePtr() != NULL && vehicle->GetAIGroundVehiclePtr()->active) {
            uint8_t otherKind = impact.kindA;
            AIGroundVehicle *ai = vehicle->GetAIGroundVehiclePtr();
            ai->hitByCollision = 1;
            ai->collisionKind = otherKind;
        }
    }
}

// A crash effect near the camera, at most one every third of a second (the player's car keeping a time of its
// own); a submarine's only underwater
void ECollision::TriggerIncidental(Zone *zone) {
    Coord4 eye = *MatrixRow(&PlayerCamera()->matrix, 3);
    if (!(VU0_v3distancesquare(&eye, &zone->position) < 625.0f))
        return;

    bool byPlayer = impact.kindA == kImpactKindPlayer || impact.kindB == kImpactKindPlayer;
    // The non-player time is the later of the two (as the original picks it)
    uint32_t *lastStep;
    if (byPlayer)
        lastStep = &LastPlayerIncidentalStep;
    else
        lastStep = LastPlayerIncidentalStep > LastIncidentalStep ? &LastPlayerIncidentalStep : &LastIncidentalStep;
    uint32_t step = fgSimulation.stepCount;
    if (step < *lastStep)
        *lastStep = 0;
    if (!(double(step) > double(fgSimulation.stepsPerSecond) * 0.33f + double(*lastStep)))
        return;

    float volume = impact.strength;
    if (!byPlayer)
        volume = volume * 0.25f;
    float threshold = 0.15f;
    if (byPlayer && PlayerCar()->GetPhysics()->isSub != 0)
        threshold = 0.015f;
    if (!(volume > threshold))
        return;

    // Neither side a body of kind 4 that is not flagged
    bool sideA = true;
    bool sideB = true;
    if (impact.kindA == kImpactKind4) {
        if (impact.kindB == kImpactKind4)
            return;
        sideA = (fgSimulation.GetRigidBody(impact.ownerA)->flags & RigidBody::kFlag1) != 0;
    }
    if (impact.kindB == kImpactKind4)
        sideB = (fgSimulation.GetRigidBody(impact.ownerB)->flags & RigidBody::kFlag1) != 0;
    if (!sideA || !sideB)
        return;

    const char *effect;
    if (byPlayer && PlayerCar()->GetPhysics()->isSub != 0) {
        if (impact.tag == 0xd || impact.tag == 0x10) {
            *lastStep = step;
            return;
        }
        effect = impact.tag == 5 ? "Un_SandCar" : "Un_DebrisW";
    } else if (impact.tag == 6) {
        effect = "bigsplash";
    } else if (impact.tag == 5 || impact.tag == 0x50) {
        effect = "Cr_Dirt";
    } else {
        effect = impact.strength < 0.75f ? "CR_Wasm" : "CR_WasmS";
    }
    UGroup *reference = GFX::LookupReference(effect);
    if (reference != NULL) {
        Coord4 up = { 0.0f, 1.0f, 0.0f, 0.0f };
        GFX::TriggerIncidental(reference, Xyz(&impact.point), &up, GameZeroVector, GameDefaultVector, NULL, 0, NULL);
    }
    *lastStep = step;
}

// FUNC_AT(0x0004eb40)
ECollision* ECollision::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// =============================================================================================================
// The camera
// =============================================================================================================

// FUNC_AT(0x0003e340)
void EAbortCinematic::Destruct() {
    vtable = kEAbortCinematicVtable;
    Process();
    vtable = kEventVtable;
}

void EAbortCinematic::Process() {
    PlayerCamera()->AbortCinematic();
}

// FUNC_AT(0x00047490)
EAbortCinematic* EAbortCinematic::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x0003e390)
void EAbortCinematic_MakeEvent_Callback(const void *) {
    EAbortCinematic *event = new EAbortCinematic;
    if (event != NULL)
        event->vtable = kEAbortCinematicVtable;
}

// FUNC_AT(0x0003eb50)
void ECameraLockOn::Destruct() {
    vtable = kECameraLockOnVtable;
    Process();
    vtable = kEventVtable;
}

void ECameraLockOn::Process() {
    Coord4 target = { targetX, targetY, targetZ, 0.0f };
    const Coord4 *lockPoint = NULL;
    if (point != NULL) {
        lockPoint = point;
    } else if (element != NULL) {
        CARP::AIElement *redirected = fgAIElementController->RedirectCarpElement(element);
        if (redirected->type == CARP::kAIElementVehicle) {
            if (wheel > 0 && wheel - 1 < 4) {
                if (redirected->object != NULL)
                    lockPoint = (static_cast<PVehicle *>(redirected->object))->GetWheelPos(wheel - 1);
            } else if (member > 0 && member - 1 < redirected->memberCount) {
                AICharacter *character = redirected->memberCharacters[member - 1];
                if (character != NULL)
                    lockPoint = MatrixRow(&character->transform, 3);
            } else if (redirected->vehicle != NULL) {
                lockPoint = redirected->vehicle->GetPosition();
            }
        } else if (redirected->type == CARP::kAIElementCharacter) {
            if (redirected->character != NULL)
                lockPoint = MatrixRow(&redirected->character->transform, 3);
        }
    }
    PlayerCamera()->CameraLockOn(lockPoint, &target, param, quiet);
}

// FUNC_AT(0x00047610)
ECameraLockOn* ECameraLockOn::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x0003ecc0)
void ECameraShake::Destruct() {
    vtable = kECameraShakeVtable;
    Process();
    vtable = kEventVtable;
}

void ECameraShake::Process() {
    RPlayerCamera *camera = PlayerCamera();
    camera->ShakeCamera(duration, float(double(range)), MatrixRow(&camera->matrix, 3), keepOrigin);
}

// FUNC_AT(0x00047630)
ECameraShake* ECameraShake::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x0003ed40)
void ECameraShake_MakeEvent_Callback(const ECameraShake::Record *record) {
    ECameraShake *event = new ECameraShake;
    if (event != NULL) {
        event->range = record->range;
        event->vtable = kECameraShakeVtable;
        event->duration = record->duration;
        event->keepOrigin = record->keepOrigin;
    }
}

// FUNC_AT(0x0003ee00)
void ECameraZoom::Destruct() {
    vtable = kECameraZoomVtable;
    Process();
    vtable = kEventVtable;
}

void ECameraZoom::Process() {
    if (time > 1)
        time = SixtiethsToSteps(time);
    else
        time = 1;
    PlayerCamera()->SetCameraZoom(kZoomBetween, float(double(percent) * kPercent), time);
}

// FUNC_AT(0x00047670)
ECameraZoom* ECameraZoom::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x0003eee0)
void ECameraZoom_MakeEvent_Callback(const ECameraZoom::Record *record) {
    ECameraZoom *event = new ECameraZoom;
    if (event != NULL) {
        event->vtable = kECameraZoomVtable;
        event->percent = record->percent;
        event->time = record->time;
    }
}

// FUNC_AT(0x0003ef10)
void EChangeCarCameraView::Destruct() {
    vtable = kEChangeCarCameraViewVtable;
    Process();
    vtable = kEventVtable;
}

void EChangeCarCameraView::Process() {
    if (delay > 1)
        delay = SixtiethsToSteps(delay);
    if (b > 1)
        b = SixtiethsToSteps(b);
    else
        b = 1;
    PlayerCamera()->ForceCameraChange(id, delay, b, unused);
}

// FUNC_AT(0x00047690)
EChangeCarCameraView* EChangeCarCameraView::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x0003f020)
void EChangeCarCameraView_MakeEvent_Callback(const EChangeCarCameraView::Record *record) {
    EChangeCarCameraView *event = new EChangeCarCameraView;
    if (event != NULL) {
        event->b = record->b;
        event->delay = record->delay;
        event->vtable = kEChangeCarCameraViewVtable;
        event->id = record->id;
        event->unused = record->unused;
    }
}

// FUNC_AT(0x00049d40)
void ECinematicCamera::Destruct() {
    vtable = kECinematicCameraVtable;
    Process();
    vtable = kEventVtable;
}

void ECinematicCamera::Process() {
    RPlayerCamera *camera = PlayerCamera();
    if (delay > 1)
        delay = SixtiethsToSteps(delay);
    if (unknown08 > 1)
        unknown08 = SixtiethsToSteps(unknown08);
    if (cinematic != 0) {
        if (anchor != NULL && anchor->type == CARP::kAIElementVehicle)
            camera->SetAnchor(anchor->object);
        // The original leaves w as its stack had it; SetCinematicCamera does not read it
        Coord4 at = { place.x, place.y, place.z, 0.0f };
        camera->SetCinematicCamera(&at, uint16_t(delay), uint16_t(unknown08));
        glbMissionManager->SetCinematicMode(kCinematicCamera);
    } else {
        camera->SetAnchor(PlayerCar());
        camera->SetLastSelectableCameraMode(uint16_t(delay), unknown08, false, true);
    }
}

// FUNC_AT(0x0004eb20)
ECinematicCamera* ECinematicCamera::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x0003f1c0)
void ECinematicCamera_MakeEvent_Callback(const ECinematicCamera::Record *record) {
    ECinematicCamera *event = new ECinematicCamera;
    if (event != NULL) {
        event->place = record->place;
        event->unknown08 = record->unknown08;
        event->delay = record->delay;
        event->vtable = kECinematicCameraVtable;
        event->cinematic = record->cinematic;
        event->anchor = record->anchor;
    }
}

// FUNC_AT(0x0003f210)
void ECinematicCamera_ResolveEvent_Callback(ECinematicCamera::Record *record, UGroup *shared, UGroup *parent) {
    reinterpret_cast<CARP::TagReference *>(&record->anchor)->Construct(shared, parent);
}

// FUNC_AT(0x0003f900)
void EEndCameraAnim::Destruct() {
    vtable = kEEndCameraAnimVtable;
    Process();
    vtable = kEventVtable;
}

void EEndCameraAnim::Process() {
    if (delay > 1)
        delay = SixtiethsToSteps(delay);
    if (b > 1)
        b = SixtiethsToSteps(b);
    else
        b = 1;
    PlayerCamera()->EndCameraAnim(uint16_t(delay), b, c != 0, id);
    glbMissionManager->ClearCinematicMode(kCinematicCameraAnim | kCinematicCamera);
}

// FUNC_AT(0x00047a30)
EEndCameraAnim* EEndCameraAnim::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x0003fa30)
void EEndCameraAnim_MakeEvent_Callback(const EEndCameraAnim::Record *record) {
    EEndCameraAnim *event = new EEndCameraAnim;
    if (event != NULL) {
        event->delay = record->delay;
        event->c = record->c;
        event->vtable = kEEndCameraAnimVtable;
        event->b = record->b;
        event->id = record->id;
    }
}

// =============================================================================================================
// The world's power-ups and triggers
// =============================================================================================================

// FUNC_AT(0x0003f550)
void EDisablePowerUp::Destruct() {
    vtable = kEDisablePowerUpVtable;
    Process();
    vtable = kEventVtable;
}

void EDisablePowerUp::Process() {
    if (trigger != NULL)
        trigger->flags &= ~WTrigger::kEnabled;
    if (instance != NULL) {
        instance->flags |= kInstanceDisabled;
        RSceneObj *sceneObj = fgWorld->GetSceneObjFromInstance(instance);
        if (sceneObj != NULL)
            sceneObj->Hide();
        fgSimulation.UntrackInstance(instance);
    }
}

// FUNC_AT(0x00047950)
EDisablePowerUp* EDisablePowerUp::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x0003f5e0)
void EDisablePowerUp_MakeEvent_Callback(const EDisablePowerUp::Record *record) {
    EDisablePowerUp *event = new EDisablePowerUp;
    if (event != NULL) {
        event->vtable = kEDisablePowerUpVtable;
        event->instance = record->instance;
        event->trigger = record->trigger;
    }
}

// FUNC_AT(0x0003f7f0)
void EEnablePowerUp::Destruct() {
    vtable = kEEnablePowerUpVtable;
    Process();
    vtable = kEventVtable;
}

void EEnablePowerUp::Process() {
    if (trigger != NULL)
        trigger->flags |= WTrigger::kEnabled;
    if (instance != NULL) {
        instance->flags &= ~kInstanceDisabled;
        RSceneObj *sceneObj = fgWorld->GetSceneObjFromInstance(instance);
        if (sceneObj != NULL)
            sceneObj->Show();
        if (ArticleOf(instance)->model->info->id != kUntrackedModel)
            fgSimulation.TrackInstance(instance, 0);
    }
}

// FUNC_AT(0x000479f0)
EEnablePowerUp* EEnablePowerUp::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x0003f890)
void EEnablePowerUp_MakeEvent_Callback(const EEnablePowerUp::Record *record) {
    EEnablePowerUp *event = new EEnablePowerUp;
    if (event != NULL) {
        event->vtable = kEEnablePowerUpVtable;
        event->instance = record->instance;
        event->trigger = record->trigger;
    }
}

// FUNC_AT(0x0003f610)
void EDisableTrigger::Destruct() {
    vtable = kEDisableTriggerVtable;
    Process();
    vtable = kEventVtable;
}

void EDisableTrigger::Process() {
    if (trigger != NULL)
        trigger->flags &= ~WTrigger::kEnabled;
}

// FUNC_AT(0x00047970)
EDisableTrigger* EDisableTrigger::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x0003f640)
void EDisableTrigger_MakeEvent_Callback(const EDisableTrigger::Record *record) {
    EDisableTrigger *event = new EDisableTrigger;
    if (event != NULL) {
        event->vtable = kEDisableTriggerVtable;
        event->trigger = record->trigger;
    }
}

// FUNC_AT(0x0003f8c0)
void EEnableTrigger::Destruct() {
    vtable = kEEnableTriggerVtable;
    Process();
    vtable = kEventVtable;
}

void EEnableTrigger::Process() {
    if (trigger != NULL)
        trigger->flags |= WTrigger::kEnabled;
}

// FUNC_AT(0x00047a10)
EEnableTrigger* EEnableTrigger::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x0003f8e0)
void EEnableTrigger_MakeEvent_Callback(const EEnableTrigger::Record *record) {
    EEnableTrigger *event = new EEnableTrigger;
    if (event != NULL) {
        event->vtable = kEEnableTriggerVtable;
        event->trigger = record->trigger;
    }
}

// =============================================================================================================
// The mission
// =============================================================================================================

// FUNC_AT(0x0003db80)
void E007Logo::Destruct() {
    vtable = kE007LogoVtable;
    Process();
    vtable = kEventVtable;
}

void E007Logo::Process() {
    glbMissionManager->unknown4fc++;
    AFader::Get(kSpeechFader)->QueuePrimary("007logo", 0.0f, true, false);
}

// FUNC_AT(0x000472c0)
E007Logo* E007Logo::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x0003dc00)
void E007Logo_MakeEvent_Callback(const void *) {
    E007Logo *event = new E007Logo;
    if (event != NULL)
        event->vtable = kE007LogoVtable;
}

// FUNC_AT(0x000474d0)
EAddObjective* EAddObjective::Delete(unsigned flags) {
    return DeleteBaseEvent(this, flags);
}

// FUNC_AT(0x0003e4e0)
void EAddObjective_MakeEvent_Callback(const EAddObjective::Record *record) {
    EAddObjective *event = new EAddObjective;
    if (event != NULL) {
        event->unknown08 = record->unknown08;
        event->unknown0c = record->unknown0c;
        event->vtable = kEAddObjectiveVtable;
        event->unknown04 = record->unknown04;
        event->unknown10 = record->unknown10;
    }
}

// FUNC_AT(0x0003e520)
void EAddScoreAction::Destruct() {
    vtable = kEAddScoreActionVtable;
    Process();
    vtable = kEventVtable;
}

// The points, and the "bondmove" stinger on the stinger channel
void EAddScoreAction::Process() {
    glbMissionManager->AddScore(kScoreStylin, points);
    if (_stricmp(StingerChannel, "speech") == 0)
        AFader::Get(kSpeechFader)->QueuePrimary("bondmove", 0.0f, true, false);
    else if (_stricmp(StingerChannel, "music") == 0)
        AStream::Get("Music")->Event("bondmove", 0.0f, false, false, false);
}

// FUNC_AT(0x000474f0)
EAddScoreAction* EAddScoreAction::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x0003e5e0)
void EAddScoreAction_MakeEvent_Callback(const EAddScoreAction::Record *record) {
    EAddScoreAction *event = new EAddScoreAction;
    if (event != NULL) {
        event->vtable = kEAddScoreActionVtable;
        event->points = record->points;
    }
}

// FUNC_AT(0x0003e600)
void EAddScoreObjective_MakeEvent_Callback(const EAddScoreObjective::Record *record) {
    EAddScoreObjective *event = new EAddScoreObjective;
    if (event != NULL) {
        event->vtable = kEAddScoreObjectiveVtable;
        event->unknown04 = record->unknown04;
    }
}

// FUNC_AT(0x0003e800)
void EAwardHitToPlayer::Destruct() {
    vtable = kEAwardHitToPlayerVtable;
    Process();
    vtable = kEventVtable;
}

void EAwardHitToPlayer::Process() {
    glbMissionManager->IncShotsHit(false);
}

// FUNC_AT(0x00047550)
EAwardHitToPlayer* EAwardHitToPlayer::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x0003e850)
void EAwardHitToPlayer_MakeEvent_Callback(const EAwardHitToPlayer::Record *record) {
    EAwardHitToPlayer *event = new EAwardHitToPlayer;
    if (event != NULL) {
        event->vtable = kEAwardHitToPlayerVtable;
        event->unknown04 = record->unknown04;
    }
}

// FUNC_AT(0x0003e870)
void EAwardKillToPlayer::Destruct() {
    vtable = kEAwardKillToPlayerVtable;
    Process();
    vtable = kEventVtable;
}

void EAwardKillToPlayer::Process() {
    if (kills == 0)
        kills = 1;
    glbMissionManager->IncKills(kills);
}

// FUNC_AT(0x00047570)
EAwardKillToPlayer* EAwardKillToPlayer::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x0003e8d0)
void EAwardKillToPlayer_MakeEvent_Callback(const EAwardKillToPlayer::Record *record) {
    EAwardKillToPlayer *event = new EAwardKillToPlayer;
    if (event != NULL) {
        event->vtable = kEAwardKillToPlayerVtable;
        event->kills = record->kills;
    }
}

// FUNC_AT(0x00048680)
EBeginDownloadCode* EBeginDownloadCode::Delete(unsigned flags) {
    return DeleteBaseEvent(this, flags);
}

// FUNC_AT(0x0003e920)
void EBeginDownloadCode_MakeEvent_Callback(const EBeginDownloadCode::Record *record) {
    EBeginDownloadCode *event = new EBeginDownloadCode;
    if (event != NULL) {
        event->vtable = kEBeginDownloadCodeVtable;
        event->unknown04 = record->unknown04;
    }
}

// FUNC_AT(0x0003e940)
void EButtonMsgOff::Destruct() {
    vtable = kEButtonMsgOffVtable;
    Process();
    vtable = kEventVtable;
}

void EButtonMsgOff::Process() {
    GHud::TheApp()->KillButtonMessage();
}

// FUNC_AT(0x00047590)
EButtonMsgOff* EButtonMsgOff::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x0003e990)
void EButtonMsgOff_MakeEvent_Callback(const void *) {
    EButtonMsgOff *event = new EButtonMsgOff;
    if (event != NULL)
        event->vtable = kEButtonMsgOffVtable;
}

// FUNC_AT(0x0003e9b0)
void EButtonMsgOn::Destruct() {
    vtable = kEButtonMsgOnVtable;
    Process();
    vtable = kEventVtable;
}

void EButtonMsgOn::Process() {
    GHud::TheApp()->TriggerButtonMsg(message, unknown0c, unknown10, unknown14, time);
}

// FUNC_AT(0x000475b0)
EButtonMsgOn* EButtonMsgOn::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x0003ea20)
void EButtonMsgOn_MakeEvent_Callback(const EButtonMsgOn::Record *record) {
    EButtonMsgOn *event = new EButtonMsgOn;
    if (event != NULL) {
        event->time = record->time;
        event->unknown0c = record->unknown0c;
        event->unknown10 = record->unknown10;
        event->vtable = kEButtonMsgOnVtable;
        event->message = record->message;
        event->unknown14 = record->unknown14;
    }
}

// FUNC_AT(0x0003ea60)
void ECall911::Destruct() {
    vtable = kECall911Vtable;
    Process();
    vtable = kEventVtable;
}

void ECall911::Process() {
    AFader::Get(kSpeechFader)->Call911();
}

// FUNC_AT(0x000475d0)
ECall911* ECall911::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x0003eac0)
void ECall911_MakeEvent_Callback(const void *) {
    ECall911 *event = new ECall911;
    if (event != NULL)
        event->vtable = kECall911Vtable;
}

// FUNC_AT(0x0003eae0)
void ECallStage::Destruct() {
    vtable = kECallStageVtable;
    Process();
    vtable = kEventVtable;
}

void ECallStage::Process() {
    glbMissionManager->CallStage(stage);
}

// FUNC_AT(0x000475f0)
ECallStage* ECallStage::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x0003eb30)
void ECallStage_MakeEvent_Callback(const ECallStage::Record *record) {
    ECallStage *event = new ECallStage;
    if (event != NULL) {
        event->vtable = kECallStageVtable;
        event->stage = record->stage;
    }
}

// FUNC_AT(0x0003f060)
EChangeMaxTraffic* EChangeMaxTraffic::Construct(int maxTraffic) {
    vtable = kEChangeMaxTrafficVtable;
    this->maxTraffic = maxTraffic;
    AIVehicleController::Get();
    MaxTraffic = int16_t(maxTraffic);
    return this;
}

// FUNC_AT(0x0003f0c0)
void EChangeMaxTraffic_MakeEvent_Callback(const EChangeMaxTraffic::Record *record) {
    EChangeMaxTraffic *event = new EChangeMaxTraffic;
    if (event != NULL)
        event->Construct(record->maxTraffic);
}

// FUNC_AT(0x0003f110)
EChangeStage* EChangeStage::Construct(int stage) {
    vtable = kEChangeStageVtable;
    this->stage = stage;
    glbMissionManager->ChangeStage(stage);
    return this;
}

// FUNC_AT(0x0003f170)
void EChangeStage_MakeEvent_Callback(const EChangeStage::Record *record) {
    EChangeStage *event = new EChangeStage;
    if (event != NULL)
        event->Construct(record->stage);
}

// FUNC_AT(0x0003f260)
void EClearBitMagicCounter::Destruct() {
    vtable = kEClearBitMagicCounterVtable;
    Process();
    vtable = kEventVtable;
}

void EClearBitMagicCounter::Process() {
    int value = glbMissionManager->GetMagicCounter(counter);
    glbMissionManager->SetMagicCounter(counter, value & ~bits);
}

// FUNC_AT(0x000476b0)
EClearBitMagicCounter* EClearBitMagicCounter::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x0003f2d0)
void EClearBitMagicCounter_MakeEvent_Callback(const EClearBitMagicCounter::Record *record) {
    EClearBitMagicCounter *event = new EClearBitMagicCounter;
    if (event != NULL) {
        event->vtable = kEClearBitMagicCounterVtable;
        event->counter = record->counter;
        event->bits = record->bits;
    }
}

// FUNC_AT(0x0003f300)
void EClearProgrammerEvent::Destruct() {
    vtable = kEClearProgrammerEventVtable;
    Process();
    vtable = kEventVtable;
}

void EClearProgrammerEvent::Process() {
    glbMissionManager->ClearProgrammerEvent(event);
}

// FUNC_AT(0x000476d0)
EClearProgrammerEvent* EClearProgrammerEvent::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x0003f350)
void EClearProgrammerEvent_MakeEvent_Callback(const EClearProgrammerEvent::Record *record) {
    EClearProgrammerEvent *event = new EClearProgrammerEvent;
    if (event != NULL) {
        event->vtable = kEClearProgrammerEventVtable;
        event->event = record->event;
    }
}

// FUNC_AT(0x00047990)
EDisplayObjectiveText* EDisplayObjectiveText::Delete(unsigned flags) {
    return DeleteBaseEvent(this, flags);
}

// FUNC_AT(0x0003f660)
void EDisplayObjectiveText_MakeEvent_Callback(const EDisplayObjectiveText::Record *record) {
    EDisplayObjectiveText *event = new EDisplayObjectiveText;
    if (event != NULL) {
        event->unknown08 = record->unknown08;
        event->vtable = kEDisplayObjectiveTextVtable;
        event->unknown04 = record->unknown04;
        event->unknown0c = record->unknown0c;
    }
}

// FUNC_AT(0x0003f690)
void EDisplayText::Destruct() {
    vtable = kEDisplayTextVtable;
    Process();
    vtable = kEventVtable;
}

void EDisplayText::Process() {
    GHud::TheApp()->TriggerMissionMsg(message, time);
}

// FUNC_AT(0x000479b0)
EDisplayText* EDisplayText::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x0003f6f0)
void EDisplayText_MakeEvent_Callback(const EDisplayText::Record *record) {
    EDisplayText *event = new EDisplayText;
    if (event != NULL) {
        event->time = record->time;
        event->vtable = kEDisplayTextVtable;
        event->message = record->message;
    }
}

// FUNC_AT(0x0003fab0)
void EEndMission::Destruct() {
    vtable = kEEndMissionVtable;
    Process();
    vtable = kEventVtable;
}

// The mission won: no more rumble, the game loop told to end, and the scores tallied into the launch page
void EEndMission::Process() {
    if (_stricmp(Launch.missionName, "mis07a") == 0)
        GHud::TheApp()->SetLevelLoadText();
    gFeedbackAllowed = 0;
    fgSimulation.simState = kSimFinished;
    Launch.unknown978 = 0;
    glbMissionManager->Win();
    glbMissionManager->TallyScores(&Launch, true);
}

// FUNC_AT(0x00047a50)
EEndMission* EEndMission::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x0003fb50)
void EEndMission_MakeEvent_Callback(const void *) {
    EEndMission *event = new EEndMission;
    if (event != NULL)
        event->vtable = kEEndMissionVtable;
}
