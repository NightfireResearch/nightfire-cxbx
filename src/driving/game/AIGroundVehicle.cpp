#pragma fp_contract(off)

#include "AIGroundVehicle.h"

#include <bit>
#include <math.h>

#include "AIElements.h"
#include "AIEnemy.h"                    // MinFloat, MaxFloat
#include "AISplinePath.h"
#include "AIVehicleController.h"
#include "BondCar.h"                    // CarClass
#include "CharacterSound.h"             // MakeCoord3
#include "MissionManager.h"             // glbMissionManager
#include "Simulation.h"                 // fgSimulation
#include "Vehicle.h"                    // PVehicle
#include "VehicleSound.h"               // AVehicle, ATrafficVehicle
#include "../../helpers.h"
#include "../anim/AnimEngine.h"         // Handle
#include "../anim/Manager.h"            // MakeCoord4
#include "../camera/CameraSpline.h"     // CrtAcosTimes, RCameraSpline
#include "../data/AttributeSet.h"
#include "../engine/CoreFoundation.h"   // NullFunction
#include "../engine/PhysicsUtil.h"      // Util_GenerateMatrix
#include "../engine/SimRandom.h"
#include "../engine/UMemory.hpp"
#include "../physics/PhysicsObject.h"
#include "../physics/RigidBody.h"
#include "../platform/RealMath.h"
#include "../platform/X87.h"
#include "../render/PathEngine.h"       // RPathHandle
#include "../render/RSceneObj.hpp"
#include "../world/Collider.h"          // WCollider
#include "../world/CollisionManager.h"  // fgCollisionMgr
#include "../world/RoadNav.h"
#include "../world/RoadNetwork.h"
#include "../world/WorldPos.h"
#include "../platform/CRuntimeStd.h"

// ---------------------------------------------------------------------------------------------------------------
// AIGroundVehicle, part 1 (0x00029d60-0x0002c6e0, 0x0002c6e0-0x0002dc90 and the vector deleting destructor at
// 0x00035d90): the constructor, Reset, the modes, the navigators, the lane weights, the attack speed, the turret,
// the auto-drive and agent updates, UnSpawn. Ported from the listing.
// ---------------------------------------------------------------------------------------------------------------

// ---- the game's code not ported yet

// ---- globals

// How far a traffic car's navigator moves on a step (a static initialiser's 7.5 x 1/60)
// XBE_GLOBAL(0x001ddf0c, 4)
float TrafficStepDistance = 0.125f;

// XBE_GLOBAL(0x001ddf00, 4)
float NextHeightPhase;
// XBE_GLOBAL(0x001ddf04, 4)
float NextParkingOffset;

namespace {

constexpr int kZoneStopType = 4;            // an agent in a zone of the type stops a second (event 0x11)
constexpr int kZoneStopEvent = 0x11;
constexpr float kZoneRadiusScale = 5.0f;

constexpr int kLanes = 0x40;                // laneWeights: lane 0x20 is the road's middle
constexpr int kMiddleLane = 0x20;
constexpr int kBlockedLane = 0xff;
constexpr float kLanePenalty = 5.0f;        // the weight added for each lane away from the one wanted
constexpr float kDirectLaneReach = 32.0f;   // GetDirectLaneWeights' barrier probes, either side
constexpr float kDirectLaneMiddle = 32.0f;

constexpr int kCollNavSteps = 15;           // the collision navigators are placed again after this many steps
constexpr float kCollNavLead = 0.25f;       // ... a quarter of a second ahead of the car
constexpr float kMaxDrivableDistance = 200.0f;
constexpr float kStuckDistance = 5.0f;      // moved less than this in five seconds: stuck
constexpr int kStuckSeconds = 5;
constexpr float kStuckControl = 0.5f;       // gas or brake at least this: the car is trying to move
constexpr float kResetStep = 7.5f;          // the navigator moved on after it is placed again
constexpr float kStuckResetStep = 5.0f;
constexpr float kNavBehind = -0.15f;        // the car faces away from its navigator below this
constexpr float kNavAhead = 0.7f;           // ... and towards it from this
constexpr int kModeHoldSteps = 60;
constexpr double kPathCheckSeconds = 0.25;   // the line-of-sight checks' interval

constexpr float kHeliHeightSwing = 2.5f;
constexpr float kHeightSwing = 10.0f;
constexpr float kHeightPhaseStep = 0.25f;

constexpr float kTrafficMinHitPoints = 0.5f;
constexpr float kArrivedDistance = 2.5f;
constexpr float kSlowRange = 10.0f;
constexpr float kAttack12Speed = 7.5f;
constexpr float kChaseSpeed = 30.0f;
constexpr float kHiddenChaseSpeed = 25.0f;
constexpr float kChaseReach = 1.25f;        // the cars' half lengths, by this: the least chase range
constexpr float kChaseFacing = 0.707f;
constexpr float kChaseSlowdown = 0.6f;
constexpr float kFollowSpeed = 0.8f;
constexpr float kApproachScale = 0.75f;
constexpr float kAttack14Speed = 10.0f;
constexpr float kHiddenAttack14Speed = 25.0f;
constexpr float kAutoDriveCatchUp = 1.1f;
constexpr float kAutoDriveSlowdown = 0.75f;
constexpr float kAutoDriveFollow = 0.85f;

constexpr float kTurretStep = 0.005f;       // turns a step
constexpr float kInvTwoPi = 0.15915494f;
constexpr float kDegreesPerTurn = 360.0f;
constexpr uint32_t kTurretAnimation = 0x10;
constexpr uint8_t kTurretStimulus = 8;

constexpr float kTargetSlowSpeed = 5.0f;
constexpr float kLeadMin = 0.2f;
constexpr float kLeadTime = 0.2f;
constexpr float kLeadRange = 15.0f;
constexpr float kRoadFacing = 0.5f;
constexpr float kDirectMinSpeed = 10.0f;
constexpr float kRandomScale = 1.0f / 65536.0f;

constexpr float kParkingStep = 50.0f;
constexpr float kParkingMax = 5000.0f;
constexpr float kParkingDistance = 40000.0f;

// new[] keeps the element count in the first word of the 16 bytes before the array
constexpr int kArrayCookieWords = 4;

static_assert(std::bit_cast<uint32_t>(kChaseFacing) == 0x3f34fdf4 &&
              std::bit_cast<uint32_t>(kChaseSlowdown) == 0x3f19999a &&
              std::bit_cast<uint32_t>(kFollowSpeed) == 0x3f4ccccd &&
              std::bit_cast<uint32_t>(kAutoDriveCatchUp) == 0x3f8ccccd &&
              std::bit_cast<uint32_t>(kAutoDriveFollow) == 0x3f59999a &&
              std::bit_cast<uint32_t>(kTurretStep) == 0x3ba3d70a &&
              std::bit_cast<uint32_t>(kInvTwoPi) == 0x3e22f983 &&
              std::bit_cast<uint32_t>(kNavBehind) == 0xbe19999a &&
              std::bit_cast<uint32_t>(kNavAhead) == 0x3f333333 &&
              std::bit_cast<uint32_t>(kLeadMin) == 0x3e4ccccd &&
              std::bit_cast<uint32_t>(kRandomScale) == 0x37800000, "constants as the original's");

RigidBody *BodyOf(AIVehicle *vehicle) {
    return fgSimulation.GetRigidBody(vehicle->GetPhysicsObject()->rigidBodySlot);
}

RigidBody *BodyOf(PhysicsObject *object) {
    return fgSimulation.GetRigidBody(object->rigidBodySlot);
}

const Coord3 &Forward(RigidBody *body) {
    return *Xyz(MatrixRow(&body->info->orientation, 2));
}

// AIZoneController's zones are statics reached through its Get(), which the original calls for each use
AIZone &Zone(int zone) {
    AIZoneController::Get();
    return AIZoneData.zones[zone];
}

int ZoneCount() {
    AIZoneController::Get();
    return kZones;
}

// The car's virtual methods (PVehicle's slots)
PVehicle *Car(PhysicsObject *object) {
    return static_cast<PVehicle *>(object);
}

ABaseSound *GetAudio(PhysicsObject *object) {
    return Car(object)->GetAudio();
}

void SetCarClass(PhysicsObject *object, int carClass) {
    Car(object)->SetCarClass(carClass);
}

void InitializeCarControls(PhysicsObject *object) {
    Car(object)->InitializeCarControls();
}

uint8_t EmpActive(PhysicsObject *object) {
    return Car(object)->EmpActive();
}

void ResetCarAt(PhysicsObject *object, Coord3 *position, const Coord3 *direction) {
    Car(object)->ResetCar(position, direction);
}

int InShock(PhysicsObject *object) {
    return Car(object)->InShock();
}

// The weight plus the penalty, kept within a byte
uint8_t AddLanePenalty(uint8_t weight, float penalty) {
    int sum = weight + RoundToInt(penalty);
    if (sum > kBlockedLane)
        return kBlockedLane;
    if (sum < 0)
        return 0;
    return uint8_t(sum);
}

} // namespace

