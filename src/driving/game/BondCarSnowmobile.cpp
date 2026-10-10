#pragma fp_contract(off)

#include "BondCarSnowmobile.h"

#include <bit>
#include <stddef.h>
#include <stdint.h>

#include "BondCar.h"
#include "BondCarPhysics.h"                 // RaiseLandingImpact
#include "BondCarState.h"                   // the tuning globals
#include "../../common/xbeOverload.h"       // XbeVirtual
#include "../../helpers.h"
#include "../data/CoordConvert.h"           // Float_COORD3toCOORD4
#include "../physics/PhysicsMath.h"         // Abs
#include "../physics/RigidBody.h"
#include "../platform/RealMath.h"
#include "../platform/X87.h"

// ---------------------------------------------------------------------------------------------------------------
// PBondCar's snowmobile physics (0x00062fe0-0x000643b0), ported from the listing. The x87 chains are computed in
// double in the original's order and rounded where it stores a float; comparisons fail on NaN where the original's
// jumps do.
// ---------------------------------------------------------------------------------------------------------------

// ---- the game's code not ported yet
#define CRT_stricmp ((int (*)(const char *, const char *))0x00134537)

// ---- globals
#define ZeroVector (*(const Coord3 *)0x00243030)        // the game's zero vector, never written

namespace {

constexpr float kLandingUpright = 0.4f;             // ImproveLanding: the body's y axis up at least this much
constexpr float kLandingDamping = 0.8f;
constexpr float kAirBump = 5.0f;                    // compression times this into unknown2EC, off the ground
constexpr float kSettleForce = -40.0f;
constexpr float kSteerScale = 0.45f;
constexpr float kAccelScale = 0.3f;
constexpr float kPitchScale = -150.0f;
constexpr float kSmallGripScale = 2.0f;             // "small_snowmobile"'s
constexpr float kGripScale = 1.5f;
constexpr float kRollSpeedLimit = 15.0f;
constexpr float kRollBoostLow = 3.0f;               // between these speeds the roll is scaled up
constexpr float kRollBoostHigh = 7.0f;
constexpr float kRollBoost = 1.33f;
constexpr float kRollScale = 3.0f;
constexpr float kSlideSpeed = 5.0f;                 // faster than this, the handbrake slides the back
constexpr float kSlideFrontGrip = 0.8f;
constexpr float kSlideRearGrip = 0.2f;
constexpr float kHandbrakeGrip = 0.5f;
constexpr float kFrontGrip = 0.3f;
constexpr float kRearGrip = 0.7f;
constexpr float kGripScaleOutOfShock = 7.5f;
constexpr float kBrakingThreshold = 0.05f;
constexpr float kHandbrakeSlowSpeed = 1.0f;
constexpr float kHandbrakeSpeedScale = 0.075f;
constexpr float kHandbrakeBase = 1.75f;
constexpr float kLateralLimit = 20.0f;
constexpr float kStopSpeed = 0.1f;                  // class 1 cars slower than this stop dead
constexpr float kTrackLift = 0.01f;
constexpr float kSlipStart = 0.4f;
constexpr float kSlipScale = 1.6666666f;
constexpr float kImpactThreshold = 0.2f;
constexpr float kSpinRateScale = 0.05f;
static_assert(std::bit_cast<uint32_t>(kLandingUpright) == 0x3ecccccd, "the original's 0.4");
static_assert(std::bit_cast<uint32_t>(kLandingDamping) == 0x3f4ccccd, "the original's 0.8");
static_assert(std::bit_cast<uint32_t>(kSteerScale) == 0x3ee66666, "the original's 0.45");
static_assert(std::bit_cast<uint32_t>(kAccelScale) == 0x3e99999a, "the original's 0.3");
static_assert(std::bit_cast<uint32_t>(kRollBoost) == 0x3faa3d71, "the original's 1.33");
static_assert(std::bit_cast<uint32_t>(kSlideRearGrip) == 0x3e4ccccd, "the original's 0.2");
static_assert(std::bit_cast<uint32_t>(kRearGrip) == 0x3f333333, "the original's 0.7");
static_assert(std::bit_cast<uint32_t>(kBrakingThreshold) == 0x3d4ccccd, "the original's 0.05");
static_assert(std::bit_cast<uint32_t>(kHandbrakeSpeedScale) == 0x3d99999a, "the original's 0.075");
static_assert(std::bit_cast<uint32_t>(kStopSpeed) == 0x3dcccccd, "the original's 0.1");
static_assert(std::bit_cast<uint32_t>(kTrackLift) == 0x3c23d70a, "the original's 0.01");
static_assert(std::bit_cast<uint32_t>(kSlipScale) == 0x3fd55555, "the original's 1.6666666");

Coord3 *Xyz(Coord4 *v) {
    return reinterpret_cast<Coord3 *>(v);
}

int IsWheelOnGroundVirtual(PBondCar *car, int8_t wheel) {
    return (car->*XbeVirtual<decltype(&PBondCar::IsWheelOnGround)>(car, 65))(wheel);
}

} // namespace

