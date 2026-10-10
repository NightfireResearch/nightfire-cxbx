#ifndef DRIVING_GAME_VEHICLE_H_
#define DRIVING_GAME_VEHICLE_H_

#include <stddef.h>
#include <stdint.h>

#include "../../helpers.h"
#include "../../common/xbeClass.h"      // offsetof on a derived class without clang's warning
#include "../../common/xbeOverload.h"   // XbeVirtual
#include "../data/AttributeSet.h"
#include "../data/Tree.h"
#include "../physics/PhysicsNamespace.h"    // PhysicsData
#include "../physics/PhysicsObject.h"

// ---------------------------------------------------------------------------------------------------------------
// PVehicle (0x74 bytes): the base of the cars (PBondCar). A PhysicsObject of type 1 in the "pvehicle" attribute
// class, with two masks of mission editor switches; its vtable (0x0018fa00, 83 slots) keeps PhysicsObject's seven
// and declares 76 more, all pure (PBondCar's vtable, 0x0018f580, fills them; the names below are its overrides').
// PHelicopter (Helicopter.h) is a "pvehicle" too but derives from PhysicsObject directly.
//
// Its statics: the "CarPhysics" structure every "pvehicle" collection holds (registered by InitializeGlobals),
// lookups of a car's attributes, and the list of car names - "default" then every "pvehicle" collection in the
// attribute system's order - with a map from a name to its index, built on first use. See Vehicle.cpp.
// ---------------------------------------------------------------------------------------------------------------

// The "pvehicle" class's "CarPhysics" structure (0x100 bytes), each field named as its attribute
struct CarPhysics {
    int32_t is4x4;                          // +0x00 IS_4X4
    int32_t isRally;                        // +0x04 IS_RALLY
    float speedCutoffReverse;               // +0x08 SPEED_CUTOFF_REVERSE
    float speedCutoffRateReverse;           // +0x0c SPEED_CUTOFF_RATE_REVERSE
    float wheelForceAppScale;               // +0x10 WHEEL_FORCE_APP_SCALE
    float maxAcc;                           // +0x14 MAXACC
    float maxRevAcc;                        // +0x18 MAXREVACC
    float maxBrake;                         // +0x1c MAXBRAKE
    float maxSteering;                      // +0x20 MAXSTEERING
    float modSteerSpeed;                    // +0x24 MODSTEERSPEED
    float minSteer;                         // +0x28 MINSTEER
    float fricModSpeed;                     // +0x2c FRIC_MOD_SPEED
    float fricModRange;                     // +0x30 FRIC_MOD_RANGE
    float fricModMin;                       // +0x34 FRIC_MOD_MIN
    float yawStabilityFactor;               // +0x38 YAW_STABILITY_FACTOR
    float noseJumpAngle;                    // +0x3c NOSE_JUMP_ANGLE
    float frictionLimitFront;               // +0x40 FRICTIONLIMITFRONT
    float frictionLimitRear;                // +0x44 FRICTIONLIMITREAR
    float tyreGripFactor;                   // +0x48 TYREGRIPFACTOR
    float slopeScale;                       // +0x4c SLOPE_SCALE
    float springStiffnessFront;             // +0x50 SPRING_STIFFNESS_FRONT
    float springStiffnessRear;              // +0x54 SPRING_STIFFNESS_REAR
    float springDampingFront;               // +0x58 SPRING_DAMPING_FRONT
    float springDampingRear;                // +0x5c SPRING_DAMPING_REAR
    float springRestLength;                 // +0x60 SPRING_REST_LENGTH
    float springCompressionLimit;           // +0x64 SPRING_COMPRESSION_LIMIT
    float springCompressionLimitDraw;       // +0x68 SPRING_COMPRESSION_LIMIT_DRAW
    float wheelRadius;                      // +0x6c WHEEL_RADIUS
    float brakingFriction;                  // +0x70 BRAKING_FRICTION
    float accelFrwRatio;                    // +0x74 ACCEL_FRW_RATIO
    float coastFrwRatio;                    // +0x78 COAST_FRW_RATIO
    float brakeFrwRatio;                    // +0x7c BRAKE_FRW_RATIO
    float reverseFrwRatio;                  // +0x80 REVERSE_FRW_RATIO
    float handbrakeFrwRatio;                // +0x84 HANDBRAKE_FRW_RATIO
    float revHandbrakeFrwRatio;             // +0x88 REV_HANDBRAKE_FRW_RATIO
    float frontSlippingScale;               // +0x8c FRONT_SLIPPING_SCALE
    float rearSlippingScale;                // +0x90 REAR_SLIPPING_SCALE
    float slippingRange;                    // +0x94 SLIPPING_RANGE
    float handbrakeForce;                   // +0x98 HANDBRAKE_FORCE
    float totalHandbrakeFrictionScale;      // +0x9c TOTAL_HANDBRAKE_FRICTION_SCALE
    float handbrakeRwFricScale;             // +0xa0 HANDBRAKE_RW_FRIC_SCALE
    float wheelSpinScale;                   // +0xa4 WHEELSPINSCALE
    float wsSpeed;                          // +0xa8 WS_SPEED
    float wsAccel;                          // +0xac WS_ACCEL
    float counterSteerScale;                // +0xb0 COUNTER_STEER_SCALE
    float counterSteerScaleLimit;           // +0xb4 COUNTER_STEER_SCALE_LIMIT
    int32_t numBlendSteps;                  // +0xb8 NUM_BLEND_STEPS
    int32_t isSub;                          // +0xbc IS_SUB
    int32_t subPhysics;                     // +0xc0 SUB_PHYSICS
    int32_t noWorldCollisions;              // +0xc4 NO_WORLD_COLLISIONS
    int32_t isSnowmobile;                   // +0xc8 IS_SNOWMOBILE
    int32_t isBoat;                         // +0xcc IS_BOAT
    float gearLimit[6];                     // +0xd0 GEAR_LIMITR, GEAR_LIMIT1..GEAR_LIMIT5
    float gearRatio[6];                     // +0xe8 GEAR_RATIOR, GEAR_RATIO1..GEAR_RATIO5
};
static_assert(sizeof(CarPhysics) == 0x100, "CarPhysics is 0x100 bytes");
static_assert(offsetof(CarPhysics, springRestLength) == 0x60 && offsetof(CarPhysics, numBlendSteps) == 0xb8 &&
              offsetof(CarPhysics, isBoat) == 0xcc && offsetof(CarPhysics, gearLimit) == 0xd0 &&
              offsetof(CarPhysics, gearRatio) == 0xe8, "CarPhysics layout");

