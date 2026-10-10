#pragma fp_contract(off)

#include "BondCarState.h"

#include <bit>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "AIVehicle.h"
#include "BondCar.h"
#include "../../common/xbeOverload.h"       // XbeVirtual
#include "../../helpers.h"
#include "VehicleSound.h"                   // AVehicle
#include "../audio/Sound.h"                 // AMenuSound
#include "../camera/PlayerCamera.h"         // RPlayerCamera
#include "../data/DebugVariables.h"         // dbattrib_float
#include "../data/Tuning.h"                 // TuningDBMgr
#include "../engine/ActionQueue.hpp"
#include "../engine/CoreFoundation.h"       // NullFunction
#include "../engine/GameLoop.h"             // LaunchPage
#include "../engine/MissionManager.h"
#include "../engine/PhysicsUtil.h"          // Util_GenerateMatrix
#include "../engine/SimRandom.h"
#include "../engine/UMemory.hpp"
#include "../physics/RigidBody.h"
#include "../platform/RealMath.h"
#include "../platform/X87.h"
#include "../render/RenderHigh.h"           // fgRenderHigh
#include "../render/RSceneObj.hpp"
#include "../world/CollisionManager.h"      // fgCollisionMgr
#include "../world/Targeting.h"             // WTargetable, TargetPicker

// ---------------------------------------------------------------------------------------------------------------
// PBondCar's set-up and state (0x00061a50-0x00062fe0), ported from the listing.
// ---------------------------------------------------------------------------------------------------------------

// ---- the game's code not ported yet
#define RTyreTrack_Construct ((RTyreTrack *(__fastcall *)(void *, int, float width, bool longTrack, bool unknown))0x000abfc0)
#define RVehicle_SetEMPVictimEffect ((void (__fastcall *)(RSceneObj *, int, bool on))0x000959d0)
#define RVehicle_TriggerTurboBoostSmoke ((void (__fastcall *)(RSceneObj *, int, int steps))0x000959f0)
#define RVehicle_TriggerEmpBolts ((void (__fastcall *)(RSceneObj *, int))0x00095a10)
#define SMissionManager_ProgrammerDefinedEvent ((void (__fastcall *)(void *, int, int event, const char *text))0x000b72f0)
#define SMissionManager_IncShotsHit ((void (__fastcall *)(void *, int, int unknown))0x000b6720)
#define CRT_stricmp ((int (*)(const char *, const char *))0x00134537)

// ---- globals
#define Launch (*(LaunchPage *)0x00243b90)
#define SimulationRandom (*(SimRandom **)0x00233ff0)   // the Simulation's first word
#define SimStepCount U32_AT(0x00234e34)                 // the Simulation's steps so far
#define ZeroVector (*(const Coord3 *)0x00243030)        // the game's zero vector, never written