// The body turned towards level, by 1 / NUM_BLEND_STEPS of the way to its heading with the nose NOSE_JUMP_ANGLE
// about x, and its spin damped after kLandingDampSteps steps in the air.
// FUNC_AT(0x00062fe0)
void PBondCar::ImproveLanding() {
    RigidBody *body = GetRigidBody();
    BondCarScratch *pad = BondCarScratchPad;
    if (pad->forceInfo->orientation.mtx[1][1] < kLandingUpright)
        return;
    pad->up.x = 0.0f;
    pad->up.y = 1.0f;
    pad->up.z = 0.0f;
    VU0_v4unitcrossprodxyz(pad->forceInfo->orientation.mtx[0], &pad->up, pad->landing.mtx[2]);
    VU0_v4crossprodxyz(&pad->up, pad->landing.mtx[2], pad->landing.mtx[0]);
    VU0_v4copy(&pad->up, pad->landing.mtx[1]);
    VU0_v4Init(pad->landing.mtx[3]);
    VU0_MATRIX4setxrot(&pad->noseRotation, physics->noseJumpAngle);
    VU0_MATRIX4_mult(&pad->landing, &pad->noseRotation, &pad->landing);
    double blend = 1.0 / physics->numBlendSteps;
    pad->blend = float(blend);
    const MATRIX4 &current = pad->forceInfo->orientation;
    for (int row = 0; row < 3; row++) {
        for (int column = 0; column < 3; column++) {
            // the first element is blended by the quotient as computed, the rest by its stored float
            double scale = row == 0 && column == 0 ? blend : pad->blend;
            pad->landing.mtx[row][column] = float((double(physics->numBlendSteps - 1) * current.mtx[row][column] +
                                                   pad->landing.mtx[row][column]) * scale);
        }
    }
    VU0_m4toquat(&pad->landingQuat, &pad->landing);
    body->SetOrientation(&pad->landingQuat);
    if (landingFlag > kLandingDampSteps) {
        Coord4 angularMomentum = {body->angularMomentum.x, body->angularMomentum.y, body->angularMomentum.z, 0.0f};
        VU0_v4scale(&angularMomentum, kLandingDamping, &angularMomentum);
        body->SetAngularMomentum(Xyz(&angularMomentum));
    }
}