// One of the 16 damage zones a vehicle answers from GetDamageZones (PhysicsObject.h declares it): two shares of
// the damage it has taken. ApplyDamage adds a hit's damage to the first at (1 - its split) and to the second at
// its split (PBondCar's keeps them at most 1 and 2, PHelicopter's at most 1); PBondCar::SetVisualDamage puts a
// random point in them.
struct DamageZone {
    float unknown00;
    float unknown04;
};
static_assert(sizeof(DamageZone) == 8, "a damage zone is 8 bytes");

constexpr int kDamageZoneCount = 16;

// The damage levels the ApplyDamage of PBondCar, PHelicopter, Sentry and Smackable compare against (initialised
// data at 0x001c3db4; PBondCar::InitializeCarVariables clears `spread`). The names are ours.
struct DamageLevels {
    float impact[4];        // +0x00 0.75, 0.5, 0.25, 0.001: a hit's damage above each sends its stimulus
    float zone[5];          // +0x10 0.05, 0.15, 0.35, 0.65, 0.85: a zone's damage stages (zone[0] is not read)
    float effects;          // +0x24 0.75: a zone above it gets the damage effects
    float spread;           // +0x28 0.75: damage in a zone above it spreads to the zones beside it
};
static_assert(sizeof(DamageLevels) == 0x2c, "the damage levels are 11 floats");

#define fgDamageLevels (*(DamageLevels *)0x001c3db4)
#define fgDamageImpulseScale FLOAT_AT(0x001c3da4)     // 1000: a bullet's push per point of damage (name ours)

// A new CarPhysics's defaults: all zero (the type's AttributeExtensionInit; the name is ours)       0x0006f8e0
void InitCarPhysics(const char *className, const char *collectionName, const char *attributeName, uint32_t type,
                    void *data);

// The map from a car's name to its index (std::map<const char *, int>, names compared with _stricmp): a compiled
// copy of the data layer's tree (data/Tree.h), the index in the node's mapped value.
class CarNameMap : public Tree {
public:
    void EraseSubtree(TreeNode *node);                                                          // 0x00071070
    TreeNode** InsertAt(TreeNode **result, bool addLeft, TreeNode *where, const TreePair *value);  // 0x000710b0
    TreeNode** EraseAt(TreeNode **result, TreeNode *where);                                     // 0x00071290
    TreeInsertResult* InsertUnique(TreeInsertResult *result, const TreePair *value);            // 0x00071560
    TreeNode** EraseRange(TreeNode **result, TreeNode *first, TreeNode *last);                  // 0x00071630
    void Destroy();                                                                             // 0x000716f0

