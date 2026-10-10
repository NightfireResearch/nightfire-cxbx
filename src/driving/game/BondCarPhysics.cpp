#pragma fp_contract(off)

#include "BondCarPhysics.h"

#include <bit>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "AIVehicle.h"
#include "BondCar.h"
#include "BondCarBasics.h"                  // GetCarColourVariation
#include "BondCarState.h"                   // CarSurface, the BondCar_* tuning
#include "Vehicle.h"
#include "VehicleSound.h"                   // AVehicle
#include "../../common/xbeOverload.h"       // XbeVirtual
#include "../../helpers.h"
#include "../EventManager.hpp"              // Event
#include "../anim/AnimEngine.h"             // Handle
#include "../audio/Sound.h"                 // ABaseSound
#include "../camera/Camera.h"               // AsCoord3
#include "../camera/PlayerCamera.h"         // RPlayerCamera
#include "../engine/ActionQueue.hpp"
#include "../engine/GameInterfaces.hpp"     // GHud
#include "../engine/MissionManager.h"       // glbMissionManager
#include "../engine/PhysicsUtil.h"          // Util_Step, Util_GenerateMatrix, Util_GenerateCarTensor
#include "../engine/UMemory.hpp"
#include "../physics/PhysicsMath.h"         // Abs, Min, Max
#include "../physics/RigidBody.h"
#include "../physics/RigidBodyResolve.h"    // CollisionImpact
#include "../physics/Simulation.h"
#include "../platform/RealMath.h"
#include "../platform/X87.h"                // Ftol
#include "../render/RenderHigh.h"           // fgRenderHigh
#include "../render/RSceneObj.hpp"          // RSceneObj, RandomScaled
#include "../world/CollisionManager.h"      // fgCollisionMgr
#include "../world/CollisionTypes.h"        // MatrixRow
#include "../world/RoadNav.h"

// ---------------------------------------------------------------------------------------------------------------
// PBondCar's driving (0x00066060-0x0006a840), ported from the listing. The physics are x87 code: chains the
// original keeps on the FPU stack are written in double, in its order, and rounded where it stores a float;
// comparisons keep the original's NaN behaviour. Vector words the original leaves as its stack had them (the w of a
// vector only the xyz functions touch) are zero here.
// ---------------------------------------------------------------------------------------------------------------

// ---- the game's code not ported yet
#define SMissionManager_GetCurrObjective ((const Coord4 *(__fastcall *)(SMissionManager *, int, int unknown))0x000b6c70)
#define RTyreTrack_Add ((void (__fastcall *)(RTyreTrack *, int, const Coord4 *point, float slip, float width, int surface, const WWorldPos *ground))0x000ac4c0)
#define RTyreTrack_Break ((void (__fastcall *)(RTyreTrack *, int, int surface))0x000ac170)
#define RVehicle_Construct ((RSceneObj *(__fastcall *)(void *, int, PBondCar *car))0x000953e0)
#define APlayerVehicle_Construct ((ABaseSound *(__fastcall *)(void *, int, const char *engineSound))0x001284b0)
#define CollisionImpact_Construct ((CollisionImpact *(__fastcall *)(CollisionImpact *, int))0x0003dc20)
#define ECollision_Construct ((void *(__fastcall *)(void *, int, CollisionImpact impact))0x0003f370)
#define CRT_stricmp ((int (*)(const char *, const char *))0x00134537)

// ---- globals
#define Sim ((void *)0x00233ff0)                        // the Simulation
#define SimStepCount U32_AT(0x00234e34)                 // the Simulation's steps so far
#define playerPhysicsObject (*(PBondCar ***)0x00234e40) // the Simulation's cars, the player's first
#define ZeroVector (*(const Coord3 *)0x00243030)        // the game's zero vector, never written

namespace {

// ForceStopOn's flags that stop the car dead; the others brake it
constexpr uint8_t kForceStopDead = 0x0a;

// unknown2B1: what the controls are doing to the friction shares (ProcessPhysics)
enum : uint8_t {
    kDriveCoasting = 0,
    kDriveAccelerating = 1,
    kDriveBraking = 2,
    kDriveReversing = 3,
    kDriveHandbrake = 4,
    kDriveReverseHandbrake = 5,
};

// RVehicle's TriggerFX types past RSceneObj's (the landing's)
constexpr int kFXType6 = 6;
constexpr uint32_t kFXLandingWhich = 0x0d;

// SetVisualDamage: the damage bands of its random points (fgDamageLevels.zone[1..4]: each band's zones get
// stimulus 0x13 + band)
constexpr int kDamageBands = 4;
constexpr uint8_t kStimulusDamageBand = 0x13;
constexpr int kStimulusModeZones = 2;

// The impact event a hard landing raises (RaiseLandingImpact)
constexpr uint32_t kCollisionEventSize = 0x58;  // sizeof(ECollision)
constexpr uint16_t kLandingImpactUnknown4A = 0x0e;
constexpr uint8_t kLandingImpactKindB = 7;
constexpr uint16_t kLandingImpactTag = 1;

constexpr uint32_t kBondCarSize = 0x420;        // sizeof(PBondCar)
constexpr uint32_t kRVehicleSize = 0x370;
constexpr uint32_t kAPlayerVehicleSize = 0x220;

constexpr float kUnitsPerTurn = 6.32f;          // the wheels' speed: the body's over steps a second x WHEEL_RADIUS x it
constexpr float kOneThirtieth = 1.0f / 30.0f;
constexpr float kSlipBase = 0.3f;               // ProcessPhysics' wheel slips: (slip - 0.3) / 0.7
constexpr float kSlipScale = 1.0f / 0.7f;
constexpr float kSimpleSlipBase = 0.4f;         // ProcessSimplePhysics': (slip - 0.4) / 0.6
constexpr float kSimpleSlipScale = 1.0f / 0.6f;
static_assert(std::bit_cast<uint32_t>(kUnitsPerTurn) == 0x40ca3d71 &&
              std::bit_cast<uint32_t>(kOneThirtieth) == 0x3d088889 &&
              std::bit_cast<uint32_t>(kSlipBase) == 0x3e99999a && std::bit_cast<uint32_t>(kSlipScale) == 0x3fb6db6e &&
              std::bit_cast<uint32_t>(kSimpleSlipBase) == 0x3ecccccd &&
              std::bit_cast<uint32_t>(kSimpleSlipScale) == 0x3fd55555, "the constants");
static_assert(std::bit_cast<uint32_t>(0.02f) == 0x3ca3d70a && std::bit_cast<uint32_t>(0.6f) == 0x3f19999a &&
              std::bit_cast<uint32_t>(0.707f) == 0x3f34fdf4 && std::bit_cast<uint32_t>(0.2f) == 0x3e4ccccd &&
              std::bit_cast<uint32_t>(0.05f) == 0x3d4ccccd && std::bit_cast<uint32_t>(0.1f) == 0x3dcccccd &&
              std::bit_cast<uint32_t>(0.95f) == 0x3f733333 && std::bit_cast<uint32_t>(0.9f) == 0x3f666666 &&
              std::bit_cast<uint32_t>(0.33f) == 0x3ea8f5c3 && std::bit_cast<uint32_t>(0.04f) == 0x3d23d70a &&
              std::bit_cast<uint32_t>(1.2f) == 0x3f99999a && std::bit_cast<uint32_t>(0.01f) == 0x3c23d70a &&
              std::bit_cast<uint32_t>(0.08f) == 0x3da3d70a && std::bit_cast<uint32_t>(0.0022f) == 0x3b102de0 &&
              std::bit_cast<uint32_t>(0.0035f) == 0x3b656042 && std::bit_cast<uint32_t>(0.075f) == 0x3d99999a &&
              std::bit_cast<uint32_t>(0.8f) == 0x3f4ccccd && std::bit_cast<uint32_t>(0.7f) == 0x3f333333 &&
              std::bit_cast<uint32_t>(0.3f) == 0x3e99999a && std::bit_cast<uint32_t>(0.15f) == 0x3e19999a &&
              std::bit_cast<uint32_t>(0.35f) == 0x3eb33333 && std::bit_cast<uint32_t>(0.65f) == 0x3f266666 &&
              std::bit_cast<uint32_t>(0.85f) == 0x3f59999a, "the literals");

// What this file reads of the HUD (provisional here)
struct CarHudFields {
    uint8_t unknown000[0x2b0];
    uint8_t unknown2B0;                 // +0x2b0 set: the fire buttons are ignored
};

const CarHudFields *Hud() {
    return reinterpret_cast<const CarHudFields *>(GHud::TheApp());
}

// ---- PBondCar's virtual methods, called through its vtable as the original does
AVehicle *CallGetAudio(PBondCar *car) {
    return static_cast<AVehicle *>((car->*XbeVirtual<decltype(&PBondCar::GetAudio)>(car, PVehicle::kGetAudio))());
}
void CallSetAudio(PBondCar *car, ABaseSound *audio) {
    (car->*XbeVirtual<decltype(&PBondCar::SetAudio)>(car, PVehicle::kSetAudio))(audio);
}
AIGroundVehicle *CallGetAIGroundVehiclePtr(PBondCar *car) {
    return (car->*XbeVirtual<decltype(&PBondCar::GetAIGroundVehiclePtr)>(car, PVehicle::kGetAIGroundVehiclePtr))();
}
const char *CallGetCarType(PBondCar *car) {
    return (car->*XbeVirtual<decltype(&PBondCar::GetCarType)>(car, PVehicle::kGetCarType))();
}
int CallGetCarClass(PBondCar *car) {
    return (car->*XbeVirtual<decltype(&PBondCar::GetCarClass)>(car, PVehicle::kGetCarClass))();
}
bool CallIsTyreShredded(PBondCar *car, int8_t wheel) {
    return (car->*XbeVirtual<decltype(&PBondCar::IsTyreShredded)>(car, PVehicle::kIsTyreShredded))(wheel);
}
void CallResetDamage(PBondCar *car) {
    (car->*XbeVirtual<decltype(&PBondCar::ResetDamage)>(car, PVehicle::kResetDamage))();
}
void CallDisableTwoWheelStunt(PBondCar *car) {
    (car->*XbeVirtual<decltype(&PBondCar::DisableTwoWheelStunt)>(car, PVehicle::kDisableTwoWheelStunt))();
}
CarPhysics *CallGetPhysics(PBondCar *car) {
    return (car->*XbeVirtual<decltype(&PBondCar::GetPhysics)>(car, PVehicle::kGetPhysics))();
}
bool CallIsWheelOnGround(PBondCar *car, int8_t wheel) {
    return (car->*XbeVirtual<decltype(&PBondCar::IsWheelOnGround)>(car, PVehicle::kIsWheelOnGround))(wheel);
}

// The car's AI is on no spline path (the original asks the AI again at each use)
bool OffSplinePath(PBondCar *car) {
    return CallGetAIGroundVehiclePtr(car) != NULL && AIVehicle_GetSplinePath(CallGetAIGroundVehiclePtr(car), 0) == NULL;
}

// ---- the scene object's virtual method
// RSceneObj's slot 15 as RVehicle overrides it, with its point and velocity arguments
void TriggerFX(RSceneObj *object, int type, uint32_t which, const void *point, const void *velocity,
               float intensity) {
    typedef void (RSceneObj::*Method)(int type, uint32_t which, uint32_t unused, const void *point,
                                      const void *velocity, float intensity);
    (object->*XbeVirtual<Method>(object, 15))(type, which, 0, point, velocity, intensity);
}

// A turn wrapped into 0 to 1 from above 1 or below 0, by its whole part (__ftol2's, one less below 0)
float WrapTurn(double turn) {
    if (turn > 1.0)
        return (float)(turn - Ftol(turn));
    if (turn < 0.0)
        return (float)(turn - int32_t(uint32_t(Ftol(turn)) - 1));
    return (float)turn;
}

} // namespace