// ---------------------------------------------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------------------------------------------

// FUNC_AT(0x0002bd40)
AIGroundVehicle* AIGroundVehicle::Construct() {
    AIVehicle::Construct();
    vtable = kAIGroundVehicleVtable;
    type = kAIGroundNone;
    WRoadNav *nav = static_cast<WRoadNav *>(UMemory::FastAlloc(sizeof(WRoadNav), "WRoadNav"));
    collNav = nav != NULL ? nav->Construct() : NULL;
    nav = static_cast<WRoadNav *>(UMemory::FastAlloc(sizeof(WRoadNav), "WRoadNav"));
    laneCollNav = nav != NULL ? nav->Construct() : NULL;
    nav = static_cast<WRoadNav *>(UMemory::FastAlloc(sizeof(WRoadNav), "WRoadNav"));
    heliCollNav = nav != NULL ? nav->Construct() : NULL;
    nav = static_cast<WRoadNav *>(UMemory::FastAlloc(sizeof(WRoadNav), "WRoadNav"));
    targetNav = nav != NULL ? nav->Construct() : NULL;
    Reset();
    return this;
}

// FUNC_AT(0x0002be70)
void AIGroundVehicle::Destruct() {
    vtable = kAIGroundVehicleVtable;
    WRoadNav *navs[] = {collNav, laneCollNav, heliCollNav, targetNav};
    for (WRoadNav *nav : navs) {
        if (nav != NULL) {
            nav->Destruct();
            UMemory::FastFree(nav, sizeof(WRoadNav));
        }
    }
    AIVehicle::Destruct();
}

// FUNC_AT(0x00035d90)
AIGroundVehicle* AIGroundVehicle::Delete(unsigned flags) {
    if (flags & 2) {
        AIVEHICLE_UNTESTED("AIGroundVehicle's vector deleting destructor (an array)");
        int32_t *block = reinterpret_cast<int32_t *>(this) - kArrayCookieWords;
        Crt::EhVectorDestructor(this, sizeof(AIGroundVehicle), block[0],
                                reinterpret_cast<Crt::ElementFn>(XbeAddress(&AIGroundVehicle::Destruct)));
        if (flags & 1)
            OperatorDelete(block);
        return reinterpret_cast<AIGroundVehicle *>(block);
    }
    Destruct();
    if (flags & 1)
        UMemory::FastFree(this, sizeof(AIGroundVehicle));
    return this;
}

// Leaves the modes (navigate, previous, target, attack) as they are
// FUNC_AT(0x00029d60)
void AIGroundVehicle::Reset() {
    const Coord3 zero = {0.0f, 0.0f, 0.0f};

    driveToNav->Reset();
    SetSplinePath(NULL);
    SetPhysicsObject(NULL);
    active = false;
    commanded = 0;
    index = 0;
    type = kAIGroundNone;
    navigateModeStep = 0;
    command = NULL;
    targetSpeed = 0.0f;
    road = -1;
    skipFullUpdate = 0;
    for (int lane = 0; lane < kLanes; lane++)
        laneWeights[lane] = 0;
    laneSpan = 3;
    hitByCollision = 0;
    collisionKind = 0;
    shocked = 0;
    collNavStep = 0;
    collNav->Reset();
    laneCollNavStep = 0;
    laneCollNav->Reset();
    heliCollNavStep = 0;
    heliCollNav->Reset();
    targetNav->Reset();
    isSub = 0;
    followTargetHeight = 0;
    maxHeight = 0.0f;
    minHeight = 0.0f;
    heightPhase = 0.0f;
    heightSwing = 0.0f;
    hasTurret = 0;
    useGuns = 0;
    useRockets = 0;
    useMissiles = 0;
    accuracy = 0;
    aimScale = 0;
    autoDriveSpeed = 1.0f;
    maxSpeed = 0.0f;
    speedScale = 0.0f;
    reversing = 0;
    chargeRollStep = fgSimulation.stepsPerSecond * 5;
    chargeSteps = 0;
    chargeChance = 0.0f;
    unknown140 = 0.0f;
    useLanes = 1;
    unknown145 = 0;
    airSteps = 0;
    autoDriveSteering = 0;
    zoneStopSteps = 0;
    enteredZone = 0;
    oiled = 0;
    stopped = 0;
    minSimUpdate = 0;
    navPosition = zero;
    navDirection = zero;
    targetPosition = zero;
    goal = zero;
    goalDirection = zero;
    steerPoint = zero;
    navHeading = zero;
    aimPoint = zero;
    leadPoint = zero;
    stopping = 0;
    atRedLight = 0;
    heldStill = 0;
    newCommand = 0;
    target = NULL;
    goalDistance = 0.0f;
    targetSegment = -1;
    commandTarget = NULL;
    secondaryTarget = NULL;
    pathCheckStep = 0;
    goalHidden = 1;
    goalInSight = 0;
    sawTarget = 0;
    navInSight = 1;
    attackRange = 0.0f;
    weaponsEnabled = 1;
    gunSteps = 0;
    fireDelay = 0.0f;
    rocketSteps = 0;
    rocketSide = 0;
    missileSteps = 0;
    missileSide = 0;
    turretAngle = 0.0f;
    turretOnTarget = 0;
    stuckEventStep = 0;
    stuckPosition = zero;
    facingNav = 1;
    spawnPosition = zero;
    stuckEvent = 0;
    stuckSteps = 0;
    stuck = 0;
    maySpawn = 0;
    spawnStep = 0;
}

