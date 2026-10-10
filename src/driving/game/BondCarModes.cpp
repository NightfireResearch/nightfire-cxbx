#pragma fp_contract(off)

#include "BondCarModes.h"

#include <stddef.h>
#include <stdint.h>
#include <bit>

#include "AIVehicle.h"
#include "BondCar.h"
#include "SoftZone.h"
#include "VehicleSound.h"               // AVehicle
#include "../../common/xbeOverload.h"   // XbeVirtual
#include "../../helpers.h"
#include "../camera/Camera.h"           // AISplinePath, AsCoord3
#include "../camera/CameraIniLoader.h"  // fgCameraTables
#include "../camera/PlayerCamera.h"     // RPlayerCamera
#include "../physics/PhysicsMath.h"
#include "../physics/RigidBody.h"
#include "../physics/Simulation.h"
#include "../platform/RealMath.h"
#include "../platform/X87.h"
#include "../render/PathEngine.h"       // RPathHandle
#include "../render/RenderHigh.h"       // fgRenderHigh
#include "../world/CollisionTypes.h"    // MatrixRow

// ---------------------------------------------------------------------------------------------------------------
// PBondCar::ProcessSubmarinePhysics and ProcessSplinePhysics, ported from the listing. Both are x87 code: chains
// the original keeps on the FPU stack are written in double, in its order, rounded where it stores.
// ---------------------------------------------------------------------------------------------------------------


#define Sim ((void *)0x00233ff0)        // the Simulation

namespace {

constexpr float kLeverHeight = 0.19f;           // the submarine's steering levers, ahead and behind
constexpr float kLeverReach = 1.25f;
constexpr float kMaxTurn = 0.222f;              // the yaw and pitch, in turns
constexpr float kSpeedFraction = 1.0f / 15.0f;
constexpr float kRollDegreesToTurns = 1.0f / 360.0f;
constexpr float kBobRateA = 0.0022f;            // the two bobbing angles' steps, in turns
constexpr float kBobRateB = 0.0035f;
constexpr float kBobHeight = 0.075f;
static_assert(std::bit_cast<uint32_t>(kLeverHeight) == 0x3e428f5c && std::bit_cast<uint32_t>(kMaxTurn) == 0x3e6353f8 &&
              std::bit_cast<uint32_t>(kSpeedFraction) == 0x3d888889 &&
              std::bit_cast<uint32_t>(kRollDegreesToTurns) == 0x3b360b61 &&
              std::bit_cast<uint32_t>(kBobRateA) == 0x3b102de0 && std::bit_cast<uint32_t>(kBobRateB) == 0x3b656042 &&
              std::bit_cast<uint32_t>(kBobHeight) == 0x3d99999a, "the submarine's constants");
static_assert(std::bit_cast<uint32_t>(0.2f) == 0x3e4ccccd && std::bit_cast<uint32_t>(0.05f) == 0x3d4ccccd &&
              std::bit_cast<uint32_t>(0.01f) == 0x3c23d70a && std::bit_cast<uint32_t>(0.1f) == 0x3dcccccd &&
              std::bit_cast<uint32_t>(0.34f) == 0x3eae147b && std::bit_cast<uint32_t>(0.7f) == 0x3f333333 &&
              std::bit_cast<uint32_t>(0.707f) == 0x3f34fdf4 && std::bit_cast<uint32_t>(6.32f) == 0x40ca3d71,
              "the literals");

// The turn within +-limit, tested against the top first: a NaN gives the top
inline float ClampTurn(float turn, float limit) {
    return turn < limit ? Max(-limit, turn) : limit;
}

// A lever's push within +-limit, tested above the top first: a NaN gives the bottom
inline float ClampPush(float push, float limit) {
    return limit < push ? limit : Max(push, -limit);
}

// ---- PBondCar's virtual methods, called through its vtable as the original does
AVehicle *CallGetAudio(PBondCar *car) {
    return static_cast<AVehicle *>((car->*XbeVirtual<decltype(&PBondCar::GetAudio)>(car, PVehicle::kGetAudio))());
}
AIGroundVehicle *CallGetAIGroundVehiclePtr(PBondCar *car) {
    return (car->*XbeVirtual<decltype(&PBondCar::GetAIGroundVehiclePtr)>(car, PVehicle::kGetAIGroundVehiclePtr))();
}
CarPhysics *CallGetPhysics(PBondCar *car) {
    return (car->*XbeVirtual<decltype(&PBondCar::GetPhysics)>(car, PVehicle::kGetPhysics))();
}

// The car's AI's spline path: the original asks for it again at each use
AISplinePath *SplinePathOf(PBondCar *car) {
    return AIVehicle_GetSplinePath(CallGetAIGroundVehiclePtr(car), 0);
}

} // namespace

