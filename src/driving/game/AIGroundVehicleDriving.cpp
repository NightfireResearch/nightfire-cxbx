#pragma fp_contract(off)

#include "AIGroundVehicleDriving.h"

#include <bit>
#include <math.h>
#include <stdlib.h>

#include "AIElements.h"
#include "AIEnemy.h"                    // MinFloat, MaxFloat
#include "AIRoadSpawn.h"
#include "AISplinePath.h"
#include "AIVehicleController.h"
#include "BondCar.h"                    // CarClass
#include "CharacterSound.h"             // MakeCoord3
#include "MissionManager.h"             // glbMissionManager
#include "Simulation.h"                 // fgSimulation
#include "Vehicle.h"                    // PVehicle, CarPhysics
#include "VehicleSound.h"               // AVehicle
#include "WeaponSound.h"                // ASceneObj
#include "../../helpers.h"
#include "../anim/AnimEngine.h"         // Handle
#include "../engine/CoreContainers.h"   // GameVector
#include "../engine/PhysicsUtil.h"      // Util_Bound
#include "../physics/PhysicsMath.h"     // Abs
#include "../physics/PhysicsObject.h"
#include "../physics/RigidBody.h"
#include "../platform/RealMath.h"
#include "../platform/X87.h"
#include "../render/PathEngine.h"       // RPathHandle
#include "../render/RSceneObj.hpp"
#include "../render/WorldCulling.h"     // fgWorldCulling
#include "../world/RoadNav.h"
#include "../world/RoadNetwork.h"

// ---------------------------------------------------------------------------------------------------------------
// AIGroundVehicle, part 2 (0x0002dc90-0x00031850): the look-ahead, the three ways of driving to a point, the stop
// points and collision ranges, the weapons, traffic spawning, the agents' and traffic's collision checks, the move
// modes and Update (its switch's jump table at 0x00031804 decoded from its bytes: types 1 to 5 in order). Ported
// from the listing.
// ---------------------------------------------------------------------------------------------------------------

namespace {

constexpr int kLanes = 0x40;                // laneWeights: lane 0x20 is the road's middle
constexpr int kMiddleLane = 0x20;
constexpr int kBlockedLane = 0xff;
constexpr int kLaneBlock = 0x80;            // the weight another car adds to the lanes it covers
constexpr int kLaneEase = 0x40;             // ... taken off for the target, or the weight a lane is eased by
constexpr float kDirectLaneMiddle = 32.0f;
constexpr float kLaneSpanScale = 2.25f;     // the other car's half length across the road, by this: the lanes it covers
constexpr float kLaneChangeRate = 0.1f;     // a lane change takes this many seconds a lane
constexpr float kLaneDrift = 2.0f;          // in mode 4, a lane offset beyond this halves; within it, back to 0

constexpr int8_t kOwnerKinds = 4;           // bodies of a kind below this have a vehicle as their owner

constexpr float kLookAheadScale = 0.35f;
constexpr float kLookAheadMargin = 7.5f;
constexpr float kTrafficLookAhead = 7.5f;

constexpr float kMinSpeed = 0.05f;          // a speed below this: stop
constexpr float kMinPedal = 0.05f;
constexpr float kStillSpeed = 0.1f;
constexpr float kFacingAhead = 0.7f;
constexpr float kFacingBehind = -0.15f;
constexpr float kSpeedRange = 15.0f;        // the speed error giving a full pedal
constexpr float kPedalScale = 1.0f / 15.0f;
constexpr float kSettleSpeedError = 2.5f;
constexpr float kCruiseSpeed = 5.0f;
constexpr float kReverseSpeed = 7.5f;
constexpr float kChaseCloseRange = 10.0f;
constexpr float kTargetOncoming = -0.7f;

constexpr float kTrafficMinSpeed = 0.5f;
constexpr float kTrafficStopDistance = 5.0f;
constexpr float kTrafficTurnRound = -0.2f;
constexpr float kTrafficSpeedRange = 7.5f;
constexpr float kTrafficCrawl = 2.5f;
constexpr float kTrafficMinHitPoints = 0.5f;
constexpr float kShockSettledSpeed = 0.2f;
constexpr float kStopPedal = 0.25f;

constexpr float kSubLevel = 0.707f;         // a sub's forward |y| up to this: its axes made from the world's up
constexpr float kSubSlowdown = 0.75f;
constexpr float kSubPitchLimit = 0.4f;
constexpr float kSubSpeedRange = 5.0f;
constexpr float kSubPedalScale = 0.2f;
constexpr float kHeightPhaseRate = 0.05f;   // turns a second

constexpr float kStopReach = 1.5f;          // the object's half length, by this: kept back from the obstacle
constexpr float kMinCollisionRange = 20.0f;
constexpr float kMaxCollisionRange = 35.0f;
constexpr float kFollowRange = 15.0f;
constexpr float kSameLevel = 5.0f;
constexpr float kMovingSpeed = 5.0f;
constexpr float kSameWay = 0.707f;
constexpr float kAcrossRoad = 0.2f;
constexpr float kAlongside = 0.9f;

constexpr float kShotInLine = 0.99f;        // ClearShot's, and the guns'
constexpr float kRocketAim = 0.9f;
constexpr float kMissileAim = 0.707f;

constexpr int kZoneSpanX = 3;               // the zones round a car where traffic looks for others
constexpr int kZoneSpanY = 2;
constexpr int kZoneSpanZ = 3;

// The car's animation systems and stimuli the weapons fire through
constexpr uint32_t kGunSystem = 9;
constexpr uint32_t kGunFlashSystem = 0x48;
constexpr uint32_t kRocketSystems[2] = {6, 5};          // by rocketSide
constexpr uint32_t kMissileSystems[2] = {8, 7};         // by missileSide
constexpr uint32_t kMissileFlashSystems[2] = {0x3e, 0x3d};
constexpr uint8_t kStimulusArm = 8;
constexpr uint8_t kStimulusFire = 0xa;
constexpr uint8_t kStimulusFireEnd = 0xb;
constexpr int kStimulusMode = 2;

static_assert(std::bit_cast<uint32_t>(kLookAheadScale) == 0x3eb33333 &&
              std::bit_cast<uint32_t>(kMinSpeed) == 0x3d4ccccd &&
              std::bit_cast<uint32_t>(kFacingAhead) == 0x3f333333 &&
              std::bit_cast<uint32_t>(kFacingBehind) == 0xbe19999a &&
              std::bit_cast<uint32_t>(kPedalScale) == 0x3d888889 &&
              std::bit_cast<uint32_t>(kTargetOncoming) == 0xbf333333 &&
              std::bit_cast<uint32_t>(kTrafficTurnRound) == 0xbe4ccccd &&
              std::bit_cast<uint32_t>(kSubLevel) == 0x3f34fdf4 &&
              std::bit_cast<uint32_t>(kSubPitchLimit) == 0x3ecccccd &&
              std::bit_cast<uint32_t>(kSubPedalScale) == 0x3e4ccccd &&
              std::bit_cast<uint32_t>(kLaneChangeRate) == 0x3dcccccd &&
              std::bit_cast<uint32_t>(kShotInLine) == 0x3f7d70a4 &&
              std::bit_cast<uint32_t>(kRocketAim) == 0x3f666666, "constants as the original's");

RigidBody *BodyOf(AIVehicle *vehicle) {
    return fgSimulation.GetRigidBody(vehicle->GetPhysicsObject()->rigidBodySlot);
}

RigidBody *BodyOf(PhysicsObject *object) {
    return fgSimulation.GetRigidBody(object->rigidBodySlot);
}

// The body's axes: its orientation's rows
const Coord3 &Side(RigidBody *body) {
    return *Xyz(MatrixRow(&body->info->orientation, 0));
}

const Coord3 &Up(RigidBody *body) {
    return *Xyz(MatrixRow(&body->info->orientation, 1));
}

const Coord3 &Forward(RigidBody *body) {
    return *Xyz(MatrixRow(&body->info->orientation, 2));
}

// The car's virtual methods (PVehicle's slots)
PVehicle *Car(PhysicsObject *object) {
    return static_cast<PVehicle *>(object);
}

void SetSteering(PhysicsObject *object, float steering) {
    Car(object)->SetCarControlSteering(steering);
}

void SetSteeringVertical(PhysicsObject *object, float steering) {
    Car(object)->SetCarControlSteeringVertical(steering);
}

void SetGas(PhysicsObject *object, float gas) {
    Car(object)->SetCarControlGas(gas);
}

void SetBrake(PhysicsObject *object, float brake) {
    Car(object)->SetCarControlBrake(brake);
}

void SetHandBrake(PhysicsObject *object, bool handBrake) {
    Car(object)->SetCarControlHandBrake(handBrake);
}

ABaseSound *GetAudio(PhysicsObject *object) {
    return Car(object)->GetAudio();
}

AIGroundVehicle *GetAIGroundVehiclePtr(PhysicsObject *object) {
    return Car(object)->GetAIGroundVehiclePtr();
}

uint8_t EmpActive(PhysicsObject *object) {
    return Car(object)->EmpActive();
}

int InShock(PhysicsObject *object) {
    return Car(object)->InShock();
}

void GlareOn(PhysicsObject *object, int glare) {
    Car(object)->GlareOn(glare);
}

void ResetDamage(PhysicsObject *object) {
    Car(object)->ResetDamage();
}

void ClearEmpState(PhysicsObject *object) {
    Car(object)->ClearEMPState();
}

void ResetCarAt(PhysicsObject *object, Coord3 *position, const Coord3 *direction) {
    Car(object)->ResetCar(position, direction);
}

// The lane weight plus `weight`, kept within a byte
uint8_t AddLaneWeight(uint8_t lane, int weight) {
    int sum = lane + weight;
    if (sum > kBlockedLane)
        return kBlockedLane;
    if (sum < 0)
        return 0;
    return uint8_t(sum);
}

// The lane offset rounded half away from 0
int RoundLaneOffset(float offset) {
    if (offset > 0.0f) {
        if (FloatFloor(offset) + 0.5 <= offset)
            return Ftol(FloatCeil(offset));
        return Ftol(floor(double(offset)));
    }
    if (FloatCeil(offset) - 0.5 >= offset)
        return Ftol(FloatFloor(offset));
    return Ftol(FloatCeil(offset));
}

// The weights from `first` across `span` lanes (and no further than the last lane) raised by `weight`
void AddLaneWeights(uint8_t *laneWeights, int first, int span, int weight) {
    if (first < 0)
        first = 0;
    int last = abs(first + span);
    if (last > kLanes)
        last = kLanes;
    for (int lane = first; lane < last; lane++)
        laneWeights[lane] = AddLaneWeight(laneWeights[lane], weight);
}

// A lane change from one offset to another takes the car kLaneChangeRate seconds a lane at its flat speed
float LaneChangeDistance(AIGroundVehicle *vehicle, float from, float to) {
    return float(double(VU0_v3lengthxz(&BodyOf(vehicle)->velocity)) * kLaneChangeRate * fabs(double(from) - to));
}

// The steps a weapon waits before it fires again: fireDelay seconds, at least half a second
int16_t ReloadSteps(float fireDelay) {
    return int16_t(RoundToInt(MaxFloat(float(fgSimulation.stepsPerSecond * 0.5),
                                       float(fgSimulation.stepsPerSecond * double(fireDelay)))));
}

// A step off a countdown, not below 0
int16_t CountDown(int16_t steps) {
    int next = steps - 1;
    return int16_t(next < 0 ? 0 : next);
}

} // namespace