// ---------------------------------------------------------------------------------------------------------------
// The modes
// ---------------------------------------------------------------------------------------------------------------

// FUNC_AT(0x0002a0d0)
void AIGroundVehicle::SetNavigateMode(int mode, CARP::AISpline *spline) {
    if (mode == 0)
        return;
    navigateMode = mode;
    if (type == kAIGroundTraffic) {
        driveToNav->mode = mode == kGroundNavigateNone ? 0 : kNavLane;
        return;
    }
    if (mode != kGroundNavigateSpline)
        SetSplinePath(NULL);
    if (mode == kGroundNavigateNone) {
        driveToNav->mode = 0;
        return;
    }

    Coord3 position = BodyOf(this)->position;
    Coord3 forward = Forward(BodyOf(this));
    if (navigateMode == kGroundNavigateRandom) {
        driveToNav->mode = kNavRandom;
        driveToNav->InitAtPoint(&position, &forward, !useLanes);
        bool heli = GetPhysicsObject()->attributes.LookupBool("IS_HELI", NULL);
        heightPhase = NextHeightPhase;
        heightSwing = heli ? kHeliHeightSwing : kHeightSwing;
        if (NextHeightPhase > 1.0f)
            NextHeightPhase = 0.0f;
        else
            NextHeightPhase += kHeightPhaseStep;
    } else if (navigateMode == kGroundNavigateRoad) {
        driveToNav->mode = kNavDirection;
        driveToNav->InitAtPoint(&position, &forward, false);
    } else if (navigateMode == kGroundNavigateSpline) {
        driveToNav->mode = 0;
        SetSplinePath(spline);
    }
}

// FUNC_AT(0x0002a2c0)
void AIGroundVehicle::SetTargetMode(int mode, const Coord3 *position) {
    if (mode == kGroundTargetPlayer) {
        target = AIVehicleController::Get()->GetPlayerGroundVehiclePtr(0);
    } else if (mode == kGroundTargetPosition) {
        target = NULL;
        targetPosition = *position;
    }
    targetMode = mode;
    commandTarget = target;
    goalHidden = 0;
}

// FUNC_AT(0x0002a330)
void AIGroundVehicle::SetAttackMode(int mode) {
    if (mode == 0)
        return;
    attackMode = mode;
    if (mode == kGroundAttackChase && GetPhysicsObject()->attributes.LookupBool("HAS_SIREN", NULL))
        static_cast<ATrafficVehicle *>(GetAudio(GetPhysicsObject()))->sirenOn = true;
}

// FUNC_AT(0x0002a380)
void AIGroundVehicle::SetSecondaryTarget(void *element) {
    if (element == NULL)
        return;
    secondaryTarget = element;
    AIElement *aiElement = fgAIElementController->RedirectCarpElement(static_cast<AIElement *>(element));
    if (aiElement->state == kElementAwake && aiElement->vehicle->vehicleType == kAIGroundVehicle)
        target = static_cast<AIGroundVehicle *>(aiElement->vehicle);
}

// FUNC_AT(0x0002a3c0)
Coord3* AIGroundVehicle::GetTargetPos() {
    if (target != NULL)
        return Xyz(target->GetPosition());
    return &targetPosition;
}

// FUNC_AT(0x0002a3e0)
void AIGroundVehicle::UpdateNavPos(Coord3 *point, float distance) {
    if (minSimUpdate) {
        if (!stopping) {
            driveToNav->IncNavPosition(TrafficStepDistance, &goalDirection, targetSegment);
            navPosition = driveToNav->roadPoint;
            WRoadSegment *segment = fgRoadNetworkData.segments[driveToNav->segment];
            WRoadNetwork::Get();
            if (segment->flags & kSegmentCurved) {
                Coord4 tangent;
                driveToNav->spline->EvaluateTangent(driveToNav->t, &tangent);
                navDirection = *Xyz(&tangent);
            } else {
                navDirection = driveToNav->direction;
            }
        }
    } else {
        Coord3 roadPoint = driveToNav->roadPoint;
        Coord3 position = BodyOf(this)->position;
        float ahead;
        if (isSub && followTargetHeight)
            ahead = VU0_v3distancexz(&position, &roadPoint);
        else
            ahead = vec3distance(&position, &roadPoint);

        // The way to move the navigator: towards the target, away from it, or towards the goal. The original reads
        // the target's position in the unknownB1 and unknownB2 cases without checking it is there.
        Coord3 heading;
        if (target != NULL && attackMode == kGroundAttackChase) {
            VU0_v4sub(&BodyOf(target)->position, &position, &heading);
            VU0_v4unitxyz(&heading, &heading);
        } else if (driveToNav->unknownB1) {
            VU0_v4sub(&BodyOf(target)->position, &position, &heading);
            VU0_v4unitxyz(&heading, &heading);
        } else if (driveToNav->unknownB2) {
            VU0_v4sub(&position, &BodyOf(target)->position, &heading);
            VU0_v4unitxyz(&heading, &heading);
        } else {
            heading = goalDirection;
        }
        if (ahead < distance)
            driveToNav->IncNavPosition(float(fabs(double(distance) - ahead)), &heading, targetSegment);
    }
    *point = driveToNav->roadPoint;
}

// FUNC_AT(0x0002a690)
void AIGroundVehicle::EngageAutoDrive() {
    SetCarClass(GetPhysicsObject(), kCarClass1);
    InitializeCarControls(GetPhysicsObject());
    type = kAIDrivingOnRoad;
    attackMode = kGroundAttackAutoDrive;
    SetNavigateMode(kGroundNavigateRandom, NULL);
    target = NULL;
    Coord3 position = BodyOf(this)->position;
    Coord3 forward = Forward(BodyOf(this));
    driveToNav->mode = kNavRandom;
    driveToNav->InitAtPoint(&position, &forward, false);
}

// FUNC_AT(0x0002a760)
void AIGroundVehicle::DisengageAutoDrive() {
    SetCarClass(GetPhysicsObject(), kPlayerCar);
    InitializeCarControls(GetPhysicsObject());
    type = kAIGroundPlayer;
    attackMode = kGroundAttackMove;
    navigateMode = kGroundNavigateNone;
    SetSplinePath(NULL);
    driveToNav->mode = 0;
    target = NULL;
}

// The body is fetched and not read
// FUNC_AT(0x0002a7c0)
void AIGroundVehicle::DoPlayerLogic() {
    BodyOf(this);
    int steps = zoneStopSteps - 1;
    zoneStopSteps = steps < 0 ? 0 : steps;
}

// FUNC_AT(0x0002a800)
void AIGroundVehicle::EngageSplinePath(CARP::AISpline *spline) {
    active = true;
    commanded = 1;
    SetCarClass(GetPhysicsObject(), kCarClass1);
    InitializeCarControls(GetPhysicsObject());
    type = kAIGroundSplinePath;
    SetSplinePath(spline);
}

// The spline path is cleared twice
// FUNC_AT(0x0002a850)
void AIGroundVehicle::DisengageSplinePath() {
    SetCarClass(GetPhysicsObject(), kPlayerCar);
    InitializeCarControls(GetPhysicsObject());
    type = kAIGroundPlayer;
    attackMode = kGroundAttackMove;
    navigateMode = kGroundNavigateNone;
    SetSplinePath(NULL);
    driveToNav->mode = 0;
    target = NULL;
    SetSplinePath(NULL);
}

