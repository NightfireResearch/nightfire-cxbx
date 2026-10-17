#pragma fp_contract(off)

#include "AIHelicopter.h"

#include <bit>
#include <math.h>

#include "AIElements.h"
#include "AISplinePath.h"
#include "AIVehicleController.h"
#include "Helicopter.h"                 // PHelicopter
#include "MissionManager.h"             // glbMissionManager
#include "Simulation.h"                 // fgSimulation
#include "Vehicle.h"                    // PVehicle
#include "VehicleSound.h"               // AVehicle
#include "WeaponSound.h"                // ASceneObj
#include "../../helpers.h"
#include "../anim/AnimEngine.h"         // Handle
#include "../anim/Manager.h"            // MakeCoord4
#include "../engine/CoreContainers.h"   // GameVector
#include "../engine/PhysicsUtil.h"      // Util_Step
#include "../engine/SimRandom.h"
#include "../engine/UMemory.hpp"
#include "../physics/PhysicsMath.h"     // Min, Max
#include "../physics/RigidBody.h"       // DamageSourceSig
#include "../physics/SimpleRigidBody.h"
#include "../platform/RealMath.h"
#include "../platform/X87.h"
#include "../render/PathEngine.h"       // RPathHandle
#include "../render/RSceneObj.hpp"
#include "../world/Collider.h"          // WCollider
#include "../world/RoadNav.h"
#include "../world/RoadNetwork.h"
#include "../world/Targeting.h"
#include "../platform/CRuntimeStd.h"

// ---------------------------------------------------------------------------------------------------------------
// AIHelicopter (0x00031850-0x000347e0, and its vector deleting destructor at 0x00035df0), ported from the listing.
// ---------------------------------------------------------------------------------------------------------------

// ---- the game's code not ported yet

// ---- globals

// XBE_GLOBAL(0x001ddf10, 0xc)
AIHelicopterLaneOffsets PlayerLaneOffsets;

namespace {

const Coord3 kZero = {0.0f, 0.0f, 0.0f};

// The animation systems and stimuli (names ours)
constexpr uint32_t kGunSystemA = 7;         // gunSide set
constexpr uint32_t kGunSystemB = 8;
constexpr uint32_t kRocketSystem = 9;
constexpr uint8_t kStimulus8 = 8;           // NewState sends it to the three weapon systems
constexpr uint8_t kStimulusFire = 10;
constexpr uint8_t kStimulus11 = 11;         // after each burst, and to the guns when the attack mode changes
constexpr uint8_t kStimulusDestruct = 2;    // the proximity destruct, to every system
constexpr int kStimulusMode = 2;

constexpr float kRocketLead = 5.0f;         // the rockets aim along five times the line to the target, plus the heading
constexpr float kRocketCone = 0.707f;
constexpr float kNoseDown = -0.01f;         // a heading lower than this spreads the rockets evenly
constexpr float kRocketSpreadSide = 0.025f;
constexpr float kRocketSpreadDrop = 0.15f;
constexpr float kRocketSpread = 0.05f;
constexpr float kGunCone = 0.8434f;
constexpr float kGunNoseDown = -0.05f;
constexpr float kGunFastSpeed = 5.0f;       // faster than this, the guns fire only nose down
constexpr float kStrafeGain = 4.0f;
constexpr float kGasStep = 0.02f;
constexpr float kMinHeight = 2.0f;          // the throttle and the climb's wobble work only above it
constexpr float kMaxClimb = 10.0f;
constexpr float kWobbleScale = 1.0f / 16384.0f;
constexpr float kLift = 9.8f;
constexpr float kMinAltitude = 3.0f;
constexpr float kMinRange = 5.0f;
constexpr float kMinLookAhead = 10.0f;
constexpr float kMaxLookAhead = 20.0f;
constexpr float kDirectSlowdown = 0.75f;
constexpr float kDirectMinScale = 1.01f;
constexpr float kDirectMaxScale = 50.0f;
constexpr float kMinRoadSpeed = 25.0f;
constexpr float kAimedSide = 0.2f;          // facing the target closer than this, it fires along its direction
constexpr float kFiringRange = 1000.0f;
constexpr float kFollowRocketRange = 65.0f;
constexpr float kChaseFast = 30.0f;
constexpr float kChaseSlowLead = 30.0f;
constexpr float kChaseLead = 50.0f;
constexpr float kChaseMaxSpeed = 100.0f;
constexpr float kChaseClimb = 0.175f;       // the extra height for each unit of speed over kChaseFast
constexpr float kLaneChangeRange = 50.0f;
constexpr float kLaneChangeFacing = 0.6427f;
constexpr float kGunLeadRange = 10.0f;
constexpr float kGunLeadSpeed = 15.0f;
constexpr float kGunLeadScale = 0.075f;
constexpr float kChaseGunRange = 150.0f;
constexpr float kChaseRocketRange = 35.0f;  // over the altitude
constexpr float kOffRoadRange = 15.0f;
constexpr float kOffRoadRocketRange = 50.0f;
constexpr float kOffRoadLead = 0.5f;
constexpr float kCrashSteer = 0.1f;
constexpr float kCrashGas = 0.01f;
constexpr float kCrashLift = 0.99f;
constexpr float kCrashMaxLift = 0.25f;
constexpr float kCrashRoll = 2.0f;
constexpr float kCrashDamage = 1000.0f;
constexpr float kCrashDamageSpread = 0.5f;
constexpr float kCrashSlowdown = 0.35f;
constexpr float kCrashedSteer = 0.01f;
constexpr float kCrashedRoll = 5.0f;
constexpr int kProximityDestructEvent = 0x17;

static_assert(std::bit_cast<uint32_t>(kRocketCone) == 0x3f34fdf4 &&
              std::bit_cast<uint32_t>(kGunCone) == 0x3f57e910 &&
              std::bit_cast<uint32_t>(kRocketSpreadSide) == 0x3ccccccd &&
              std::bit_cast<uint32_t>(kRocketSpreadDrop) == 0x3e19999a &&
              std::bit_cast<uint32_t>(kRocketSpread) == 0x3d4ccccd &&
              std::bit_cast<uint32_t>(kGasStep) == 0x3ca3d70a && std::bit_cast<uint32_t>(kLift) == 0x411ccccd &&
              std::bit_cast<uint32_t>(kDirectMinScale) == 0x3f8147ae &&
              std::bit_cast<uint32_t>(kAimedSide) == 0x3e4ccccd &&
              std::bit_cast<uint32_t>(kChaseClimb) == 0x3e333333 &&
              std::bit_cast<uint32_t>(kLaneChangeFacing) == 0x3f2487fd &&
              std::bit_cast<uint32_t>(kGunLeadScale) == 0x3d99999a &&
              std::bit_cast<uint32_t>(kCrashSteer) == 0x3dcccccd &&
              std::bit_cast<uint32_t>(kCrashGas) == 0x3c23d70a &&
              std::bit_cast<uint32_t>(kCrashLift) == 0x3f7d70a4 &&
              std::bit_cast<uint32_t>(kCrashSlowdown) == 0x3eb33333 &&
              std::bit_cast<uint32_t>(kNoseDown) == 0xbc23d70a &&
              std::bit_cast<uint32_t>(kGunNoseDown) == 0xbd4ccccd,
              "the original's constants");

PHelicopter *Helicopter(AIVehicle *vehicle) {
    return static_cast<PHelicopter *>(vehicle->GetPhysicsObject());
}

SimpleRigidBody *Body(AIVehicle *vehicle) {
    return fgSimulation.GetSimpleRigidBody(vehicle->GetPhysicsObject()->rigidBodySlot);
}

RigidBody *CarBody(AIVehicle *vehicle) {
    return fgSimulation.GetRigidBody(vehicle->GetPhysicsObject()->rigidBodySlot);
}

ASceneObj *Sound(AIVehicle *vehicle) {
    return static_cast<ASceneObj *>(vehicle->GetPhysicsObject()->audioObject);
}

} // namespace