// ---------------------------------------------------------------------------------------------------------------
// Driving to a point
// ---------------------------------------------------------------------------------------------------------------

// The heading to the navigator's road point is worked out and not used
// FUNC_AT(0x0002dc90)
double AIGroundVehicle::GetLookAhead(float /* speed */) {
    Coord3 position = BodyOf(this)->position;
    float flatSpeed = VU0_v3lengthxz(&BodyOf(this)->velocity);
    Coord3 roadPoint = driveToNav->roadPoint;
    Coord3 heading;
    VU0_v4sub(&roadPoint, &position, &heading);
    VU0_v4unitxyz(&heading, &heading);
    double ahead = flatSpeed * double(kLookAheadScale);
    double least = BodyOf(this)->info->halfExtents.z + double(kLookAheadMargin);
    if (least > ahead)
        return float(least);
    return ahead;
}

// The point's height kept to the target's (swinging with the height phase), then driven to as a sub; otherwise:
// steering at the point (full lock when reversing, none while slow or rolling backwards), and gas or brake by the
// speed error. The reverse flag turns on once the point is well behind and off once it is ahead with the car moving
// that way; a barrier in the way reverses the flag for a second. Close behind an oncoming class-1 target in a chase,
// the hand brake is let off.
// FUNC_AT(0x0002f610)
void AIGroundVehicle::DriveToPoint(Coord3 *point, float speed) {
    if (isSub) {
        if (followTargetHeight) {
            if (heightSwing > 0.0f) {
                double phase = double(fgSimulation.timeStep) * kHeightPhaseRate + heightPhase;
                heightPhase = float(phase);
                if (phase > 1.0)
                    heightPhase = float(phase - 1.0);
                const Coord4 *at = target->GetPosition();
                point->y = float(SineTurns(heightPhase) * heightSwing + at->y);
            } else {
                point->y = target->GetPosition()->y;
            }
            float y = point->y;
            if (minHeight > y)
                y = minHeight;
            if (maxHeight < y)
                y = maxHeight;
            point->y = y;
        }
        DriveToPointSub(point, speed);
        return;
    }

    PhysicsObject *car = GetPhysicsObject();
    Coord3 forward = Forward(BodyOf(car));
    Coord3 side = Side(BodyOf(car));
    Coord3 toPoint;
    VU0_v4sub(point, &BodyOf(car)->position, &toPoint);
    VU0_v4unitxyz(&toPoint, &toPoint);
    float sideways = v3dotprod(&toPoint, &side);
    Coord3 heading = BodyOf(this)->velocity;
    VU0_v4unitxyz(&heading, &heading);
    float moving = v3dotprod(&forward, &heading);
    float facing = v3dotprod(&forward, &toPoint);
    float carSpeed = VU0_v3lengthxz(&BodyOf(car)->velocity);
    if (moving < 0.0f)
        carSpeed = -carSpeed;
    double error = fabs(double(speed) - carSpeed);
    float speedError = kSpeedRange < error ? kSpeedRange : float(error);
    float pedal = speedError * kPedalScale;
    if (kMinPedal > pedal)
        pedal = kMinPedal;
    else if (1.0f < pedal)
        pedal = 1.0f;

    if (stuckEventStep > 0 && stuckEventStep <= fgSimulation.stepsPerSecond / 2 && facing > kFacingAhead)
        stuckEventStep = 0;
    bool letOff = false;
    uint8_t reverse;
    if (stuckEventStep > 0) {
        reverse = stuckEvent;
        stuckEventStep = CountDown(stuckEventStep);
    } else {
        if (reversing == 1) {
            if ((facing > kFacingAhead && moving < 0.0f) || (facing >= kFacingBehind && moving > 0.0f))
                reversing = 0;
        } else if (reversing == 0 && facing < kFacingBehind) {
            reversing = 1;
        }
        reverse = reversing;
        if (target != NULL && attackMode == kGroundAttackChase &&
            Car(target->GetPhysicsObject())->GetCarClass() == kCarClass1) {
            Coord3 targetHeading;
            if (VU0_v3lengthxz(&BodyOf(target)->velocity) > 1.0f) {
                targetHeading = BodyOf(target)->velocity;
                VU0_v4unitxyz(&targetHeading, &targetHeading);
            } else {
                targetHeading = Forward(BodyOf(target));
            }
            Coord3 targetAt = BodyOf(target)->position;
            if (vec3distance(&BodyOf(car)->position, &targetAt) <= kChaseCloseRange &&
                v3dotprod(&targetHeading, &goalDirection) <= kTargetOncoming)
                letOff = true;
        }
        if (!letOff && skipFullUpdate == 0 && BarriersInPath(reversing)) {
            reverse = reversing == 0;
            stuckEventStep = int16_t(fgSimulation.stepsPerSecond);
            stuckEvent = reverse;
        }
    }

    if (zoneStopSteps != 0 && navigateMode != kGroundNavigateRandom) {
        // Stopping in an AI zone: the steering held as it is, doubled
        BondCarControl control;
        float steering = Car(car)->GetCarControl(&control)->steering;
        if (steering == 0.0f)
            steering = 0.1f;
        SetSteering(car, Util_Bound(steering + steering, 1.0f, 1.0f));
    } else if (!autoDriveSteering && airSteps == 0) {
        float steer = 0.0f;
        SetSteering(car, 0.0f);
        SetSteeringVertical(car, 0.0f);
        if (reverse)
            steer = sideways < 0.0f ? 1.0f : -1.0f;
        else if (Abs(carSpeed) >= 1.0f && moving >= 0.0f)
            steer = sideways;
        SetSteering(car, Util_Bound(steer, 1.0f, 1.0f));
    }

    SetGas(car, 0.0f);
    SetBrake(car, 0.0f);
    SetHandBrake(car, false);
    if (speed < kMinSpeed) {
        float absSpeed = Abs(carSpeed);
        if (absSpeed >= 1.0f) {
            if (moving < 0.0f)
                SetGas(car, 1.0f);
            else
                SetBrake(car, 1.0f);
        } else {
            SetSteering(car, 0.0f);
            SetHandBrake(car, absSpeed >= kStillSpeed);
        }
    } else if (!reverse) {
        if (speed > carSpeed) {
            SetGas(car, pedal);
        } else if (speedError > kSettleSpeedError || speed < kCruiseSpeed) {
            if (moving > 0.0f)
                SetBrake(car, pedal);
            else
                SetGas(car, pedal);
        }
    } else if (moving > 0.0f) {
        SetBrake(car, 1.0f);
    } else if (Abs(carSpeed) <= kReverseSpeed) {
        SetBrake(car, 0.5f);
    }
    if (letOff)
        SetHandBrake(car, false);
}