namespace {

// The modes of the turn signal (turnSignalMode)
enum : int32_t {
    kSignalOff = 0,
    kSignal1 = 1,                                   // glare 3
    kSignal2 = 2,                                   // glare 4
};

// RVehicle's TriggerFX types past RSceneObj's (GlareOn's and GlareOff's)
enum : int {
    kFXGlareOn = 11,
    kFXGlareOff = 12,
};

constexpr float kSnowmobileTrackScale = 9.0f;       // the third track's width
constexpr float kResetCompression = 0.15f;          // of SPRING_COMPRESSION_LIMIT, each wheel's at a reset
constexpr float kResetLift = 1.0f;                  // without ground under the car
constexpr float kWheelsAverage = 0.25f;
constexpr float kBobAngleScale = 1.0f / 65536.0f;   // SimRandom's 0-65535 to 0..1
constexpr float kHitPointsAtReset = 700.0f;
constexpr float kPlayerDamageEvent = 499.0f;
constexpr float kMaxShock = 1.0f;
constexpr int kShockSteps3 = 60;                    // per unit of shock, by class
constexpr int kShockSteps2 = 7;
constexpr int kShockSteps = 15;
constexpr int kTopGear = 5;
constexpr float kSubRpmScale = 100.0f;
constexpr float kAirRpm = 7000.0f;
constexpr float kRpmScale = 6000.0f;
constexpr float kGear0RpmScale = 0.05f;
constexpr int32_t kLaserSteps = 120;
constexpr int kBoostLimit = 3;                      // BOOST_TIMEs left that still allow a boost
constexpr int kBoostSteps = 4;                      // BOOST_TIMEs of boost
constexpr int kSmokeWheels = 4;
constexpr int kMissionEvent0 = 0;                   // AttackWithEmp's, on "paris_bombvan"
constexpr int kMissionEvent2 = 2;                   // AddDamageByPlayer's
constexpr char kBombVan[] = "paris_bombvan";
static_assert(std::bit_cast<uint32_t>(kResetCompression) == 0x3e19999a, "the original's 0.15");
static_assert(std::bit_cast<uint32_t>(kGear0RpmScale) == 0x3d4ccccd, "the original's 0.05");
static_assert(std::bit_cast<uint32_t>(kBobAngleScale) == 0x37800000, "the original's 1/65536");

// ---- the car's virtual methods, through its vtable (the slot in BondCar.h)

void InitializeCarControlsVirtual(PBondCar *car) {
    (car->*XbeVirtual<decltype(&PBondCar::InitializeCarControls)>(car, 21))();
}

bool IsTyreShreddedVirtual(PBondCar *car, int8_t wheel) {
    return (car->*XbeVirtual<decltype(&PBondCar::IsTyreShredded)>(car, 40))(wheel);
}

void GlareOnVirtual(PBondCar *car, int glare) {
    (car->*XbeVirtual<decltype(&PBondCar::GlareOn)>(car, 45))(glare);
}

void SetInShockVirtual(PBondCar *car, float shock) {
    (car->*XbeVirtual<decltype(&PBondCar::SetInShock)>(car, 1))(shock);
}

const char *GetCarTypeVirtual(PBondCar *car) {
    return (car->*XbeVirtual<decltype(&PBondCar::GetCarType)>(car, 15))();
}

ABaseSound *GetAudioVirtual(PBondCar *car) {
    return (car->*XbeVirtual<decltype(&PBondCar::GetAudio)>(car, 7))();
}

CarPhysics *GetPhysicsVirtual(PBondCar *car) {
    return (car->*XbeVirtual<decltype(&PBondCar::GetPhysics)>(car, 57))();
}

// RSceneObj::TriggerFX (its vtable's slot 15) as the car calls it: no position or direction, full intensity
void TriggerFX(RSceneObj *object, int type, uint32_t which) {
    typedef void (RSceneObj::*TriggerFXMethod)(int type, uint32_t which, uint32_t unused, const Coord3 *position,
                                               const Coord3 *direction, float intensity);
    (object->*XbeVirtual<TriggerFXMethod>(object, 15))(type, which, 0, &ZeroVector, &ZeroVector, -1.0f);
}

RTyreTrack *NewTyreTrack(float width, bool longTrack, bool unknown) {
    void *block = UMemory::FastAlloc(kTyreTrackSize, "RTyreTrack");
    return block != NULL ? RTyreTrack_Construct(block, 0, width, longTrack, unknown) : NULL;
}

} // namespace

// FUNC_AT(0x00061a50)
CarPhysics* PBondCar::GetPhysics() {
    return physics;
}

// FUNC_AT(0x00061a60)
void AVehicle::SetOrientation(const Coord3 *forward, const Coord3 *right, const Coord3 *up) {
    this->forward = *forward;
    this->right = *right;
    this->up = *up;
}

