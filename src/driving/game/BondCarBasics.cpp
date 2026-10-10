#include "BondCarBasics.h"

#include "BondCar.h"
#include "Vehicle.h"
#include "../data/AttributeSet.h"
#include "../engine/ActionQueue.hpp"
#include "../engine/CoreFoundation.h"   // NullFunction
#include "../engine/UMemory.hpp"
#include "../world/Targeting.h"
#include "../../common/xbeOverload.h"   // XbeAddress

// ---------------------------------------------------------------------------------------------------------------
// PBondCar's small methods (0x00065ba0-0x00066060), ported from the listing: DebugObject, the destructor,
// GetCarColourVariation and the accessors.
//
// IsWheelOnGround's "return 0" tail (0x00066051, Ghidra's FUN_00066051) is only reached by its own branch.
// ---------------------------------------------------------------------------------------------------------------

// The C runtime's `eh vector destructor iterator`
#define EhVectorDestructor ((void (__stdcall *)(void *array, uint32_t size, int count, void *destructor))0x0013332e)

static_assert(sizeof(ActionQueue) == 0x974, "an action queue is 0x974 bytes");

// FUNC_AT(0x00065ba0)
void PBondCar::DebugObject() {
    PhysicsObject::DebugObject();
    if (physics->coastFrwRatio < physics->accelFrwRatio)
        physics->coastFrwRatio = physics->accelFrwRatio;
    if (physics->coastFrwRatio < physics->brakeFrwRatio)
        physics->coastFrwRatio = physics->brakeFrwRatio;
    physics->frictionLimitRear = physics->frictionLimitFront;
}

// FUNC_AT(0x00065bf0)
uint32_t GetCarColourVariation(const char *carType, uint32_t colour) {
    AttributeSet attributes;
    PVehicle::GetNamedAttribs(&attributes, carType);
    uint32_t colours = PVehicle::NumColoursAttrib(&attributes);
    uint32_t variations = colours < 1 ? 1 : colours;
    attributes.Destruct();
    return colour % variations;
}

// FUNC_AT(0x00065c70)
void PBondCar::Destruct() {
    vtable = reinterpret_cast<void **>(kPBondCarVtable);
    if (actionQueue != NULL) {
        actionQueue->Destruct();
        UMemory::FastFree(actionQueue, sizeof(ActionQueue));
    }
    if (targetBeacon != NULL) {
        TargetPicker.UnregisterTarget(targetBeacon);
        targetBeacon->ownerType = kTargetPoint;
        targetBeacon->RemoveReference();
    }
    for (int i = 0; i < kCarWheels; i++) {
        if (tyreTracks[i] != NULL)
            UMemory::FastFree(tyreTracks[i], kTyreTrackSize);
    }
    EhVectorDestructor(wheels, sizeof(WWorldPos), kCarWheels, (void *)XbeAddress(&NullFunction));
    PhysicsObject::Destruct();
}

// FUNC_AT(0x00065d50)
ABaseSound* PBondCar::GetAudio() {
    return audio;
}

// FUNC_AT(0x00065d60)
void PBondCar::SetAudio(ABaseSound *audio) {
    this->audio = audio;
}

// FUNC_AT(0x00065d70)
void PBondCar::SetAIGroundVehicle(AIGroundVehicle *vehicle) {
    aiGroundVehicle = vehicle;
}

// FUNC_AT(0x00065d80)
AIGroundVehicle* PBondCar::GetAIGroundVehiclePtr() {
    return aiGroundVehicle;
}

// FUNC_AT(0x00065d90)
int PBondCar::GetResetAvailable() {
    return resetAvailable;
}

// FUNC_AT(0x00065da0)
const char* PBondCar::GetCarType() {
    return carType;
}

// FUNC_AT(0x00065db0)
void PBondCar::SetCarClass(int carClass) {
    this->carClass = carClass;
}

// FUNC_AT(0x00065dc0)
int PBondCar::GetCarClass() {
    return carClass;
}

// FUNC_AT(0x00065dd0)
uint32_t PBondCar::GetCarColour() {
    return carColour;
}

// FUNC_AT(0x00065de0)
void PBondCar::SetCarControlSteering(float steering) {
    control.steering = steering;
}

// FUNC_AT(0x00065df0)
void PBondCar::SetCarControlSteeringVertical(float steering) {
    control.steeringVertical = steering;
}