// ---- the target beacon

// FUNC_AT(0x00031850)
void AIHelicopter::EnableTargetBeacon(int unused) {
    if (targetBeacon == NULL) {
        WTargetable *made = static_cast<WTargetable *>(UMemory::FastAlloc(sizeof(WTargetable), "WTargetable"));
        targetBeacon = made != NULL ? made->Construct(this) : NULL;
        TargetPicker.RegisterTarget(targetBeacon);
    }
}

// FUNC_AT(0x000318d0)
void AIHelicopter::DisableTargetBeacon() {
    if (targetBeacon != NULL) {
        TargetPicker.UnregisterTarget(targetBeacon);
        targetBeacon->ownerType = kTargetPoint;
        targetBeacon->RemoveReference();
        targetBeacon = NULL;
    }
}

// ---- where it is going

// FUNC_AT(0x00031910)
void AIHelicopter::GetDirectionToTarget(const Coord3 *target) {
    Coord4 position;
    *Xyz(&position) = Body(this)->position;
    Coord4 offset;
    VU0_v4sub(target, &position, &offset);
    VU0_v4unitxyz(&offset, &destinationDirection);
    offset.y = 0.0f;
    VU0_v4unitxyz(&offset, &flatDirection);
}

// FUNC_AT(0x00031990)
AIHelicopterLaneOffsets* AIHelicopter::GetPlayerLaneOffsets() {
    Coord3 heading = driveToNav->direction;
    WRoadNav *nav = targetVehicle->GetHeliCollNav(&heading);
    PlayerLaneOffsets.laneOffset = nav->laneOffset;
    if (nav->forward)
        PlayerLaneOffsets.left = float(WRoadNetwork::Get()->GetSegmentLeftDrivableDist(nav->segment));
    else
        PlayerLaneOffsets.left = float(WRoadNetwork::Get()->GetSegmentRightDrivableDist(nav->segment));
    if (nav->forward)
        PlayerLaneOffsets.right = float(WRoadNetwork::Get()->GetSegmentRightDrivableDist(nav->segment));
    else
        PlayerLaneOffsets.right = float(WRoadNetwork::Get()->GetSegmentLeftDrivableDist(nav->segment));
    return &PlayerLaneOffsets;
}

// FUNC_AT(0x00032a20)
double AIHelicopter::GetLookAhead(float speed, float unused) {
    double lookAhead = speed * 0.5;
    if (kMinLookAhead > lookAhead)
        return kMinLookAhead;
    if (kMaxLookAhead < lookAhead)
        return kMaxLookAhead;
    return lookAhead;
}

// ---- the weapons