// Traffic: steering at the point (full lock once it is behind), held still under heldStill, otherwise gas or
// brake by the speed error, the speed wanted scaled by how squarely the car faces the point and nothing within the
// stopping distance (half the flat speed, at least 5)
// FUNC_AT(0x0002dd80)
void AIGroundVehicle::DriveToPointTraffic(const Coord3 *point, float speed) {
    if (minSimUpdate)
        return;
    PhysicsObject *car = GetPhysicsObject();
    Coord3 forward = Forward(BodyOf(car));
    Coord3 side = Side(BodyOf(car));
    Coord3 toPoint;
    VU0_v4sub(point, &BodyOf(car)->position, &toPoint);
    VU0_v4unitxyz(&toPoint, &toPoint);
    float sideways = v3dotprod(&toPoint, &side);
    Coord3 heading = BodyOf(this)->velocity;
    VU0_v4unitxyz(&heading, &heading);
    float moving = v3dotprod(&forward, &heading);
    float facing = v3dotprod(&forward, &toPoint);

    double scaled = double(Abs(facing)) * speed;
    float wanted = kTrafficMinSpeed > scaled ? kTrafficMinSpeed : float(scaled);
    float flatSpeed = VU0_v3lengthxz(&BodyOf(car)->velocity);
    float halfSpeed = flatSpeed * 0.5f;
    float stopDistance = kTrafficStopDistance > halfSpeed ? kTrafficStopDistance : halfSpeed;
    if (VU0_v3distancexz(point, &BodyOf(car)->position) <= stopDistance)
        wanted = 0.0f;
    float steer = sideways;
    if (facing <= kTrafficTurnRound)
        steer = sideways < 0.0f ? -1.0f : 1.0f;

    SetSteering(car, Util_Bound(steer, 1.0f, 1.0f));
    SetGas(car, 0.0f);
    SetBrake(car, 0.0f);
    SetHandBrake(car, false);
    if (heldStill) {
        SetGas(car, 0.0f);
        SetBrake(car, 0.0f);
        SetHandBrake(car, true);
        return;
    }
    double error = fabs(double(wanted) - flatSpeed);
    if (kTrafficSpeedRange < error)
        error = kTrafficSpeedRange;
    double share = error / kTrafficSpeedRange;
    float pedal = kMinPedal > share ? kMinPedal : float(share);
    if (wanted > flatSpeed || !(moving > 0.0f))
        SetGas(car, pedal);
    else if (error > 1.0 || wanted < kTrafficCrawl)
        SetBrake(car, pedal);
}