// Each wheel's spring against the ground, its grip sideways and its drive, resolved on the body; the body lifted
// by the most a wheel off the ground is compressed, and turned by the steering and the throttle. Answers the
// number of wheels whose springs pushed. The front wheels (0 and 1) take the first axis, the back the second.
// FUNC_AT(0x000632a0)
int PBondCar::AddSnowmobileForces(const BondCarWheelInput *inputs, const Coord4 *axes, const Coord4 *roadNormals,
                                  const Coord4 *wheelPos, float *loads) {
    RigidBody *body = GetRigidBody();
    BondCarScratch *pad = BondCarScratchPad;
    pad->forceInfo = body->info;
    pad->wheelsOnGround = 0;
    pad->liftAboveGround = 0.0f;
    for (int wheel = 0; wheel < kCarWheels; wheel++) {
        pad->lever = *wheelPos;
        pad->compression = physics->springRestLength + roadNormals->w;
        if (!IsWheelOnGroundVirtual(this, wheel))
            pad->liftAboveGround = pad->liftAboveGround > pad->compression ? pad->liftAboveGround : pad->compression;
        float compression = pad->compression < physics->springCompressionLimit ? pad->compression
                                                                                : physics->springCompressionLimit;
        pad->compression = compression > 0.0f ? compression : 0.0f;

        if ((pad->compression > 0.0f || suspensionCompression[wheel] > 0.0f) &&
            double(pad->forceInfo->orientation.mtx[1][1]) * roadNormals->y > BondCar_ENABLE_ROLL_STOPS_THRESHOLD) {
            if (pad->compression > 0.0f) {
                pad->wheelsOnGround++;
                double change = double(pad->compression) - suspensionCompression[wheel];
                pad->compressionChange = float(change);
                double force;
                if (wheel > 1)
                    force = change * physics->springDampingRear + double(pad->compression) * physics->springStiffnessRear;
                else
                    force = change * physics->springDampingFront +
                            double(pad->compression) * physics->springStiffnessFront;
                pad->springForce = float(force);
                if (!IsWheelOnGroundVirtual(this, wheel)) {
                    double bump = double(kAirBump) * pad->compression;
                    if (bump > unknown2EC)
                        unknown2EC = float(bump);
                }
            } else {
                pad->springForce = 0.0f;
            }

            // grip sideways, against the velocity at the wheel across the direction it rolls
            VU0_v4unitcrossprodxyz(axes, roadNormals, &pad->axle);
            VU0_v4unitcrossprodxyz(roadNormals, &pad->axle, &pad->rolling);
            VU0_v4crossprodxyz(roadNormals, &pad->rolling, &pad->lateral);
            VU0_v4sub(&pad->lever, &body->position, &pad->lever);
            VU0_v4crossprodxyz(&body->angularVelocity, &pad->lever, &pad->pointVelocity);
            VU0_v3add(&pad->pointVelocity, &body->velocity, &pad->pointVelocity);
            double lateral = double(v3dotprod(&pad->pointVelocity, &pad->lateral)) * inputs->grip;
            pad->lateralForce = float(lateral);
            float limit = -inputs->frictionLimit;
            float clamped = limit < lateral ? float(lateral) : limit;
            pad->lateralForce = clamped < inputs->frictionLimit ? clamped : inputs->frictionLimit;
            double load = Abs(pad->lateralForce) * 2.0 / inputs->frictionLimit;
            loads[wheel] = load < 1.0 ? float(load) : 1.0f;
            VU0_v4scale(&pad->lateral, pad->lateralForce, &pad->lateral);
            body->ConvertWorldToLocal(&pad->lateral);
            pad->lateral.z = 0.0f;
            body->ConvertLocalToWorld(Xyz(&pad->lateral));

            // drive along the direction it rolls, the spring along the ground's normal
            VU0_v4scale(&pad->rolling, inputs->drive, &pad->force);
            if (body->info->unknown4fd == 0 && body->info->unknown4fe == 0)
                pad->lever.y = physics->wheelForceAppScale * pad->lever.y;
            else
                pad->lever.y = 0.0f;
            VU0_v4scale(roadNormals, pad->springForce, &pad->axle);
            pad->axle.x = physics->slopeScale * pad->axle.x;
            pad->axle.z = physics->slopeScale * pad->axle.z;
            if (inputs->handbrake != 0.0f) {
                VU0_v4unitxyz(&pad->pointVelocity, &pad->drag);
                VU0_v4scale(&pad->drag, inputs->handbrake, &pad->drag);
                VU0_v4sub(&pad->axle, &pad->drag, &pad->axle);
            }
            if (inputs->handbrake == 0.0f || carSpeed > 1.0f)
                VU0_v4sub(&pad->axle, &pad->lateral, &pad->axle);
            VU0_v3add(&pad->force, &pad->axle, &pad->force);
            VU0_v4crossprodxyz(&pad->lever, &pad->force, &pad->torque);
            body->ResolveMassScaledForce4(&pad->force);
            body->ResolveMassScaledTorque4(&pad->torque);
        }

        if (pad->compression == 0.0f) {
            if (numWheelsOnGround != 0 &&
                double(pad->forceInfo->orientation.mtx[1][1]) * roadNormals->y > BondCar_ENABLE_ROLL_STOPS_THRESHOLD) {
                bool wrecked = carClass == kCarClass2 && GetHitPoints() <= 0.0f;
                if (carClass == kCarClass1 ||
                    (!wrecked && inShock == 0 && body->info->unknown4fe == 0 && (body->flags & RigidBody::kFlag0) != 0 &&
                     body->info->unknown4fd == 0)) {
                    // a wheel just off the ground turning up is pushed back down
                    pad->lever = *wheelPos;
                    VU0_v4sub(&pad->lever, &body->position, &pad->lever);
                    VU0_v4crossprodxyz(&body->angularVelocity, &pad->lever, &pad->spin);
                    if (pad->spin.y > 0.0f) {
                        pad->force.x = 0.0f;
                        pad->force.y = kSettleForce;
                        pad->force.z = 0.0f;
                        VU0_v4crossprodxyz(&pad->lever, &pad->force, &pad->torque);
                        body->ResolveMassScaledTorque4(&pad->torque);
                    }
                }
            }
            loads[wheel] = 0.0f;
        }
        suspensionCompression[wheel] = pad->compression;
        if (wheel == 1)
            axes++;
        inputs++;
        roadNormals++;
        wheelPos++;
    }

    body->position.y = pad->liftAboveGround + body->position.y;

    float steer = float(control.steering * double(kSteerScale));
    pad->force.x = 0.0f;
    float accel = float((double(control.brake) - control.gas) * kAccelScale);
    float damping = physics->springDampingFront > physics->springDampingRear ? physics->springDampingFront
                                                                             : physics->springDampingRear;
    pad->force.z = 0.0f;
    pad->force.y = float(damping * double(kPitchScale));
    double grip = CRT_stricmp(carType, "small_snowmobile") == 0 ? kSmallGripScale : kGripScale;
    if (suspensionCompression[0] == 0.0f && suspensionCompression[1] == 0.0f)
        pad->axle.x = 0.0f;
    else
        pad->axle.x = float(steer * grip);
    pad->axle.y = 0.0f;
    double steady = 1.0 - Abs(control.steering);
    if (suspensionCompression[2] == 0.0f || suspensionCompression[3] == 0.0f || carSpeed > kRollSpeedLimit)
        steady = 0.0;
    float roll = float(steady * grip * accel * kRollScale);
    pad->axle.z = roll;
    if (carSpeed > kRollBoostLow && carSpeed < kRollBoostHigh)
        pad->axle.z = float(roll * double(kRollBoost));
    // The original's local leaves its w in pad->axle.w; ours is zero
    Coord4 torque = {0.0f, 0.0f, 0.0f, 0.0f};
    VU0_v4crossprodxyz(&pad->axle, &pad->force, &torque);
    pad->axle = torque;
    body->ConvertLocalToWorld(Xyz(&pad->axle));
    body->ResolveTorque(Xyz(&pad->axle));
    return pad->wheelsOnGround;
}