// FUNC_AT(0x00031a50)
void AIHelicopter::FireRockets(float range, const Coord3 *position, const Coord3 *target, const Coord3 *heading) {
    if (GetPhysicsObject()->GetHitPoints() <= 0.0f)
        return;
    if (!(fireDelay >= 0.0f))
        return;
    if (rocketDelay > 0) {
        rocketDelay--;
        return;
    }
    if (!(VU0_v3distancexz(position, target) < range))
        return;

    Coord4 toTarget;
    VU0_v4sub(target, position, &toTarget);
    VU0_v4unitxyz(&toTarget, &toTarget);
    VU0_v4scaleadd(&toTarget, kRocketLead, heading, &rocketAim);
    VU0_v4unitxyz(&rocketAim, &rocketAim);
    if (!(v3dotprod(&toTarget, heading) > kRocketCone)) {
        Sound(this)->servo = 0;
        Sound(this)->firing = 0;
        return;
    }

    RSceneObj *render = GetPhysicsObject()->renderObject;
    Handle *anim;
    if (render != NULL && (anim = render->animHandle) != NULL) {
        if (heading->y > kNoseDown) {
            rocketAim.x = float((fgSimulation.random->Fraction() - 0.5) * kRocketSpreadSide + rocketAim.x);
            rocketAim.y = float(rocketAim.y - fgSimulation.random->Fraction() * kRocketSpreadDrop);
            rocketAim.z = float((fgSimulation.random->Fraction() - 0.5) * kRocketSpreadSide + rocketAim.z);
        } else {
            rocketAim.x = float((fgSimulation.random->Fraction() - 0.5) * kRocketSpread + rocketAim.x);
            rocketAim.y = float((fgSimulation.random->Fraction() - 0.5) * kRocketSpread + rocketAim.y);
            rocketAim.z = float((fgSimulation.random->Fraction() - 0.5) * kRocketSpread + rocketAim.z);
        }
        anim->ProcessStimuli(kRocketSystem, kStimulusFire, fgSimulation.stepCount, kStimulusMode);
        rocketDelay = int16_t(Ftol(double(fgSimulation.stepsPerSecond) * fireDelay));
    }
    Sound(this)->servo = 1;
    Sound(this)->firing = 1;
}

// FUNC_AT(0x000324d0)
void AIHelicopter::FireBullets(float range, float speed, const Coord3 *position, const Coord3 *target,
                               Coord3 *heading) {
    if (attackMode == kHeliAttackGuns && shotsLeft == 0)
        return;
    if (GetPhysicsObject()->GetHitPoints() <= 0.0f)
        return;
    float distance = VU0_v3distancexz(position, target);
    if (!(fireDelay >= 0.0f))
        return;
    if (bulletDelay > 0) {
        bulletDelay--;
        return;
    }
    if (!(distance < range))
        return;
    if (speed > kGunFastSpeed && heading->y > kGunNoseDown)
        return;

    Coord4 toTarget;
    VU0_v4sub(target, position, &toTarget);
    toTarget.y = 0.0f;
    heading->y = 0.0f;
    VU0_v4unitxyz(&toTarget, &toTarget);
    VU0_v4unitxyz(heading, heading);
    if (!(v3dotprod(&toTarget, heading) > kGunCone))
        return;

    RSceneObj *render = GetPhysicsObject()->renderObject;
    if (render == NULL)
        return;
    Handle *anim = render->animHandle;
    if (anim == NULL)
        return;
    uint32_t gun = gunSide ? kGunSystemA : kGunSystemB;
    if (anim->IsSystemPlaying(gun))
        return;
    bulletTarget = *target;
    anim->ProcessStimuli(gun, kStimulusFire, fgSimulation.stepCount, kStimulusMode);
    anim->ProcessStimuli(gun, kStimulus11, fgSimulation.stepCount, kStimulusMode);
    double stepsPerSecond = fgSimulation.stepsPerSecond;
    bulletDelay = int16_t(Ftol(Max(float(stepsPerSecond * 0.5), float(stepsPerSecond * fireDelay))));
    gunSide = (gunSide - 1) & 1;
    shotsLeft--;
}

// ---- moving

// FUNC_AT(0x00031cd0)
void AIHelicopter::ManeuverToPos(Coord3 *target, float altitude, float speed, bool steer) {
    Coord4 position;
    *Xyz(&position) = Body(this)->position;
    GetPhysicsObject()->worldPos.FindClosestFace(Xyz(&position), true);
    float ground = float(GetPhysicsObject()->worldPos.HeightAtPoint(Xyz(&position), false));
    if (pauseSteps != 0) {
        speed = 0.0f;
        steer = false;
        pauseSteps--;
    }
    if (altitude != 0.0f)
        target->y = ground;
    target->y = altitude + target->y;

    Coord4 toTarget;
    VU0_v4sub(target, &position, &toTarget);
    toTarget.y = 0.0f;
    Coord4 direction;
    VU0_v4unitxyz(&toTarget, &direction);
    Coord4 velocity;
    *Xyz(&velocity) = Body(this)->velocity;
    velocity.y = 0.0f;
    Coord4 velocityDirection;
    VU0_v4unitxyz(&velocity, &velocityDirection);   // not read again
    Coord4 forward;
    Body(this)->GetForwardVector(Xyz(&forward));
    Coord4 right;
    Body(this)->GetRightVector(Xyz(&right));
    right.y = 0.0f;
    VU0_v4unitxyz(&right, &right);
    float facing = v3dotprod(&direction, &forward);
    float flatSpeed = VU0_v3length(&velocity);
    float gas = Helicopter(this)->controlGas;

    float sideways;
    if (attackMode != kHeliAttackFaceTarget && navigateMode == kHeliNavigateRoad) {
        localNav->mode = kNavDirection5;
        localNav->InitAtPoint(&Body(this)->position, Xyz(&forward), true);
        Coord4 roadPoint;
        *Xyz(&roadPoint) = localNav->roadPoint;
        Coord4 offset;
        VU0_v4sub(&roadPoint, &position, &offset);
        sideways = v3dotprod(&offset, &right);
    } else {
        sideways = v3dotprod(&toTarget, &right);
    }

    // The velocity in the helicopter's own frame
    Coord4 localVelocity;
    *Xyz(&localVelocity) = Body(this)->velocity;
    MATRIX4 orientation;
    Body(this)->RecalcOrientMat(&orientation);
    VU0_MATRIX4_transpose(&orientation, &orientation);
    VU0_MATRIX4_vect3rotate(&localVelocity, &orientation, &localVelocity);
    if (speed > 0.0f && steer)
        Helicopter(this)->controlStrafe = float((sideways - localVelocity.x * 0.5) * 2.0 * kStrafeGain);
    else if (attackMode != kHeliAttackFaceTarget)
        Helicopter(this)->controlStrafe = -localVelocity.x;
    else if (speed > flatSpeed)
        Helicopter(this)->controlStrafe = sideways;
    else
        Helicopter(this)->controlStrafe = 0.0f;

    // The original compares the unrounded heights first, then the stored one
    double height = double(Body(this)->position.y) - ground;
    float storedHeight = float(height);
    if (height > kMinHeight) {
        if (speed > flatSpeed)
            Helicopter(this)->controlGas = float(Util_Step(gas, float(double(speed) * kGasStep * facing), kGasStep));
        else
            Helicopter(this)->controlGas = 0.0f;
    }
    double climb = double(target->y) - Body(this)->position.y;
    float storedClimb = float(climb);
    if (climb > kMaxClimb)
        storedClimb = kMaxClimb;
    float wobble = float(fgSimulation.random->Generate()) * kWobbleScale;
    if (wobble < 0.5f)
        wobble = 0.5f;
    else if (wobble > 1.0f)
        wobble = 1.0f;
    float lift = float((double(storedClimb) - Body(this)->velocity.y) * 2.0 + kLift);
    if (storedHeight > kMinHeight)
        lift = lift * wobble;
    Helicopter(this)->controlAltitude = lift;
}

