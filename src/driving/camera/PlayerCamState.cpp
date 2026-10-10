#pragma fp_contract(off)

#include "PlayerCamState.h"

#include "PlayerCamera.h"
#include "../../common/xbeOverload.h"
#include "../engine/GameInterfaces.hpp"   // GHud
#include "../engine/GameLoop.h"         // LaunchPage
#include "../engine/MissionManager.h"
#include "../physics/PhysicsMath.h"     // Abs
#include "../game/VehicleSound.h"     // AVehicle
#include "../physics/RigidBody.h"       // PVehicle
#include "../../helpers.h"

#include <stddef.h>
#include <stdint.h>

// ---------------------------------------------------------------------------------------------------------------
// RPlayerCamState. See PlayerCamState.h.
// ---------------------------------------------------------------------------------------------------------------

// The game's code not ported yet
#define GHud_AimOn ((void (__fastcall *)(GHud *, int))0x000d9ba0)
#define GHud_AimOff ((void (__fastcall *)(GHud *, int))0x000d9bd0)
#define Crt_stricmp ((int (*)(const char *, const char *))0x00134537)

namespace {

constexpr int32_t kSimPaused = 3;
constexpr int32_t kFirstSteps = 3;      // the steps in which the drive inputs are held off
constexpr int32_t kMissionNoCameraInput = 4;
constexpr int32_t kUnknown30Time = 120;
const char kSpinTrack[] = "snow1a_mis3";   // the one track with the spin

} // namespace

#define SimState I32_AT(0x00234e24)
#define SimStepCount I32_AT(0x00234e34)
#define playerPhysicsObject (*(PVehicle ***)0x00234e40)
#define Launch (*(LaunchPage *)0x00243b90)

namespace {

// The player's car's engine sound (its GetAudio, through its vtable)
AVehicle *CarAudio() {
    typedef AVehicle *(PVehicle::*Method)();
    PVehicle *car = *playerPhysicsObject;
    return (car->*XbeVirtual<Method>(car, PVehicle::kGetAudio))();
}

} // namespace

// FUNC_AT(0x00089b70)
void RPlayerCamState::ResetState() {
    if (!resetEnabled) {
        resetEnabled = true;
        return;
    }
    unknown0D = false;
    rotationY[0] = 0.0f;
    rotationX[0] = 0.0f;
    rotationY[1] = 0.0f;
    rotationX[1] = 0.0f;
    CameraLockOnFlag = 0;
    lockedOn = false;
    aiming = false;
    unknown0B = false;
    unknown0F = false;
    unknown05 = false;
    unknown06 = false;
    unknown07 = false;
    unknown08 = false;
    unknown20 = false;
    paused = SimState == kSimPaused;
    unknown24 = 0.0f;
    unknown28 = 0.0f;
    unknown2C = false;
    unknown30 = 0;
}

// FUNC_AT(0x00089bd0)
void RPlayerCamState::ResetStateForAnimation() {
    CameraLockOnFlag = 0;
    unknown05 = true;
    unknown20 = false;
}

// FUNC_AT(0x00089be0)
void RPlayerCamState::DriveCamInputHandler(int input, float value) {
    paused = SimState == kSimPaused || SimStepCount < kFirstSteps;
    switch (input) {
    case kActionChangeCamera:
    case kActionChangeCameraUp:
        if (!paused && !DriveInputHeld())
            camera->NextCameraMode(0);
        break;
    case kActionChangeCameraDown:
        if (!paused && !DriveInputHeld())
            camera->PrevCameraMode(0);
        break;
    case kActionCamLookBack:
        lookBackOff = false;
        if (!paused && !DriveInputHeld())
            camera->SetCameraLookBack(true);
        break;
    case kActionCamLookBackRelease:
        if (!lookBackOff) {
            lookBackOff = true;
            if (!paused)
                camera->SetCameraLookBack(false);
        }
        break;
    case kActionSteer:
        if (!paused && !DriveInputHeld())
            unknown24 = -value;
        break;
    case kActionCamRotateX:
        if (!paused && !DriveInputHeld())
            unknown24 = value;
        break;
    case kActionHandbrake:
        if (!paused && !DriveInputHeld()) {
            unknown2C = true;
            unknown30 = kUnknown30Time;
        }
        break;
    case kActionHandbrakeRelease:
        unknown2C = false;
        break;
    }
}

// FUNC_AT(0x00089df0)
RPlayerCamState* RPlayerCamState::Construct(RPlayerCamera *camera) {
    this->camera = camera;
    resetEnabled = true;
    ResetState();
    return this;
}