// The car put where the spline path's instance is, moving as it moves: the path's position goes into the
// orientation's translation row before the placement is applied, and the body takes that row as its position
// FUNC_AT(0x0002a8b0)
void AIGroundVehicle::DoSplinePathUpdate() {
    AISplinePath *path = GetSplinePath();
    RPathHandle *handle = path->path;
    MATRIX4 orientation;
    handle->GetOrientMat(&orientation);
    Coord3 *position = Xyz(MatrixRow(&orientation, 3));
    *position = *handle->GetPosition();
    VU0_MATRIX4_mult(&orientation, &orientation, &path->placement);
    BodyOf(this)->ResetObject(&orientation, position);
    BodyOf(this)->velocity = handle->velocity;
    Coord3 momentum = handle->velocity;
    VU0_v4scale(&momentum, BodyOf(this)->mass, &momentum);
    BodyOf(this)->momentum = momentum;
}

// ---------------------------------------------------------------------------------------------------------------
// The world and the roads
// ---------------------------------------------------------------------------------------------------------------

// The world position is made and not used
// FUNC_AT(0x0002a9e0)
bool AIGroundVehicle::DrivableVec(const Coord3 *from, const Coord3 *to) {
    WWorldPos place;
    place.Construct();
    bool drivable = false;
    if (!(vec3distance(from, to) > kMaxDrivableDistance)) {
        Coord4 segment[2] = {
            {from->x, from->y, from->z, 1.0f},
            {to->x, to->y, to->z, 1.0f},
        };
        WorldCollisionInfo info;
        info.Construct();
        int hit = fgCollisionMgr->CheckHitWorld(segment, &info);
        drivable = hit == 0 || hit == 1;
    }
    NullFunction();     // WWorldPos's destructor
    return drivable;
}

// The world position is made and not used; the second probe reuses the first's collision record unreset
// FUNC_AT(0x0002aaf0)
bool AIGroundVehicle::BarriersInPath(bool reversing) {
    if (!hitByCollision || collisionKind != 6)
        return false;
    hitByCollision = 0;
    WWorldPos place;
    place.Construct();

    Coord3 position = BodyOf(this)->position;
    Coord3 heading = Forward(BodyOf(this));
    if (reversing) {
        Coord3 backwards = {-heading.x, -heading.y, -heading.z};
        heading = backwards;
    }
    float reach = BodyOf(this)->info->halfExtents.z * 2.0f;

    bool blocked;
    Coord4 segment[2];
    if (reversing) {
        // A probe to each rear corner
        Coord3 side = {heading.z, heading.y, -heading.x};
        Coord3 end;
        VU0_v4scaleadd(&side, BodyOf(this)->info->halfExtents.x, &position, &end);
        VU0_v4scaleadd(&heading, reach, &end, &end);
        Coord4 made;
        segment[0] = *MakeCoord4(&made, &position, 1.0f);
        segment[1] = *MakeCoord4(&made, &end, 1.0f);
        WorldCollisionInfo info;
        info.Construct();
        int hit = fgCollisionMgr->CheckHitWorld(segment, &info);
        if (hit > 0 && hit != 1) {
            blocked = true;
        } else {
            Coord3 otherSide;
            MakeCoord3(&otherSide, -side.x, side.y, -side.z);
            VU0_v4scaleadd(&otherSide, BodyOf(this)->info->halfExtents.x, &position, &end);
            VU0_v4scaleadd(&heading, reach, &end, &end);
            segment[0] = *MakeCoord4(&made, &position, 1.0f);
            segment[1] = *MakeCoord4(&made, &end, 1.0f);
            hit = fgCollisionMgr->CheckHitWorld(segment, &info);
            blocked = hit > 0 && hit != 1;
        }
    } else {
        Coord3 end;
        VU0_v4scaleadd(&heading, reach, &position, &end);
        Coord4 made;
        segment[0] = *MakeCoord4(&made, &position, 1.0f);
        segment[1] = *MakeCoord4(&made, &end, 1.0f);
        WorldCollisionInfo info;
        info.Construct();
        int hit = fgCollisionMgr->CheckHitWorld(segment, &info);
        blocked = hit > 0 && hit != 1;
    }
    NullFunction();     // WWorldPos's destructor
    return blocked;
}

// FUNC_AT(0x0002af50)
void AIGroundVehicle::UpdateStuck() {
    BondCarControl control;
    if (!(Car(GetPhysicsObject())->GetCarControl(&control)->gas >= kStuckControl ||
          Car(GetPhysicsObject())->GetCarControl(&control)->brake >= kStuckControl ||
          InShock(GetPhysicsObject()))) {
        stuckSteps = 0;
        return;
    }
    if (!stuck) {
        if (stuckSteps == 0) {
            stuckPosition = *Xyz(GetPosition());
            stuckSteps++;
            return;
        }
        stuckSteps++;
        if (stuckSteps < fgSimulation.stepsPerSecond * kStuckSeconds)
            return;
        Coord3 position = *Xyz(GetPosition());
        if (vec3distance(&stuckPosition, &position) < kStuckDistance) {
            stuck = 1;
            return;
        }
    } else {
        Coord3 position = *Xyz(GetPosition());
        if (!(vec3distance(&stuckPosition, &position) > kStuckDistance))
            return;
        stuck = 0;
    }
    stuckSteps = 0;
}

// FUNC_AT(0x0002b0c0)
WRoadNav* AIGroundVehicle::GetHeliCollNav(const Coord3 *heading) {
    if (heliCollNavStep == 0 || heliCollNavStep + kCollNavSteps <= fgSimulation.stepCount) {
        Coord3 ahead = BodyOf(this)->position;
        Coord3 velocity = BodyOf(this)->velocity;
        VU0_v4scaleadd(&velocity, kCollNavLead, &ahead, &ahead);
        heliCollNavStep = fgSimulation.stepCount;
        heliCollNav->mode = kNavDirection5;
        Coord3 forward = Forward(BodyOf(this));
        heliCollNav->InitAtPoint(&ahead, &forward, false);
    }
    Coord3 direction = heliCollNav->direction;
    if (v3dotprod(&direction, heading) < 0.0f)
        heliCollNav->ReverseNavDirection();
    return heliCollNav;
}

// FUNC_AT(0x0002b210)
WRoadNav* AIGroundVehicle::GetCollNav(const Coord3 *heading, bool inLane, int8_t lane) {
    if (inLane) {
        if (laneCollNavStep == 0 || laneCollNavStep + kCollNavSteps <= fgSimulation.stepCount ||
            laneCollNav->unknown04 != lane) {
            Coord3 ahead = BodyOf(this)->position;
            Coord3 velocity = BodyOf(this)->velocity;
            VU0_v4scaleadd(&velocity, kCollNavLead, &ahead, &ahead);
            laneCollNavStep = fgSimulation.stepCount;
            laneCollNav->mode = kNavRandom;
            laneCollNav->unknown04 = uint8_t(lane);
            Coord3 forward = Forward(BodyOf(this));
            laneCollNav->InitAtPoint(&ahead, &forward, false);
        }
        Coord3 direction = laneCollNav->direction;
        if (v3dotprod(&direction, heading) < 0.0f)
            laneCollNav->ReverseNavDirection();
        return laneCollNav;
    }
    if (collNavStep == 0 || collNavStep + kCollNavSteps <= fgSimulation.stepCount) {
        Coord3 ahead = BodyOf(this)->position;
        Coord3 velocity = BodyOf(this)->velocity;
        VU0_v4scaleadd(&velocity, kCollNavLead, &ahead, &ahead);
        collNavStep = fgSimulation.stepCount;
        collNav->mode = kNavDirection;
        Coord3 forward = Forward(BodyOf(this));
        collNav->InitAtPoint(&ahead, &forward, false);
    }
    Coord3 direction = collNav->direction;
    if (v3dotprod(&direction, heading) < 0.0f)
        collNav->ReverseNavDirection();
    return collNav;
}