// FUNC_AT(0x00032af0)
void AIHelicopter::ControlMoveState(bool steer, bool faceForward) {
    Coord4 position;
    *Xyz(&position) = Body(this)->position;
    switch (navigateMode) {
    case kHeliNavigateDirect: {
        destinationDistance = VU0_v3distancexz(&position, &destination);
        GetDirectionToTarget(&destination);
        // The first clamp keeps the scale at 1.01 or more, so the second leaves 50 unless the distance is a NaN
        double scale = double(destinationDistance) / range * kDirectSlowdown;
        if (kDirectMinScale > scale)
            scale = kDirectMinScale;
        scale *= kDirectMaxScale;
        if (kDirectMaxScale < scale)
            scale = kDirectMaxScale;
        if (destinationDistance < range)
            scale = 0.0;
        float speed = float(scale * speedScale);
        if (!(speed < maxSpeed))
            speed = maxSpeed;
        Coord4 at;
        *Xyz(&at) = destination;
        ManeuverToPos(Xyz(&at), 0.0f, speed, false);
        Coord4 right;
        Body(this)->GetRightVector(Xyz(&right));
        right.y = 0.0f;
        VU0_v4unitxyz(&right, &right);
        Helicopter(this)->controlSteer = v3dotprod(&flatDirection, &right);
        break;
    }

    case kHeliNavigateRoad: {
        destinationDistance = VU0_v3distancexz(&position, &destination);
        GetDirectionToTarget(&destination);
        float speed;
        if (targetMode == 0 && attackMode == kHeliAttackFaceTarget) {
            if (destinationDistance <= range)
                speed = 0.0f;
            else
                speed = float(double(VU0_v3length(&CarBody(targetVehicle)->velocity)) * speedScale);
            if (maxSpeed < speed)
                speed = maxSpeed;
        } else if (targetMode == 0 && attackMode != kHeliAttackRockets && attackMode != kHeliAttackGuns) {
            if (destinationDistance < range) {
                Coord4 targetVelocity;
                *Xyz(&targetVelocity) = CarBody(targetVehicle)->velocity;
                if (v3dotprod(&targetVelocity, &flatDirection) > 0.0f)
                    speed = VU0_v3length(&CarBody(targetVehicle)->velocity);
                else
                    speed = 0.0f;
                speed = Min(speed, maxSpeed);
            } else {
                float following = float(double(VU0_v3length(&CarBody(targetVehicle)->velocity)) * speedScale);
                speed = Min(maxSpeed, Max(kMinRoadSpeed, following));
            }
        } else {
            speed = destinationDistance < range ? 0.0f : maxSpeed;
        }

        float lookAhead = float(GetLookAhead(Body(this)->GetScalarVelocity(), speed));
        Coord4 navPoint;
        *Xyz(&navPoint) = driveToNav->roadPoint;
        float navDistance = VU0_v3distancexz(&position, &navPoint);
        Coord4 navDirection;
        *Xyz(&navDirection) = driveToNav->direction;
        targetSegment = targetVehicle->GetHeliCollNav(Xyz(&navDirection))->segment;
        if (destinationDistance >= range) {
            // Turned round when the target's segment lies behind
            bool reverse;
            if (driveToNav->segment == targetSegment) {
                Coord4 heading;
                *Xyz(&heading) = driveToNav->direction;
                reverse = v3dotprod(&flatDirection, &heading) < 0.0f;
            } else {
                bool sameRoad;
                reverse = !driveToNav->PathForwardRoadSegment(targetSegment, &sameRoad) && sameRoad;
            }
            if (reverse) {
                driveToNav->InitAtPoint(Xyz(&position), &driveToNav->direction, false);
                driveToNav->ReverseNavDirection();
                driveToNav->IncNavPosition(lookAhead, &driveToNav->direction, -1);
            }
        }
        if (navDistance < lookAhead)
            driveToNav->IncNavPosition(float(double(lookAhead) * 2.0 - navDistance), &flatDirection, targetSegment);
        Coord4 at;
        *Xyz(&at) = driveToNav->roadPoint;
        ManeuverToPos(Xyz(&at), altitude, speed, steer);

        Coord4 right;
        Body(this)->GetRightVector(Xyz(&right));
        right.y = 0.0f;
        VU0_v4unitxyz(&right, &right);
        if (faceForward && destinationDistance < range) {
            Coord4 forward;
            Body(this)->GetForwardVector(Xyz(&forward));
            Coord4 between;
            VU0_v4addscale(&forward, &right, 0.5f, &between);
            Helicopter(this)->controlSteer = v3dotprod(&flatDirection, &between);
        } else {
            Helicopter(this)->controlSteer = v3dotprod(&flatDirection, &right);
        }
        break;
    }

    case kHeliNavigateSpline: {
        if (attackMode == kHeliAttackCrash)
            break;
        AISplinePath *spline = GetSplinePath();
        if (spline == NULL)
            break;
        Helicopter(this)->unknownA8 = 0;
        RPathHandle *path = spline->path;
        // The path's position goes into the frame's translation row before the placement is applied, and the
        // body takes that row as its position
        MATRIX4 frame;
        path->GetOrientMat(&frame);
        Coord3 *at = Xyz(MatrixRow(&frame, 3));
        at->x = path->GetPosition()->x;
        at->y = path->GetPosition()->y;
        at->z = path->GetPosition()->z;
        VU0_MATRIX4_mult(&frame, &frame, &spline->placement);
        SimpleRigidBody *body = Body(this);
        body->SetOrientMat(&frame);
        body->position = *at;
        body->velocity = path->velocity;
        break;
    }
    }
}