void RaiseLandingImpact(const RigidBody *body, const Coord3 *position, float strength) {
    CollisionImpact impact;
    CollisionImpact_Construct(&impact, 0);
    impact.closingSpeed = strength;
    impact.strength = strength;
    impact.kindA = body->kind;
    impact.ownerA = body->ownerIndex;
    impact.normal.x = 0.0f;
    impact.normal.y = 1.0f;
    impact.normal.z = 0.0f;
    impact.relativeVelocity.x = 0.0f;
    impact.relativeVelocity.y = 0.0f;
    impact.relativeVelocity.z = 0.0f;
    impact.unknown4a = kLandingImpactUnknown4A;
    impact.kindB = kLandingImpactKindB;
    impact.tag = kLandingImpactTag;
    Coord4 point;
    impact.point = *Float_COORD3toCOORD4(&point, position);
    void *event = Event::operator new(kCollisionEventSize);
    if (event != NULL)
        ECollision_Construct(event, 0, impact);
}

namespace {

Coord3 *Xyz(Coord4 *v) {
    return AsCoord3(v);
}

// fabs as the x87 code branches it: -0 stays -0, a NaN stays
double AbsDouble(double x) {
    return x < 0.0 ? std::bit_cast<double>(std::bit_cast<uint64_t>(x) ^ 0x8000000000000000ull) : x;
}

} // namespace

// ---- accessors

// FUNC_AT(0x00066060)
float* PBondCar::GetSuspensionCompression() {
    return suspensionCompression;
}

// FUNC_AT(0x00066070)
float PBondCar::GetCarWheelSlip(int wheel) {
    return wheelSlip[wheel];
}

// FUNC_AT(0x00066080)
void PBondCar::DisableTyreBlowOuts() {
    tyreBlowOuts = 0;
}

// FUNC_AT(0x00066090)
bool PBondCar::GetIsInTwoWheelMode() {
    return twoWheelMode != 0;
}

// FUNC_AT(0x000660a0)
uint8_t PBondCar::GetWasInAir() {
    return wasInAir;
}

// FUNC_AT(0x000660b0)
void PBondCar::SetWasInAir(bool inAir) {
    wasInAir = inAir;
}

// FUNC_AT(0x000660c0)
void PBondCar::SetImmunity(bool on) {
    immunity = on;
}

// FUNC_AT(0x000660d0)
Coord4* PBondCar::GetWheelPos(int wheel) {
    return &wheelPos[wheel];
}

// FUNC_AT(0x000660e0)
uint8_t PBondCar::GetForceStop() {
    return forceStop;
}

// FUNC_AT(0x000660f0)
void PBondCar::ForceStopOn(uint8_t flags) {
    forceStop |= flags;
}

// FUNC_AT(0x00066110)
void PBondCar::ForceStopOff(uint8_t flags) {
    forceStop &= ~flags;
}

// FUNC_AT(0x00066130)
void PBondCar::ForceRollDirection(int8_t direction) {
    forceRollDirection = direction;
}

// FUNC_AT(0x00066140)
void PBondCar::RollSub(int8_t degrees, bool rolling) {
    rollSub = degrees;
    subRolling = rolling;
}

// FUNC_AT(0x00066160)
const char* PBondCar::GetSecondaryType() {
    return secondaryType;
}

// FUNC_AT(0x00066170)
WTargetable* PBondCar::GetTargetBeacon() {
    return targetBeacon;
}

// ---- damage, reset, model

// Each damage zone gets a random point in [fromA, toA] x [fromB, toB]; the larger of its two coordinates picks the
// bands it is in. The zones in each band get that band's stimulus, those above the second band are damaged and
// those above fgDamageLevels.effects start their damage effects. Nothing happens if either range is reversed or
// reaches past 1.
// FUNC_AT(0x00066180)
void PBondCar::SetVisualDamage(float fromA, float toA, float fromB, float toB) {
    float rangeA = toA - fromA;
    float rangeB = toB - fromB;
    if (!(rangeA >= 0.0f) || !(rangeB >= 0.0f))
        return;
    if (!((double)rangeA + fromA <= 1.0) || !((double)rangeB + fromB <= 1.0))
        return;

    uint32_t bandZones[kDamageBands] = {};
    uint32_t damagedZones = 0;
    uint32_t effectZones = 0;
    for (int zone = 0; zone < kDamageZoneCount; zone++) {
        DamageZone &point = damageZones[zone];
        point.unknown00 = (float)(RandomScaled(rangeA) + fromA);
        point.unknown04 = (float)(RandomScaled(rangeB) + fromB);
        float depth = Max(point.unknown04, point.unknown00);
        for (int band = kDamageBands - 1; band >= 0; band--)
            if (depth >= fgDamageLevels.zone[band + 1])
                bandZones[band] |= 1u << zone;
        if (depth > fgDamageLevels.zone[2])
            damagedZones |= 1u << zone;
        if (depth > fgDamageLevels.effects)
            effectZones |= 1u << zone;
    }

    RSceneObj *render = renderObject;
    Handle *handle = render->animHandle;
    for (int band = kDamageBands - 1; band >= 0; band--)
        if (bandZones[band] != 0)
            handle->ProcessStimuliZones(kStimulusDamageBand + band, bandZones[band], SimStepCount, kStimulusModeZones);
    if (damagedZones != 0)
        TriggerFX(render, RSceneObj::kFXDamage, damagedZones, &ZeroVector, &ZeroVector, -1.0f);
    if (effectZones != 0)
        TriggerFX(render, RSceneObj::kFXDamageEffects, effectZones, &ZeroVector, &ZeroVector, -1.0f);
}

// The car put back on the ground where it is: facing the current objective if there is one, else (the player's car)
// its reset direction, or (the AI's) along its own heading - an AI on its road first moved 5 along it - with its
// variables reset, its body at the ground's height plus TYRE_RADIUS, SPRING_REST_LENGTH and its half height, the
// player's camera reset, and new tyre tracks.
// FUNC_AT(0x00066390)
void PBondCar::ResetCar(bool unused) {
    InitializeCarVariables(false);
    RigidBody *body = Simulation_GetRigidBody(Sim, 0, rigidBodySlot);
    RigidBodyInfo *info = body->info;
    Coord4 position = { body->position.x, body->position.y, body->position.z, 0.0f };
    Coord4 direction = {};

    const Coord4 *objective = SMissionManager_GetCurrObjective(glbMissionManager, 0, 0);
    if (objective != NULL) {
        VU0_v4sub(objective, &position, &direction);
        VU0_v4unitxyz(&direction, &direction);
    } else if (CallGetCarClass(this) == kPlayerCar) {
        *Xyz(&direction) = resetDirection;
    } else if (Abs(info->orientation.mtx[1][1]) > 0.1f) {
        *Xyz(&direction) = *AsCoord3(MatrixRow(&body->info->orientation, 2));
    } else {
        *Xyz(&direction) = *AsCoord3(MatrixRow(&body->info->orientation, 1));
    }

    AIGroundVehicle *ai = aiGroundVehicle;
    if (ai != NULL && ai->active && ai->type == kAIDrivingOnRoad) {
        WRoadNav *nav = ai->driveToNav;
        Coord3 heading = nav->direction;
        nav->ChangeLanes(0.0f, 0.0f);
        aiGroundVehicle->driveToNav->IncNavPosition(5.0f, &heading, -1);
        nav = aiGroundVehicle->driveToNav;
        *Xyz(&position) = nav->position;
        *Xyz(&direction) = nav->direction;
        resetDirection = nav->direction;
    }

    Coord3 at = *Xyz(&position);
    MATRIX4 basis;
    MATRIX4 orientation = *Util_GenerateMatrix(&basis, AsCoord3(&direction));
    float groundHeight;
    if (!fgCollisionMgr->GetWorldHeightAtPoint(&at, &groundHeight, false))
        groundHeight = 0.0f;
    RigidBodyInfo *bodyInfo = body->info;
    float springRest = attributes.LookupFloat("SPRING_REST_LENGTH", NULL);
    at.y = (float)((double)attributes.LookupFloat("TYRE_RADIUS", NULL) + springRest + bodyInfo->halfExtents.y +
                   groundHeight);
    body->ResetObject(&orientation, &at);
    renderObject->UpdatePositionVirtual(true);
    if (carClass == kPlayerCar)
        fgRenderHigh->views[0].camera->ResetCamera();

    for (int wheel = 0; wheel < kCarWheels; wheel++)
        if (tyreTracks[wheel] != NULL)
            UMemory::FastFree(tyreTracks[wheel], kTyreTrackSize);
    InitTyreTracks();
}