// Simulate's step for a snowmobile: the controls into grip, drive and handbrake drag for the four skis, the forces,
// a class 1 car stopped dead when nearly still, the tracks' heights and slips, ImproveLanding in the air, a
// collision event for a hard landing (unknown2EC), and the track's spin.
// FUNC_AT(0x00063ad0)
void PBondCar::ProcessSnowmobilePhysics() {
    RigidBody *body = GetRigidBody();
    BondCarScratch *pad = BondCarScratchPad;
    pad->info = body->info;
    pad->velocity.x = body->velocity.x;
    pad->velocity.y = body->velocity.y;
    pad->velocity.z = body->velocity.z;
    const float *zAxis = body->info->orientation.mtx[2];
    pad->forward.x = zAxis[0];
    pad->forward.y = zAxis[1];
    pad->forward.z = zAxis[2];
    pad->steeredForward = pad->forward;
    if (control.gas > control.brake)
        control.brake = 0.0f;
    else
        control.gas = 0.0f;
    unknown238 = 0.0f;
    carSpeed = VU0_v3lengthxz(&body->velocity);
    pad->velocity.y = 0.0f;
    pad->forwardSpeed = v3dotprod(&pad->velocity, &pad->forward);

    if (control.handBrake) {
        if (carSpeed > kSlideSpeed) {
            pad->frontGrip = kSlideFrontGrip;
            pad->rearGrip = kSlideRearGrip;
        } else {
            pad->rearGrip = kHandbrakeGrip;
            pad->frontGrip = kHandbrakeGrip;
        }
        control.brake = 0.0f;
        control.gas = 0.0f;
    } else {
        pad->frontGrip = kFrontGrip;
        pad->rearGrip = kRearGrip;
    }
    if (inShock == 0) {
        pad->frontGrip = pad->frontGrip * kGripScaleOutOfShock;
        pad->rearGrip = pad->rearGrip * kGripScaleOutOfShock;
    }
    unknown298 = control.brake > kBrakingThreshold;
    if (!control.handBrake)
        pad->handbrake = 0.0f;
    else if (carSpeed < kHandbrakeSlowSpeed)
        pad->handbrake = float(double(carSpeed) + carSpeed + 1.0);
    else
        pad->handbrake = float((carSpeed * double(kHandbrakeSpeedScale) + kHandbrakeBase) * physics->handbrakeForce);
    pad->driveForce = float(double(physics->maxAcc) * control.gas - double(physics->maxBrake) * control.brake);
    carSteer = physics->maxSteering * control.steering;
    float steering = control.steering;
    if (steering < -1.0f)
        steering = -1.0f;
    pad->steeringInput = steering;
    if (steering > 1.0f)
        steering = 1.0f;
    pad->steeringInput = steering;
    VU0_MATRIX4setyrot(&pad->steeringTurn, steering * physics->maxSteering);
    VU0_MATRIX4_mult(&pad->steeredOrientation, &pad->info->orientation, &pad->steeringTurn);
    pad->steeredForward.x = pad->steeredOrientation.mtx[2][0];
    pad->steeredForward.y = pad->steeredOrientation.mtx[2][1];
    pad->steeredForward.z = pad->steeredOrientation.mtx[2][2];
    pad->position = body->position;
    // rollingResistance is not set in this step (the stack as it was), and the copy is not read
    Coord4 widened;
    pad->rollingResistance4 = *Float_COORD3toCOORD4(&widened, &pad->rollingResistance);
    unknown2EC = 0.0f;

    // the skis: the front two steer and grip by frontGrip, the back two grip by rearGrip and drive
    pad->wheelInput[1].grip = pad->wheelInput[0].grip = pad->frontGrip;
    pad->wheelHeading[0].x = pad->steeredForward.x;
    pad->wheelHeading[0].y = pad->steeredForward.y;
    pad->wheelHeading[0].z = pad->steeredForward.z;
    pad->wheelInput[3].grip = pad->wheelInput[2].grip = pad->rearGrip;
    pad->wheelInput[3].drive = pad->wheelInput[2].drive = pad->driveForce;
    pad->wheelHeading[1].x = pad->forward.x;
    pad->wheelHeading[1].y = pad->forward.y;
    pad->wheelHeading[1].z = pad->forward.z;
    pad->wheelInput[1].drive = pad->wheelInput[0].drive = 0.0f;
    for (int wheel = kCarWheels - 1; wheel >= 0; wheel--)
        pad->wheelInput[wheel].frictionLimit = kLateralLimit;
    for (int wheel = 0; wheel < kCarWheels; wheel++) {
        pad->wheelInput[wheel].handbrake = pad->handbrake;
        wheelPos[wheel] = body->info->worldLevers[wheel];
        VU0_v3add(&wheelPos[wheel], &pad->position, &wheelPos[wheel]);
        body->TempGetHeightInformation(false, Xyz(&wheelPos[wheel]), &wheelRoadNormal[wheel], &wheels[wheel]);
    }
    for (int wheel = kCarWheels - 1; wheel >= 0; wheel--)
        pad->wheelSlip[wheel] = 0.0f;
    numWheelsOnGround = int8_t(AddSnowmobileForces(pad->wheelInput, pad->wheelHeading, wheelRoadNormal, wheelPos,
                                                   pad->wheelSlip));

    if (carClass == kCarClass1 && carSpeed <= kStopSpeed && carSpeed > 0.0f &&
        (control.handBrake || (control.gas == 0.0f && control.brake == 0.0f))) {
        body->ApplyHeavyFriction();
        body->velocity = ZeroVector;
        body->momentum = ZeroVector;
    }

    for (int wheel = 0; wheel < kCarWheels; wheel++) {
        wheelPos[wheel].y = float(double(wheelRoadNormal[wheel].w) + wheelPos[wheel].y + kTrackLift);
        double slip = (double(pad->wheelSlip[wheel]) - kSlipStart) * kSlipScale;
        wheelSlip[wheel] = float(slip < 0.0 ? 0.0 : slip);
    }

    if (numWheelsOnGround != 0)
        landingFlag = 0;
    else if (landingFlag < 0xff)
        landingFlag++;
    if ((carClass == kCarClass1 || (GetHitPoints() > 0.0f && carClass == kCarClass2)) &&
        body->info->unknown4fd == 0 && body->info->unknown4de > kLandingAirSteps && numWheelsOnGround == 0 &&
        body->groundContacts == 0)
        ImproveLanding();

    if (unknown2EC > kImpactThreshold) {
        float strength = unknown2EC < 1.0f ? unknown2EC : 1.0f;
        unknown2EC = strength;
        RaiseLandingImpact(body, &pad->position, strength);
    }

    // the track's spin, its angle kept within 0..1
    double spinRate = pad->forwardSpeed * double(kSpinRateScale);
    pad->wheelSpeed = float(spinRate);
    if (!(spinRate < BondCar_MAX_WHEEL_SPIN_RATE_AI))
        spinRate = BondCar_MAX_WHEEL_SPIN_RATE_AI;
    pad->wheelSpeed = float(spinRate);
    double angle = spinRate + wheelSpinAngle[1];
    wheelSpinAngle[1] = float(angle);
    if (angle > 1.0)
        wheelSpinAngle[1] = float(angle - Ftol(angle));
    else if (angle < 0.0)
        wheelSpinAngle[1] = float(angle - (Ftol(angle) - 1));
}