// ---- the state

// FUNC_AT(0x00032700)
void AIHelicopter::NewState(AICommand *command) {
    RSceneObj *render = GetPhysicsObject()->renderObject;
    pauseSteps = 0;
    Handle *anim;
    if (render != NULL && (anim = render->animHandle) != NULL) {
        Sound(this)->turningUpDown = 1;
        anim->ProcessStimuli(kGunSystemA, kStimulus8, fgSimulation.stepCount, kStimulusMode);
        anim->ProcessStimuli(kGunSystemB, kStimulus8, fgSimulation.stepCount, kStimulusMode);
        anim->ProcessStimuli(kRocketSystem, kStimulus8, fgSimulation.stepCount, kStimulusMode);
        if (command->attackMode != attackMode) {
            anim->ProcessStimuli(kGunSystemA, kStimulus11, fgSimulation.stepCount, kStimulusMode);
            anim->ProcessStimuli(kGunSystemB, kStimulus11, fgSimulation.stepCount, kStimulusMode);
        }
    }
    targetVehicle = AIVehicleController::Get()->GetPlayerGroundVehiclePtr(0);
    attackMode = kHeliAttackGuns;
    targetMode = command->targetMode;
    if (command->attackMode != 0)
        attackMode = command->attackMode;
    if (command->navigateMode != 0) {
        navigateMode = command->navigateMode;
        if (navigateMode == kHeliNavigateSpline)
            SetSplinePath(command->navigateTarget);
    }
    speedScale = command->speedScale;
    maxSpeed = command->maxSpeed;
    unknownE8 = 0;
    shotsLeft = uint8_t(Ftol(command->shotCount));
    if (shotsLeft == 0 && attackMode == kHeliAttackGuns)
        shotsLeft = 0xff;
    if (attackMode == kHeliAttackRockets || attackMode == kHeliAttackGuns)
        targetPosition = command->position;
    else
        destination = command->position;
    if (attackMode == kHeliAttackHold) {
        navigateMode = kHeliNavigateNone;
        Body(this)->position = destination;
        MATRIX4 orientation = command->orientation;
        Coord4 rotation;
        VU0_m4toquat(&rotation, &orientation);
        Body(this)->SetOrientation(&rotation);
    }
    if (navigateMode == kHeliNavigateNone)
        navigateMode = kHeliNavigateDirect;
    Helicopter(this)->unknownA8 = 1;

    range = command->range;
    if (command->range < 0.0f) {
        nearSide = 0;
        range = -range;
    } else {
        nearSide = 1;
    }
    range = range > kMinRange ? range : kMinRange;
    altitude = command->altitude > kMinAltitude ? command->altitude : kMinAltitude;
    fireDelay = command->fireDelay;
    rocketDelay = 0;
    bulletDelay = 0;
    if (navigateMode == kHeliNavigateRoad)
        driveToNav->InitAtPoint(&Body(this)->position, &flatDirection, true);
    Sound(this)->servo = 0;
    Sound(this)->firing = 0;
}