// ---- controls

// The player's car's controls from its action queue: steering, gas and brake (and the analogue gas-brake), the
// hand brake, the fire buttons and the sound steering flags. Forced to stop, the controls are cleared instead and the
// car stopped dead or braked. Gas and brake snap to 0 inside the pad's dead zone and to 1 outside 1 - it; the
// larger of the two wins; the steering is stepped towards its input.
// FUNC_AT(0x00066640)
void PBondCar::GetControllerInput() {
    RigidBody *body = Simulation_GetRigidBody(Sim, 0, rigidBodySlot);

    if (forceStop != 0) {
        control.steering = 0.0f;
        unknown2C4 = 0.0f;
        control.steeringVertical = 0.0f;
        unknown400 = 0.0f;
        control.gas = 0.0f;
        unknown278 = 0.0f;
        control.handBrake = 0;
        control.firePrimary = 0;
        control.unknown1B = 0;
        if (forceStop & kForceStopDead) {
            body->ApplyHeavyFriction();
            body->velocity = ZeroVector;
            body->momentum = ZeroVector;
            control.brake = 0.0f;
            unknown27C = 0.0f;
        } else if (v3dotprod(MatrixRow(&body->info->orientation, 2), &body->velocity) > 0.0f) {
            control.brake = 1.0f;
            unknown27C = control.brake;
        } else {
            control.brake = 0.0f;
            unknown27C = 0.0f;
        }
        return;
    }

    Coord4 velocity = {};
    *Xyz(&velocity) = body->velocity;
    Coord4 forward = *MatrixRow(&body->info->orientation, 2);
    float along = v3dotprod(&velocity, &forward);

    while (!actionQueue->IsEmpty()) {
        ActionRef ref;
        actionQueue->GetAction(&ref);
        const ActionData *action = ref.data;
        float value = action != NULL ? action->value : 0.0f;
        switch (action != NULL ? action->action : 0) {
        case kActionSteer:
            unknown2C4 = value;
            break;
        case kActionSteerVertical:
            unknown400 = value;
            break;
        case kActionSteerVerticalInv:
            unknown400 = -value;
            break;
        case kActionSoundSteerLeft:
        case kActionSoundSteerRight:
        case kActionSoundSteerUp:
        case kActionSoundSteerDown:
            CallGetAudio(this)->xyTrigger = 1;
            break;
        case kActionSoundSteerHorizontalRelease:
        case kActionSoundSteerVerticalRelease:
            CallGetAudio(this)->xyReleaseTrigger = 1;
            break;
        case kActionGas:
            unknown278 = value;
            break;
        case kActionGasBrake:
            if (value < -BondCar_PAD_DEAD_ZONE) {
                unknown284 = 0.0f;
                unknown280 = -value;
            } else {
                unknown280 = 0.0f;
                unknown284 = value > BondCar_PAD_DEAD_ZONE ? value : 0.0f;
            }
            break;
        case kActionBrake:
            unknown27C = value;
            break;
        case kActionHandbrake:
            control.handBrake = 1;
            unknown29F = !(along >= 0.0f);
            break;
        case kActionHandbrakeRelease:
            control.handBrake = 0;
            break;
        case kActionFirePrimary:
            if (Hud()->unknown2B0 == 0)
                control.firePrimary = 1;
            break;
        case kActionFirePrimaryRelease:
            control.firePrimary = 0;
            break;
        case kActionFireSecondary:
            if (Hud()->unknown2B0 == 0)
                control.unknown1B = 1;
            break;
        case kActionFireSecondaryRelease:
            control.unknown1B = 0;
            break;
        case kActionToggleSecondary:
            GHud::TheApp();
            break;
        default:
            break;
        }
        actionQueue->PopAction();
    }

    if (unknown278 > 1.0 - BondCar_PAD_DEAD_ZONE)
        unknown278 = 1.0f;
    if (unknown278 < BondCar_PAD_DEAD_ZONE)
        unknown278 = 0.0f;
    if (1.0 - BondCar_PAD_DEAD_ZONE < unknown27C)
        unknown27C = 1.0f;
    if (unknown27C < BondCar_PAD_DEAD_ZONE)
        unknown27C = 0.0f;
    float gas = Max(unknown280, unknown278);
    targetGas = gas;
    float brake = Max(unknown284, unknown27C);
    targetBrake = brake;

    unknown2D4 = Abs(unknown2C4) > 0.9f;
    CarPhysics *tuning = physics;
    double spinSpeed = tuning->wsSpeed;
    if (unknown29B)
        spinSpeed *= 4.0f;
    if (numWheelsOnGround > 2 && gas > 0.9f && unknown2D4)
        unknown29B = spinSpeed > carSpeed && !againstWall;
    else
        unknown29B = 0;

    control.gas = gas;
    control.brake = brake;
    if (gas > brake)
        control.brake = 0.0f;
    else
        control.gas = 0.0f;
    if (control.handBrake && tuning->isSub == 0) {
        control.brake = 0.0f;
        control.gas = 0.0f;
    }

    if (-BondCar_PAD_DEAD_ZONE < unknown2C4 && unknown2C4 < BondCar_PAD_DEAD_ZONE)
        unknown2C4 = 0.0f;
    unknown260 = control.steering;
    unknown264 = control.steeringVertical;
    control.steering = (float)Util_Step(control.steering, unknown3F8 == 0.0f ? unknown2C4 : unknown3F8, 0.25f);
    if (-BondCar_PAD_DEAD_ZONE < unknown400 && unknown400 < BondCar_PAD_DEAD_ZONE)
        unknown400 = 0.0f;
    control.steeringVertical = (float)Util_Step(control.steeringVertical,
                                                unknown404 == 0.0f ? unknown400 : unknown404, 0.25f);
}

// The two-wheel stunt, each step while it is on and the car on the ground: the inertia about z scaled by how far
// the car has rolled towards TWO_WHEEL_ANGLE, the roll's spin damped when it carries on past it, and a push towards
// the angle (or none near it). Off below 20, or while braking or on the hand brake.
// FUNC_AT(0x00066c90)
void PBondCar::TwoWheelStunt() {
    if (numWheelsOnGround == 0 || unknown2B1 == kDriveBraking || unknown2B1 == kDriveHandbrake || carSpeed < 20.0f) {
        CallDisableTwoWheelStunt(this);
        return;
    }
    RigidBody *body = Simulation_GetRigidBody(Sim, 0, rigidBodySlot);
    RigidBodyInfo *info = body->info;

    // The level frame about the car's right, tilted by the angle about its z
    Coord4 up = { 0.0f, 1.0f, 0.0f, 0.0f };
    MATRIX4 tilted = {};
    VU0_v4unitcrossprodxyz(MatrixRow(&info->orientation, 0), &up, MatrixRow(&tilted, 2));
    VU0_v4crossprodxyz(&up, MatrixRow(&tilted, 2), MatrixRow(&tilted, 0));
    VU0_v4copy(&up, MatrixRow(&tilted, 1));
    VU0_v4Init(MatrixRow(&tilted, 3));
    MATRIX4 tilt;
    VU0_MATRIX4setzrot(&tilt, BondCar_TWO_WHEEL_ANGLE);
    VU0_MATRIX4_mult(&tilted, &tilt, &tilted);

    Coord3 push = { 0.0f, 0.0f, 0.0f };
    push.z = v3dotprod(MatrixRow(&tilted, 0), MatrixRow(&info->orientation, 0));
    float roll = push.z;
    double tensorScale = (double)Abs(roll) * BondCar_TWO_WHEEL_TENSOR_SCALE;
    twoWheelStuntTimer = (float)tensorScale;
    if (tensorScale > 1.0)
        twoWheelStuntTimer = 1.0f;

    Coord3 spin;
    body->GetLocalAngularMomentum(&spin);
    if ((roll > 0.0f && spin.z > 0.0f) || (roll < 0.0f && spin.z < 0.0f)) {
        spin.z = BondCar_TWO_WHEEL_OPPOSITE_SCALE * spin.z;
        body->SetAngularMomentum(&spin);
    }
    if (Abs(roll) < 0.1f) {
        spin.z = 0.0f;
        body->SetAngularMomentum(&spin);
        return;
    }
    push.z = Min(Max(-(BondCar_TWO_WHEEL_SCALE * roll), -BondCar_TWO_WHEEL_LIMIT), BondCar_TWO_WHEEL_LIMIT);
    body->ModifyAngularMomentum(&push);
}

// Each wheel's tyre track: added to while the wheel is on the ground and slipping (or always on a snowmobile), at
// the wheel a 30th of a second ahead, as wide as the car's spin; broken off on ground of kind 11, off the ground or
// with the tyre shredded.
// FUNC_AT(0x00066ee0)
void PBondCar::ControlTyreTracks() {
    bool snowmobile = CallGetPhysics(this)->isSnowmobile != 0;
    for (int wheel = 0; wheel < kCarWheels; wheel++) {
        RTyreTrack *track = tyreTracks[wheel];
        if (track == NULL)
            continue;
        uint8_t surface = wheels[wheel].face.corner[2].tag.type;
        if (surface == kSurface11 || !(suspensionCompression[wheel] > 0.0f) || CallIsTyreShredded(this, wheel) ||
            (!(wheelSlip[wheel] > 0.0f) && !snowmobile)) {
            RTyreTrack_Break(track, 0, surface);
            continue;
        }
        float width;
        if (unknown3FE == 0 && !snowmobile)
            width = Abs(Simulation_GetRigidBody(Sim, 0, rigidBodySlot)->angularVelocity.y);
        else
            width = 1.0f;
        Coord4 point = wheelPos[wheel];
        Coord4 velocity = {};
        *Xyz(&velocity) = Simulation_GetRigidBody(Sim, 0, rigidBodySlot)->velocity;
        VU0_v4scaleadd(&velocity, kOneThirtieth, &point, &point);
        WWorldPos ground = wheels[wheel];
        RTyreTrack_Add(track, 0, &point, wheelSlip[wheel], width, surface, &ground);
    }
}