// A sub: its axes from the body's when it points steeply up or down, otherwise made from the world's up; steering and
// pitching at the point (not while reversing, nor pitching further past 0.4 up or down when stopping or following a
// height), and gas or brake by the speed error
// FUNC_AT(0x0002e0a0)
void AIGroundVehicle::DriveToPointSub(const Coord3 *point, float speed) {
    PhysicsObject *car = GetPhysicsObject();
    Coord3 forward = Forward(BodyOf(car));
    Coord3 up;
    Coord3 side;
    if (Abs(forward.y) <= kSubLevel) {
        const Coord3 worldUp = {0.0f, 1.0f, 0.0f};
        up = worldUp;
        VU0_v4crossprodxyz(&up, &forward, &side);
        VU0_v4crossprodxyz(&forward, &side, &up);
    } else {
        up = Up(BodyOf(car));
        side = Side(BodyOf(car));
    }
    Coord3 toPoint;
    VU0_v4sub(point, &BodyOf(car)->position, &toPoint);
    VU0_v4unitxyz(&toPoint, &toPoint);
    float sideways = v3dotprod(&toPoint, &side);
    float upwards = v3dotprod(&toPoint, &up);
    Coord3 heading = BodyOf(this)->velocity;
    VU0_v4unitxyz(&heading, &heading);
    float moving = v3dotprod(&forward, &heading);
    float facing = v3dotprod(&forward, &toPoint);
    float wanted = speed;
    float carSpeed = VU0_v3length(&BodyOf(car)->velocity);
    if (moving < 0.0f)
        carSpeed = -carSpeed;

    uint8_t reverse = 0;
    if (stuckEventStep > 0) {
        reverse = stuckEvent;
        stuckEventStep = CountDown(stuckEventStep);
    } else if (skipFullUpdate == 0 && BarriersInPath(reversing)) {
        reverse = reversing == 0;
        stuckEventStep = int16_t(fgSimulation.stepsPerSecond);
        stuckEvent = reverse;
    }
    SetSteering(car, 0.0f);
    SetSteeringVertical(car, 0.0f);
    if (reverse) {
        sideways = 0.0f;
        upwards = 0.0f;
    }
    if (speed >= kMinSpeed && facing < kFacingAhead)
        wanted = carSpeed * kSubSlowdown;
    if ((wanted == 0.0f || followTargetHeight) &&
        ((upwards < 0.0f && forward.y < -kSubPitchLimit) || (upwards > 0.0f && forward.y > kSubPitchLimit)))
        upwards = 0.0f;
    float steering = Util_Bound(sideways, 1.0f, 1.0f);
    float pitch = Util_Bound(upwards, 1.0f, 1.0f);
    SetSteering(car, steering);
    SetSteeringVertical(car, pitch * -1.0f);
    SetGas(car, 0.0f);
    SetBrake(car, 0.0f);
    SetHandBrake(car, false);

    double error = fabs(double(wanted) - carSpeed);
    if (kSubSpeedRange < error)
        error = kSubSpeedRange;
    float pedal = float(error * kSubPedalScale);
    if (kMinPedal > pedal)
        pedal = kMinPedal;
    else if (1.0f < pedal)
        pedal = 1.0f;
    if (wanted < kMinSpeed) {
        if (Abs(carSpeed) >= 1.0f) {
            if (moving < 0.0f)
                SetGas(car, 1.0f);
            else
                SetBrake(car, 1.0f);
        }
    } else if (reverse) {
        SetBrake(car, 0.5f);
    } else if (wanted > carSpeed) {
        SetGas(car, pedal);
    } else if (moving > 0.0f) {
        SetBrake(car, pedal);
    } else {
        SetGas(car, pedal);
    }
}

// FUNC_AT(0x0002e580)
void GetCollisionStopPoint(PhysicsObject *object, Coord3 *point, const Coord3 *obstacle, float obstacleReach) {
    Coord3 position = BodyOf(object)->position;
    Coord3 at = *point;
    Coord3 direction;
    VU0_v4sub(&at, &position, &direction);
    VU0_v4unitxyz(&direction, &direction);
    Coord3 other = *obstacle;
    Coord3 toObstacle;
    VU0_v4sub(&other, &position, &toObstacle);
    float along = v3dotprod(&direction, &toObstacle) - obstacleReach;
    along = float(along - BodyOf(object)->info->halfExtents.z * double(kStopReach));
    if (!(along > 0.0f))
        along = 0.0f;
    Coord3 stop;
    VU0_v4scaleadd(&direction, along, &position, &stop);
    *point = stop;
}

// FUNC_AT(0x0002e6b0)
bool InCollisionRange(const Coord3 *from, const Coord3 *to, const Coord3 *heading, float speed, float otherSpeed,
                      bool anyHeight) {
    Coord3 at = *from;
    Coord3 other = *to;
    if (!anyHeight && fabs(double(from->y) - to->y) > kSameLevel)
        return false;
    float distance = vec3distance(&at, &other);
    float reach = speed + speed;
    float range = kMinCollisionRange > reach ? kMinCollisionRange
                                             : (kMaxCollisionRange < reach ? kMaxCollisionRange : reach);
    if (otherSpeed > 0.0f && double(speed) + 1.0 <= otherSpeed)
        range = kFollowRange;
    if (distance > range)
        return false;
    Coord3 direction = *heading;
    VU0_v4unitxyz(&direction, &direction);
    Coord3 toOther;
    VU0_v4sub(&other, &at, &toOther);
    VU0_v4unitxyz(&toOther, &toOther);
    return v3dotprod(&direction, &toOther) >= 0.0f;
}

// ---------------------------------------------------------------------------------------------------------------
// The weapons
// ---------------------------------------------------------------------------------------------------------------

// FUNC_AT(0x0002e810)
bool ClearShot(int slot, const Coord3 *heading, const Coord3 *from, float range) {
    for (PVehicle **car = fgSimulation.cars.first; car != fgSimulation.cars.last; car++) {
        if (slot == (*car)->rigidBodySlot)
            continue;
        RigidBody *body = fgSimulation.GetRigidBody((*car)->rigidBodySlot);
        if (body->kind >= kOwnerKinds || body->sleepState == RigidBody::kFrozen)
            continue;
        if (GetAIGroundVehiclePtr(body->GetOwner())->type != kAIGroundAgent)
            continue;
        Coord3 position = body->position;
        Coord3 direction;
        VU0_v4sub(&position, from, &direction);
        VU0_v4unitxyz(&direction, &direction);
        if (v3dotprod(&direction, heading) > kShotInLine && vec3distance(from, &position) < range)
            return false;
    }
    return true;
}