// FUNC_AT(0x000331c0)
void AIHelicopter::DoAgentLogic() {
    if (GetPhysicsObject()->GetHitPoints() <= 0.0f) {
        attackMode = kHeliAttackCrash;
        Sound(this)->servo = 0;
        Sound(this)->firing = 0;
        AVehicle *sound = static_cast<AVehicle *>(GetPhysicsObject()->audioObject);
        SimpleRigidBody *moving = Body(this);
        SimpleRigidBody *at = Body(this);
        sound->CallPlayLanding(1.0f, &at->position, &moving->velocity);
    }
    Helicopter(this)->unknownA8 = 1;
    lanesChanged = 0;

    switch (attackMode) {
    case kHeliAttackHold:
        Body(this)->position = destination;
        Body(this)->velocity = kZero;
        Body(this)->acceleration = kZero;
        break;

    case kHeliAttackFollow: {
        Coord4 carPosition;
        *Xyz(&carPosition) = CarBody(targetVehicle)->position;
        Coord4 targetForward;
        *Xyz(&targetForward) = *Xyz(MatrixRow(&CarBody(targetVehicle)->info->orientation, 2));
        CarBody(targetVehicle);         // the original fetches the body a third time, unused
        if (nearSide) {
            Coord4 beyond;
            VU0_v4scaleadd(&targetForward, float(double(range) + range), &carPosition, &beyond);
            destination = *Xyz(&beyond);
        } else {
            destination = *Xyz(&carPosition);
        }
        ControlMoveState(true, false);
        if (shotsLeft != 0 && (destinationDistance > range || !nearSide)) {
            Coord4 forward;
            Body(this)->GetForwardVector(Xyz(&forward));
            Coord4 position;
            *Xyz(&position) = Body(this)->position;
            FireRockets(kFollowRocketRange, Xyz(&position), Xyz(&carPosition), Xyz(&forward));
        } else {
            Sound(this)->servo = 0;
            Sound(this)->firing = 0;
        }
        break;
    }

    case kHeliAttackMove:
    case kHeliAttackMove4:
        ControlMoveState(true, false);
        break;

    case kHeliAttackMoveToPosition:
        targetMode = 1;
        ControlMoveState(false, false);
        break;

    case kHeliAttackRockets:
    case kHeliAttackGuns: {
        ControlMoveState(true, false);
        Coord4 forward;
        Body(this)->GetForwardVector(Xyz(&forward));
        Coord4 right;
        Body(this)->GetRightVector(Xyz(&right));
        Coord4 position;
        *Xyz(&position) = Body(this)->position;
        Coord4 carPosition;
        *Xyz(&carPosition) = CarBody(targetVehicle)->position;
        GetDirectionToTarget(targetMode == 0 ? Xyz(&carPosition) : &targetPosition);
        right.y = 0.0f;
        VU0_v4unitxyz(&right, &right);
        float side = v3dotprod(&flatDirection, &right);
        Helicopter(this)->controlSteer = side;
        if (fabsf(side) < kAimedSide)
            *Xyz(&forward) = destinationDirection;
        const Coord3 *target = targetMode == 0 ? Xyz(&carPosition) : &targetPosition;
        if (attackMode == kHeliAttackRockets)
            FireRockets(kFiringRange, Xyz(&position), target, Xyz(&forward));
        else
            FireBullets(kFiringRange, 0.0f, Xyz(&position), target, Xyz(&forward));
        break;
    }

    case kHeliAttackFaceTarget: {
        ControlMoveState(false, true);
        Coord4 forward;
        Body(this)->GetForwardVector(Xyz(&forward));
        Coord4 carPosition;
        *Xyz(&carPosition) = CarBody(targetVehicle)->position;
        GetDirectionToTarget(Xyz(&carPosition));
        forward.y = 0.0f;
        VU0_v4unitxyz(&forward, &forward);
        Helicopter(this)->controlSteer = v3dotprod(&flatDirection, &forward);
        break;
    }

    case kHeliAttackChaseRockets:
    case kHeliAttackChaseGuns:
    case kHeliAttackChase: {
        Coord4 forward;
        Body(this)->GetForwardVector(Xyz(&forward));
        Coord4 right;
        Body(this)->GetRightVector(Xyz(&right));
        Coord4 position;
        *Xyz(&position) = Body(this)->position;
        Coord4 velocity;
        *Xyz(&velocity) = Body(this)->velocity;
        Coord4 carPosition;
        *Xyz(&carPosition) = CarBody(targetVehicle)->position;
        Coord4 carVelocity;
        *Xyz(&carVelocity) = CarBody(targetVehicle)->velocity;
        AIHelicopterLaneOffsets *lanes = GetPlayerLaneOffsets();
        float carSpeed = VU0_v3lengthxz(&CarBody(targetVehicle)->velocity);
        float ownSpeed = Body(this)->GetScalarVelocity();

        if (-lanes->left > lanes->laneOffset || lanes->laneOffset > lanes->right) {
            // The target is off its road: make for the road beside it, at a short range
            Coord4 navDirection;
            *Xyz(&navDirection) = driveToNav->direction;
            destination = targetVehicle->GetHeliCollNav(Xyz(&navDirection))->roadPoint;
            float commandedRange = range;
            range = kOffRoadRange;
            targetMode = 1;
            ControlMoveState(true, false);
            driveToNav->ChangeLanes(0.0f, 0.0f);
            targetMode = 0;
            if (destinationDistance > range) {
                // Never fires: the range was just found shorter than the distance
                if (attackMode == kHeliAttackChaseRockets && destinationDistance < range) {
                    FireRockets(kOffRoadRocketRange, Xyz(&position), Xyz(&carPosition), Xyz(&forward));
                } else if (attackMode == kHeliAttackChaseGuns) {
                    carVelocity.y = 0.0f;
                    VU0_v4scaleadd(&carVelocity, kOffRoadLead, &carPosition, &carPosition);
                    FireBullets(kChaseGunRange, ownSpeed, Xyz(&position), Xyz(&carPosition), Xyz(&forward));
                }
            }
            range = commandedRange;
            break;
        }

        destinationDistance = VU0_v3distancexz(&position, &destination);
        GetDirectionToTarget(&destination);
        Coord4 toCar;
        VU0_v4sub(&carPosition, &position, &toCar);
        Coord4 flatForward = {forward.x, 0.0f, forward.z, 0.0f};
        toCar.y = 0.0f;
        VU0_v4unitxyz(&toCar, &toCar);
        VU0_v4unitxyz(&flatForward, &flatForward);
        float carAhead = v3dotprod(&toCar, &flatForward);
        velocity.y = 0.0f;
        VU0_v4unitxyz(&velocity, &velocity);
        Coord4 carForward;
        *Xyz(&carForward) = *Xyz(MatrixRow(&CarBody(targetVehicle)->info->orientation, 2));
        carForward.y = 0.0f;
        VU0_v4unitxyz(&carForward, &carForward);

        // Whether it flies against the car's way: its velocity's, or (nearly stopped) its heading's
        double along;
        if (carSpeed > 1.0f) {
            carVelocity.y = 0.0f;
            VU0_v4unitxyz(&carVelocity, &carVelocity);
            along = double(velocity.z) * carVelocity.z + double(velocity.x) * carVelocity.x;
        } else {
            along = double(carForward.z) * velocity.z + double(carForward.x) * velocity.x;
        }
        bool opposing = along < 0.0;
        bool behindCar = v3dotprod(&toCar, &carForward) > 0.0f;

        float lead;
        if (carAhead > 0.0f)
            lead = float(double(carSpeed) + kChaseLead);
        else if (opposing || !(carSpeed > kChaseFast))
            lead = float(double(carSpeed) + kChaseSlowLead);
        else
            lead = float(double(carSpeed) + kChaseLead);
        float speed = Min(kChaseMaxSpeed, lead);
        float lookAhead = float(GetLookAhead(ownSpeed, speed));
        Coord4 navPoint;
        *Xyz(&navPoint) = driveToNav->roadPoint;
        float navDistance = VU0_v3distancexz(&position, &navPoint);
        driveToNav->LeftDrivableDist();       // the original drops its answer
        float laneTarget = Min(lanes->laneOffset, float(driveToNav->RightDrivableDist()));
        if (destinationDistance < kLaneChangeRange && carAhead > kLaneChangeFacing) {
            driveToNav->ChangeLanes(laneTarget, 0.0f);
            lanesChanged = 1;
        }
        Coord4 navDirection;
        *Xyz(&navDirection) = driveToNav->direction;
        targetSegment = targetVehicle->GetHeliCollNav(Xyz(&navDirection))->segment;
        if (destinationDistance >= range || behindCar) {
            // Turned round when the target's segment lies behind (behindCar doubles as the road test's flag)
            bool reverse;
            if (driveToNav->segment != targetSegment && !behindCar)
                reverse = !driveToNav->PathForwardRoadSegment(targetSegment, &behindCar) && behindCar;
            else {
                Coord4 heading;
                *Xyz(&heading) = driveToNav->direction;
                reverse = v3dotprod(&flatDirection, &heading) < 0.0f;
            }
            if (reverse) {
                driveToNav->InitAtPoint(Xyz(&position), &driveToNav->direction, false);
                driveToNav->ReverseNavDirection();
                driveToNav->IncNavPosition(lookAhead, &driveToNav->direction, -1);
            }
        }
        if (navDistance < lookAhead)
            driveToNav->IncNavPosition(float(double(lookAhead) * 2.0 - navDistance), &flatDirection, targetSegment);
        Coord4 at;
        *Xyz(&at) = driveToNav->roadPoint;
        float climb = 0.0f;
        if (speed > kChaseFast)
            climb = float((double(speed) - kChaseFast) * kChaseClimb);
        ManeuverToPos(Xyz(&at), climb + altitude, speed, true);

        if (attackMode == kHeliAttackChaseGuns) {
            if (destinationDistance > kGunLeadRange) {
                Coord4 carLead;
                *Xyz(&carLead) = *Xyz(MatrixRow(&CarBody(targetVehicle)->info->orientation, 2));
                if (carSpeed > kGunLeadSpeed) {
                    VU0_v4scale(&carLead, float((double(ownSpeed) + carSpeed) *
                                                (double(destinationDistance) * kGunLeadScale)), &carLead);
                    carPosition.x = carPosition.x + carLead.x;
                    carPosition.z = carPosition.z + carLead.z;
                }
                FireBullets(climb + kChaseGunRange, ownSpeed, Xyz(&position), Xyz(&carPosition), Xyz(&forward));
            }
        } else if (attackMode == kHeliAttackChaseRockets) {
            FireRockets(altitude + kChaseRocketRange, Xyz(&position), Xyz(&carPosition), Xyz(&forward));
        }
        if (pauseSteps == 0) {
            VU0_v4sub(&at, &position, &flatDirection);
            flatDirection.y = 0.0f;
            VU0_v4unitxyz(&flatDirection, &flatDirection);
        }
        right.y = 0.0f;
        VU0_v4unitxyz(&right, &right);
        Helicopter(this)->controlSteer = v3dotprod(&flatDirection, &right);
        break;
    }

    case kHeliAttackCrash:
        if (crashStep == 0) {
            navigateMode = kHeliNavigateNone;
            Helicopter(this)->controlSteer = Max(-1.0f, Helicopter(this)->controlSteer - kCrashSteer);
            Helicopter(this)->controlGas = Min(1.0f, Helicopter(this)->controlGas + kCrashGas);
            float lift = Min(kCrashMaxLift, Helicopter(this)->controlAltitude * kCrashLift);
            Helicopter(this)->controlAltitude = lift;
            Helicopter(this)->unknown74 = Helicopter(this)->unknown74 - kCrashRoll;
            Coord4 position;
            *Xyz(&position) = Body(this)->position;
            Coord4 forward;
            Body(this)->GetForwardVector(Xyz(&forward));
            bool crashed = false;
            GetPhysicsObject()->worldPos.FindClosestFace(Xyz(&position), true);
            float ground = float(GetPhysicsObject()->worldPos.HeightAtPoint(Xyz(&position), false));
            if (GetPhysicsObject()->worldPos.valid && double(position.y) - 1.0f <= ground) {
                crashed = true;
            } else {
                // A segment through the helicopter, half its radius ahead and behind, tested against the world
                VU0_v4unitxyz(&forward, &forward);
                VU0_v4scale(&forward, Body(this)->radius * 0.5f, &forward);
                Coord4 front;
                VU0_v3add(&position, &forward, &front);
                Coord4 back;
                VU0_v4sub(&position, &forward, &back);
                Coord4 segment[2];
                MakeCoord4(&segment[0], Xyz(&front), 1.0f);
                MakeCoord4(&segment[1], Xyz(&back), 1.0f);
                WorldCollisionInfo info;
                info.Construct();
                WCollider collider;
                collider.Construct(segment, WCollider::kCollideBarriers | WCollider::kCollideInstances);
                if (collider.GetWorldNormal(segment, &info) || front.y <= ground)
                    crashed = true;
                collider.Destruct();
            }
            if (crashed) {
                GetPhysicsObject()->ApplyDamageVirtual(&kZero, &position, kCrashDamage, kCrashDamageSpread, 0,
                                                       &DamageSourceSig);
                crashStep++;
                Coord4 velocity;
                *Xyz(&velocity) = Body(this)->velocity;
                VU0_v4scale(&velocity, kCrashSlowdown, &velocity);
                Body(this)->velocity = *Xyz(&velocity);
                Helicopter(this)->controlGas = 0.0f;
            }
        } else {
            Helicopter(this)->controlSteer = Max(-1.0f, Helicopter(this)->controlSteer - kCrashedSteer);
            Helicopter(this)->unknown74 = Helicopter(this)->unknown74 + kCrashedRoll;
            crashStep++;
            if (crashStep >= fgSimulation.stepsPerSecond / 4) {
                fgAIElementController->ForceVehicleToSleep(this);
                crashStep = 0;
            }
        }
        break;
    }
}