// ---- the wheels' forces

// Each wheel's suspension, grip and drive as forces and torques on the body, from the wheel inputs the physics step
// made: the spring's push along the ground's normal (scaled on slopes), the drive along the wheel's heading, the
// sideways grip against the wheel's slip up to its friction limit (looser on ice and when sliding), the hand brake's
// drag, and rolling resistance; a wheel off the ground with the body spinning down onto it gets a push that stops
// the roll. Answers the wheels whose springs are compressed or were.
// FUNC_AT(0x000670e0)
int8_t PBondCar::AddWheelForces(BondCarWheelInput *input, float *slips, const Coord4 *headings,
                                const Coord4 *roadNormals, const Coord4 *wheelPos, const Coord4 *rollingResistance,
                                bool wheelSpin) {
    RigidBody *body = Simulation_GetRigidBody(Sim, 0, rigidBodySlot);
    BondCarScratch *step = BondCarScratchPad;
    step->forceInfo = body->info;
    step->wheelsOnGround = 0;
    step->liftAboveGround = 0.0f;
    step->heading = *AsCoord3(MatrixRow(&body->info->orientation, 2));
    step->heading.y = 0.0f;
    VU0_v4unitxyz(&step->heading, &step->heading);

    for (int wheel = 0; wheel < kCarWheels; wheel++) {
        BondCarWheelInput &in = input[wheel];
        const Coord4 *normal = &roadNormals[wheel];
        step->limited = 0;
        step->lever = wheelPos[wheel];

        // The spring
        float compression = physics->springRestLength + normal->w;
        step->compression = compression;
        if (gear > 0 && gearChangeTimer != 0 && wheel < 2 && twoWheelMode == 0 && numWheelsOnGround == 4 &&
            compression > 0.02f)
            step->compression = compression - 0.02f;
        if (!CallIsWheelOnGround(this, wheel))
            step->liftAboveGround = Max(step->liftAboveGround, step->compression);
        float limited = Min(step->compression, physics->springCompressionLimit);
        step->compression = limited;
        if (limited < 0.0f)
            step->compression = 0.0f;
        step->onGround = limited > 0.0f || suspensionCompression[wheel] > 0.0f;
        if (step->onGround)
            step->wheelsOnGround++;
        else if (twoWheelMode == 0)
            step->onGround = numWheelsOnGround > 0;

        if (step->onGround &&
            (double)step->forceInfo->orientation.mtx[1][1] * normal->y > BondCar_ENABLE_ROLL_STOPS_THRESHOLD) {
            step->force = Coord4{};
            if (step->compression > 0.0f) {
                double change = (double)step->compression - suspensionCompression[wheel];
                step->compressionChange = (float)change;
                float stiffness = wheel > 1 ? physics->springStiffnessRear : physics->springStiffnessFront;
                float damping = wheel > 1 ? physics->springDampingRear : physics->springDampingFront;
                step->springForce = (float)((double)stiffness * step->compression + change * damping);
                if (!CallIsWheelOnGround(this, wheel)) {
                    double landing = 5.0 * step->compression;
                    if (landing > unknown2EC)
                        unknown2EC = (float)landing;
                }
            } else {
                step->springForce = 0.0f;
            }

            // The wheel's frame on the ground, and the body's velocity at it
            VU0_v4unitcrossprodxyz(&headings[wheel / 2], normal, &step->axle);
            VU0_v4unitcrossprodxyz(normal, &step->axle, &step->rolling);
            VU0_v4crossprodxyz(normal, &step->rolling, &step->lateral);
            step->lateral.y = 0.0f;
            VU0_v4unitxyz(&step->lateral, &step->lateral);
            step->axle.z = 0.0f;
            step->axle.y = 0.0f;
            step->axle.x = 0.0f;
            VU0_v4sub(&step->lever, &body->position, &step->lever);
            VU0_v4crossprodxyz(&body->angularVelocity, &step->lever, &step->pointVelocity);
            VU0_v3add(&step->pointVelocity, &body->velocity, &step->pointVelocity);
            if (in.handbrake != 0.0f && wheel > 1) {
                VU0_v4unitxyz(&step->pointVelocity, &step->lateral);
                VU0_v3negate(&step->lateral);
            }
            if (physics->isRally != 0) {
                double ease = 5.0 - carSpeed;
                if (1.0 > ease)
                    ease = 1.0;
                in.frictionLimit = (float)(ease * in.frictionLimit);
                in.grip = (float)(ease * in.grip);
            }

            // Sideways grip, up to the friction limit
            double lateral = (double)v3dotprod(&step->pointVelocity, &step->lateral) * in.grip;
            step->lateralForce = (float)lateral;
            step->lateralForce = (float)(lateral * unknown2E8);
            uint8_t surface = wheels[wheel].face.corner[2].tag.type;
            if (surface == kICE) {
                if (in.drive < 0.0f || in.handbrake != 0.0f) {
                    in.drive = in.drive * 0.5f;
                    in.handbrake = in.handbrake * 0.5f;
                }
                in.frictionLimit = in.frictionLimit * 0.6f;
            }
            if (Abs(step->lateralForce) > in.frictionLimit) {
                step->lateralExcess = Abs(step->lateralForce) - in.frictionLimit;
                step->lateralForce = step->lateralForce < 0.0f ? -in.frictionLimit : in.frictionLimit;
                step->limited = 1;
            }
            if (wheelSpin && wheel > 1) {
                step->lateralForce = physics->wheelSpinScale * step->lateralForce;
                VU0_v4scale(&step->rolling, physics->wheelSpinScale, &step->rolling);
            }

            // The drive, while the wheel or the other on its axle is on the ground
            if (CallIsWheelOnGround(this, wheel) || CallIsWheelOnGround(this, wheel ^ 1)) {
                VU0_v4scale(&step->rolling, in.drive, &step->rolling);
                VU0_v3add(&step->force, &step->rolling, &step->force);
            }
            RigidBodyInfo *bodyInfo = body->info;
            if (bodyInfo->unknown4fd == 0 && bodyInfo->unknown4fe == 0)
                step->lever.y = physics->wheelForceAppScale * step->lever.y;
            else
                step->lever.y = 0.0f;
            if (step->limited && !wheelSpin) {
                double fraction = (double)step->lateralExcess / in.frictionLimit;
                if (!(fraction < 1.0))
                    fraction = 1.0;
                step->slipFraction = (float)fraction;
                double slipping = wheel < 2 ? physics->frontSlippingScale : physics->rearSlippingScale;
                step->lateralForce = (float)((1.0 - (1.0 - slipping) * fraction) * step->lateralForce);
            }
            VU0_v4scale(&step->lateral, step->lateralForce, &step->lateral);

            // The spring along the ground's normal, less on gentle slopes
            VU0_v4scale(normal, step->springForce, &step->axle);
            step->groundNormal.x = normal->x;
            step->groundNormal.y = 0.0f;
            step->groundNormal.z = normal->z;
            VU0_v4unitxyz(&step->groundNormal, &step->groundNormal);
            double slope = Abs(v3dotprod(&step->groundNormal, &step->heading));
            step->unknown430 = (float)slope;
            if (!(slope > 0.707f))
                slope *= 0.2f;
            if (0.05f > slope)
                slope = 0.05f;
            step->unknown430 = (float)slope;
            if (!(physics->isRally != 0 && carSpeed < 5.0f && in.handbrake != 0.0f)) {
                step->axle.x = (float)((double)physics->slopeScale * step->axle.x * slope);
                step->axle.z = (float)((double)physics->slopeScale * step->axle.z * slope);
            }
            if (in.handbrake != 0.0f) {
                VU0_v4unitxyz(&step->pointVelocity, &step->drag);
                VU0_v4scale(&step->drag, in.handbrake, &step->drag);
                VU0_v4sub(&step->axle, &step->drag, &step->axle);
            }
            if (in.handbrake == 0.0f || carSpeed > 1.0f)
                VU0_v4sub(&step->axle, &step->lateral, &step->axle);
            VU0_v4sub(&step->axle, rollingResistance, &step->axle);
            VU0_v3add(&step->force, &step->axle, &step->force);

            double ratio = (double)Abs(step->lateralForce) / in.frictionLimit;
            slips[wheel] = (float)(1.0 < ratio ? 1.0 : ratio);

            // Fast, the ground's lateral loss lets the car slide: the force across the body cut
            if (carSpeed > 10.0f) {
                float loss = BondCar_lateralLoss[surface];
                if (loss != 1.0f) {
                    if (wheel < 2)
                        loss = (float)((1.0 - loss) * 0.5 + loss);
                    if (Abs(step->lateralForce) > (double)loss * in.frictionLimit) {
                        body->ConvertWorldToLocal(&step->force);
                        double held = (double)loss * in.frictionLimit / Abs(step->lateralForce);
                        step->force.x = (float)(held * step->force.x);
                        body->ConvertLocalToWorld(Xyz(&step->force));
                    }
                }
            }
            VU0_v4crossprodxyz(&step->lever, &step->force, &step->torque);
            body->ResolveMassScaledForce4(&step->force);
            body->ResolveMassScaledTorque4(&step->torque);
        }

        if (step->compression == 0.0f) {
            if (twoWheelMode == 0 && numWheelsOnGround != 0 &&
                (double)step->forceInfo->orientation.mtx[1][1] * normal->y > BondCar_ENABLE_ROLL_STOPS_THRESHOLD &&
                inShock == 0 && body->info->unknown4fe == 0 && body->info->unknown4fd == 0) {
                step->lever = wheelPos[wheel];
                VU0_v4sub(&step->lever, &body->position, &step->lever);
                VU0_v4crossprodxyz(&body->angularVelocity, &step->lever, &step->spin);
                if (step->spin.y > 0.0f) {
                    step->force.x = 0.0f;
                    step->force.y = -50.0f;
                    step->force.z = 0.0f;
                    VU0_v4crossprodxyz(&step->lever, &step->force, &step->torque);
                    body->ResolveMassScaledTorque4(&step->torque);
                }
            }
            slips[wheel] = 0.0f;
        }
        suspensionCompression[wheel] = step->compression;
    }

    body->position.y = step->liftAboveGround + body->position.y;
    if (physics->yawStabilityFactor != 0.0f) {
        Coord3 local;
        double yaw;
        if (unknown2DA)
            yaw = (double)body->GetLocalAngularVelocity(&local)->y * physics->yawStabilityFactor * -8.0f;
        else
            yaw = -((double)body->GetLocalAngularVelocity(&local)->y * physics->yawStabilityFactor);
        step->axle.y = (float)yaw;
        step->axle.z = 0.0f;
        step->axle.x = 0.0f;
        body->ConvertLocalToWorld(Xyz(&step->axle));
        body->ResolveMassScaledTorque4(&step->axle);
    }
    return step->wheelsOnGround;
}