// FUNC_AT(0x0002b4d0)
bool TestForCollision(PhysicsObject *object, const Coord3 *point, const Coord3 *other, float range) {
    Coord3 toPoint;
    VU0_v4sub(point, &BodyOf(object)->position, &toPoint);
    VU0_v4unitxyz(&toPoint, &toPoint);
    Coord3 toOther;
    VU0_v4sub(other, &BodyOf(object)->position, &toOther);
    VU0_v4unitxyz(&toOther, &toOther);
    if (v3dotprod(&toPoint, &toOther) < 0.9f)
        return false;
    if (vec3distance(other, point) < range)
        return true;
    RigidBody *body = BodyOf(object);
    RigidBody *sameBody = BodyOf(object);
    float otherDistance = vec3distance(&body->position, other);
    return vec3distance(&sameBody->position, point) > otherDistance;
}

// FUNC_AT(0x0002b5e0)
int AIGroundVehicle::GetBestWeightedLane(int /* lane */) {
    int span = laneSpan;
    int best = kMiddleLane;
    int bestWeight = span * kBlockedLane;
    for (int first = 0; first < kLanes - span; first++) {
        int weight = 0;
        for (int lane = first; lane < first + span; lane++)
            weight += laneWeights[lane];
        if (weight < bestWeight) {
            best = span / 2 + first;
            bestWeight = weight;
        }
    }
    return best;
}

// FUNC_AT(0x0002b660)
void AIGroundVehicle::GetRoadNetworkLaneWeights() {
    WRoadNetwork::Get()->GetSegmentLaneWeights(driveToNav->segment, int8_t(driveToNav->forward),
                                               reinterpret_cast<int8_t *>(laneWeights), kLanes, kMiddleLane);
    int wanted = RoundToInt(driveToNav->laneOffset) + kMiddleLane;
    for (int lane = 0; lane < kLanes; lane++) {
        if (laneWeights[lane] >= kBlockedLane)
            continue;
        float penalty = float(fabs(double(wanted - lane)) * kLanePenalty);
        laneWeights[lane] = AddLanePenalty(laneWeights[lane], penalty);
    }
}

// The world position is made and not used; the second probe reuses the first's collision record unreset
// FUNC_AT(0x0002b7a0)
void AIGroundVehicle::GetDirectLaneWeights(const Coord3 *point) {
    WWorldPos place;
    place.Construct();
    for (int lane = 0; lane < kLanes; lane++)
        laneWeights[lane] = 0;

    Coord3 position = BodyOf(this)->position;
    Coord3 at = *point;
    Coord3 heading;
    VU0_v4sub(&at, &position, &heading);
    VU0_v4unitxyz(&heading, &heading);
    Coord3 side = {heading.z, heading.y, -heading.x};
    WCollider collider;
    collider.Construct(&at, kDirectLaneReach + kDirectLaneReach, false,
                       WCollider::kCollideBarriers | WCollider::kCollideInstances);
    WorldCollisionInfo info;
    info.Construct();

    // A barrier to one side blocks the lanes past it
    Coord4 widened;
    Coord4 segment[2];
    segment[0] = *Float_COORD3toCOORD4(&widened, &at);
    Coord3 end;
    VU0_v4scaleadd(&side, kDirectLaneReach, &at, &end);
    segment[1] = *Float_COORD3toCOORD4(&widened, &end);
    segment[1].w = 1.0f;
    segment[0].w = 1.0f;
    if (collider.GetWorldNormal(segment, &info)) {
        Coord3 hit = *Float_COORD4toCOORD3(&end, &info.point);
        float distance = vec3distance(&at, &hit);
        int edge = RoundToInt(distance) + kMiddleLane;
        if (edge < 0)
            edge = 0;
        for (int lane = kLanes - 1; lane > edge; lane--)
            laneWeights[lane] = kBlockedLane;
    }
    // ... and to the other
    Coord3 otherSide = {-side.x, side.y, -side.z};
    side = otherSide;
    VU0_v4scaleadd(&side, kDirectLaneReach, &at, &end);
    segment[1] = *Float_COORD3toCOORD4(&widened, &end);
    segment[1].w = 1.0f;
    segment[0].w = 1.0f;
    if (collider.GetWorldNormal(segment, &info)) {
        Coord3 hit = *Float_COORD4toCOORD3(&end, &info.point);
        float distance = vec3distance(&at, &hit);
        int edge = kMiddleLane - RoundToInt(distance);
        if (edge > kLanes)
            edge = kLanes;
        for (int lane = 0; lane < edge; lane++)
            laneWeights[lane] = kBlockedLane;
    }

    for (int lane = 0; lane < kLanes; lane++) {
        float penalty = float(fabs(double(kDirectLaneMiddle) - lane) * kLanePenalty);
        laneWeights[lane] = AddLanePenalty(laneWeights[lane], penalty);
    }
    laneWeights[kMiddleLane - 1] = 0;
    laneWeights[kMiddleLane] = 0;
    laneWeights[kMiddleLane + 1] = 0;
    collider.Destruct();
    NullFunction();     // WWorldPos's destructor
}

// ---------------------------------------------------------------------------------------------------------------
// Each step
// ---------------------------------------------------------------------------------------------------------------