    // operator[] (inlined by its callers): the name's index, a new entry of 0 if it has none
    int &operator[](const char *name);
};
static_assert(sizeof(CarNameMap) == 12, "a map is 12 bytes");

// The car's controls (CarControl, 0x20 bytes: GetCarControl copies them out)
struct BondCarControl {
    float steering;             // +0x00
    float steeringVertical;     // +0x04
    float strafeVertical;       // +0x08
    float strafeHorizontal;     // +0x0c
    float gas;                  // +0x10
    float brake;                // +0x14
    uint8_t gear;               // +0x18
    uint8_t handBrake;          // +0x19
    uint8_t firePrimary;        // +0x1a
    uint8_t unknown1B;          // +0x1b
    uint8_t unknown1C[4];
};
static_assert(sizeof(BondCarControl) == 0x20, "a car control is 32 bytes");

struct PVehicle : PhysicsObject {
    // The vtable's slots past PhysicsObject's (all pure in PVehicle's), named as PBondCar's overrides
    enum Slot {
        kGetAudio = 7,
        kSetAudio,
        kSetAIGroundVehicle,
        kGetAIGroundVehiclePtr,
        kGetSplinePath,                 // 11
        kGetResetAvailable,
        kEnableTargetBeacon,
        kDisableTargetBeacon,
        kGetCarType,                    // 15
        kChangeCarType,
        kSetCarClass,
        kGetCarClass,
        kGetCarColour,
        kGetCarSpeed,                   // 20
        kInitializeCarControls,
        kFlushCarControls,
        kGetControllerInput,
        kGetCarSteer,
        kSetCarControlSteering,         // 25
        kSetCarControlSteeringVertical,
        kSetCarControlStrafeHorizontal,
        kSetCarControlStrafeVertical,
        kSetCarControlGas,
        kSetCarControlBrake,            // 30
        kSetCarControlHandBrake,
        kGetCarControlHandBrake,
        kSetCarControlFirePrimary,
        kLockCarControlForever,
        kSetTargetGas,                  // 35
        kSetTargetBrake,
        kGetCarControl,
        kGetNumTires,
        kEmpActive,
        kIsTyreShredded,                // 40
        kGetTyreTrackPtr,
        kResetCarAt,                    // ResetCar(position, direction)
        kResetCar,                      // ResetCar(bool)
        kSetScoreable,
        kGlareOn,                       // 45
        kInShock,
        kSetOilSlick,
        kGetNumWheelsOnGround,
        kGetCarWheelSpinAngle,
        kResetDamage,                   // 50
        kSetVisualDamage,
        kAddDamageByPlayer,
        kGetDamageByPlayerTimer,
        kClearEMPState,
        kEnableTwoWheelStunt,           // 55
        kDisableTwoWheelStunt,
        kGetPhysics,
        kIsReversing,
        kSetAgainstWallFlag,
        kGetSuspensionCompression,      // 60
        kGetWheelRoadHeight,
        kGetWheelRoadSurface,
        kGetWheelRoadNormal,
        kSetShieldPointLoc,
        kIsWheelOnGround,               // 65
        kGetCarWheelSlip,
        kDisableTyreBlowOuts,
        kGetIsInTwoWheelMode,
        kEnableRocketBoost,
        kGetWasInAir,                   // 70
        kSetWasInAir,
        kFireLaser,
        kAttackWithEmp,
        kGetWheelPos,
        kGetForceStop,                  // 75
        kSetImmunity,
        kForceStopOn,
        kForceStopOff,
        kForceRollDirection,
        kRollSub,                       // 80
        kGetSecondaryType,
        kGetTargetBeacon,
        kSlotCount                      // 83
    };

    uint32_t missionEditorOnMask;   // +0x6c MissionEditorSwitch: bits of the switches turned on
    uint32_t missionEditorOffMask;  // +0x70 ... and turned off