// FUNC_AT(0x00034600)
bool AIHelicopter::DoProximityDestruct() {
    if (crashStep != 0) {
        GetPhysicsObject()->LoseHitPoints(GetPhysicsObject()->GetHitPoints());
        fgAIElementController->ForceVehicleToSleep(this);
        crashStep = 0;
        return true;
    }
    if (!Helicopter(this)->proximityDestructEnabled)
        return false;
    Coord4 player;
    *Xyz(&player) = *fgSimulation.GetPlayerObject()->GetPosition();
    Coord4 position;
    *Xyz(&position) = Body(this)->position;
    Coord4 offset;
    VU0_v4sub(&player, &position, &offset);
    float distance = VU0_v3length(&offset);
    distance = distance - fgSimulation.GetRigidBody(fgSimulation.cars.first[0]->rigidBodySlot)->info->halfExtents.z;
    if (!(Helicopter(this)->destructDistance >= distance))
        return false;
    glbMissionManager->ProgrammerDefinedEvent(kProximityDestructEvent, "");
    uint32_t step = fgSimulation.stepCount;
    GetPhysicsObject()->renderObject->animHandle->ProcessStimuli(kStimulusDestruct, step, kStimulusMode);
    crashStep++;
    return true;
}

// ---- AIVehicle's virtual methods