// FUNC_AT(0x000644c0)
void PBondCar::ProcessSubmarinePhysics() {
    RigidBody *body = Simulation_GetRigidBody(Sim, 0, rigidBodySlot);
    float yawInput = control.steering;
    float pitchInput = control.steeringVertical;
    float brakeInput = control.brake;
    Coord3 local;

    // Held brake: after a while slow, reversing
    reversing = 0;
    if (control.brake != 0.0f) {
        if (body->GetLocalVelocity(&local)->z < 2.5f) {
            if (reverseTimer < 15) {
                reverseTimer++;
                if (body->GetLocalVelocity(&local)->z < 0.5f)
                    control.brake = 0.0f;
            } else if (body->GetLocalVelocity(&local)->z <= 0.0f) {
                reversing = 1;
            }
        } else {
            reverseTimer = 0;
        }
    } else {
        reverseTimer = 30;
    }

    bool interiorView;
    if (carClass == 0) {
        pitchInput = (float)((double)pitchInput * pitchInput * pitchInput * pitchInput * pitchInput);
        interiorView = fgCameraTables.modes[fgRenderHigh->views[0].camera->cameraMode].interiorView != 0;
        yawInput = (float)((double)yawInput * yawInput * yawInput);
    } else {
        interiorView = false;
    }

    // The frame the levers are placed and the velocities read in: with the hand brake (seen from outside), the
    // body's heading levelled
    Coord4 frontLever = { 0.0f, kLeverHeight, kLeverReach, 0.0f };
    Coord4 rearLever = { 0.0f, kLeverHeight, -kLeverReach, 0.0f };
    MATRIX4 frame = {};
    if (!interiorView && control.handBrake) {
        const Coord4 *heading = MatrixRow(&body->info->orientation, 2);
        Coord4 forward = { heading->x, heading->y, heading->z, 0.0f };
        Coord4 up = { 0.0f, 1.0f, 0.0f, 0.0f };
        Coord4 right = {}, levelUp = {};
        VU0_v4unitcrossprodxyz(&up, &forward, &right);
        VU0_v4crossprodxyz(&forward, &right, &levelUp);
        *MatrixRow(&frame, 0) = right;
        *MatrixRow(&frame, 1) = levelUp;
        *MatrixRow(&frame, 2) = forward;
    } else {
        frame = body->info->orientation;
    }
    Coord4 frontPoint = {}, rearPoint = {};
    VU0_MATRIX4_vect3rotate(&frontLever, &frame, &frontPoint);
    VU0_MATRIX4_vect3rotate(&rearLever, &frame, &rearPoint);
    MATRIX4 toFrame;
    VU0_MATRIX4_transpose(&toFrame, &frame);
    Coord4 velocity = {}, spin = {};
    VU0_MATRIX4_vect3rotate(&body->velocity, &toFrame, &velocity);
    VU0_MATRIX4_vect3rotate(&body->angularVelocity, &toFrame, &spin);
    if (control.handBrake) {
        rearLever.y = 0.0f;
        frontLever.y = 0.0f;
    }

    // Thrust, with the rocket boost's extra while it lasts, and drag
    float thrust = (float)(((double)control.gas - control.brake) * 7.0f);
    if (rocketBoost != 0) {
        int boostEnd = attributes.LookupInt("BOOST_TIME", NULL) * 3;
        if (--rocketBoost > boostEnd)
            thrust += attributes.LookupFloat("BOOST_EXTRA_ACC", NULL);
        if (rocketBoost == boostEnd)
            CallGetAudio(this)->boost = 0;
    }
    Coord4 force = { 0.0f, 0.0f, 2.0f * thrust, 0.0f };
    body->ConvertLocalToWorld(AsCoord3(&force));
    body->ResolveMassScaledForce4(&force);
    VU0_v4scale(&body->velocity, carClass == 0 ? -0.5f : -0.2f, &force);
    body->ResolveMassScaledForce4(&force);

    carSpeed = VU0_v3length(&body->velocity);
    double speedFraction = (double)carSpeed * kSpeedFraction;
    if (!(speedFraction < 1.0))
        speedFraction = 1.0;
    float pitchScale = (float)((1.0 - speedFraction) * 2.5f + 1.0);
    float yawScale = (float)((1.0 - speedFraction) * 3.0f + 1.0);
    if (carClass == 0) {
        if (body->GetLocalVelocity(&local)->z < 0.0f || brakeInput > control.gas) {
            yawScale = 2.0f;
            pitchScale = 2.0f;
            float speed = Min(carSpeed, 20.0f);
            yawInput = (float)((double)yawInput * speed * 0.05f);
            pitchInput = (float)((double)speed * pitchInput * 0.05f);
        }
    }
    if (carClass == 0 ? body->GetLocalVelocity(&local)->z < 0.0f : reversing != 0) {
        carSpeed = -carSpeed;
        pitchInput = -pitchInput;
    }

    // Yaw: the levers' velocities across the frame, the front one turned by the steering, pushed back towards zero.
    // Slow (not the player's), the turn itself moves them and only the torques are applied.
    Coord4 frontVelocity = {}, rearVelocity = {};
    VU0_v4crossprodxyz(&spin, &frontLever, &frontVelocity);
    VU0_v4crossprodxyz(&spin, &rearLever, &rearVelocity);
    VU0_v3add(&frontVelocity, &velocity, &frontVelocity);
    VU0_v3add(&rearVelocity, &velocity, &rearVelocity);
    yawInput = (float)((double)yawScale * yawInput * (carClass == 0 ? 0.125f : kMaxTurn));
    carSteer = ClampTurn(yawInput, kMaxTurn);
    bool slowTurn = false;
    if (carClass != 0 && carSpeed < 5.0f && GetHitPoints() > 0.0f) {
        double turn = (5.0f - (double)Abs(carSpeed)) * 4.0f * carSteer;
        slowTurn = true;
        frontVelocity.x = (float)(frontVelocity.x - turn);
        rearVelocity.x = (float)(turn + rearVelocity.x);
    }
    MATRIX4 rotation;
    VU0_MATRIX4setyrot(&rotation, -carSteer);
    VU0_MATRIX4_vect3mult(&frontVelocity, &rotation, &frontVelocity);

    Coord4 push = {}, torque = {};
    push.x = ClampPush((float)(frontVelocity.x * -5.0f), 12.5f);
    push.y = 0.0f;
    push.z = 0.0f;
    VU0_MATRIX4_vect3rotate(&push, &frame, &push);
    if (!interiorView || !control.handBrake)
        push.y = 0.0f;
    VU0_v4crossprodxyz(&frontPoint, &push, &torque);
    if (!slowTurn)
        body->ResolveMassScaledForce4(&push);
    if (carClass != 0) {
        body->ConvertWorldToLocal(&torque);
        torque.z = torque.z * 4.0f;
        body->ConvertLocalToWorld(AsCoord3(&torque));
    }
    body->ResolveMassScaledTorque4(&torque);

    push.x = ClampPush((float)(rearVelocity.x * -5.0f), 9.375f);
    push.y = 0.0f;
    push.z = 0.0f;
    VU0_MATRIX4_vect3rotate(&push, &frame, &push);
    if (!interiorView || !control.handBrake)
        push.y = 0.0f;
    VU0_v4crossprodxyz(&rearPoint, &push, &torque);
    if (!slowTurn)
        body->ResolveMassScaledForce4(&push);
    if (carClass != 0) {
        body->ConvertWorldToLocal(&torque);
        torque.z = torque.z * 4.0f;
        body->ConvertLocalToWorld(AsCoord3(&torque));
    }
    body->ResolveMassScaledTorque4(&torque);

    // Pitch, the same way up and down
    if (Abs(pitchInput) > 0.01f || Abs(yawInput) < 0.01f) {
        VU0_v4crossprodxyz(&spin, &frontLever, &frontVelocity);
        VU0_v4crossprodxyz(&spin, &rearLever, &rearVelocity);
        VU0_v3add(&frontVelocity, &velocity, &frontVelocity);
        VU0_v3add(&rearVelocity, &velocity, &rearVelocity);
        pitchInput = ClampTurn((float)((double)pitchScale * pitchInput * (carClass == 0 ? 0.125f : kMaxTurn)),
                               kMaxTurn);
        slowTurn = false;
        if (carClass != 0 && carSpeed < 5.0f && GetHitPoints() > 0.0f) {
            double turn = (5.0f - (double)Abs(carSpeed)) * 10.0f * pitchInput;
            slowTurn = true;
            frontVelocity.y = (float)(frontVelocity.y + turn);
            rearVelocity.y = (float)(rearVelocity.y - turn);
        }
        VU0_MATRIX4setxrot(&rotation, -pitchInput);
        VU0_MATRIX4_vect3mult(&frontVelocity, &rotation, &frontVelocity);

        push.y = ClampPush((float)(frontVelocity.y * -4.0f), 12.0f);
        push.z = 0.0f;
        push.x = 0.0f;
        VU0_MATRIX4_vect3rotate(&push, &frame, &push);
        VU0_v4crossprodxyz(&frontPoint, &push, &torque);
        if (!slowTurn)
            body->ResolveMassScaledForce4(&push);
        body->ResolveMassScaledTorque4(&torque);

        push.y = ClampPush((float)(rearVelocity.y * -4.0f), 12.0f);
        push.z = 0.0f;
        push.x = 0.0f;
        VU0_MATRIX4_vect3rotate(&push, &frame, &push);
        VU0_v4crossprodxyz(&rearPoint, &push, &torque);
        if (!slowTurn)
            body->ResolveMassScaledForce4(&push);
        body->ResolveMassScaledTorque4(&torque);
    }

    // Damping, and while not near vertical a roll back upright - or over, while RollSub's roll lasts
    Coord4 damping = {};
    body->GetLocalAngularMomentum(AsCoord3(&damping));
    VU0_v4scale(&damping, -2.0f, &damping);
    const MATRIX4 *orientation = &body->info->orientation;
    const Coord4 *heading = MatrixRow(orientation, 2);
    Coord4 forward = { heading->x, heading->y, heading->z, 0.0f };
    Coord4 up = { 0.0f, 1.0f, 0.0f, 0.0f };
    Coord4 right = {}, carUp = {};
    VU0_v4unitcrossprodxyz(&up, &forward, &right);
    VU0_v4crossprodxyz(&forward, &right, &carUp);
    if (Abs(forward.y) < 0.7f) {
        if (control.handBrake && carClass != 0) {
            float mass = body->mass;
            double roll = (double)v3dotprod(&carUp, MatrixRow(orientation, 1)) * mass * 10.0f;
            if (!unknown408)
                damping.z = (float)(roll + damping.z);
            else
                damping.z = (float)(damping.z - roll);
        } else {
            const int8_t rollAngle = rollSub;   // degrees
            if (rollAngle != 0) {
                float turns = (float)((double)rollAngle * kRollDegreesToTurns);
                float lean = Abs(MatrixRow(orientation, 0)->y);
                double sine = SineTurns(turns);
                if (!(lean >= (sine < 0.0 ? -sine : sine))) {
                    float mass = body->mass;
                    double roll = (double)v3dotprod(&carUp, MatrixRow(orientation, 1)) * mass * 10.0f;
                    if (rollAngle < 0)
                        damping.z = (float)(roll + damping.z);
                    else
                        damping.z = (float)(damping.z - roll);
                } else if (subRolling) {
                    rollSub = 0;
                }
            } else {
                float mass = body->mass;
                damping.z = (float)(damping.z - (double)v3dotprod(&carUp, MatrixRow(orientation, 0)) * mass * 10.0f);
            }
            if (forceRollDirection == 0) {
                unknown408 = MatrixRow(&body->info->orientation, 0)->y < 0.0f;
                Coord3 localSpin;
                float spinZ = body->GetLocalAngularVelocity(&localSpin)->z;
                if (Abs(spinZ) > 0.1f && MatrixRow(&body->info->orientation, 1)->y > 0.34f)
                    unknown408 = spinZ < 0.0f;
            } else {
                unknown408 = forceRollDirection > 0;
            }
        }
    }
    body->ConvertLocalToWorld(AsCoord3(&damping));
    body->ResolveTorque(AsCoord3(&damping));

    // Bobbing: the last step's offset taken off the height, the new one added
    body->position.y = body->position.y - bob;
    float waveA = sin_fractionalangle(bobAngle);
    double bobOffset = (SineTurns(unknown3E8) + waveA + 1.0f) * kBobHeight;
    bob = (float)bobOffset;
    double angle = (double)bobAngle + kBobRateA;
    bobAngle = (float)angle;
    if (angle > 1.0f)
        bobAngle = (float)(angle - 1.0f);
    angle = (double)unknown3E8 + kBobRateB;
    unknown3E8 = (float)angle;
    if (angle > 1.0f)
        unknown3E8 = (float)(angle - 1.0f);
    body->position.y = (float)(bobOffset + body->position.y);

    // The soft zones: inside one, pushed back out against the speed and force into it, and turned away from it
    if (carClass != 2 || !(GetHitPoints() <= 0.0f)) {
        for (int i = 0; i < kSoftZones; i++) {
            const SoftZone *zone = &SoftZones[i];
            Coord4 offset = {};
            VU0_v4sub(&body->position, &zone->corners[3], &offset);
            float depth = v3dotprod(&offset, &zone->plane);
            if (!(depth >= 0.0f) || !(depth < zone->plane.w))
                continue;
            if (!PointInsideQuad(AsVector4(&body->position), zone->corners, &zone->plane))
                continue;
            float approach = -v3dotprod(&body->velocity, &zone->plane);
            float pushIn = -v3dotprod(&body->force, &zone->plane);
            double fraction = ((double)zone->plane.w - depth) / zone->plane.w;
            float scale = (float)fraction;
            if (!(fraction > 0.1f))
                scale = 0.1f;
            if (approach < 0.0f) {
                if (-1.0f < approach)
                    approach = -1.0f;
            } else if (approach > 0.0f) {
                if (1.0f > approach)
                    approach = 1.0f;
            }
            Coord4 out = {}, acceleration = {};
            VU0_v4scale(&zone->plane, (float)((double)SimStepsPerSecond * scale * approach), &out);
            VU0_v4scaleadd(&zone->plane, (float)((double)pushIn / body->mass * scale), &out, &acceleration);
            if (!(v3dotprod(&acceleration, &zone->plane) > 0.0f))
                continue;
            VU0_v4scale(&acceleration, scale, &acceleration);
            body->ResolveMassScaledForce4(&acceleration);

            float facing = v3dotprod(MatrixRow(&body->info->orientation, 2), &zone->plane);
            float turn = (float)((double)Max(carSpeed, 10.0f) * Abs(facing));
            Coord4 zoneTorque;
            zoneTorque.x = (float)((double)Abs(zone->plane.y) * turn * 2.0);
            float across = VU0_sqrt((float)((double)zone->plane.z * zone->plane.z +
                                            (double)zone->plane.x * zone->plane.x));
            zoneTorque.z = 0.0f;
            zoneTorque.y = (float)((double)across * turn * 1.5f);
            zoneTorque.w = 0.0f;
            if (facing > 0.0f)
                zoneTorque.x = -zoneTorque.x;
            if (v3dotprod(MatrixRow(&body->info->orientation, 0), &zone->plane) < 0.0f)
                zoneTorque.y = -zoneTorque.y;
            body->ConvertLocalToWorld(AsCoord3(&zoneTorque));
            body->ResolveMassScaledTorque4(&zoneTorque);
        }
    }

    numWheelsOnGround = kCarWheels;
    for (int i = kCarWheels - 1; i >= 0; i--)
        suspensionCompression[i] = 0.0f;
    if (againstWall) {
        Coord4 wallSpin = { control.steeringVertical, control.steering, 0.0f, 0.0f };
        VU0_v4scale(&wallSpin, body->mass * control.gas, &wallSpin);
        body->ModifyAngularMomentum(AsCoord3(&wallSpin));
    }
}