// FUNC_AT(0x00061ae0)
void PBondCar::InitializeBondCarGlobals() {
    TuningDBMgr->LoadDatabase("Physics:Physical", Launch.missionName, 0, false);
    dbattrib_float("PAD_DEAD_ZONE", &BondCar_PAD_DEAD_ZONE, 0.0f, 0.5f, 4, 1.0f, NULL);
    dbattrib_float("SKID_AUDIO_SCALE", &BondCar_SKID_AUDIO_SCALE, 0.0f, 2.0f, 4, 1.0f, NULL);
    dbattrib_float("ROLLING_RESISTANCE", &BondCar_ROLLING_RESISTANCE, 0.0f, 0.5f, 4, 1.0f, NULL);
    dbattrib_float("TWO_WHEEL_ANGLE", &BondCar_TWO_WHEEL_ANGLE, 0.0f, 1.0f, 4, 1.0f, NULL);
    dbattrib_float("TWO_WHEEL_SCALE", &BondCar_TWO_WHEEL_SCALE, 0.0f, 2000.0f, 4, 1.0f, NULL);
    dbattrib_float("TWO_WHEEL_LIMIT", &BondCar_TWO_WHEEL_LIMIT, 0.0f, 2000.0f, 4, 1.0f, NULL);
    dbattrib_float("TWO_WHEEL_OPPOSITE_SCALE", &BondCar_TWO_WHEEL_OPPOSITE_SCALE, 0.0f, 1.0f, 4, 1.0f, NULL);
    dbattrib_float("TWO_WHEEL_TENSOR_SCALE", &BondCar_TWO_WHEEL_TENSOR_SCALE, 0.0f, 500.0f, 4, 1.0f, NULL);
    dbattrib_float("TYRE_DAMAGE_RADIUS", &BondCar_TYRE_DAMAGE_RADIUS, 0.0f, 10.0f, 4, 1.0f, NULL);
    dbattrib_float("ENABLE_ROLL_STOPS_THRESHOLD", &BondCar_ENABLE_ROLL_STOPS_THRESHOLD, 0.0f, 1.0f, 4, 1.0f, NULL);
    dbattrib_float("BASE_FRICTION_MASS", &BondCar_BASE_FRICTION_MASS, 0.0f, 1.0f, 4, 1.0f, NULL);
    dbattrib_float("SHRED_DRAG", &BondCar_SHRED_DRAG, 0.0f, 100.0f, 4, 1.0f, NULL);
    dbattrib_float("MIN_BUTTON_VALUE", &BondCar_MIN_BUTTON_VALUE, 0.0f, 1.0f, 4, 1.0f, NULL);
    dbattrib_float("WHEEL_SPIN_EXTRA_RPM", &BondCar_WHEEL_SPIN_EXTRA_RPM, 0.0f, 25000.0f, 4, 1.0f, NULL);
    dbattrib_float("DAMAGE_SCALE_COLLISION", &BondCar_DAMAGE_SCALE_COLLISION, 0.0f, 10.0f, 4, 1.0f, NULL);
    dbattrib_s8("EMP_LIFETIME", &BondCar_EMP_LIFETIME, 0, 10000, 4, 1.0f, NULL);
    dbattrib_float("MAX_WHEEL_SPIN_RATE", &BondCar_MAX_WHEEL_SPIN_RATE, 0.0f, 1.0f, 4, 1.0f, NULL);
    dbattrib_float("MAX_WHEEL_SPIN_RATE_AI", &BondCar_MAX_WHEEL_SPIN_RATE_AI, 0.0f, 1.0f, 4, 1.0f, NULL);
    dbattrib_s8("POST_BRAKE_ACCEL_COUNT", &BondCar_POST_BRAKE_ACCEL_COUNT, 0, 1000, 4, 1.0f, NULL);
    dbattrib_float("POST_BRAKE_ACCEL_SCALE", &BondCar_POST_BRAKE_ACCEL_SCALE, 0.0f, 1.0f, 4, 1.0f, NULL);
    dbattrib_float("POST_BRAKE_ACCEL_MIN", &BondCar_POST_BRAKE_ACCEL_MIN, 0.0f, 1.0f, 4, 1.0f, NULL);
    TuningDBMgr->CloseCurrent();

    TuningDBMgr->LoadDatabase("Physics:Friction", Launch.missionName, 0, false);
    dbattrib_float("friction[kNODRIVE]", &BondCar_friction[kNODRIVE], 0.0f, 1.0f, 4, 1.0f, NULL);
    dbattrib_float("friction[kPAVED]", &BondCar_friction[kPAVED], 0.0f, 1.0f, 4, 1.0f, NULL);
    dbattrib_float("friction[kGRAVEL]", &BondCar_friction[kGRAVEL], 0.0f, 1.0f, 4, 1.0f, NULL);
    dbattrib_float("friction[kGRASS]", &BondCar_friction[kGRASS], 0.0f, 1.0f, 4, 1.0f, NULL);
    dbattrib_float("friction[kCOBBLE]", &BondCar_friction[kCOBBLE], 0.0f, 1.0f, 4, 1.0f, NULL);
    dbattrib_float("friction[kDIRT]", &BondCar_friction[kDIRT], 0.0f, 1.0f, 4, 1.0f, NULL);
    dbattrib_float("friction[kWATER]", &BondCar_friction[kWATER], 0.0f, 1.0f, 4, 1.0f, NULL);
    dbattrib_float("friction[kWOOD]", &BondCar_friction[kWOOD], 0.0f, 1.0f, 4, 1.0f, NULL);
    dbattrib_float("friction[kICE]", &BondCar_friction[kICE], 0.0f, 1.0f, 4, 1.0f, NULL);
    dbattrib_float("friction[kSNOW]", &BondCar_friction[kSNOW], 0.0f, 1.0f, 4, 1.0f, NULL);
    dbattrib_float("friction[kPAVED_ROUGH]", &BondCar_friction[kPAVED_ROUGH], 0.0f, 1.0f, 4, 1.0f, NULL);
    dbattrib_float("friction[kRAILROAD]", &BondCar_friction[kRAILROAD], 0.0f, 1.0f, 4, 1.0f, NULL);
    dbattrib_float("friction[kMETAL]", &BondCar_friction[kMETAL], 0.0f, 1.0f, 4, 1.0f, NULL);
    dbattrib_float("lateralLoss[kNODRIVE]", &BondCar_lateralLoss[kNODRIVE], 0.0f, 1.0f, 4, 1.0f, NULL);
    dbattrib_float("lateralLoss[kPAVED]", &BondCar_lateralLoss[kPAVED], 0.0f, 1.0f, 4, 1.0f, NULL);
    dbattrib_float("lateralLoss[kGRAVEL]", &BondCar_lateralLoss[kGRAVEL], 0.0f, 1.0f, 4, 1.0f, NULL);
    dbattrib_float("lateralLoss[kGRASS]", &BondCar_lateralLoss[kGRASS], 0.0f, 1.0f, 4, 1.0f, NULL);
    dbattrib_float("lateralLoss[kCOBBLE]", &BondCar_lateralLoss[kCOBBLE], 0.0f, 1.0f, 4, 1.0f, NULL);
    dbattrib_float("lateralLoss[kDIRT]", &BondCar_lateralLoss[kDIRT], 0.0f, 1.0f, 4, 1.0f, NULL);
    dbattrib_float("lateralLoss[kWATER]", &BondCar_lateralLoss[kWATER], 0.0f, 1.0f, 4, 1.0f, NULL);
    dbattrib_float("lateralLoss[kWOOD]", &BondCar_lateralLoss[kWOOD], 0.0f, 1.0f, 4, 1.0f, NULL);
    dbattrib_float("lateralLoss[kICE]", &BondCar_lateralLoss[kICE], 0.0f, 1.0f, 4, 1.0f, NULL);
    dbattrib_float("lateralLoss[kSNOW]", &BondCar_lateralLoss[kSNOW], 0.0f, 1.0f, 4, 1.0f, NULL);
    dbattrib_float("lateralLoss[kPAVED_ROUGH]", &BondCar_lateralLoss[kPAVED_ROUGH], 0.0f, 1.0f, 4, 1.0f, NULL);
    dbattrib_float("lateralLoss[kRAILROAD]", &BondCar_lateralLoss[kRAILROAD], 0.0f, 1.0f, 4, 1.0f, NULL);
    dbattrib_float("lateralLoss[kMETAL]", &BondCar_lateralLoss[kMETAL], 0.0f, 1.0f, 4, 1.0f, NULL);
    TuningDBMgr->CloseCurrent();
}