// ---- the physics steps

// A car's step with the full model: the speed and heading, the controls into the axles' shares of the grip
// (reversing, braking, accelerating, coasting, the hand brake), counter-steering, the boost, the gear's drive and
// the brakes, the wheels' speed and the steering, the wheels put on the ground and AddWheelForces' forces, then the
// wheels' spin and slip for drawing and sound, landing help, the two-wheel stunt and a hard landing's impact (the
// player's), and the landing's dust.
// FUNC_AT(0x00067bd0)
void PBondCar::ProcessPhysics() {
    RigidBody *body = Simulation_GetRigidBody(Sim, 0, rigidBodySlot);
    BondCarScratch *step = BondCarScratchPad;
    step->frontGrip = 0.5f;
    step->info = body->info;
    step->velocity = body->velocity;
    step->forward = *AsCoord3(MatrixRow(&body->info->orientation, 2));
    step->steeredForward = step->forward;
    step->handbrakeOn = 0;
    carSpeed = VU0_v3lengthxz(&body->velocity);
    unknown238 = 0.0f;
    if (twoWheelStuntTimer != 1.0f || unknown2B3 != 0) {
        RigidBodyInfo *info = body->info;
        Coord3 tensor;
        Util_GenerateCarTensor(&tensor, body->mass, info->halfExtents.x, info->halfExtents.y, info->halfExtents.z);
        body->inverseInertiaX = tensor.x;
        body->inverseInertiaY = tensor.y;
        body->inverseInertiaZ = tensor.z * twoWheelStuntTimer;
        unknown2B3 = 0;
    }
    step->velocity.y = 0.0f;
    step->forwardSpeed = v3dotprod(&step->velocity, &step->forward);
    VU0_v4scale(&step->velocity, BondCar_ROLLING_RESISTANCE, &step->rollingResistance);

    // Reversing: the brake held while nearly stopped, for a while
    unknown298 = 0;
    reversing = 0;
    if (control.brake != 0.0f) {
        if (step->forwardSpeed < 2.5f) {
            if (reverseTimer < 30) {
                reverseTimer++;
                if (step->forwardSpeed < 0.5f)
                    control.brake = 0.0f;
            } else if (!(step->forwardSpeed > 0.0f)) {
                step->frontGrip = physics->reverseFrwRatio;
                unknown2B1 = kDriveReversing;
                reversing = 1;
            }
        } else {
            reverseTimer = 0;
            unknown298 = 1;
        }
    } else {
        reverseTimer = 30;
    }
    if (!reversing) {
        if (control.brake > BondCar_MIN_BUTTON_VALUE) {
            step->frontGrip = physics->brakeFrwRatio;
            unknown2B1 = kDriveBraking;
        } else if (control.gas > BondCar_MIN_BUTTON_VALUE) {
            step->frontGrip = physics->accelFrwRatio;
            unknown2B1 = kDriveAccelerating;
        } else {
            step->frontGrip = physics->coastFrwRatio;
            unknown2B1 = kDriveCoasting;
        }
    }
    step->handbrake = 0.0f;
    step->rearGrip = 1.0f - step->frontGrip;

    if (control.handBrake) {
        if (carSpeed < 1.0f) {
            if (body->info->orientation.mtx[1][1] > 0.707f) {
                body->momentum.x = 0.0f;
                body->momentum.z = 0.0f;
                step->handbrake = 0.0f;
            } else {
                step->handbrake = (float)((double)carSpeed + carSpeed + 1.0);
            }
        } else {
            step->handbrake = physics->handbrakeForce;
            step->handbrakeOn = 1;
        }
        if (unknown29F == 0) {
            float share = physics->handbrakeFrwRatio;
            step->frontGrip = share;
            step->rearGrip = (float)((1.0 - share) * physics->handbrakeRwFricScale);
            step->frontGrip = physics->totalHandbrakeFrictionScale * step->frontGrip;
            step->rearGrip = physics->totalHandbrakeFrictionScale * step->rearGrip;
            unknown2B1 = kDriveHandbrake;
        } else {
            double share = physics->revHandbrakeFrwRatio * 0.5;
            step->frontGrip = (float)share;
            step->rearGrip = (float)((1.0 - share) * 0.5);
            step->frontGrip = physics->totalHandbrakeFrictionScale * step->frontGrip;
            step->rearGrip = physics->totalHandbrakeFrictionScale * step->rearGrip;
            unknown2B1 = kDriveReverseHandbrake;
        }
        if (carSpeed < 5.0f) {
            step->rearGrip = 0.5f;
            step->frontGrip = 0.5f;
        }
    }
    if (unknown2B1 == kDriveBraking) {
        step->frontGrip = physics->brakingFriction * step->frontGrip;
        step->rearGrip = physics->brakingFriction * step->rearGrip;
    }
    step->wheelSpin = unknown29B && !control.handBrake;
    if (unknown2B1 == kDriveAccelerating && carWheelSpeed < 0.0f) {
        if (physics->isRally) {
            double grip = (double)physics->brakingFriction + physics->brakingFriction;
            if (1.0 < grip)
                grip = 1.0;
            step->frontGrip = (float)(grip * step->frontGrip);
        }
        double grip = (double)physics->brakingFriction + physics->brakingFriction;
        if (1.0 < grip)
            grip = 1.0;
        step->rearGrip = (float)(grip * step->rearGrip);
        if (control.gas > 0.75f)
            step->wheelSpin = 1;
    }

    // Counter-steering: the spin against the steering widens the rear's friction limit
    unknown2DA = 0;
    step->counterSteer = 1.0f;
    if (physics->counterSteerScale != 0.0f) {
        if ((body->angularVelocity.y > 0.0f && control.steering < 0.0f) ||
            (body->angularVelocity.y < 0.0f && control.steering > 0.0f))
            unknown2DA = 1;
        if (unknown2DA) {
            double counter = (double)Abs(body->angularVelocity.y) * Abs(control.steering) * physics->counterSteerScale;
            step->counterSteer = (float)counter;
            if (unknown2B1 == kDriveAccelerating)
                step->counterSteer = (float)(counter * 1.2f);
            step->counterSteer = Max(Min(step->counterSteer, physics->counterSteerScaleLimit), 1.0f);
        }
    }

    // The boost
    step->maxAcc = physics->maxAcc;
    step->boostAcc = 0.0f;
    if (rocketBoost != 0) {
        int32_t boostTime = attributes.LookupInt("BOOST_TIME", NULL);
        rocketBoost--;
        int32_t boostEnd = boostTime * 3;
        if (rocketBoost > boostEnd)
            step->boostAcc = attributes.LookupFloat("BOOST_EXTRA_ACC", NULL);
        if (rocketBoost == boostEnd)
            CallGetAudio(this)->boost = 0;
    }

    // The wheels' speed, and the drive and brakes
    step->maxRevAcc = physics->maxRevAcc;
    float speedXZ = VU0_sqrt((float)((double)step->velocity.x * step->velocity.x +
                                     (double)step->velocity.z * step->velocity.z));
    step->wheelSpeed = speedXZ;
    double turns = speedXZ / ((double)SimStepsPerSecond * physics->wheelRadius * kUnitsPerTurn);
    step->wheelSpeed = (float)turns;
    if (turns < 0.01f && control.handBrake)
        step->wheelSpeed = 0.0f;
    if (!(step->wheelSpeed < 1.0f))
        step->wheelSpeed = 1.0f;
    if (reversing) {
        double over = (double)carSpeed - physics->speedCutoffReverse;
        step->unknown430 = (float)over;
        if (over > 0.0) {
            double revAcc = step->maxRevAcc - over * physics->speedCutoffRateReverse;
            step->maxRevAcc = (float)revAcc;
            if (revAcc < 0.0)
                step->maxRevAcc = 0.0f;
        }
    }
    if (gear != previousGear) {
        gearChangeTimer = 15;
        previousGear = gear;
    }
    double acc = (double)physics->gearRatio[gear] * step->maxAcc;
    step->maxAcc = (float)acc;
    double brakeAcc = reversing && rocketBoost == 0 ? step->maxRevAcc : physics->maxBrake;
    step->driveForce = (float)((acc * control.gas + step->boostAcc) - brakeAcc * control.brake);
    if (numWheelsOnGround != 0 && step->info->orientation.mtx[1][1] > 0.1f) {
        float spin = Min(step->wheelSpeed, BondCar_MAX_WHEEL_SPIN_RATE);
        carWheelSpeed = spin;
        if (step->forwardSpeed < 0.0f)
            carWheelSpeed = -spin;
    } else if (unknown298) {
        carWheelSpeed = carWheelSpeed * 0.25f;
    } else {
        carWheelSpeed = carWheelSpeed * 0.95f;
    }

    // The steering, less above MODSTEERSPEED (down to MINSTEER)
    step->scale42C = 1.0f;
    if (carSpeed > physics->modSteerSpeed) {
        double over = ((double)carSpeed - physics->modSteerSpeed) * 0.04f;
        if (1.0 < over)
            over = 1.0;
        step->scale42C = (float)(1.0 - over);
    }
    step->scale42C = Max(step->scale42C, physics->minSteer);
    double steer = (double)step->scale42C * physics->maxSteering * control.steering;
    float steerTurns = (float)steer;
    carSteer = (float)steer;
    VU0_MATRIX4setyrot(&step->steeringTurn, steerTurns);
    VU0_MATRIX4_mult(&step->steeredOrientation, &step->info->orientation, &step->steeringTurn);
    step->steeredForward = *AsCoord3(MatrixRow(&step->steeredOrientation, 2));
    if (againstWall && numWheelsOnGround == 4) {
        Coord3 turn = { 0.0f, (float)((double)body->mass * control.steering * control.gas * 0.5f), 0.0f };
        body->ModifyAngularMomentum(&turn);
    }

    // The wheels' inputs
    step->position = body->position;
    Coord4 converted;
    step->rollingResistance4 = *Float_COORD3toCOORD4(&converted, &step->rollingResistance);
    step->wheelSlip[3] = 0.0f;
    step->wheelSlip[2] = 0.0f;
    step->wheelSlip[1] = 0.0f;
    step->wheelSlip[0] = 0.0f;
    unknown2EC = 0.0f;
    *AsCoord3(&step->wheelHeading[0]) = step->steeredForward;
    *AsCoord3(&step->wheelHeading[1]) = step->forward;
    double rearLimit;
    if (rocketBoost != 0) {
        int32_t boostLeft = rocketBoost;
        int32_t boostEnd = attributes.LookupInt("BOOST_TIME", NULL) * 3;
        if (boostLeft > boostEnd) {
            step->boostGrip = 3.0f;
        } else {
            double fraction = (double)boostLeft / boostEnd;
            step->boostGrip = (float)(fraction + fraction + 1.0);
        }
        float frontLimit = (float)((double)physics->frictionLimitFront * step->boostGrip);
        step->wheelInput[1].frictionLimit = frontLimit;
        step->wheelInput[0].frictionLimit = frontLimit;
        rearLimit = (double)physics->frictionLimitRear * step->boostGrip;
    } else {
        step->wheelInput[1].frictionLimit = physics->frictionLimitFront;
        step->wheelInput[0].frictionLimit = physics->frictionLimitFront;
        rearLimit = physics->frictionLimitRear;
    }
    float rear = (float)(rearLimit * step->counterSteer);
    step->wheelInput[3].frictionLimit = rear;
    step->wheelInput[2].frictionLimit = rear;

    for (int wheel = 0; wheel < kCarWheels; wheel++) {
        BondCarWheelInput &in = step->wheelInput[wheel];
        wheelPos[wheel] = body->info->worldLevers[wheel];
        VU0_v3add(&wheelPos[wheel], &step->position, &wheelPos[wheel]);
        body->TempGetHeightInformation(false, Xyz(&wheelPos[wheel]), &wheelRoadNormal[wheel], &wheels[wheel]);
        if (wheel < 2) {
            in.grip = physics->tyreGripFactor * step->frontGrip;
            in.drive = !(step->driveForce >= 0.0f) || physics->isRally != 0 ? step->driveForce : 0.0f;
        } else {
            in.grip = (float)((double)physics->tyreGripFactor * step->counterSteer * step->rearGrip);
            if (step->wheelSpin)
                in.drive = physics->wsAccel * step->driveForce;
            else
                in.drive = step->driveForce >= 0.0f ? step->driveForce : 0.0f;
        }
        double drive = (double)in.drive * in.grip;
        in.drive = (float)drive;
        in.handbrake = step->handbrake;
        float friction = BondCar_friction[wheels[wheel].face.corner[2].tag.type];
        in.frictionLimit = friction * in.frictionLimit;
        in.drive = (float)(drive * friction);
        if (oilSlick && wheel >= 2) {
            in.frictionLimit = in.frictionLimit * 0.1f;
            in.drive = in.drive * 0.75f;
        }
    }

    // Grip fading above FRIC_MOD_SPEED, down to FRIC_MOD_MIN
    if (carSpeed > physics->fricModSpeed) {
        double over = (double)carSpeed - physics->fricModSpeed;
        step->scale42C = (float)over;
        if (over > physics->fricModRange)
            step->scale42C = physics->fricModRange;
        double friction = 1.0 - (double)step->scale42C / physics->fricModRange;
        step->scale42C = (float)friction;
        if (friction < physics->fricModMin)
            step->scale42C = physics->fricModMin;
    } else {
        step->scale42C = 1.0f;
    }
    unknown2E8 = step->scale42C;
    numWheelsOnGround = AddWheelForces(step->wheelInput, step->wheelSlip, step->wheelHeading, wheelRoadNormal, wheelPos,
                                       &step->rollingResistance4, step->wheelSpin);

    // On the hand brake and nearly still: stopped, but for falling
    if (numWheelsOnGround == 4 && control.handBrake && control.gas < 0.1f && VU0_v3length(&body->velocity) < 0.08f) {
        body->velocity.x = 0.0f;
        body->velocity.z = 0.0f;
        Coord4 momentum = { 0.0f, body->velocity.y, 0.0f, 0.0f };
        VU0_v4scale(&momentum, body->mass, &momentum);
        body->momentum = *Xyz(&momentum);
        body->force.x = 0.0f;
        body->force.z = 0.0f;
        body->torque.y = 0.0f;
        body->SetAngularMomentum(&ZeroVector);
    }

    for (int wheel = 0; wheel < kCarWheels; wheel++)
        wheelPos[wheel].y = (float)((double)wheelRoadNormal[wheel].w + wheelPos[wheel].y + 0.01f);
    for (int wheel = 0; wheel < kCarWheels; wheel++) {
        double slip = ((double)step->wheelSlip[wheel] - kSlipBase) * kSlipScale;
        wheelSlip[wheel] = (float)slip;
        if (slip < 0.0)
            wheelSlip[wheel] = 0.0f;
    }

    if (carClass == kPlayerCar) {
        RigidBodyInfo *info = body->info;
        if (info->unknown4fd == 0 && info->unknown4de > kLandingAirSteps && numWheelsOnGround == 0 &&
            body->groundContacts == 0)
            ImproveLanding();
        if (twoWheelMode)
            TwoWheelStunt();
        if (unknown2EC > 0.01f) {
            float strength = unknown2EC < 1.0f ? unknown2EC : 1.0f;
            unknown2EC = strength;
            RaiseLandingImpact(body, &step->position, strength);
        }
    }

    // The wheels' spin
    if (!unknown298 || control.brake <= 0.5f) {
        if (step->wheelSpin) {
            wheelSpinAngle[0] = (float)((double)carWheelSpeed * 0.1f + wheelSpinAngle[0]);
        } else {
            wheelSpinAngle[0] = wheelSpinAngle[0] + carWheelSpeed;
            if (!control.handBrake)
                wheelSpinAngle[1] = wheelSpinAngle[1] + carWheelSpeed;
        }
    }
    if (step->wheelSpin)
        wheelSpinAngle[1] = BondCar_MAX_WHEEL_SPIN_RATE + wheelSpinAngle[1];
    wheelSpinAngle[0] = WrapTurn(wheelSpinAngle[0]);
    wheelSpinAngle[1] = WrapTurn(wheelSpinAngle[1]);
    if (step->wheelSpin) {
        wheelSlip[3] = 1.0f;
        wheelSlip[2] = 1.0f;
        unknown238 = 1.0f;
    }

    // Landing
    if (numWheelsOnGround != 0 && unknown3FD == 0 && landingFlag > 15) {
        unknown3FD = landingFlag >> 1;
        Coord4 centre = {};
        for (int wheel = 0; wheel < kCarWheels; wheel++)
            VU0_v4scaleadd4(&wheelPos[wheel], 0.25f, &centre, &centre);
        centre.y = centre.y + 0.2f;
        TriggerFX(renderObject, kFXType6, kFXLandingWhich, &centre, &step->velocity, -1.0f);
    }
    if (unknown3FD) {
        unknown3FD--;
        unknown3FE = 1;
        wheelSlip[3] = 0.95f;
        wheelSlip[2] = 0.95f;
        wheelSlip[1] = 0.95f;
        wheelSlip[0] = 0.95f;
    } else {
        unknown3FE = 0;
    }
    if (step->handbrakeOn && carSpeed > 0.5f) {
        unknown3FE = 1;
        wheelSlip[3] = 1.0f;
        wheelSlip[2] = 1.0f;
    }
    if (control.brake > 0.33f && !reversing) {
        float half = control.brake * 0.5f;
        unknown3FE = 1;
        for (int wheel = 0; wheel < kCarWheels; wheel++)
            wheelSlip[wheel] = Max(half, wheelSlip[wheel]);
    }
    if (numWheelsOnGround)
        landingFlag = 0;
    else if (landingFlag < 255)
        landingFlag++;
    if (gearChangeTimer)
        gearChangeTimer--;
}