// At the target, in sight: when the turret is on it, or with none when nothing is in the way and the car faces it.
// The gun's systems are armed and the aim point set; a shot every fireDelay seconds (none while it is negative). The
// sound's servo and firing flags follow.
// FUNC_AT(0x0002e910)
void AIGroundVehicle::FireBullets() {
    if (target == NULL || goalHidden != 0 || !goalInSight)
        return;
    int slot = GetPhysicsObject()->rigidBodySlot;
    Coord3 forward = Forward(BodyOf(this));
    bool fire;
    if (hasTurret)
        fire = turretOnTarget == 1;
    else
        fire = ClearShot(slot, &forward, Xyz(GetPosition()), goalDistance) &&
               v3dotprod(&goalDirection, &forward) > kShotInLine;
    if (!fire) {
        ASceneObj *sound = static_cast<ASceneObj *>(GetPhysicsObject()->audioObject);
        sound->servo = 0;
        sound->firing = 0;
        return;
    }

    RSceneObj *scene = GetPhysicsObject()->renderObject;
    if (scene != NULL && scene->animHandle != NULL) {
        Handle *animation = scene->animHandle;
        animation->ProcessStimuli(kGunSystem, kStimulusArm, fgSimulation.stepCount, kStimulusMode);
        animation->ProcessStimuli(kGunFlashSystem, kStimulusArm, fgSimulation.stepCount, kStimulusMode);
        if (hasTurret)
            VU0_v4unitxyz(&goalDirection, &aimPoint);
        else
            VU0_v4unitxyz(&forward, &aimPoint);
        if (fireDelay >= 0.0f) {
            if (gunSteps > 0) {
                gunSteps = CountDown(gunSteps);
            } else {
                animation->ProcessStimuli(kGunSystem, kStimulusFire, fgSimulation.stepCount, kStimulusMode);
                animation->ProcessStimuli(kGunFlashSystem, kStimulusFire, fgSimulation.stepCount, kStimulusMode);
                gunSteps = int16_t(RoundToInt(float(fgSimulation.stepsPerSecond * double(fireDelay))));
            }
        }
    }
    ASceneObj *sound = static_cast<ASceneObj *>(GetPhysicsObject()->audioObject);
    sound->servo = 1;
    sound->firing = 1;
}

// As the guns (the car facing the target within 0.9), the lead point set; a rocket from each side in turn, when that
// side's system is not still playing, every fireDelay seconds (at least half a second)
// FUNC_AT(0x0002eb40)
void AIGroundVehicle::FireRockets() {
    if (target == NULL || goalHidden != 0 || !goalInSight)
        return;
    int slot = GetPhysicsObject()->rigidBodySlot;
    Coord3 forward = Forward(BodyOf(this));
    if (hasTurret) {
        if (turretOnTarget != 1)
            return;
    } else if (!ClearShot(slot, &forward, Xyz(GetPosition()), goalDistance) ||
               !(v3dotprod(&goalDirection, &forward) > kRocketAim)) {
        return;
    }

    RSceneObj *scene = GetPhysicsObject()->renderObject;
    if (scene == NULL || scene->animHandle == NULL)
        return;
    Handle *animation = scene->animHandle;
    animation->ProcessStimuli(kRocketSystems[1], kStimulusArm, fgSimulation.stepCount, kStimulusMode);
    animation->ProcessStimuli(kRocketSystems[0], kStimulusArm, fgSimulation.stepCount, kStimulusMode);
    if (hasTurret)
        VU0_v4unitxyz(&goalDirection, &leadPoint);
    else
        VU0_v4unitxyz(&forward, &leadPoint);
    if (!(fireDelay >= 0.0f))
        return;
    if (rocketSteps > 0) {
        rocketSteps = CountDown(rocketSteps);
        return;
    }
    uint32_t system = kRocketSystems[rocketSide ? 1 : 0];
    if (animation->IsSystemPlaying(system))
        return;
    animation->ProcessStimuli(system, kStimulusFire, fgSimulation.stepCount, kStimulusMode);
    animation->ProcessStimuli(system, kStimulusFireEnd, fgSimulation.stepCount, kStimulusMode);
    rocketSteps = ReloadSteps(fireDelay);
    rocketSide = (rocketSide + 1) & 1;
}

// With nothing in the way and the car facing the target within 0.707 (turret or not); a missile from each side in
// turn, as the rockets
// FUNC_AT(0x0002ee00)
void AIGroundVehicle::FireMissiles() {
    if (target == NULL || goalHidden != 0 || !goalInSight)
        return;
    int slot = GetPhysicsObject()->rigidBodySlot;
    Coord3 forward = Forward(BodyOf(this));
    if (!ClearShot(slot, &forward, Xyz(GetPosition()), goalDistance) ||
        !(v3dotprod(&goalDirection, &forward) > kMissileAim))
        return;

    RSceneObj *scene = GetPhysicsObject()->renderObject;
    if (scene == NULL || scene->animHandle == NULL)
        return;
    Handle *animation = scene->animHandle;
    animation->ProcessStimuli(kMissileSystems[1], kStimulusArm, fgSimulation.stepCount, kStimulusMode);
    animation->ProcessStimuli(kMissileSystems[0], kStimulusArm, fgSimulation.stepCount, kStimulusMode);
    animation->ProcessStimuli(kMissileFlashSystems[1], kStimulusArm, fgSimulation.stepCount, kStimulusMode);
    animation->ProcessStimuli(kMissileFlashSystems[0], kStimulusArm, fgSimulation.stepCount, kStimulusMode);
    if (!(fireDelay >= 0.0f))
        return;
    if (missileSteps > 0) {
        missileSteps = CountDown(missileSteps);
        return;
    }
    int side = missileSide ? 1 : 0;
    if (animation->IsSystemPlaying(kMissileSystems[side]))
        return;
    animation->ProcessStimuli(kMissileSystems[side], kStimulusFire, fgSimulation.stepCount, kStimulusMode);
    animation->ProcessStimuli(kMissileSystems[side], kStimulusFireEnd, fgSimulation.stepCount, kStimulusMode);
    animation->ProcessStimuli(kMissileFlashSystems[side], kStimulusFire, fgSimulation.stepCount, kStimulusMode);
    animation->ProcessStimuli(kMissileFlashSystems[side], kStimulusFireEnd, fgSimulation.stepCount, kStimulusMode);
    missileSteps = ReloadSteps(fireDelay);
    missileSide = (missileSide + 1) & 1;
}