// One of the two rotation-about-y inputs: the camera turns by whichever of the two is larger.
void RPlayerCamState::RotateY(int input, float value) {
    float &own = rotationY[input == kActionCamRotateX ? 0 : 1];
    float other = rotationY[input == kActionCamRotateX ? 1 : 0];
    own = value;
    if (value != 0.0f)
        unknown0D = false;
    float rotation = Abs(other) <= Abs(value) ? value : other;
    bool rotating = !camera->SetAutoDriveRotationY(rotation) && rotation != 0.0f;
    CarAudio()->flagC2 = rotating;
}

// ... about x, `negate`d for one input of each pair
void RPlayerCamState::RotateX(int input, float value, bool negate) {
    bool first = input == kActionCamRotateY || input == kActionCamRotateYInv;
    float &own = rotationX[first ? 0 : 1];
    float other = rotationX[first ? 1 : 0];
    own = value;
    if (value != 0.0f)
        unknown0D = false;
    float rotation = Abs(other) <= Abs(value) ? value : other;
    bool rotating = !camera->SetAutoDriveRotationX(negate ? -rotation : rotation) &&
                    rotation != 0.0f;
    CarAudio()->flagC3 = rotating;
}

// FUNC_AT(0x00089e10)
void RPlayerCamState::AutoDriveCamInputHandler(int input, float value) {
    paused = SimState == kSimPaused;
    SMissionManager *mission = glbMissionManager;
    if (mission->unknown474 == kMissionNoCameraInput)
        return;
    bool allowed = mission->unknown47c == 0;
    switch (input) {
    case kActionCamRotateX:
        if (allowed)
            RotateY(input, value);
        break;
    case kActionCamRotateX2:
        if (aiming && unknown0F)
            break;
        if (allowed)
            RotateY(input, value);
        break;
    case kActionCamRotateY:
    case kActionCamRotateYInv:
        if (allowed)
            RotateX(input, value, input == kActionCamRotateY);
        break;
    case kActionCamRotateY2:
    case kActionCamRotateY2Inv:
        if (aiming && unknown0F)
            break;
        if (allowed)
            RotateX(input, value, input == kActionCamRotateY2);
        break;
    case kActionCamSpin:
        if (mission->unknown4f0 != 0 || Crt_stricmp(Launch.missionName, kSpinTrack) != 0)
            break;
        CarAudio()->flagC2 = 1;
        if (allowed && !paused)
            camera->InitSpin();
        break;
    case kActionCamCentre:
        if (allowed && !paused)
            unknown0D = true;
        CarAudio()->flagC2 = 0;
        break;
    case kActionAimZoomRight:
        if (!aiming) {
            if (mission->unknown710 == 0 || lockedOn)
                CameraLockOnFlag = 1;
        } else if (Launch.unknowna44 != 0 && !lockedOn) {
            CameraLockOnFlag = 0;
        }
        break;
    case kActionAimRelease:
        if (Launch.unknowna44 == 0 && !lockedOn)
            CameraLockOnFlag = 0;
        break;
    case kActionCamZoomInOut:
        if (aiming && !paused && allowed && !lockedOn)
            camera->SetAutoDriveZoom(-value);
        break;
    case kActionCamZoomIn:
        if (aiming && !paused && allowed && !lockedOn)
            camera->SetAutoDriveZoom(value > 0.5f ? -1.0f : 0.0f);
        break;
    case kActionCamZoomOut:
        if (aiming && !paused && allowed && !lockedOn)
            camera->SetAutoDriveZoom(value > 0.5f ? 1.0f : 0.0f);
        break;
    case kActionToggleSecondary:
    case kActionToggleSecondaryUp:
    case kActionToggleSecondaryDown:
        if (allowed && !paused)
            camera->InitWeaponChange();
        break;
    }
}

// FUNC_AT(0x0008a310)
void RPlayerCamState::AimZoom() {
    if (aiming || glbMissionManager->unknown47c != 0)
        return;
    aiming = true;
    unknown0D = false;
    unknown0B = false;
    camera->InitAimZoom();
    rotationX[0] = 0.0f;
    rotationY[0] = 0.0f;
    rotationX[1] = 0.0f;
    rotationY[1] = 0.0f;
    camera->SetAutoDriveRotationX(0.0f);
    camera->SetAutoDriveRotationY(rotationY[0]);
    CarAudio()->flagC4 = 1;
    if (GHud::TheApp() != NULL)
        GHud_AimOn(GHud::TheApp(), 0);
}

// FUNC_AT(0x0008a390)
void RPlayerCamState::AimRelease() {
    if (lockedOn)
        return;
    if (aiming) {
        CarAudio()->flagC4 = 0;
        if (GHud::TheApp() != NULL)
            GHud_AimOff(GHud::TheApp(), 0);
    }
    aiming = false;
    unknown0B = false;
    unknown0F = false;
    camera->ResetZoomSlope();
    camera->ToggleAutoDriveZoom(aiming);
}