// The simpler wheel forces of the AI's cars: the spring, the drive along the wheel's heading and the sideways grip
// clamped to the friction limit, the hand brake's drag; a wheel off the ground with the body spinning down onto it
// gets a push that stops the roll. Nothing for a boat with no hit points left. Answers the wheels whose springs are
// compressed.
// FUNC_AT(0x000690a0)
int8_t PBondCar::AddSimpleWheelForces(BondCarWheelInput *input, const Coord4 *headings, const Coord4 *roadNormals,
                                      const Coord4 *wheelPos, float *slips) {
    RigidBody *body = Simulation_GetRigidBody(Sim, 0, rigidBodySlot);
    BondCarScratch *step = BondCarScratchPad;
    step->forceInfo = body->info;
    step->wheelsOnGround = 0;
    step->liftAboveGround = 0.0f;
    if (GetHitPoints() <= 0.0f && physics->isBoat != 0)
        return 0;

    for (int wheel = 0; wheel < kCarWheels; wheel++) {
        BondCarWheelInput &in = input[wheel];
        const Coord4 *normal = &roadNormals[wheel];
        step->lever = wheelPos[wheel];
        step->compression = physics->springRestLength + normal->w;
        if (!CallIsWheelOnGround(this, wheel))
            step->liftAboveGround = Max(step->liftAboveGround, step->compression);
        float limited = Min(step->compression, physics->springCompressionLimit);
        step->compression = limited;
        if (limited < 0.0f)
            step->compression = 0.0f;

        if ((limited > 0.0f || suspensionCompression[wheel] > 0.0f) &&
            (double)step->forceInfo->orientation.mtx[1][1] * normal->y > BondCar_ENABLE_ROLL_STOPS_THRESHOLD) {
            if (step->compression > 0.0f) {
                step->wheelsOnGround++;
                double change = (double)step->compression - suspensionCompression[wheel];
                step->compressionChange = (float)change;
                float stiffness = wheel > 1 ? physics->springStiffnessRear : physics->springStiffnessFront;
                float damping = wheel > 1 ? physics->springDampingRear : physics->springDampingFront;
                step->springForce = (float)(change * damping + (double)stiffness * step->compression);
                if (!CallIsWheelOnGround(this, wheel)) {
                    double landing = 5.0 * step->compression;
                    if (landing > unknown2EC)
                        unknown2EC = (float)landing;
                }
            } else {
                step->springForce = 0.0f;
            }

            VU0_v4unitcrossprodxyz(&headings[wheel / 2], normal, &step->axle);
            VU0_v4unitcrossprodxyz(normal, &step->axle, &step->rolling);
            VU0_v4crossprodxyz(normal, &step->rolling, &step->lateral);
            VU0_v4sub(&step->lever, &body->position, &step->lever);
            VU0_v4crossprodxyz(&body->angularVelocity, &step->lever, &step->pointVelocity);
            VU0_v3add(&step->pointVelocity, &body->velocity, &step->pointVelocity);

            // Sideways grip within the friction limit, and the slip
            double lateral = (double)v3dotprod(&step->pointVelocity, &step->lateral) * in.grip;
            step->lateralForce = (float)lateral;
            float limit = in.frictionLimit;
            float held = -limit < lateral ? (float)lateral : -limit;
            step->lateralForce = held;
            held = held < limit ? held : limit;
            step->lateralForce = held;
            double ratio = AbsDouble((double)held + held) / limit;
            slips[wheel] = (float)(ratio < 1.0 ? ratio : 1.0);
            VU0_v4scale(&step->lateral, step->lateralForce, &step->lateral);
            VU0_v4scale(&step->rolling, in.drive, &step->force);
            RigidBodyInfo *bodyInfo = body->info;
            if (bodyInfo->unknown4fd == 0 && bodyInfo->unknown4fe == 0)
                step->lever.y = physics->wheelForceAppScale * step->lever.y;
            else
                step->lever.y = 0.0f;

            VU0_v4scale(normal, step->springForce, &step->axle);
            step->axle.x = physics->slopeScale * step->axle.x;
            step->axle.z = physics->slopeScale * step->axle.z;
            if (in.handbrake != 0.0f) {
                VU0_v4unitxyz(&step->pointVelocity, &step->drag);
                VU0_v4scale(&step->drag, in.handbrake, &step->drag);
                VU0_v4sub(&step->axle, &step->drag, &step->axle);
            }
            if (in.handbrake == 0.0f || carSpeed > 1.0f)
                VU0_v4sub(&step->axle, &step->lateral, &step->axle);
            VU0_v3add(&step->force, &step->axle, &step->force);
            VU0_v4crossprodxyz(&step->lever, &step->force, &step->torque);
            body->ResolveMassScaledForce4(&step->force);
            body->ResolveMassScaledTorque4(&step->torque);
        }

        if (step->compression == 0.0f) {
            if (!CallIsTyreShredded(this, wheel) && twoWheelMode == 0 && numWheelsOnGround != 0 &&
                (double)step->forceInfo->orientation.mtx[1][1] * normal->y > BondCar_ENABLE_ROLL_STOPS_THRESHOLD) {
                bool class2Dead = carClass == kCarClass2 && GetHitPoints() <= 0.0f;
                if (carClass == kCarClass1 ||
                    (!class2Dead && inShock == 0 && body->info->unknown4fe == 0 && (body->flags & RigidBody::kFlag0) &&
                     body->info->unknown4fd == 0)) {
                    step->lever = wheelPos[wheel];
                    VU0_v4sub(&step->lever, &body->position, &step->lever);
                    VU0_v4crossprodxyz(&body->angularVelocity, &step->lever, &step->spin);
                    if (step->spin.y > 0.0f) {
                        step->force.x = 0.0f;
                        step->force.y = -25.0f;
                        step->force.z = 0.0f;
                        VU0_v4crossprodxyz(&step->lever, &step->force, &step->torque);
                        body->ResolveMassScaledTorque4(&step->torque);
                    }
                }
            }
            slips[wheel] = 0.0f;
        }
        suspensionCompression[wheel] = step->compression;
    }

    body->position.y = step->liftAboveGround + body->position.y;
    return step->wheelsOnGround;
}

