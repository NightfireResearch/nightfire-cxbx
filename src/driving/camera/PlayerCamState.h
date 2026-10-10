#ifndef DRIVING_CAMERA_PLAYERCAMSTATE_H_
#define DRIVING_CAMERA_PLAYERCAMSTATE_H_

// ---------------------------------------------------------------------------------------------------------------
// RPlayerCamState (0x34 bytes): what the player's inputs have asked of the player camera - the input handlers for
// driving (mode changes, looking back) and for the auto-drive camera (its two rotations, zoom, spin, weapon change,
// lock-on), aiming, and the flags that hold input off.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "../engine/ActionQueue.hpp"   // GameAction, the inputs the handlers act on

class RPlayerCamera;

class RPlayerCamState {
public:
    RPlayerCamera *camera;              // +0x00
    bool resetEnabled;                  // +0x04 ResetState only sets it while clear
    bool unknown05;                     // +0x05 set by ResetStateForAnimation; 05-08 hold the drive inputs off
    bool unknown06;                     // +0x06
    bool unknown07;                     // +0x07 RPlayerCamera::SetControlToCPU's
    bool unknown08;                     // +0x08
    bool paused;                        // +0x09 the simulation paused (or, driving, in its first three steps)
    bool lookBackOff;                   // +0x0a
    bool unknown0B;                     // +0x0b the auto-drive target angles are being followed
    uint8_t aiming;                     // +0x0c AimZoom to AimRelease (a byte: the camera compares it with 1)
    bool unknown0D;                     // +0x0d cleared by a rotation input other than zero
    bool lockedOn;                      // +0x0e (RPlayerCamera::CameraLockOn sets it)
    bool unknown0F;                     // +0x0f set: no second rotation inputs while aiming
    float rotationY[2];                 // +0x10 the auto-drive rotation inputs, two of each
    float rotationX[2];                 // +0x18
    bool unknown20;                     // +0x20 (RPlayerCamera's missile and mode-change code sets and clears it)
    uint8_t lookingBack;                // +0x21 (RPlayerCamera's; a byte: compared with 1)
    uint8_t pad22[2];
    float unknown24;                    // +0x24 a drive input's value (the dashboard camera's steering)
    float unknown28;                    // +0x28 (the dashboard camera's glance input)
    bool unknown2C;                     // +0x2c
    uint8_t pad2D[3];
    int32_t unknown30;                  // +0x30

    RPlayerCamState* Construct(RPlayerCamera *camera);                  // 0x00089df0
    void ResetState();                                                  // 0x00089b70
    void ResetStateForAnimation();                                      // 0x00089bd0
    void DriveCamInputHandler(int input, float value);                  // 0x00089be0
    void AutoDriveCamInputHandler(int input, float value);              // 0x00089e10
    void AimZoom();                                                     // 0x0008a310
    void AimRelease();                                                  // 0x0008a390

private:
    bool DriveInputHeld() const { return unknown05 || unknown06 || unknown07 || unknown08; }
    void RotateY(int input, float value);
    void RotateX(int input, float value, bool negate);
};
static_assert(sizeof(RPlayerCamState) == 0x34, "RPlayerCamState is 52 bytes");
static_assert(offsetof(RPlayerCamState, rotationY) == 0x10 && offsetof(RPlayerCamState, unknown20) == 0x20 &&
              offsetof(RPlayerCamState, lookingBack) == 0x21 && offsetof(RPlayerCamState, unknown24) == 0x24 &&
              offsetof(RPlayerCamState, unknown28) == 0x28 && offsetof(RPlayerCamState, unknown30) == 0x30,
              "RPlayerCamState layout");

#endif // DRIVING_CAMERA_PLAYERCAMSTATE_H_