// Snowmobiles leave three tracks (the third nine times as wide), class 2 cars two at the back (four if "750"),
// classes 0 and 1 four, and the rest and submarines none.
// FUNC_AT(0x00062120)
void PBondCar::InitTyreTracks() {
    float width = attributes.LookupFloat("TYRE_TREAD_WIDTH", NULL);
    if (GetPhysicsVirtual(this)->isSnowmobile != 0) {
        tyreTracks[0] = NewTyreTrack(width, true, true);
        tyreTracks[1] = NewTyreTrack(width, true, true);
        tyreTracks[2] = NewTyreTrack(width * kSnowmobileTrackScale, true, true);
        tyreTracks[3] = NULL;
        return;
    }
    if (GetPhysicsVirtual(this)->subPhysics != 0) {
        for (int wheel = 0; wheel < kCarWheels; wheel++)
            tyreTracks[wheel] = NULL;
        return;
    }
    if (carClass == kCarClass2 && CRT_stricmp(carType, "750") != 0) {
        for (int wheel = 2; wheel < kCarWheels; wheel++)
            tyreTracks[wheel] = NewTyreTrack(width, false, false);
        tyreTracks[1] = NULL;
        tyreTracks[0] = NULL;
        return;
    }
    if (carClass == kPlayerCar || carClass == kCarClass1 || CRT_stricmp(carType, "750") == 0) {
        for (int wheel = 0; wheel < kCarWheels; wheel++)
            tyreTracks[wheel] = NewTyreTrack(width, true, false);
        return;
    }
    for (int wheel = 0; wheel < kCarWheels; wheel++)
        tyreTracks[wheel] = NULL;
}