// A car's step with the simpler model (the AI's cars): a boat bobs on its waves, the controls into the axles' shares
// (with the hand brake, in shock, by class), the wheels' speed and the drive, the steering pulled by shredded tyres,
// the wheels put on the ground (a shredded one dragging and lower; a class 3 car off the ground copies its front
// wheels' ground to the rear), AddSimpleWheelForces' forces, the wheels' slip and spin, landing help and a hard
// landing's impact.
// FUNC_AT(0x00069710)
void PBondCar::ProcessSimplePhysics() {
    RigidBody *body = Simulation_GetRigidBody(Sim, 0, rigidBodySlot);
    BondCarScratch *step = BondCarScratchPad;
    step->info = body->info;
    step->velocity = body->velocity;
    step->forward = *AsCoord3(MatrixRow(&body->info->orientation, 2));
    step->steeredForward = step->forward;
    if (control.gas > control.brake)
        control.brake = 0.0f;
    else
        control.gas = 0.0f;

    if (physics->isBoat != 0 && OffSplinePath(this)) {
        body->position.y = bob + body->position.y;
        float waveB = sin_fractionalangle(unknown3E8);
        bob = (float)((SineTurns(bobAngle) + waveB + 1.0) * 0.1f + 1.0);
        double angle = (double)bobAngle + 0.0022f;
        bobAngle = (float)angle;
        if (angle > 1.0)
            bobAngle = (float)(angle - 1.0);
        angle = (double)unknown3E8 + 0.0035f;
        unknown3E8 = (float)angle;
        if (angle > 1.0)
            unknown3E8 = (float)(angle - 1.0);
    }

    unknown238 = 0.0f;
    carSpeed = VU0_v3lengthxz(&body->velocity);
    step->velocity.y = 0.0f;
    step->forwardSpeed = v3dotprod(&step->velocity, &step->forward);
    if (control.handBrake) {
        if (carSpeed > 5.0f) {
            step->frontGrip = 0.8f;
            step->rearGrip = 0.2f;
        } else {
            step->rearGrip = 0.5f;
            step->frontGrip = 0.5f;
        }
        control.brake = 0.0f;
        control.gas = 0.0f;
    } else {
        step->frontGrip = 0.3f;
        step->rearGrip = 0.7f;
    }
    if (!inShock) {
        step->frontGrip = step->frontGrip * 5.0f;
        step->rearGrip = step->rearGrip * 5.0f;
        if (CallGetCarClass(this) == kCarClass1 || CallGetCarClass(this) == kCarClass2) {
            step->frontGrip = step->frontGrip * 1.5f;
            step->rearGrip = step->rearGrip * 1.5f;
        }
    }
    unknown298 = control.brake > 0.05f;
    if (control.handBrake) {
        if (carSpeed < 1.0f)
            step->handbrake = (float)((double)carSpeed + carSpeed + 1.0);
        else
            step->handbrake = (float)(((double)carSpeed * 0.075f + 1.75f) * physics->handbrakeForce);
    } else {
        step->handbrake = 0.0f;
    }

    // The wheels' speed, the drive and the steering
    float speedXZ = VU0_sqrt((float)((double)step->velocity.x * step->velocity.x +
                                     (double)step->velocity.z * step->velocity.z));
    step->wheelSpeed = speedXZ;
    double turns = speedXZ / ((double)SimStepsPerSecond * physics->wheelRadius * kUnitsPerTurn);
    step->wheelSpeed = (float)turns;
    if (turns < 0.01f && control.handBrake)
        step->wheelSpeed = 0.0f;
    if (!(step->wheelSpeed < 1.0f))
        step->wheelSpeed = 1.0f;
    float maxAcc = 12.5f > physics->maxAcc ? 12.5f : physics->maxAcc;
    float maxBrake = 12.5f > physics->maxBrake ? 12.5f : physics->maxBrake;
    step->driveForce = (float)((double)maxAcc * control.gas - (double)maxBrake * control.brake);
    if (numWheelsOnGround != 0 && step->info->orientation.mtx[1][1] > 0.1f) {
        float spin = Min(step->wheelSpeed, BondCar_MAX_WHEEL_SPIN_RATE_AI);
        carWheelSpeed = spin;
        if (step->forwardSpeed < 0.0f)
            carWheelSpeed = -spin;
    } else {
        carWheelSpeed = carWheelSpeed * 0.95f;
    }
    float maxSteering = 0.2f > physics->maxSteering ? 0.2f : physics->maxSteering;
    carSteer = (float)((double)maxSteering * control.steering);
    step->steeringInput = control.steering;
    if (tyreDamagePoints[0] == 0)
        step->steeringInput = control.steering - 1.5f;
    if (tyreDamagePoints[1] == 0)
        step->steeringInput = step->steeringInput + 1.5f;
    if (tyreDamagePoints[2] == 0)
        step->steeringInput = step->steeringInput - 0.5f;
    if (tyreDamagePoints[3] == 0)
        step->steeringInput = step->steeringInput + 0.5f;
    float steering = -1.0f > step->steeringInput ? -1.0f : step->steeringInput;
    step->steeringInput = steering;
    steering = 1.0f < steering ? 1.0f : steering;
    step->steeringInput = steering;
    maxSteering = 0.2f > physics->maxSteering ? 0.2f : physics->maxSteering;
    VU0_MATRIX4setyrot(&step->steeringTurn, maxSteering * steering);
    VU0_MATRIX4_mult(&step->steeredOrientation, &step->info->orientation, &step->steeringTurn);
    step->steeredForward = *AsCoord3(MatrixRow(&step->steeredOrientation, 2));

    // The wheels' inputs
    step->position = body->position;
    Coord4 converted;
    // rollingResistance is not set in this step (the stack as it was), and the copy is not read
    step->rollingResistance4 = *Float_COORD3toCOORD4(&converted, &step->rollingResistance);
    unknown2EC = 0.0f;
    step->wheelInput[1].grip = step->frontGrip;
    step->wheelInput[0].grip = step->frontGrip;
    *AsCoord3(&step->wheelHeading[0]) = step->steeredForward;
    step->wheelInput[3].grip = step->rearGrip;
    step->wheelInput[2].grip = step->rearGrip;
    step->wheelInput[3].drive = step->driveForce;
    step->wheelInput[2].drive = step->driveForce;
    *AsCoord3(&step->wheelHeading[1]) = step->forward;
    step->wheelInput[1].drive = 0.0f;
    step->wheelInput[0].drive = 0.0f;
    step->wheelInput[3].frictionLimit = 20.0f;
    step->wheelInput[2].frictionLimit = 20.0f;
    step->wheelInput[1].frictionLimit = 20.0f;
    step->wheelInput[0].frictionLimit = 20.0f;
    if (oilSlick) {
        step->wheelInput[3].frictionLimit = 2.0f;
        step->wheelInput[2].frictionLimit = 2.0f;
        float slick = (float)((double)step->driveForce * 0.75f);
        step->wheelInput[3].drive = slick;
        step->wheelInput[2].drive = slick;
    }
    step->shreddedTyres = 0;
    for (int wheel = 0; wheel < kCarWheels; wheel++)
        if (tyreDamagePoints[wheel] == 0)
            step->shreddedTyres++;
    step->shreddedDrag = step->shreddedTyres == 1 ? 0.5f : 1.0f;

    float tyreRadius = -1.0f;
    for (int wheel = 0; wheel < kCarWheels; wheel++) {
        BondCarWheelInput &in = step->wheelInput[wheel];
        in.handbrake = step->handbrake;
        wheelPos[wheel] = body->info->worldLevers[wheel];
        if (carClass != kCarClass1 && step->shreddedTyres) {
            // A shredded tyre's wheel sits lower (when the car is upright), and the car drags on the others
            if (tyreDamagePoints[wheel] == 0 && step->info->orientation.mtx[1][1] > 0.707f) {
                if (tyreRadius < 0.0f)
                    tyreRadius = attributes.LookupFloat("TYRE_RADIUS", NULL);
                wheelPos[wheel].y = tyreRadius + wheelPos[wheel].y;
            }
            in.drive = (float)((1.0 - step->shreddedDrag) * in.drive);
            in.handbrake = step->shreddedDrag;
        }
        VU0_v3add(&wheelPos[wheel], &step->position, &wheelPos[wheel]);
        if (carClass == kCarClass3 && !(body->flags & RigidBody::kFlag0) && (wheel == 1 || wheel == 3)) {
            double rise = (double)wheelPos[wheel - 1].y - wheelPos[wheel].y;
            wheels[wheel] = wheels[wheel - 1];
            wheelRoadNormal[wheel] = wheelRoadNormal[wheel - 1];
            wheelRoadNormal[wheel].w = (float)(rise + wheelRoadNormal[wheel].w);
        } else {
            body->TempGetHeightInformation(false, Xyz(&wheelPos[wheel]), &wheelRoadNormal[wheel], &wheels[wheel]);
        }
    }
    step->wheelSlip[3] = 0.0f;
    step->wheelSlip[2] = 0.0f;
    step->wheelSlip[1] = 0.0f;
    step->wheelSlip[0] = 0.0f;
    numWheelsOnGround = AddSimpleWheelForces(step->wheelInput, step->wheelHeading, wheelRoadNormal, wheelPos,
                                             step->wheelSlip);
    for (int wheel = 0; wheel < kCarWheels; wheel++) {
        wheelPos[wheel].y = (float)((double)wheelRoadNormal[wheel].w + wheelPos[wheel].y + 0.01f);
        double slip = ((double)step->wheelSlip[wheel] - kSimpleSlipBase) * kSimpleSlipScale;
        wheelSlip[wheel] = (float)slip;
        if (slip < 0.0)
            wheelSlip[wheel] = 0.0f;
    }

    // Landing
    if (numWheelsOnGround != 0 && unknown3FD == 0 && landingFlag > 10)
        unknown3FD = landingFlag;
    if (unknown3FD) {
        unknown3FE = 1;
        unknown3FD--;
        float slip = CRT_stricmp(carType, "750") == 0 ? 1.75f : 0.95f;
        wheelSlip[3] = slip;
        wheelSlip[2] = slip;
        wheelSlip[1] = slip;
        wheelSlip[0] = slip;
    } else {
        unknown3FE = 0;
    }
    if (numWheelsOnGround)
        landingFlag = 0;
    else if (landingFlag < 255)
        landingFlag++;

    // The wheels' spin: the first wrapped from its unrounded sum, the second from its float
    double spinA = (double)carWheelSpeed + wheelSpinAngle[0];
    wheelSpinAngle[0] = (float)spinA;
    float spinB = (float)((double)carWheelSpeed + wheelSpinAngle[1]);
    wheelSpinAngle[1] = spinB;
    if (spinA > 1.0 || spinA < 0.0)
        wheelSpinAngle[0] = WrapTurn(spinA);
    if (spinB > 1.0f || spinB < 0.0f)
        wheelSpinAngle[1] = WrapTurn(spinB);

    if (carClass == kCarClass1 || (GetHitPoints() > 0.0f && carClass == kCarClass2)) {
        RigidBodyInfo *info = body->info;
        if (info->unknown4fd == 0 && info->unknown4de > kLandingAirSteps && numWheelsOnGround == 0 &&
            body->groundContacts == 0)
            ImproveLanding();
    }
    if (unknown2EC > 0.01f) {
        float strength = unknown2EC < 1.0f ? unknown2EC : 1.0f;
        unknown2EC = strength;
        RaiseLandingImpact(body, &step->position, strength);
    }
    if (physics->isBoat != 0 && OffSplinePath(this))
        body->position.y = -bob + body->position.y;
}