    // The virtual methods the engine calls on a vehicle, each through its own vtable (so reaching PBondCar's)
    void *GetSplinePath() { return (this->*XbeVirtual<void *(PVehicle::*)()>(this, kGetSplinePath))(); }
    const char *GetCarType() { return (this->*XbeVirtual<const char *(PVehicle::*)()>(this, kGetCarType))(); }
    int GetCarClass() { return (this->*XbeVirtual<int (PVehicle::*)()>(this, kGetCarClass))(); }
    // The controls are returned by value: the caller's record, filled and answered
    BondCarControl *GetCarControl(BondCarControl *result) {
        typedef BondCarControl *(PVehicle::*Method)(BondCarControl *result);
        return (this->*XbeVirtual<Method>(this, kGetCarControl))(result);
    }
    int GetNumWheelsOnGround() { return (this->*XbeVirtual<int (PVehicle::*)()>(this, kGetNumWheelsOnGround))(); }
    void AddDamageByPlayer(float damage) {
        (this->*XbeVirtual<void (PVehicle::*)(float)>(this, kAddDamageByPlayer))(damage);
    }
    uint8_t GetDamageByPlayerTimer() {
        return (this->*XbeVirtual<uint8_t (PVehicle::*)()>(this, kGetDamageByPlayerTimer))();
    }
    CarPhysics *GetPhysics() { return (this->*XbeVirtual<CarPhysics *(PVehicle::*)()>(this, kGetPhysics))(); }
    int IsReversing() { return (this->*XbeVirtual<int (PVehicle::*)()>(this, kIsReversing))(); }
    void SetAgainstWallFlag(bool against) {
        (this->*XbeVirtual<void (PVehicle::*)(bool)>(this, kSetAgainstWallFlag))(against);
    }
    bool GetIsInTwoWheelMode() { return (this->*XbeVirtual<bool (PVehicle::*)()>(this, kGetIsInTwoWheelMode))(); }
    uint8_t GetWasInAir() { return (this->*XbeVirtual<uint8_t (PVehicle::*)()>(this, kGetWasInAir))(); }
    void SetWasInAir(bool wasInAir) { (this->*XbeVirtual<void (PVehicle::*)(bool)>(this, kSetWasInAir))(wasInAir); }

    PVehicle* Construct(const char *name);                                                      // 0x0006f900
    // The scalar deleting destructor, vtable slot 0 (the destructor is PhysicsObject's)
    PVehicle* Delete(unsigned flags);                                                           // 0x00071050
    void MissionEditorSwitch(bool on, int which);                                               // 0x00071000

    // The attribute set of the car `name` (the caller's set, constructed)
    static AttributeSet* GetNamedAttribs(AttributeSet *result, const char *name);               // 0x0006f810
    static CarPhysics* CarPhysicsAttrib(AttributeSet *attributes);                              // 0x0006f840
    // RENDER_FILENAME, or "<name>.crp" without one
    static const char* RenderNameAttrib(AttributeSet *attributes);                              // 0x0006f860
    // RENDER_NUMCOLORS, 1 without one
    static uint32_t NumColoursAttrib(AttributeSet *attributes);                                 // 0x0006f8c0
    // Registers CarPhysics and its fields
    static void InitializeGlobals();                                                            // 0x0006f930
    static void Shutdown();                                                                     // 0x00071900

    // The car names ("default" first) and the index of a name (a name not in the list gets an entry of 0)
    static uint32_t GetNameCount();                                                             // 0x00071880
    static int NameToIndex(const char *name);                                                   // 0x000718a0
    static const char* const* GetCarNames();                                                    // 0x000718e0
    // Builds the list and the map (FUN_00071730; the name is ours)
    static void BuildCarNames();                                                                // 0x00071730
};
static_assert(PVehicle::kGetTargetBeacon == 82 && PVehicle::kSlotCount == 83, "PVehicle's vtable slots");
static_assert(sizeof(PVehicle) == 0x74, "a vehicle is 0x74 bytes");
static_assert(offsetof(PVehicle, missionEditorOnMask) == 0x6c, "PVehicle layout");

constexpr uint32_t kPVehicleVtable = 0x0018fa00;

#define CarPhysicsType U32_AT(0x001c3de8)                   // the extension type's id (InitializeGlobals)
#define fgCarNameMap (*(CarNameMap **)0x001e7ac0)           // BuildCarNames' map
#define fgCarNames (*(const char ***)0x001e7ac4)            // ... and list
#define fgCarNameCount U32_AT(0x001e7ac8)

#endif // DRIVING_GAME_VEHICLE_H_