// FUNC_AT(0x000623b0)
void PBondCar::InitializeCarControls() {
    unknown404 = 0.0f;
    unknown3F8 = 0.0f;
    unknown400 = 0.0f;
    unknown2C4 = 0.0f;
    targetBrake = 0.0f;
    targetGas = 0.0f;
    unknown284 = 0.0f;
    unknown280 = 0.0f;
    unknown27C = 0.0f;
    unknown278 = 0.0f;
    control.steering = 0.0f;
    control.strafeVertical = 0.0f;
    control.strafeHorizontal = 0.0f;
    control.steeringVertical = 0.0f;
    control.brake = 0.0f;
    control.gas = 0.0f;
    control.unknown1B = 0;
    control.firePrimary = 0;
    control.handBrake = 0;
    control.gear = 0;
}

// FUNC_AT(0x00062430)
void PBondCar::FlushCarControls() {
    actionQueue->Flush();
}

// FUNC_AT(0x00062440)
void PBondCar::InitializeCarVariables(bool initializeControls) {
    laserTimer = 0;
    gearChangeTimer = 0;
    landingFlag = 0;
    previousGear = -1;
    gear = 1;
    if (initializeControls)
        InitializeCarControlsVirtual(this);
    control.steering = 0.0f;
    control.strafeVertical = 0.0f;
    control.strafeHorizontal = 0.0f;
    control.steeringVertical = 0.0f;
    unknown268 = 0.0f;
    control.brake = 0.0f;
    control.gas = 0.0f;
    control.gear = 0;
    resetAvailable = 0;
    control.unknown1B = 0;
    control.handBrake = 0;
    unknown3FD = 0;
    unknown2E8 = 1.0f;
    controlLockTimer = -1;
    fgDamageLevels.spread = 0.0f;
    damageByPlayer = 0.0f;
    carWheelSpeed = 0.0f;
    wheelSpinAngle[1] = 0.0f;
    wheelSpinAngle[0] = 0.0f;
    twoWheelStuntTimer = 1.0f;
    damageByPlayerTimer = 0;
    unknown2B3 = 0;
    unknown3FE = 0;
    rocketBoost = 0;
    twoWheelMode = 0;
    inShock = 0;
    unknown29B = 0;
    reverseTimer = 0;
    unknown2D4 = 0;
    oilSlick = 0;
    empActive = 0;
    empTimer = 0;
    for (int wheel = 0; wheel < kCarWheels; wheel++)
        suspensionCompression[wheel] = 0.0f;
    unknown2B1 = 0;
    uint32_t tyreDamage = attributes.LookupUInt("TYRE_DAMAGE_POINTS", NULL);
    for (int wheel = 0; wheel < kCarWheels; wheel++)
        tyreDamagePoints[wheel] = tyreDamage;
    controlLockTimer = -1;
    bob = 0.0f;
    int random = SimulationRandom->Generate();
    unknown3E8 = 0.0f;
    wasInAir = 1;
    bobAngle = float(random * double(kBobAngleScale));
    for (int wheel = 0; wheel < kCarWheels; wheel++) {
        WWorldPos fresh;
        fresh.Construct();
        wheels[wheel] = fresh;
        NullFunction();     // the temporary's destructor
    }
}