// ---- the model

// The car made another type where it stands: the attributes renamed, the scene object replaced by a new RVehicle
// of the type's model (with purgeModel the old model's preloaded file - the player's car type's - purged), a new
// engine sound, the variables and damage reset, and the body made again from the new MASS and the model's size,
// facing the reset direction. Nothing if the type is the car's already.
// FUNC_AT(0x0006a460)
void PBondCar::ChangeCarType(const char *newType, bool purgeModel) {
    static const char kModelDirectory[] = "data\\car\\model\\";
    if (CRT_stricmp(carType, newType) == 0)
        return;
    Coord3 position = Simulation_GetRigidBody(Sim, 0, rigidBodySlot)->position;
    AttributeSet playerAttributes;
    const char *oldModel =
        PVehicle::RenderNameAttrib(PVehicle::GetNamedAttribs(&playerAttributes, CallGetCarType(*playerPhysicsObject)));
    playerAttributes.Destruct();
    attributes.SetName(newType, false);
    carType = newType;

    RSceneObj *oldRender = renderObject;
    if (oldRender != NULL)
        oldRender->animHandle->Stop();
    SetRenderObject(NULL);
    if (oldRender != NULL)
        oldRender->DeleteVirtual(1);
    if (purgeModel)
        RSceneObj::PurgePreloaded(kModelDirectory, oldModel);
    physics = PVehicle::CarPhysicsAttrib(&attributes);
    void *block = UMemory::FastAlloc(kRVehicleSize, "RVehicle");
    SetRenderObject(block != NULL ? RVehicle_Construct(block, 0, this) : NULL);
    RSceneObj *render = renderObject;
    uint32_t variation = GetCarColourVariation(carType, carColour);
    const char *label = attributes.Name();
    const char *file = PVehicle::RenderNameAttrib(&attributes);
    render->Load(kModelDirectory, file, label, variation);
    renderObject->renderTypeIndex = PVehicle::NameToIndex(carType);
    Coord4 halfExtents = {};
    renderObject->GetBoundingDimensions(&halfExtents);

    const char *engineSound = attributes.LookupString("SFX_ENGINE", NULL);
    ABaseSound *oldSound = (this->*XbeVirtual<decltype(&PBondCar::GetAudio)>(this, PVehicle::kGetAudio))();
    if (oldSound != NULL)
        oldSound->CallDelete(1);
    SetAudioObject(NULL);
    void *soundBlock = ABaseSound::OperatorNew(kAPlayerVehicleSize, "APlayerVehicle");
    ABaseSound *sound = soundBlock != NULL ? APlayerVehicle_Construct(soundBlock, 0, engineSound) : NULL;
    CallSetAudio(this, sound);
    SetAudioObject(sound);
    sound->volume = attributes.LookupFloat("SFX_VOLUME", NULL);
    sound->minDistance = 15.0f;
    sound->maxDistance = 500.0f;
    sound->maxDistanceSq = 250000.0f;

    control.firePrimary = 0;
    InitializeCarVariables(false);
    CallResetDamage(this);
    MATRIX4 basis;
    MATRIX4 orientation = *Util_GenerateMatrix(&basis, &resetDirection);
    int kind;
    switch (carClass) {
    case kPlayerCar:
    case kCarClass1:
        kind = 1;
        break;
    case kCarClass3:
        kind = 3;
        break;
    default:
        kind = 2;
        break;
    }
    Coord3 velocity = { 0.0f, 0.0f, 0.0f };
    Coord3 inverseInertia;
    Util_GenerateCarTensor(&inverseInertia, attributes.LookupFloat("MASS", NULL), halfExtents.x, halfExtents.y,
                           halfExtents.z);
    RigidBody *body = Simulation_GetRigidBody(Sim, 0, rigidBodySlot);
    if (body != NULL)
        body->Construct(int8_t(rigidBodySlot), int8_t(kind), &position, &velocity, &ZeroVector, &orientation,
                        attributes.LookupFloat("MASS", NULL), &inverseInertia, &halfExtents, this, true, false);
}

// FUNC_AT(0x0006a810)
PBondCar* PBondCar::Delete(unsigned flags) {
    Destruct();
    if (flags & 1)
        UMemory::FastFree(this, kBondCarSize);
    return this;
}