// FUNC_AT(0x0002f0e0)
void AIGroundVehicle::DoWeaponUpdate() {
    if (GetPhysicsObject()->GetHitPoints() <= 0.0f || stopped || !weaponsEnabled || target == NULL)
        return;
    if (hasTurret)
        TurnTurret();
    if (attackMode == kGroundAttackRockets) {
        FireRockets();
    } else if (attackMode == kGroundAttackGuns) {
        FireBullets();
    } else {
        if (useGuns)
            FireBullets();
        if (useRockets)
            FireRockets();
        if (useMissiles)
            FireMissiles();
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Traffic
// ---------------------------------------------------------------------------------------------------------------

// When the road spawner has a place free near the car (its navigator's, with minSimUpdate), every five seconds at
// most: a spawn point chosen while fewer than maxTraffic are about (maySpawn set when it is out of view in
// auto-drive), and the car put there, awake and repaired, in attack mode 12 along the road
// FUNC_AT(0x0002f190)
void AIGroundVehicle::Spawn(int16_t maxTraffic) {
    if (!active)
        return;
    Coord3 position;
    if (minSimUpdate)
        position = driveToNav->position;
    else
        position = BodyOf(this)->position;
    float halfLength = BodyOf(this)->info->halfExtents.z;
    if (AIRoadSpawn::Get()->TrafficRespawnAvailable(&position, halfLength)) {
        if (spawnStep > fgSimulation.stepCount)
            return;
        if (attackMode == kGroundAttack12 && BodyOf(this)->sleepState == RigidBody::kAwake)
            UnSpawn();
        int16_t count = TrafficSpawnCount;
        short segment = 0;
        int8_t lane = 0;
        float along = 0.0f;
        bool forwards = true;
        AIVehicleController::Get();
        if (count < maxTraffic && AIRoadSpawn::Get()->GetRandomTrafficSpawn(&segment, &lane, &along, &forwards)) {
            driveToNav->InitAtSegment(segment, lane, along, forwards);
            spawnPosition = driveToNav->roadPoint;
            Coord4 widened;
            Coord4 sphere = *Float_COORD3toCOORD4(&widened, &spawnPosition);
            sphere.w = 1.0f;
            if (glbMissionManager->autoDrive)
                maySpawn = !fgWorldCulling.IsInFrustum2d(&sphere, BodyOf(this)->info->halfExtents.z, true, 1.0f,
                                                           NULL);
            else
                maySpawn = 1;
        }
    }

    if (!maySpawn)
        return;
    halfLength = BodyOf(this)->info->halfExtents.z;
    if (AIRoadSpawn::Get()->TrafficRespawnPosCheck(&spawnPosition, halfLength)) {
        attackMode = kGroundAttack12;
        SetNavigateMode(kGroundNavigateRoad, NULL);
        Coord3 direction = driveToNav->direction;
        BodyOf(this)->sleepState = RigidBody::kAwake;
        ResetDamage(GetPhysicsObject());
        ClearEmpState(GetPhysicsObject());
        ResetCarAt(GetPhysicsObject(), &spawnPosition, &direction);
        shocked = 0;
        // AIVehicleController's count, reached through its Get()
        int16_t count = TrafficSpawnCount;
        AIVehicleController::Get();
        int16_t next = int16_t(count + 1);
        AIVehicleController::Get();
        TrafficSpawnCount = next < 0 ? 0 : next;
        spawnStep = fgSimulation.stepCount + fgSimulation.stepsPerSecond * 5;
    }
    maySpawn = 0;
}

// Spawning, the road the car is counted on (in attack mode 12), and shocked: set while the car is in shock, clear
// again once it is out of it and nearly still
// FUNC_AT(0x0002f4c0)
void AIGroundVehicle::DoTrafficStateUpdate() {
    if (skipFullUpdate == 0) {
        int16_t limit = MaxTraffic;
        AIVehicleController::Get();
        Spawn(limit);
    }
    if (attackMode == kGroundAttack12) {
        WRoadSegment *segment = fgRoadNetworkData.segments[driveToNav->segment];
        WRoadNetwork::Get();
        if (road != -1) {
            if (segment->road != -1 && segment->road != road) {
                WRoadNetwork::Get()->RemoveRoadCar(road);
                WRoadNetwork::Get()->AddRoadCar(segment->road);
                road = segment->road;
            }
        } else {
            int counted = segment->road;
            WRoadNetwork::Get();
            if (counted >= 0 && counted < fgRoadNetworkData.roadCount)
                fgRoadNetworkData.roads[counted]->carCount++;
            road = segment->road;
        }
    }
    if (!shocked) {
        if (InShock(GetPhysicsObject()))
            shocked = 1;
    } else if (!InShock(GetPhysicsObject()) && VU0_v3lengthxz(&BodyOf(this)->velocity) <= kShockSettledSpeed) {
        shocked = 0;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// The collision checks
// ---------------------------------------------------------------------------------------------------------------

// Each other car in range ahead adds kLaneBlock across the lanes it covers (none for a car whose target is this one
// and is attacking in mode 14 on whole tyres; less for this car's own target, and a lane eased where it is alongside
// while charging or across the road from the player's car when following a secondary target). With nothing in the
// way in mode 4, the middle lanes are freed and a wide lane offset halved; otherwise the navigator changes to the
// cheapest lane.
// FUNC_AT(0x0002fde0)
void AIGroundVehicle::CheckAgentRoadNetworkCollision(Coord3 * /* point */) {
    if (!useLanes || driveToNav->laneOffset != driveToNav->laneChangeTo)
        return;
    if (skipFullUpdate == 0) {
        Coord3 position = BodyOf(this)->position;
        Coord3 forward = Forward(BodyOf(this));
        float speed = VU0_v3lengthxz(&BodyOf(this)->velocity);
        bool blocked = false;
        GetRoadNetworkLaneWeights();
        int slot = GetPhysicsObject()->rigidBodySlot;
        for (PVehicle **car = fgSimulation.cars.first; car != fgSimulation.cars.last; car++) {
            if (slot == (*car)->rigidBodySlot)
                continue;
            RigidBody *body = fgSimulation.GetRigidBody((*car)->rigidBodySlot);
            if (body->kind >= kOwnerKinds || body->sleepState == RigidBody::kFrozen)
                continue;
            PhysicsObject *owner = body->GetOwner();
            AIGroundVehicle *other = GetAIGroundVehiclePtr(owner);
            Coord3 otherAt = body->position;
            float otherSpeed = VU0_v3lengthxz(&body->velocity);
            Coord3 otherForward = Forward(body);
            Coord3 otherHeading = body->velocity;
            VU0_v4unitxyz(&otherHeading, &otherHeading);
            // Its speed counts only when it moves forwards, the way this car faces
            if (otherSpeed < kMovingSpeed || v3dotprod(&otherForward, &otherHeading) <= 0.0f ||
                v3dotprod(&forward, &otherForward) <= kSameWay)
                otherSpeed = 0.0f;
            if (!InCollisionRange(&position, &otherAt, &forward, speed, otherSpeed, isSub && other->isSub))
                continue;

            Coord3 heading = Forward(BodyOf(this));
            WRoadNav *nav = other->GetCollNav(&heading, driveToNav->mode == kNavRandom, driveToNav->unknown04);
            int lane = RoundLaneOffset(nav->laneOffset) + kMiddleLane;
            int span = other->laneSpan;
            int weight = kLaneBlock;
            if (other->target == this && other->attackMode == kGroundAttack14 && !Car(owner)->IsTyreShredded(0) &&
                !Car(owner)->IsTyreShredded(1) && !Car(owner)->IsTyreShredded(2) && !Car(owner)->IsTyreShredded(3) &&
                owner->GetHitPoints() > 0.0f)
                weight = 0;
            Coord3 toOther;
            VU0_v4sub(&otherAt, &position, &toOther);
            VU0_v4unitxyz(&toOther, &toOther);
            if (target == other && goalHidden == 0) {
                if (driveToNav->mode != kNavRandom)
                    weight = weight - kLaneEase < 0 ? 0 : weight - kLaneEase;
                if (Car(other->GetPhysicsObject())->GetCarClass() != kCarClass1) {
                    Coord3 side = Side(BodyOf(this));
                    float across = v3dotprod(&side, &toOther);
                    if (chargeSteps != 0 && Abs(across) > kAlongside)
                        weight = -kLaneEase;
                }
            } else if (secondaryTarget != NULL && other->type == kAIGroundPlayer) {
                Coord3 direction = driveToNav->direction;
                if (Abs(v3dotprod(&direction, &toOther)) < kAcrossRoad)
                    weight = -kLaneEase;
            }
            if (weight == 0)
                continue;

            // A car turned across the road covers more lanes
            Coord3 otherSide;
            MakeCoord3(&otherSide, otherForward.z, otherForward.y, -otherForward.x);
            float across = Abs(v3dotprod(&otherSide, &heading));
            int covered = RoundToInt(float(across * double(body->info->halfExtents.z) * kLaneSpanScale));
            if (span <= covered)
                span = covered;
            AddLaneWeights(laneWeights, lane - span / 2, span, weight);
            blocked = true;
        }

        targetNav->mode = driveToNav->mode == kNavRandom ? kNavRandom : kNavDirection;
        if (!blocked && driveToNav->laneOffset != 0.0f && driveToNav->mode == kNavRandom) {
            laneWeights[kMiddleLane - 1] = 0;
            laneWeights[kMiddleLane] = 0;
            laneWeights[kMiddleLane + 1] = 0;
            float offset = driveToNav->laneOffset;
            float wanted = Abs(offset) <= kLaneDrift ? 0.0f : offset * 0.5f;
            driveToNav->ChangeLanes(wanted, LaneChangeDistance(this, offset, wanted));
            return;
        }
    }

    WRoadNav *nav = driveToNav;
    int best = GetBestWeightedLane(Ftol(nav->laneOffset) + kMiddleLane);
    float offset = float(best - kMiddleLane);
    if (!(laneSpan & 1))
        offset -= 0.5f;
    if (offset != nav->laneOffset)
        driveToNav->ChangeLanes(offset, LaneChangeDistance(this, nav->laneOffset, offset));
}

// Off the road the lane weights are laid across the heading to the goal: each other car in range ahead (not the
// target) blocks the lanes it covers, then the point moves across to the cheapest stretch
// FUNC_AT(0x000306b0)
void AIGroundVehicle::CheckAgentDirectCollision(Coord3 *point) {
    if (skipFullUpdate == 0) {
        Coord3 position = BodyOf(this)->position;
        Coord3 direction = goalDirection;
        Coord3 side = {direction.z, direction.y, -direction.x};
        float speed = VU0_v3lengthxz(&BodyOf(this)->velocity);
        GetDirectLaneWeights(point);
        int slot = GetPhysicsObject()->rigidBodySlot;
        for (PVehicle **car = fgSimulation.cars.first; car != fgSimulation.cars.last; car++) {
            if (slot == (*car)->rigidBodySlot)
                continue;
            RigidBody *body = fgSimulation.GetRigidBody((*car)->rigidBodySlot);
            if (body->kind >= kOwnerKinds || body->sleepState == RigidBody::kFrozen)
                continue;
            AIGroundVehicle *other = GetAIGroundVehiclePtr(body->GetOwner());
            if (other == target)
                continue;
            Coord3 otherAt = body->position;
            if (!InCollisionRange(&position, &otherAt, &direction, speed, 0.0f, false))
                continue;

            Coord3 offset;
            VU0_v4sub(&otherAt, &position, &offset);
            float lane = v3dotprod(&side, &offset) + kDirectLaneMiddle;
            Coord3 otherSide = Side(body);
            float across = Abs(v3dotprod(&otherSide, &direction));
            int span = other->laneSpan;
            int covered = RoundToInt(across * body->info->halfExtents.z);
            if (span <= covered)
                span = covered;
            AddLaneWeights(laneWeights, RoundToInt(lane) - span / 2, span, kLaneBlock);
        }
    }

    float offset = float(GetBestWeightedLane(kMiddleLane) - kMiddleLane);
    if (!(laneSpan & 1))
        offset -= 0.5f;
    Coord3 across = {goalDirection.z, goalDirection.y, -goalDirection.x};
    VU0_v4unitxyz(&across, &across);
    VU0_v4scaleadd(&across, offset, point, point);
}

// Traffic: at a red light at a junction of more than two roads (stopping and atRedLight set), the point pulled
// back to stop short of the junction's far node; otherwise, of the cars in the zones round this one that it would hit,
// the point pulled back behind the nearest (stopping set; the horn for any but traffic). heldStill: stopped, under
// a speed of 1.
// FUNC_AT(0x00030b80)
void AIGroundVehicle::CheckTrafficCollision(Coord3 *point) {
    stopping = 0;
    atRedLight = 0;
    WRoadSegment *segment = fgRoadNetworkData.segments[driveToNav->segment];
    WRoadNetwork::Get();
    if (segment->flags & kSegmentFlag80) {
        WRoadNode *node = fgRoadNetworkData.nodes[segment->node[int8_t(driveToNav->forward)]];
        WRoadNetwork::Get();
        if (node->segmentCount > 2) {
            WRoadNode *farNode = WRoadNetwork::Get()->GetSegmentOppNode(segment, node);
            WRoadIntersection *intersection =
                fgRoadNetworkData.intersections[segment->intersection[int8_t(driveToNav->forward)]];
            WRoadNetwork::Get();
            // The light phases: the junction's segments without kSegmentFlag02, this one's among them
            int phases = intersection->segmentCount;
            int counted = 0;
            int phase = 0;
            for (int i = 0; i < intersection->segmentCount; i++) {
                int16_t entry = intersection->segments[i];
                if (entry == driveToNav->segment)
                    phase = counted;
                WRoadSegment *joining = fgRoadNetworkData.segments[entry];
                WRoadNetwork::Get();
                if (joining->flags & kSegmentFlag02)
                    phases--;
                else
                    counted++;
            }
            WRoadSegment *own = fgRoadNetworkData.segments[driveToNav->segment];
            WRoadNetwork::Get();
            if (!(own->flags & kSegmentLaneType2) && phases > 2 &&
                !AIVehicleController::Get()->IntersectionGreenLight(&intersection->position, phase, phases)) {
                stopping = 1;
                atRedLight = 1;
                GetCollisionStopPoint(GetPhysicsObject(), point, &farNode->position, 0.0f);
                if (VU0_v3lengthxz(&BodyOf(this)->velocity) <= 1.0f)
                    heldStill = 1;
            }
        }
    }
    if (stopping)
        return;

    RigidBody *own = BodyOf(this);
    int zone[3] = {own->info->zone[0], own->info->zone[1], own->info->zone[2]};
    int ownIndex = own->ownerIndex;
    Coord3 stop = *point;
    bool found = false;
    float nearest = 0.0f;
    for (PVehicle **car = fgSimulation.cars.first; car != fgSimulation.cars.last; car++) {
        if (ownIndex == (*car)->rigidBodySlot)
            continue;
        RigidBody *body = fgSimulation.GetRigidBody((*car)->rigidBodySlot);
        if (body->kind >= kOwnerKinds || body->sleepState == RigidBody::kFrozen)
            continue;
        AIGroundVehicle *other = GetAIGroundVehiclePtr(body->GetOwner());
        const int32_t *otherZone = body->info->zone;
        if (abs(zone[0] - otherZone[0]) > kZoneSpanX || abs(zone[1] - otherZone[1]) > kZoneSpanY ||
            abs(zone[2] - otherZone[2]) > kZoneSpanZ)
            continue;
        const Coord4 &extents = body->info->halfExtents;
        float reach = extents.x > extents.z ? extents.x : extents.z;
        if (!TestForCollision(GetPhysicsObject(), point, &body->position, reach))
            continue;
        if (other->type != kAIGroundTraffic)
            static_cast<AVehicle *>(GetAudio(GetPhysicsObject()))->hornHonk = true;
        stopping = 1;
        // The stop point is pulled back from where the last one left it
        GetCollisionStopPoint(GetPhysicsObject(), &stop, &body->position, reach);
        if (VU0_v3lengthxz(&BodyOf(this)->velocity) <= 1.0f)
            heldStill = 1;
        float distance = vec3distance(&BodyOf(this)->position, &stop);
        if (!found || nearest > distance) {
            nearest = distance;
            *point = stop;
            found = true;
        }
    }
    if (!stopping)
        heldStill = 0;
}

// ---------------------------------------------------------------------------------------------------------------
// Each step
// ---------------------------------------------------------------------------------------------------------------

// The point to steer for by navigate mode, the collision checks, and the drive there. A dead agent, wrecked traffic,
// an EMP or `stopped` lets go of the controls and brakes the car to a stop (not a sub).
// FUNC_AT(0x00030fb0)
void AIGroundVehicle::DoMoveMode() {
    if ((type == kAIGroundAgent && GetPhysicsObject()->GetHitPoints() <= 0.0f) ||
        (type == kAIGroundTraffic && GetPhysicsObject()->GetHitPoints() < kTrafficMinHitPoints) ||
        EmpActive(GetPhysicsObject()) || stopped) {
        if (Car(GetPhysicsObject())->GetPhysics()->subPhysics != 0)
            return;
        SetGas(GetPhysicsObject(), 0.0f);
        SetBrake(GetPhysicsObject(), 0.0f);
        SetHandBrake(GetPhysicsObject(), false);
        SetSteering(GetPhysicsObject(), 0.0f);
        if (!(VU0_v3length(&BodyOf(this)->velocity) >= kStillSpeed) || InShock(GetPhysicsObject()))
            return;
        Coord3 forward = Forward(BodyOf(this));
        Coord3 heading = BodyOf(this)->velocity;
        VU0_v4unitxyz(&heading, &heading);
        float speed = VU0_v3length(&BodyOf(this)->velocity);
        float pedal = kStopPedal < speed ? kStopPedal : speed;
        if (v3dotprod(&heading, &forward) < 0.0f)
            SetGas(GetPhysicsObject(), pedal);
        else
            SetBrake(GetPhysicsObject(), pedal);
        return;
    }

    switch (navigateMode) {
    case kGroundNavigateNone:
        steerPoint = BodyOf(this)->position;
        if (type == kAIGroundAgent)
            DriveToPoint(&steerPoint, 0.0f);
        break;
    case kGroundNavigateDirect: {
        Coord3 position = BodyOf(this)->position;
        float ahead = float(GetLookAhead(targetSpeed));
        VU0_v4scaleadd(&goalDirection, ahead, &position, &steerPoint);
        if (useLanes) {
            if (type == kAIGroundAgent)
                CheckAgentDirectCollision(&steerPoint);
            else
                CheckTrafficCollision(&steerPoint);
        }
        DriveToPoint(&steerPoint, targetSpeed);
        break;
    }
    case kGroundNavigateRoad: {
        float ahead = float(GetLookAhead(targetSpeed));
        if (type != kAIGroundTraffic) {
            UpdateNavPos(&steerPoint, ahead);
            CheckAgentRoadNetworkCollision(&steerPoint);
            DriveToPoint(&steerPoint, targetSpeed);
            break;
        }
        if (!atRedLight || InShock(GetPhysicsObject()))
            UpdateNavPos(&steerPoint, ahead);
        if (shocked) {
            // In shock: no gas, braking (or gas when rolling backwards) to a stop
            Coord3 forward = Forward(BodyOf(this));
            Coord3 heading = BodyOf(this)->velocity;
            VU0_v4unitxyz(&heading, &heading);
            float speed = VU0_v3length(&BodyOf(this)->velocity);
            SetGas(GetPhysicsObject(), 0.0f);
            SetBrake(GetPhysicsObject(), 0.0f);
            if (v3dotprod(&heading, &forward) < 0.0f)
                SetGas(GetPhysicsObject(), MinFloat(kStopPedal, speed));
            else
                SetBrake(GetPhysicsObject(), MinFloat(kStopPedal, speed));
            SetHandBrake(GetPhysicsObject(), false);
            break;
        }
        if (minSimUpdate) {
            Coord3 forward = Forward(BodyOf(this));
            Coord3 position = BodyOf(this)->position;
            VU0_v4scaleadd(&forward, kTrafficLookAhead, &position, &steerPoint);
        }
        if (useLanes)
            CheckTrafficCollision(&steerPoint);
        if (driveToNav->atDeadEnd)
            steerPoint = BodyOf(this)->position;
        DriveToPointTraffic(&steerPoint, targetSpeed);
        break;
    }
    case kGroundNavigateRandom:
        UpdateNavPos(&steerPoint, float(GetLookAhead(targetSpeed)));
        if (skipFullUpdate == 0)
            CheckAgentRoadNetworkCollision(&steerPoint);
        DriveToPoint(&steerPoint, targetSpeed);
        break;
    case kGroundNavigateSpline: {
        // The car put where the spline path's instance is (its position taken through the placement too), moving
        // as it moves
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
        break;
    }
    }
}

// FUNC_AT(0x00031750)
void AIGroundVehicle::Update() {
    if (!active || !commanded)
        return;
    if (newCommand) {
        skipFullUpdate = 0;
        newCommand = 0;
    }
    switch (type) {
    case kAIGroundPlayer:
        DoPlayerLogic();
        break;
    case kAIDrivingOnRoad:
        UpdateStuck();
        DoAutoDriveUpdate();
        DoAttackMode();
        DoMoveMode();
        break;
    case kAIGroundSplinePath:
        DoSplinePathUpdate();
        break;
    case kAIGroundTraffic:
        DoTrafficStateUpdate();
        DoAttackMode();
        DoMoveMode();
        break;
    case kAIGroundAgent:
        UpdateStuck();
        DoAgentStateUpdate();
        DoAttackMode();
        DoWeaponUpdate();
        DoMoveMode();
        break;
    }
    GlareOn(GetPhysicsObject(), 1);
}