// The speed by attack mode, within the limits each sets
// FUNC_AT(0x0002bf40)
void AIGroundVehicle::DoAttackMode() {
    if (type == kAIGroundAgent && GetPhysicsObject()->GetHitPoints() <= 0.0f) {
        targetSpeed = 0.0f;
        return;
    }
    if (type == kAIGroundTraffic && GetPhysicsObject()->GetHitPoints() < kTrafficMinHitPoints) {
        targetSpeed = 0.0f;
        return;
    }
    if (EmpActive(GetPhysicsObject()) || stopped) {
        targetSpeed = 0.0f;
        return;
    }

    switch (attackMode) {
    case kGroundAttackMove: {
        Coord3 at = goal;
        Coord3 position = BodyOf(this)->position;
        float flatDistance = VU0_v3distancexz(&at, &position);
        if (flatDistance <= kArrivedDistance)
            break;
        if (MaxFloat(kSlowRange, attackRange) >= flatDistance)
            targetSpeed = goalDistance * 0.5f;
        else
            targetSpeed = maxSpeed;
        return;
    }
    case kGroundAttack12:
        targetSpeed = kAttack12Speed;
        return;
    case kGroundAttackChase: {
        targetSpeed = kChaseSpeed;
        if (goalHidden == 0 && target != NULL) {
            float range = attackRange;
            if (Car(target->GetPhysicsObject())->GetCarClass() == kCarClass1) {
                RigidBodyInfo *targetInfo = BodyOf(target)->info;
                RigidBodyInfo *info = BodyOf(this)->info;
                float reach = float(double(info->halfExtents.z) * kChaseReach +
                                    double(targetInfo->halfExtents.z) * kChaseReach);
                range = MaxFloat(reach, attackRange);
                if (chargeSteps != 0 && goalDistance <= reach)     // the half lengths', not the range
                    chargeSteps = 0;
            }
            float targetFlatSpeed = VU0_v3lengthxz(&BodyOf(target)->velocity);
            bool turnedAway = false;
            if (navigateMode == kGroundNavigateRandom) {
                Coord3 heading = BodyOf(this)->velocity;
                VU0_v4unitxyz(&heading, &heading);
                turnedAway = v3dotprod(&heading, &goalDirection) < kChaseFacing;
            }
            if (turnedAway) {
                chargeSteps = 0;
                targetSpeed = float(double(targetFlatSpeed) * kChaseSlowdown);
            } else if (goalDistance < range) {
                // Close: by the target's speed while it heads towards the goal (the car's own body is fetched and
                // not read)
                BodyOf(this);
                Coord3 heading = BodyOf(target)->velocity;
                VU0_v4unitxyz(&heading, &heading);
                float facing = v3dotprod(&heading, &goalDirection);
                if (chargeSteps != 0)
                    targetSpeed = MaxFloat(kChaseSpeed, float(double(targetFlatSpeed) * speedScale));
                else if (facing > 0.0f)
                    targetSpeed = float(double(targetFlatSpeed) * kFollowSpeed);
                else
                    targetSpeed = 0.0f;
            } else {
                float approach =
                    MinFloat(speedScale, float(goalDistance / double(MaxFloat(range, 1.0f)) * kApproachScale));
                float speed = MaxFloat(kChaseSpeed, float(double(approach) * targetFlatSpeed));
                targetSpeed = speed;
                if (double(range) * 2.0 < goalDistance || chargeSteps != 0)
                    targetSpeed = float(double(speed) * speedScale);
            }
        } else if (goalHidden == 1) {
            float speed = kHiddenChaseSpeed;
            if (target != NULL)
                speed = MaxFloat(kHiddenChaseSpeed,
                                 float(double(VU0_v3lengthxz(&BodyOf(target)->velocity)) * speedScale));
            targetSpeed = speed;
        }
        float cap = maxSpeed;
        if (secondaryTarget != NULL)
            cap = float(double(glbMissionManager->GetEnemyGlueFactor()) * cap);
        targetSpeed = MinFloat(cap, targetSpeed);
        return;
    }
    case kGroundAttack14:
        targetSpeed = kAttack14Speed;
        if (goalHidden == 0) {
            // The original reads the target without checking it is there
            Coord3 forward = Forward(BodyOf(this));
            float facing = v3dotprod(&forward, &goalDirection);
            if (goalDistance < attackRange)
                targetSpeed = float(double(VU0_v3lengthxz(&BodyOf(target)->velocity)) * speedScale);
            else if (facing >= 0.0f)
                targetSpeed = float(double(VU0_v3lengthxz(&BodyOf(target)->velocity)) * speedScale);
            else
                targetSpeed = float(double(VU0_v3lengthxz(&BodyOf(target)->velocity)) * kFollowSpeed);
        } else {
            targetSpeed = kHiddenAttack14Speed;
        }
        targetSpeed = MinFloat(maxSpeed, MaxFloat(kAttack14Speed, targetSpeed));
        return;
    case kGroundAttackAutoDrive: {
        float roadSpeed = driveToNav->SegmentSpeed();
        float speed = roadSpeed;
        if (unknown145 && target != NULL) {
            // Keeping with the target: catching up behind it, easing off ahead of it
            float targetFlatSpeed = VU0_v3lengthxz(&BodyOf(target)->velocity);
            float range = float(double(glbMissionManager->GetEnemyGlueFactor()) * attackRange);
            if (goalDistance < range || v3dotprod(&navHeading, &goalDirection) >= 0.0f)
                speed = MaxFloat(roadSpeed, float(double(targetFlatSpeed) * kAutoDriveCatchUp));
            else if (double(range) * 2.0 <= goalDistance)
                speed = float(double(roadSpeed) * kAutoDriveSlowdown);
            else if (goalDistance >= range)
                speed = MaxFloat(float(double(roadSpeed) * kAutoDriveFollow),
                                 float(double(targetFlatSpeed) * kAutoDriveFollow));
            speed = MinFloat(float(double(glbMissionManager->GetEnemyGlueFactor()) * maxSpeed),
                             speed);
            if (zoneStopSteps != 0)
                speed = 0.0f;
        }
        targetSpeed = speed * autoDriveSpeed;
        return;
    }
    }
    targetSpeed = 0.0f;
}

// The turret turned a step at a time towards the goal (within TURRET_RANGE), and its animation set to match
// FUNC_AT(0x0002c6e0)
void AIGroundVehicle::TurnTurret() {
    if (goalHidden != 0 && targetMode != kGroundTargetPosition)
        return;
    Coord3 forward = Forward(BodyOf(this));
    float facing = v3dotprod(&goalDirection, &forward);
    float cosine = facing < -1.0f ? -1.0f : facing;
    cosine = 1.0f < cosine ? 1.0f : cosine;
    float angle = float(CrtAcosTimes(cosine, &kInvTwoPi));
    Coord3 across;
    VU0_v4crossprodxyz(&goalDirection, &forward, &across);
    if (across.y < 0.0f)
        angle = 1.0f - angle;

    bool found = false;
    const AttributeVector *range = GetPhysicsObject()->attributes.LookupVector("TURRET_RANGE", &found);
    bool inRange = true;
    if (found) {
        double degrees = double(angle) * kDegreesPerTurn;
        inRange = (degrees >= range->x && degrees <= range->y) || (degrees >= range->z && degrees <= range->w);
    }
    if (inRange) {
        if (fabs(double(angle) - turretAngle) > kTurretStep) {
            // The shorter way round, through 0 when the two are either side of it
            bool decrease;
            if (angle > turretAngle && angle >= 0.75f)
                decrease = turretAngle <= 0.25f;
            else if (angle < turretAngle && angle <= 0.25f)
                decrease = !(turretAngle >= 0.75f);
            else
                decrease = !(angle > turretAngle);
            if (decrease)
                turretAngle = turretAngle - kTurretStep;
            else
                turretAngle = kTurretStep + turretAngle;
            if (turretAngle > 1.0f)
                turretAngle = turretAngle - 1.0f;
            else if (turretAngle < 0.0f)
                turretAngle = turretAngle + 1.0f;
        } else {
            turretAngle = angle;
        }
    }
    int frame = RoundToInt(turretAngle * kDegreesPerTurn);
    Handle *animation = GetPhysicsObject()->renderObject->animHandle;
    if (animation != NULL)
        animation->SetFrame(kTurretAnimation, kTurretStimulus, frame);
    turretOnTarget = turretAngle == angle;
}