// FUNC_AT(0x00062600)
void PBondCar::ResetDamage() {
    for (int zone = 0; zone < kDamageZoneCount; zone++) {
        damageZones[zone].unknown04 = 0.0f;
        damageZones[zone].unknown00 = 0.0f;
    }
    if (renderObject != NULL)
        renderObject->Reset();
    memset(glareOn, 0, sizeof(glareOn));
    damageByPlayer = 0.0f;
    damageByPlayerTimer = 0;
    hitPoints = kHitPointsAtReset;
}

// FUNC_AT(0x00062660)
void PBondCar::AddDamageByPlayer(float damage) {
    damageByPlayer += damage;
    damageByPlayerTimer = uint8_t(SimStepsPerSecond);
    bool police = attributes.LookupBool("IS_POLICE", NULL);
    if (carClass == kCarClass3 || police) {
        if (damageByPlayer > kPlayerDamageEvent || GetHitPoints() < damageByPlayer)
            SMissionManager_ProgrammerDefinedEvent(glbMissionManager, 0, kMissionEvent2, "");
    }
}

// FUNC_AT(0x000626e0)
int PBondCar::GetNumTires() {
    int tyres = 0;
    for (int8_t wheel = kCarWheels - 1; wheel >= 0; wheel--) {
        if (!IsTyreShreddedVirtual(this, wheel))
            tyres++;
    }
    return tyres;
}

// FUNC_AT(0x00062740)
void PBondCar::SetInShock(float shock) {
    if (carClass == kCarClass1 || inShock != 0)
        return;
    if (shock > kMaxShock)
        shock = kMaxShock;
    int steps;
    if (carClass == kCarClass3)
        steps = kShockSteps3;
    else
        steps = carClass != kCarClass2 ? kShockSteps : kShockSteps2;
    inShock = int8_t(Ftol(double(steps) * shock));
    if (carClass != kPlayerCar)
        inShock++;
}