// FUNC_AT(0x00065e00)
void PBondCar::SetCarControlStrafeHorizontal(float strafe) {
    control.strafeHorizontal = strafe;
}

// FUNC_AT(0x00065e10)
void PBondCar::SetCarControlStrafeVertical(float strafe) {
    control.strafeVertical = strafe;
}

// FUNC_AT(0x00065e20)
void PBondCar::SetCarControlGas(float gas) {
    control.gas = gas;
}

// FUNC_AT(0x00065e30)
void PBondCar::SetCarControlBrake(float brake) {
    control.brake = brake;
}

// FUNC_AT(0x00065e40)
void PBondCar::SetCarControlHandBrake(bool handBrake) {
    control.handBrake = handBrake;
}

// FUNC_AT(0x00065e50)
uint8_t PBondCar::GetCarControlHandBrake() {
    return control.handBrake;
}

// FUNC_AT(0x00065e60)
void PBondCar::SetCarControlFirePrimary(bool fire) {
    control.firePrimary = fire;
}

// FUNC_AT(0x00065e70)
void PBondCar::LockCarControlForever() {
    lockedControl = control;
    controlLockTimer = 0x7fffffff;
}

// FUNC_AT(0x00065ea0)
void PBondCar::SetTargetGas(float gas) {
    targetGas = gas;
}

// FUNC_AT(0x00065eb0)
void PBondCar::SetTargetBrake(float brake) {
    targetBrake = brake;
}

// FUNC_AT(0x00065ec0)
BondCarControl* PBondCar::GetCarControl(BondCarControl *result) {
    *result = control;
    return result;
}

// FUNC_AT(0x00065ee0)
uint8_t PBondCar::EmpActive() {
    return empActive;
}

// FUNC_AT(0x00065ef0)
float PBondCar::GetCarSpeed() {
    return carSpeed;
}

// FUNC_AT(0x00065f00)
bool PBondCar::IsTyreShredded(int8_t wheel) {
    return tyreDamagePoints[wheel] == 0;
}

// FUNC_AT(0x00065f20)
RTyreTrack* PBondCar::GetTyreTrackPtr(int wheel) {
    return tyreTracks[wheel];
}

// FUNC_AT(0x00065f30)
void PBondCar::SetScoreable(bool scoreable) {
    this->scoreable = scoreable;
}

// FUNC_AT(0x00065f40)
int PBondCar::InShock() {
    return inShock > 0;
}

// FUNC_AT(0x00065f50)
void PBondCar::SetOilSlick(bool oilSlick) {
    this->oilSlick = oilSlick;
}

// FUNC_AT(0x00065f60)
int PBondCar::GetNumWheelsOnGround() {
    return numWheelsOnGround;
}

// FUNC_AT(0x00065f70)
uint8_t PBondCar::GetDamageByPlayerTimer() {
    return damageByPlayerTimer;
}

// FUNC_AT(0x00065f80)
float PBondCar::GetCarWheelSpinAngle(int8_t axle) {
    return wheelSpinAngle[axle];
}

// FUNC_AT(0x00065f90)
float PBondCar::GetCarSteer() {
    return carSteer;
}

// FUNC_AT(0x00065fa0)
float PBondCar::GetSuspensionCompression(int wheel) {
    return suspensionCompression[wheel];
}

// FUNC_AT(0x00065fb0)
int PBondCar::IsReversing() {
    return reversing != 0;
}

// FUNC_AT(0x00065fc0)
void PBondCar::SetAgainstWallFlag(bool against) {
    againstWall = against;
}

// FUNC_AT(0x00065fd0)
float PBondCar::GetWheelRoadHeight(int wheel) {
    return wheelRoadNormal[wheel].w;
}

// FUNC_AT(0x00065ff0)
int PBondCar::GetWheelRoadSurface(int wheel) {
    return wheels[wheel].face.corner[2].tag.type;
}

// FUNC_AT(0x00066010)
Coord4* PBondCar::GetWheelRoadNormal(int wheel) {
    return &wheelRoadNormal[wheel];
}

// FUNC_AT(0x00066020)
void PBondCar::SetShieldPointLoc(float *shieldPoints) {
    shieldPointLoc = shieldPoints;
}

// FUNC_AT(0x00066030)
int PBondCar::IsWheelOnGround(int8_t wheel) {
    return suspensionCompression[wheel] != 0.0f;
}