// A player's car in auto-drive: its navigator kept in sight and ahead of it, placed again after a jump, and the car
// put back on the road when stuck
// FUNC_AT(0x0002c9d0)
void AIGroundVehicle::DoAutoDriveUpdate() {
    if (skipFullUpdate == 0 &&
        short(Ftol(fgSimulation.stepsPerSecond * kPathCheckSeconds)) + pathCheckStep <= fgSimulation.stepCount) {
        pathCheckStep = fgSimulation.stepCount;
        Coord3 position = BodyOf(this)->position;
        Coord3 roadPoint = driveToNav->roadPoint;
        navInSight = DrivableVec(&position, &roadPoint);
    }
    if (!navInSight) {
        Coord3 forward = Forward(BodyOf(this));
        Coord3 position = BodyOf(this)->position;
        driveToNav->InitAtPoint(&position, &forward, true);
        navInSight = 1;
    }

    Coord3 forward = Forward(BodyOf(this));
    Coord3 position = BodyOf(this)->position;
    Coord3 navAt = driveToNav->position;
    VU0_v4sub(&navAt, &position, &navHeading);
    VU0_v4unitxyz(&navHeading, &navHeading);
    if (facingNav) {
        if (v3dotprod(&forward, &navHeading) < kNavBehind) {
            driveToNav->InitAtPoint(&position, &forward, false);
            driveToNav->IncNavPosition(kResetStep, &forward, -1);
            facingNav = 0;
        }
    } else if (v3dotprod(&forward, &navHeading) >= kNavAhead) {
        facingNav = 1;
    }

    if (Car(GetPhysicsObject())->GetNumWheelsOnGround() == 0) {
        airSteps++;
    } else {
        if (airSteps >= fgSimulation.stepsPerSecond) {
            Coord3 landedForward = Forward(BodyOf(this));
            Coord3 landedAt = BodyOf(this)->position;
            driveToNav->InitAtPoint(&landedAt, &landedForward, !useLanes);
        }
        airSteps = 0;
    }

    if (stuck) {
        BodyOf(this)->info->unknown4fc = 1;
        Coord3 direction = driveToNav->direction;
        driveToNav->ChangeLanes(0.0f, 0.0f);
        driveToNav->IncNavPosition(kStuckResetStep, &direction, -1);
        direction = driveToNav->direction;
        Coord3 at = driveToNav->position;
        ResetCarAt(GetPhysicsObject(), &at, &direction);
        stuck = 0;
        stuckSteps = 0;
    }
}