// The car on the ground at `position` (its height there rewritten) facing `direction`, its body, wheels, controls
// and tyre tracks reset.
// FUNC_AT(0x000627c0)
void PBondCar::ResetCar(Coord3 *position, const Coord3 *direction) {
    InitializeCarVariables(false);
    alignas(16) MATRIX4 generated;
    alignas(16) MATRIX4 orientation = *Util_GenerateMatrix(&generated, direction);
    RigidBody *body = GetRigidBody();
    if (physics->subPhysics == 0) {
        float groundHeight;
        if (!fgCollisionMgr->GetWorldHeightAtPoint(position, &groundHeight, false))
            groundHeight = position->y + kResetLift;
        RigidBodyInfo *info = body->info;
        alignas(16) Coord4 centre = {position->x, 0.0f, position->z, 0.0f};
        float restLength = attributes.LookupFloat("SPRING_REST_LENGTH", NULL);
        centre.y = float(double(attributes.LookupFloat("TYRE_RADIUS", NULL)) + restLength + info->halfExtents.y +
                         groundHeight);
        float wheelHeights = 0.0f;
        for (int wheel = 0; wheel < kCarWheels; wheel++) {
            VU0_MATRIX4_vect3rotate(&info->levers[wheel], &orientation, &info->worldLevers[wheel]);
            wheelPos[wheel] = info->worldLevers[wheel];
            VU0_v3add(&wheelPos[wheel], &centre, &wheelPos[wheel]);
            body->TempGetHeightInformation(false, reinterpret_cast<const Coord3 *>(&wheelPos[wheel]),
                                           &wheelRoadNormal[wheel], &wheels[wheel]);
            double compression = double(physics->springCompressionLimit) * kResetCompression;
            suspensionCompression[wheel] = float(compression);
            double height = double(wheelRoadNormal[wheel].w) + wheelPos[wheel].y - compression;
            wheelPos[wheel].y = float(height);
            wheelHeights = float(height + wheelHeights);
        }
        restLength = attributes.LookupFloat("SPRING_REST_LENGTH", NULL);
        position->y = float(double(attributes.LookupFloat("TYRE_RADIUS", NULL)) + restLength +
                            double(wheelHeights) * kWheelsAverage + info->halfExtents.y);
    }
    GetRigidBody()->ResetObject(&orientation, position);
    renderObject->UpdatePositionVirtual(true);
    RVehicle_SetEMPVictimEffect(renderObject, 0, false);
    if (carClass == kPlayerCar)
        fgRenderHigh->views[0].camera->ResetCamera();
    for (int wheel = 0; wheel < kCarWheels; wheel++) {
        if (tyreTracks[wheel] != NULL)
            UMemory::FastFree(tyreTracks[wheel], kTyreTrackSize);
    }
    InitTyreTracks();
}

// FUNC_AT(0x00062a40)
DamageZone* PBondCar::GetDamageZones(uint32_t *count) {
    *count = kDamageZoneCount;
    return damageZones;
}

// The engine's revs from the speed through the gears' limits, plus the wheel spin's. While gearChangeTimer runs
// the gear does not drop below the previous one.
// FUNC_AT(0x00062a60)
double PBondCar::CalculateRPM() {
    float speed = VU0_v3lengthxz(&GetRigidBody()->velocity);
    if (physics->subPhysics == 1)
        return speed * double(kSubRpmScale);
    if (numWheelsOnGround == 0)
        return control.gas * double(kAirRpm) + double(BondCar_WHEEL_SPIN_EXTRA_RPM) * unknown238;
    if (control.gear != 0) {
        gear = 0;
        return double(speed) * kRpmScale * kGear0RpmScale + double(BondCar_WHEEL_SPIN_EXTRA_RPM) * unknown238;
    }
    int8_t selected = 1;
    float limit;
    do {
        limit = physics->gearLimit[selected];
        if (speed < limit)
            break;
        selected++;
    } while (selected < kTopGear);
    if (selected < previousGear && gearChangeTimer != 0) {
        selected = previousGear;
        limit = physics->gearLimit[selected];
    }
    gear = selected;
    return double(speed) * kRpmScale / limit + double(BondCar_WHEEL_SPIN_EXTRA_RPM) * unknown238;
}

// FUNC_AT(0x00062b70)
void PBondCar::FireLaser() {
    if (!laserActive) {
        laserActive = 1;
        laserTimer = kLaserSteps;
    }
}

// FUNC_AT(0x00062b90)
void PBondCar::AttackWithEmp() {
    if (empActive)
        return;
    AMenuSound::Trigger(4, "SFX_EMPdetonate", "POV Weapons", 0);
    RSceneObj *vehicle = renderObject;
    RVehicle_SetEMPVictimEffect(vehicle, 0, true);
    RVehicle_TriggerEmpBolts(vehicle, 0);
    empActive = 1;
    empTimer = SimStepCount + BondCar_EMP_LIFETIME;
    SetInShockVirtual(this, 1.0f);
    if (strncmp(GetCarTypeVirtual(this), kBombVan, sizeof(kBombVan) - 1) == 0) {
        SMissionManager_ProgrammerDefinedEvent(glbMissionManager, 0, kMissionEvent0, "");
        SMissionManager_IncShotsHit(glbMissionManager, 0, 0);
    }
}