// FUNC_AT(0x00065740)
void PBondCar::ProcessSplinePhysics() {
    if (physics->subPhysics != 0)
        return;
    RigidBody *body = Simulation_GetRigidBody(Sim, 0, rigidBodySlot);

    // The path's frame with its position as the translation, both carried through the spline's placement
    MATRIX4 orientation;
    SplinePathOf(this)->path->GetOrientMat(&orientation);
    const Coord3 *pathPosition = SplinePathOf(this)->path->GetPosition();
    orientation.mtx[3][0] = pathPosition->x;
    orientation.mtx[3][1] = pathPosition->y;
    orientation.mtx[3][2] = pathPosition->z;
    VU0_MATRIX4_mult(&orientation, &orientation, &SplinePathOf(this)->placement);
    const Coord4 *position = MatrixRow(&orientation, 3);
    const Coord3 *pathVelocity = &SplinePathOf(this)->path->velocity;
    Coord4 direction = { pathVelocity->x, pathVelocity->y, pathVelocity->z, 0.0f };

    // The wheels turned by the speed across the ground
    carSpeed = VU0_v3length(&direction);
    float groundSpeed = VU0_sqrt((float)((double)direction.z * direction.z + (double)direction.x * direction.x));
    carWheelSpeed = groundSpeed;
    if (physics->wheelRadius > 0.0f)
        carWheelSpeed = (float)(groundSpeed / ((double)SimStepsPerSecond * physics->wheelRadius * 6.32f));
    if (carWheelSpeed < 0.01f)
        carWheelSpeed = 0.0f;
    float turn = Min(carWheelSpeed, 1.0f);
    carWheelSpeed = turn;
    float frontAngle = turn + wheelSpinAngle[0];
    wheelSpinAngle[0] = frontAngle;
    double rearAngle = (double)turn + wheelSpinAngle[1];   // kept unrounded
    wheelSpinAngle[1] = (float)rearAngle;
    if (frontAngle > 1.0f)
        wheelSpinAngle[0] = (float)((double)frontAngle - Ftol(frontAngle));
    else if (frontAngle < 0.0f)
        wheelSpinAngle[0] = (float)((double)frontAngle - (Ftol(frontAngle) - 1));
    if (rearAngle > 1.0f)
        wheelSpinAngle[1] = (float)(rearAngle - Ftol(rearAngle));
    else if (rearAngle < 0.0f)
        wheelSpinAngle[1] = (float)(rearAngle - (Ftol(rearAngle) - 1));

    // Sliding when the path's direction is away from the car's heading: the rear wheels first, then all four
    VU0_v4unitxyz(&direction, &direction);
    float heading = v3dotprod(MatrixRow(&orientation, 2), &direction);
    if (heading < 0.707f) {
        unknown3FE = 1;
        wheelSlip[3] = 1.0f;
        wheelSlip[2] = 1.0f;
        if (heading < 0.25f) {
            wheelSlip[1] = 1.0f;
            wheelSlip[0] = 1.0f;
        }
    } else {
        unknown3FE = 0;
    }
    float side = v3dotprod(MatrixRow(&orientation, 0), &direction);
    carSteer = -1.0f > side ? -1.0f : (1.0f < side ? 1.0f : side);

    // The wheels put on the ground under the levers
    if (CallGetPhysics(this)->isSnowmobile == 0 && CallGetPhysics(this)->subPhysics == 0) {
        for (int i = 0; i < kCarWheels; i++) {
            VU0_MATRIX4_vect3rotate(&body->info->levers[i], &orientation, &body->info->worldLevers[i]);
            wheelPos[i] = body->info->worldLevers[i];
            VU0_v3add(&wheelPos[i], position, &wheelPos[i]);
            body->TempGetHeightInformation(false, AsCoord3(&wheelPos[i]), &wheelRoadNormal[i], &wheels[i]);
            double compression = (double)wheelRoadNormal[i].w + physics->springRestLength;
            if (!(compression < physics->springCompressionLimit))
                compression = physics->springCompressionLimit;
            if (compression > 0.0) {
                suspensionCompression[i] = (float)compression;
            } else {
                suspensionCompression[i] = 0.0f;
                wheelSlip[i] = 0.0f;
            }
        }
    } else {
        for (int i = 0; i < kCarWheels; i++) {
            wheelSlip[i] = 0.0f;
            suspensionCompression[i] = 0.0f;
        }
    }
}