// An agent's step: its target and goal, the AI zones, the charge rolls, the line-of-sight checks, the target's road
// segment and the changes between direct and road navigation
// FUNC_AT(0x0002cd80)
void AIGroundVehicle::DoAgentStateUpdate() {
    PhysicsObject *object = GetPhysicsObject();
    Coord3 position = BodyOf(object)->position;
    if (secondaryTarget != NULL) {
        AIElement *element = fgAIElementController->RedirectCarpElement(static_cast<AIElement *>(secondaryTarget));
        if (element->state == kElementAwake) {
            if (element->vehicle->vehicleType == kAIGroundVehicle)
                target = static_cast<AIGroundVehicle *>(element->vehicle);
        } else {
            target = NULL;
        }
    }

    if (!isSub && skipFullUpdate == 0 && zoneStopSteps == 0) {
        for (int zone = 0; zone < ZoneCount(); zone++) {
            if (Zone(zone).type == kZoneStopType &&
                AIZoneController::Get()->InZone(zone, &position, kZoneRadiusScale) &&
                object->GetHitPoints() > 0.0f) {
                zoneStopSteps = int16_t(fgSimulation.stepsPerSecond);
                enteredZone = 1;
                glbMissionManager->ProgrammerDefinedEvent(kZoneStopEvent, "");
            }
        }
    }

    int steps = zoneStopSteps - 1;
    zoneStopSteps = int16_t(steps < 0 ? 0 : steps);
    steps = chargeSteps - 1;
    chargeSteps = steps < 0 ? 0 : steps;
    steps = chargeRollStep - 1;
    chargeRollStep = steps < 0 ? 0 : steps;
    if (!isSub && chargeRollStep == 0 && goalHidden == 0) {
        chargeRollStep = fgSimulation.stepsPerSecond * 5;
        if (int(fgSimulation.random->Generate()) * double(kRandomScale) < chargeChance) {
            chargeSteps = fgSimulation.stepsPerSecond * 3;
            if (target != NULL && Car(target->GetPhysicsObject())->GetCarClass() == kCarClass1)
                chargeSteps = fgSimulation.stepsPerSecond;
        }
    }
    if (!isSub && stuck && stuckEventStep <= 0) {
        stuckEventStep = int16_t(fgSimulation.stepsPerSecond);
        stuckEvent = 1;
    }
    if (Car(GetPhysicsObject())->GetNumWheelsOnGround() == 0)
        airSteps++;
    else
        airSteps = 0;

    if (skipFullUpdate == 0 &&
        short(Ftol(fgSimulation.stepsPerSecond * kPathCheckSeconds)) + pathCheckStep <= fgSimulation.stepCount) {
        pathCheckStep = fgSimulation.stepCount;
        if (target != NULL) {
            Coord3 targetAt = BodyOf(target)->position;
            goalInSight = DrivableVec(&position, &targetAt);
            if (goalInSight)
                sawTarget = 1;
        } else if (targetMode == kGroundTargetPosition) {
            goalInSight = 1;
        }
        Coord3 roadPoint = driveToNav->roadPoint;
        navInSight = DrivableVec(&position, &roadPoint);
    }

    // The goal: the target (ahead of it on its way when it moves), the command's position, or failing those the
    // road ahead. steerByRoad: the navigator is to follow the target along the roads.
    bool steerByRoad = false;
    Coord3 chosen;
    goalHidden = goalInSight ? 0 : 1;
    if (attackMode == kGroundAttackAutoDrive && target != NULL) {
        goal = BodyOf(target)->position;
    } else if (goalHidden == 0) {
        if (target != NULL && targetMode != kGroundTargetPosition) {
            Coord3 at = BodyOf(target)->position;
            if (VU0_v3lengthxz(&BodyOf(target)->velocity) >= kTargetSlowSpeed) {
                float targetFlatSpeed = VU0_v3lengthxz(&BodyOf(target)->velocity);
                if (navigateMode == kGroundNavigateRoad) {
                    float lead = MinFloat(MaxFloat(kLeadMin, unknown140), targetFlatSpeed);
                    Coord3 heading = BodyOf(target)->velocity;
                    VU0_v4unitxyz(&heading, &heading);
                    VU0_v4scaleadd(&heading, lead, &at, &at);
                    steerByRoad = !(double(targetFlatSpeed) * lead > attackRange) &&
                                  !(MaxFloat(kLeadRange, attackRange) >= goalDistance);
                } else {
                    Coord3 velocity = BodyOf(target)->velocity;
                    VU0_v4scaleadd(&velocity, kLeadTime, &at, &at);
                }
            }
            goal = at;
            chosen = at;
        } else if (targetMode == kGroundTargetPosition) {
            goal = targetPosition;
            chosen = targetPosition;
            steerByRoad = true;
        } else {
            goalHidden = 1;
        }
    }
    goalDistance = vec3distance(&position, &goal);
    VU0_v4sub(&goal, &position, &goalDirection);
    VU0_v4unitxyz(&goalDirection, &goalDirection);
    Coord3 navAt = driveToNav->position;
    VU0_v4sub(&navAt, &position, &navHeading);
    VU0_v4unitxyz(&navHeading, &navHeading);
    if (goalHidden == 1) {
        if (navigateMode == kGroundNavigateDirect) {
            if (target != NULL)
                goal = *Xyz(target->GetPosition());
            else
                VU0_v3add(&position, &Forward(BodyOf(this)), &goal);
        } else {
            goal = driveToNav->roadPoint;
        }
        steerByRoad = target != NULL;
    }
    if (attackMode == kGroundAttack14 || attackMode == kGroundAttackAutoDrive || navigateMode != kGroundNavigateRoad)
        steerByRoad = false;

    if (skipFullUpdate == 0) {
        if (steerByRoad) {
            Coord3 forward = Forward(BodyOf(this));
            if (target != NULL) {
                // On the target's segment, or one ahead: as it is. Behind: turned round.
                targetSegment = target->GetCollNav(&forward, false, 0)->segment;
                if (targetSegment != driveToNav->segment &&
                    !driveToNav->PathForwardRoadSegment(targetSegment, &steerByRoad) && steerByRoad) {
                    Coord3 at = BodyOf(object)->position;
                    driveToNav->InitAtPoint(&at, &driveToNav->direction, false);
                    driveToNav->ReverseNavDirection();
                    driveToNav->IncNavPosition(kResetStep, &driveToNav->direction, -1);
                    facingNav = 0;
                }
            } else {
                targetNav->mode = driveToNav->mode == kNavRandom ? kNavRandom : kNavDirection;
                targetNav->InitAtPoint(&chosen, &forward, false);
                targetSegment = targetNav->segment;
            }
        } else {
            targetSegment = -1;
        }
    }

    if (navigateModeStep != 0) {
        if (fgSimulation.stepCount < navigateModeStep)
            return;
        navigateModeStep = 0;
    }
    if (skipFullUpdate != 0)
        return;
    bool hasRoads = fgRoadNetworkData.unknownAD;
    WRoadNetwork::Get();
    if (!hasRoads)
        return;

    if (attackMode == kGroundAttackChase) {
        if (navigateMode == kGroundNavigateRoad) {
            // To direct driving when the road leads away from the goal, or the target is slow and in range (the
            // original reads the target without checking it is there)
            Coord3 direction = driveToNav->direction;
            if (v3dotprod(&direction, &goalDirection) < kRoadFacing && goalHidden != 1) {
                previousNavigateMode = kGroundNavigateRoad;
                navigateMode = kGroundNavigateDirect;
                navigateModeStep = fgSimulation.stepCount + kModeHoldSteps;
            }
            if (goalHidden == 0 && VU0_v3lengthxz(&BodyOf(target)->velocity) < kTargetSlowSpeed &&
                goalDistance < attackRange) {
                previousNavigateMode = kGroundNavigateRoad;
                navigateMode = kGroundNavigateDirect;
                navigateModeStep = fgSimulation.stepCount + kModeHoldSteps;
            }
        } else if (navigateMode == kGroundNavigateDirect) {
            // Back to the road when moving along it within its drivable width, or when the target is out of sight
            Coord3 forward = Forward(BodyOf(this));
            Coord3 at = BodyOf(object)->position;
            float laneOffset = GetCollNav(&forward, false, 0)->laneOffset;
            float leftWidth = float(GetCollNav(&forward, false, 0)->LeftDrivableDist());
            float along = v3dotprod(&GetCollNav(&forward, false, 0)->direction, &forward);
            if ((VU0_v3lengthxz(&BodyOf(this)->velocity) >= kDirectMinSpeed && fabsf(laneOffset) < leftWidth &&
                 along > kNavAhead) || goalHidden == 1) {
                driveToNav->mode = kNavDirection;
                driveToNav->InitAtPoint(&at, &forward, false);
                driveToNav->IncNavPosition(kResetStep, &forward, -1);
                Coord3 placed = driveToNav->position;
                VU0_v4sub(&placed, &at, &navHeading);
                VU0_v4unitxyz(&navHeading, &navHeading);
                if (v3dotprod(&forward, &navHeading) >= kNavAhead) {
                    previousNavigateMode = kGroundNavigateDirect;
                    navigateMode = kGroundNavigateRoad;
                    facingNav = 0;
                }
                navigateModeStep = fgSimulation.stepCount + kModeHoldSteps;
            }
        }
    }

    if (navigateMode == kGroundNavigateRoad || navigateMode == kGroundNavigateRandom) {
        Coord3 forward = Forward(BodyOf(this));
        if (facingNav) {
            if (v3dotprod(&forward, &navHeading) < kNavBehind) {
                driveToNav->InitAtPoint(&position, &forward, !useLanes);
                driveToNav->IncNavPosition(kResetStep, &forward, -1);
                facingNav = 0;
            }
        } else if (v3dotprod(&forward, &navHeading) >= kNavAhead) {
            facingNav = 1;
        }
    }
    if (navigateMode != kGroundNavigateNone && airSteps >= fgSimulation.stepsPerSecond) {
        Coord3 forward = Forward(BodyOf(this));
        Coord3 at = BodyOf(object)->position;
        driveToNav->InitAtPoint(&at, &forward, !useLanes);
    }
    if (!navInSight && attackMode == kGroundAttackAutoDrive) {
        Coord3 forward = Forward(BodyOf(this));
        Coord3 at = BodyOf(this)->position;
        driveToNav->InitAtPoint(&at, &forward, true);
        navInSight = 1;
    }
}

// FUNC_AT(0x0002dac0)
void AIGroundVehicle::UnSpawn() {
    // Each car parked 50 further along than the last, up to 5000
    float next = NextParkingOffset + kParkingStep;
    NextParkingOffset = next < 0.0f ? 0.0f : (next > kParkingMax ? kParkingMax : next);
    float distance = NextParkingOffset + kParkingDistance;
    attackMode = kGroundAttackMove;
    navigateMode = kGroundNavigateNone;
    if (type != kAIGroundTraffic)
        SetSplinePath(NULL);
    driveToNav->mode = 0;

    Coord3 at = {0.0f, 0.0f, distance};
    Coord3 direction = {1.0f, 0.0f, 0.0f};
    MATRIX4 made;
    MATRIX4 orientation = *Util_GenerateMatrix(&made, &direction);
    BodyOf(this)->ResetObject(&orientation, &at);
    BodyOf(this)->sleepState = RigidBody::kFrozen;

    // AIVehicleController's count, reached through its Get()
    AIVehicleController::Get();
    int16_t count = int16_t(TrafficSpawnCount - 1);
    AIVehicleController::Get();
    TrafficSpawnCount = count < 0 ? 0 : count;

    if (road != -1) {
        int counted = road;
        WRoadNetwork::Get();
        if (counted >= 0 && counted < fgRoadNetworkData.roadCount)
            fgRoadNetworkData.roads[counted]->carCount--;
        road = -1;
    }
    static_cast<AVehicle *>(GetAudio(GetPhysicsObject()))->Reset();
    spawnStep = 0;
}
