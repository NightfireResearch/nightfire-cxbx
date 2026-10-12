#pragma fp_contract(off)

#include "PlayerCamState.h"

#include "PlayerCamera.h"
#include "../../common/xbeOverload.h"
#include "../engine/GameLoop.h"         // LaunchPage
#include "../game/Hud.h"
#include "../game/MissionManager.h"
#include "../game/Simulation.h"
#include "../physics/PhysicsMath.h"     // Abs
#include "../game/VehicleSound.h"     // AVehicle
#include "../physics/RigidBody.h"       // PVehicle
#include "../../helpers.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// RPlayerCamState. See PlayerCamState.h.
// ---------------------------------------------------------------------------------------------------------------

// The game's code not ported yet

namespace {

constexpr int32_t kFirstSteps = 3;      // the steps in which the drive inputs are held off
constexpr int32_t kUnknown30Time = 120;
const char kSpinTrack[] = "snow1a_mis3";   // the one track with the spin

} // namespace


namespace {

// The player's car's engine sound (its GetAudio, through its vtable)
AVehicle *CarAudio() {
    typedef AVehicle *(PVehicle::*Method)();
    PVehicle *car = fgSimulation.cars.first[0];
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
    paused = fgSimulation.simState == kSimPaused;
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
    paused = fgSimulation.simState == kSimPaused || fgSimulation.stepCount < kFirstSteps;
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
    CarAudio()->turningSideways = rotating;
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
    CarAudio()->turningUpDown = rotating;
}

// FUNC_AT(0x00089e10)
void RPlayerCamState::AutoDriveCamInputHandler(int input, float value) {
    paused = fgSimulation.simState == kSimPaused;
    SMissionManager *mission = glbMissionManager;
    if (mission->missionState == kMissionLost)
        return;
    bool allowed = mission->driveMissile == NULL;
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
        if (mission->cinematicMode != 0 || _stricmp(Launch.missionName, kSpinTrack) != 0)
            break;
        CarAudio()->turningSideways = 1;
        if (allowed && !paused)
            camera->InitSpin();
        break;
    case kActionCamCentre:
        if (allowed && !paused)
            unknown0D = true;
        CarAudio()->turningSideways = 0;
        break;
    case kActionAimZoomRight:
        if (!aiming) {
            if (mission->superEasy == 0 || lockedOn)
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
    if (aiming || glbMissionManager->driveMissile != NULL)
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
    CarAudio()->aiming = 1;
    if (GHud::TheApp() != NULL)
        GHud::TheApp()->AimOn();
}

// FUNC_AT(0x0008a390)
void RPlayerCamState::AimRelease() {
    if (lockedOn)
        return;
    if (aiming) {
        CarAudio()->aiming = 0;
        if (GHud::TheApp() != NULL)
            GHud::TheApp()->AimOff();
    }
    aiming = false;
    unknown0B = false;
    unknown0F = false;
    camera->ResetZoomSlope();
    camera->ToggleAutoDriveZoom(aiming);
}