// FUNC_AT(0x00062c40)
void* PBondCar::GetSplinePath() {
    if (aiGroundVehicle != NULL)
        return AIVehicle_GetSplinePath(aiGroundVehicle, 0);
    return NULL;
}

// FUNC_AT(0x00062c60)
void PBondCar::EnableTargetBeacon(bool enable) {
    if (enable && targetBeacon == NULL) {
        void *block = UMemory::FastAlloc(sizeof(WTargetable), "WTargetable");
        targetBeacon = block != NULL ? static_cast<WTargetable *>(block)->Construct(this) : NULL;
        TargetPicker.RegisterTarget(targetBeacon);
    }
    if (targetBeacon != NULL)
        targetBeacon->enabled = 1;
}

// FUNC_AT(0x00062cf0)
void PBondCar::DisableTargetBeacon() {
    if (targetBeacon != NULL)
        targetBeacon->enabled = 0;
}

// FUNC_AT(0x00062d00)
void PBondCar::GlareOn(int glare) {
    if (!glareOn[glare]) {
        TriggerFX(renderObject, RSceneObj::kFXOn, 1u << (glare + 16));
        TriggerFX(renderObject, kFXGlareOn, glare);
        glareOn[glare] = 1;
    }
}

// FUNC_AT(0x00062d70)
void PBondCar::GlareOff(int glare) {
    if (glareOn[glare] == 1) {
        TriggerFX(renderObject, RSceneObj::kFXOff, 1u << (glare + 16));
        TriggerFX(renderObject, kFXGlareOff, glare);
        glareOn[glare] = 0;
    }
}

// Both signal glares off, then the one for the signal on in the lit part of its period. The signal goes off once
// the steering is past turnSignalSteering the other way.
// FUNC_AT(0x00062de0)
void PBondCar::HandleTurnSignals() {
    GlareOff(3);
    GlareOff(4);
    switch (turnSignalMode) {
    case kSignal1:
        if (control.steering > turnSignalSteering)
            turnSignalMode = kSignalOff;
        break;
    case kSignal2:
        if (control.steering < turnSignalSteering)
            turnSignalMode = kSignalOff;
        break;
    default:
        return;
    }
    uint32_t period = uint32_t(turnSignalPeriod);
    uint32_t phase = (SimStepCount - uint32_t(turnSignalStart)) % period;
    bool lit = (1.0 - turnSignalLit) * double(period) < double(phase);
    if (turnSignalMode == kSignal1) {
        if (lit)
            GlareOnVirtual(this, 3);
    } else if (turnSignalMode == kSignal2) {
        if (lit)
            GlareOnVirtual(this, 4);
    }
}

// FUNC_AT(0x00062ec0)
void PBondCar::ClearEMPState() {
    RVehicle_SetEMPVictimEffect(renderObject, 0, false);
    empActive = 0;
    empTimer = 0;
}

// FUNC_AT(0x00062ee0)
bool PBondCar::EnableRocketBoost() {
    int boostTime = attributes.LookupInt("BOOST_TIME", NULL);
    if (rocketBoost > boostTime * kBoostLimit)
        return false;
    if (attributes.LookupInt("NUM_WHEELS", NULL) == kSmokeWheels)
        RVehicle_TriggerTurboBoostSmoke(renderObject, 0, boostTime);
    rocketBoost = boostTime * kBoostSteps;
    static_cast<AVehicle *>(GetAudioVirtual(this))->boost = 1;
    return true;
}

// FUNC_AT(0x00062f50)
void PBondCar::EnableTwoWheelStunt() {
    twoWheelMode = 1;
}

// FUNC_AT(0x00062f60)
void PBondCar::DisableTwoWheelStunt() {
    RigidBody *body = GetRigidBody();
    Coord3 angularMomentum = body->angularMomentum;
    angularMomentum.z = 0.0f;
    body->SetAngularMomentum(&angularMomentum);
    twoWheelStuntTimer = 1.0f;
    twoWheelMode = 0;
    unknown2B3 = 1;
    AMenuSound::Trigger(1, "SFX_TiresHeavy", "Landings", 0);
}