// FUNC_AT(0x00031c80)
void AIHelicopter::EngageSplinePath(CARP::AISpline *spline) {
    active = true;
    commanded = 1;
    navigateMode = kHeliNavigateSpline;
    SetSplinePath(spline);
}

// FUNC_AT(0x00031ca0)
void AIHelicopter::DisengageSplinePath() {
    attackMode = kHeliAttackMove;
    navigateMode = kHeliNavigateNone;
    targetVehicle = NULL;
    SetSplinePath(NULL);
}

// FUNC_AT(0x00032280)
void AIHelicopter::Reset() {
    driveToNav->Reset();
    driveToNav->mode = kNavDirection5;
    SetPhysicsObject(NULL);
    navigateMode = kHeliNavigateNone;
    attackMode = kHeliAttackMove;
    active = false;
    commanded = 0;
    index = 0;
    targetMode = 0;
    speedScale = 0.0f;
    maxSpeed = 0.0f;
    DisableTargetBeacon();
    localNav->Reset();
    targetPosition = kZero;
    destination = kZero;
    destinationDirection = kZero;
    flatDirection = kZero;
    targetSegment = -1;
    pauseSteps = 0xff;
    rocketAim = kZero;
    bulletTarget = kZero;
    targetVehicle = NULL;
    destinationDistance = 0.0f;
    range = 0.0f;
    unknownDC = 0;
    unknownE0[0] = 0;
    unknownE0[1] = 0;
    unknownE0[2] = 0;
    unknownE0[3] = 0;
    unknownE7 = 0;
    unknownE8 = 0;
    shotsLeft = 0;
    nearSide = 0;
    crashStep = 0;
    lanesChanged = 0;
    unknown10C = 0;
    rocketDelay = 0;
    bulletDelay = 0;
    gunSide = 0;
    altitude = 0.0f;
    fireDelay = 0.0f;
}

// FUNC_AT(0x00034760)
void AIHelicopter::Update() {
    if (!active || !commanded)
        return;
    if (attackMode != kHeliAttackHold && attackMode != kHeliAttackGuns && attackMode != kHeliAttackRockets &&
        targetMode == 0)
        destination = CarBody(targetVehicle)->position;
    if (!DoProximityDestruct())
        DoAgentLogic();
}

// ---- construction

// FUNC_AT(0x00032a60)
AIHelicopter* AIHelicopter::Construct() {
    AIVehicle::Construct();
    vtable = kAIHelicopterVtable;
    targetBeacon = NULL;
    WRoadNav *nav = static_cast<WRoadNav *>(UMemory::FastAlloc(sizeof(WRoadNav), "WRoadNav"));
    localNav = nav != NULL ? nav->Construct() : NULL;
    Reset();
    return this;
}

// FUNC_AT(0x000321d0)
void AIHelicopter::Destruct() {
    vtable = kAIHelicopterVtable;
    DisableTargetBeacon();
    if (localNav != NULL) {
        localNav->Destruct();
        UMemory::FastFree(localNav, sizeof(WRoadNav));
    }
    AIVehicle::Destruct();
}

// FUNC_AT(0x00035df0)
AIHelicopter* AIHelicopter::Delete(unsigned flags) {
    if (flags & 2) {
        AIVEHICLE_UNTESTED("AIHelicopter's vector deleting destructor (an array)");
        int32_t *block = reinterpret_cast<int32_t *>(this) - 4;     // new[]'s count, 16 bytes before the array
        Crt::EhVectorDestructor(this, sizeof(AIHelicopter), block[0],
                                reinterpret_cast<Crt::ElementFn>(XbeAddress(&AIHelicopter::Destruct)));
        if (flags & 1)
            OperatorDelete(block);
        return reinterpret_cast<AIHelicopter *>(block);
    }
    Destruct();
    if (flags & 1)
        UMemory::FastFree(this, sizeof(AIHelicopter));
    return this;
}
