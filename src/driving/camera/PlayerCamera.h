#ifndef DRIVING_CAMERA_PLAYERCAMERA_H_
#define DRIVING_CAMERA_PLAYERCAMERA_H_

// RPlayerCamera, the player's camera (0x370 bytes, vtable 0x001918ac): an RWorldCamera that follows its anchor in
// one of the modes the camera tuning file describes (chase, bumper, dashboard, heli, tumble, ellipse, spline,
// fixed, auto-drive, missile, animation), with zoom, pitch and yaw limits, shaking, collision with the world, and
// the director's mode changes. Its methods are ported in PlayerCameraA.cpp (0x00080a60..0x00083190),
// PlayerCameraB.cpp (0x00083190..0x00086bb0) and PlayerCameraC.cpp (0x00086bb0..0x00089b70).

#include <stddef.h>
#include <stdint.h>

#include "../../helpers.h"
#include "../world/CollisionTypes.h"    // Coord4, MATRIX4
#include "Camera.h"                     // RWorldCamera
#include "CameraIniLoader.h"            // the tuning file's tables
#include "DirectorQueue.h"

struct PhysicsObject;
struct RigidBody;
struct SimpleRigidBody;
class RCameraSpline;
class RPlayerCamState;
class WCollider;
class WWorldPos;
class WRoadNav;

namespace CARP {
class Instance;
}

class RPlayerCamera : public RWorldCamera {
public:
    int cameraMode;                     // +0x130 an index into the tuning file's mode table
    int previousCameraMode;             // +0x134
    int lastSelectableCameraMode;       // +0x138
    int unknown13C;                     // +0x13c
    unsigned tumbleCamIndex;            // +0x140
    bool unknown144;                    // +0x144
    bool unknown145;                    // +0x145
    uint8_t pad146[2];
    int lastUpdateStep;                 // +0x148 the simulation step UpdateCamera last ran
    uint8_t pad14C[4];
    const Coord4 *lockOnPoint;          // +0x150 the point locked on to (CameraLockOn's first argument), NULL for none
    uint8_t pad154[0xc];
    Coord4 lockOnTarget;                // +0x160
    int lockOnParam1;                   // +0x170
    int lockOnParam2;                   // +0x174
    bool autoDriveForwardLock;          // +0x178
    uint8_t pad179[7];
    Coord4 forwardAimVec;               // +0x180
    MATRIX4 aimMatrix;                  // +0x190
    float unknown1D0;                   // +0x1d0
    float unknown1D4;                   // +0x1d4
    float unknown1D8;                   // +0x1d8
    float unknown1DC;                   // +0x1dc
    float aimPitch;                     // +0x1e0
    float aimYaw;                       // +0x1e4
    float targetAngleX;                 // +0x1e8
    float targetAngleY;                 // +0x1ec
    float unknown1F0;                   // +0x1f0
    float unknown1F4;                   // +0x1f4
    int adWeaponAnimState;              // +0x1f8
    bool adWeaponFlag;                  // +0x1fc
    uint8_t pad1FD[3];
    int unknown200;                     // +0x200
    int weaponFired;                    // +0x204
    int spinState;                      // +0x208
    uint8_t pad20C[4];
    Coord4 spinVec;                     // +0x210
    bool unknown220;                    // +0x220
    bool unknown221;                    // +0x221
    uint8_t pad222[2];
    int collisionState;                 // +0x224
    float unknown228;                   // +0x228
    float transitionFactor;             // +0x22c
    Coord4 unknown230;                  // +0x230
    Coord4 unknown240;                  // +0x240
    float unknown250;                   // +0x250
    float unknown254;                   // +0x254
    float unknown258;                   // +0x258
    float unknown25C;                   // +0x25c
    float unknown260;                   // +0x260
    int unknown264;                     // +0x264
    int unknown268;                     // +0x268
    int unknown26C;                     // +0x26c
    int shakeStepsLeft;                 // +0x270 the shake's steps remaining
    float shakeAmount;                  // +0x274
    bool shaking;                       // +0x278
    uint8_t transitionActive;           // +0x279
    uint8_t transitionStep;             // +0x27a
    int8_t currentArm;                  // +0x27b
    int8_t autoDriveArm;                // +0x27c the auto-drive arm in use (fgCameraTables.autoDriveArms), -1 none
    uint8_t unknown27D;                 // +0x27d
    uint8_t pad27E[0x12];
    float unknown290;                   // +0x290
    float unknown294;                   // +0x294
    float zoom298;                      // +0x298
    float zoom29C;                      // +0x29c
    float autoDriveRotationX;           // +0x2a0
    float autoDriveRotationY;           // +0x2a4
    bool autoDriveRotating;             // +0x2a8
    uint8_t pad2A9[3];
    float autoDriveZoom;                // +0x2ac
    float zoom2B0;                      // +0x2b0
    float zoomSlope;                    // +0x2b4
    uint8_t pad2B8[8];
    Coord4 upVector;                    // +0x2c0
    Coord4 unknown2D0;                  // +0x2d0
    Coord4 transitionVec;               // +0x2e0
    Coord4 cameraOffset;                // +0x2f0
    Coord4 fixedCamPosition;            // +0x300
    MATRIX4 relativeAnimMatrix;         // +0x310
    RCameraSpline *spline;              // +0x350
    RPlayerCamState *state;             // +0x354
    RCameraIniLoader *iniLoader;        // +0x358
    WCollider *collider;                // +0x35c
    WWorldPos *worldPos;                // +0x360
    WRoadNav *roadNav;                  // +0x364
    RDirectorQueue *directorQueue;      // +0x368
    uint8_t pad36C[4];

    // ---- PlayerCameraA.cpp: the small methods, the dashboard, bumper and animation cameras
    static void Shutdown();                                                                     // 0x00080a60
    void UpdateBumperCam();                                                                     // 0x00080b90
    void UpdateAIPathAnimationCam();                                                            // 0x00080cf0
    double GetZoomPercent();                                                                    // 0x00080e20
    void SetupCameraZoom();                                                                     // 0x00080e80
    void GetSafeWRoadNavPosition(Coord4 *position, float *height);                              // 0x00081110
    int8_t FindHeliArmInd(int mode);                                                            // 0x000811d0
    int8_t FindAutoDriveArmInd(int mode);                                                       // 0x00081290
    bool DirectorSetAnchor(RDirectorQueueData *data);                                           // 0x00081340
    void ResetCamera();                                                                         // 0x00081430
    bool DoSmoothModeChange();                                                                  // 0x00081460
    void SetCameraLookBack(bool lookBack);                                                      // 0x000814c0
    double SetAutoDriveRotation(float rotation);                                                // 0x00081540
    void CameraLockOn(const Coord4 *point, const Coord4 *target, int param, int quiet);         // 0x00081610
    void SetAutoDriveZoom(float zoom);                                                          // 0x000816e0
    void ToggleAutoDriveZoom(bool aim);                                                         // 0x00081700
    void TriggerCarAnimationCamera(uint32_t animationId, int steps, uint16_t delay,
                                   PhysicsObject *animAnchor, int flags);                       // 0x00081740
    void PauseOff();                                                                            // 0x000817f0
    void CameraInputCallback(int input, float value);                                           // 0x00081840
    const Coord4* GetForwardAimVec4(float blend);                                               // 0x00081890
    void SetAutoDriveForwardLock(bool lock);                                                    // 0x00081930
    void SetControlToCPU(bool cpu);                                                             // 0x00081940
    void WeaponFired(int weapon);                                                               // 0x00081950
    uint8_t CameraAiming();                                                                     // 0x00081980
    void InitSpin();                                                                            // 0x000819a0
    void InitWeaponChange();                                                                    // 0x00081a20
    void InitAimZoom();                                                                         // 0x00081a70
    void ResetZoomSlope();                                                                      // 0x00081ad0
    static int GetMaxTumble();                                                                  // 0x00081af0
    void Destruct();                                                                            // 0x00081bf0
    void UpdateDashboardCam();                                                                  // 0x00081d10
    void UpdateWorldAnimationCam();                                                             // 0x00082400
    void UpdateRelativeAnimationCam();                                                          // 0x00082840
    bool UpdateADTargetAngles(float *pitchOut, float *yawOut);                                  // 0x00082e00

    // ---- PlayerCameraB.cpp: limits, zoom, mode changes, transitions, the spline, fixed, auto-drive cameras
    bool LimitPitchYaw(const AutoDriveArmInfo *arm, float *pitch, float *yaw);                  // 0x00083190
    void SetCameraZoom(int kind, float value, int steps);                                       // 0x00083940
    bool AdjustCamAroundObjectEllipse(Coord4 *position, RigidBody *body, SimpleRigidBody *simpleBody,
                                      int unknown);                                             // 0x00083b40
    void InitTransition(char active, Coord4 *from, Coord4 *to, int steps);                      // 0x00083d70
    void UpdateTransition(Coord4 *position, Coord4 *target);                                    // 0x00083e10
    void ShakeCamera(int duration, float range, Coord4 *origin, int keepOrigin);                // 0x00083e60
    void SetCameraModeByIndex(int mode, uint16_t delay, uint16_t flags, uint16_t unknown14, uint32_t unknown24,
                              CARP::Instance *data18, const Coord4 *position);                  // 0x00083f50
    void NextCameraMode(uint16_t delay);                                                        // 0x00084030
    void PrevCameraMode(uint16_t delay);                                                        // 0x000840b0
    void DirectorChangeCameraMode(RDirectorQueueData *data);                                    // 0x00084130
    void UpdateCurrentArm();                                                                    // 0x00084680
    bool SetAutoDriveRotationX(float rotation);                                                 // 0x000847f0
    bool SetAutoDriveRotationY(float rotation);                                                 // 0x00084820
    void TriggerFixedCamera(Coord4 *position, int index, uint16_t delay);                       // 0x00084850
    void SetCinematicCamera(Coord4 *place, uint16_t delay, uint16_t unknown14);                 // 0x000848d0
    RPlayerCamera* Delete(unsigned flags);                                                      // 0x00084a30
    void UpdateSplineCam();                                                                     // 0x00084a60
    void UpdateFixedCam();                                                                      // 0x00085160
    void UpdateADWeaponAnims();                                                                 // 0x000853e0
    void UpdateAutoDriveCam();                                                                  // 0x000857d0

    // ---- PlayerCameraC.cpp: collisions, shaking, the aim matrix, UpdateCamera, the heli, tumble and ellipse
    //      cameras, the Set*Camera functions, construction, restart, old/last mode, the animation triggers
    bool CheckObjectCollisions(Coord4 *position);                                               // 0x00086bb0
    int ResolveAllCollisions(Coord4 *position, const Coord4 *target, bool objects);             // 0x00086d00
    void CheckForCameraShaking();                                                               // 0x000873e0
    MATRIX4* GetAimMatrix4(float yawScale, float pitchScale);                                   // 0x000876b0
    void UpdateCamera();                // vtable slot 3                                        // 0x000877e0
    void UpdateMomentumHeliCam();                                                               // 0x00087880
    void UpdateTumbleCam();                                                                     // 0x00088380
    void UpdateEllipseCam();                                                                    // 0x000886f0
    void SetAutoDriveCamera(float x, float y, int w);                                           // 0x00088b10
    void SetMissileCamera(PhysicsObject *missile);                                              // 0x00088be0
    void SetPauseCamera();                                                                      // 0x00088cc0
    void SetTumbleCam(unsigned index);                                                          // 0x00088d10
    void RestartCamera();               // vtable slot 4                                        // 0x00088d70
    void AbortCinematic();                                                                      // 0x00089010
    RPlayerCamera* Construct();                                                                 // 0x00089060
    void SetOldCameraMode(int mode, int delay, unsigned b, bool relative);                      // 0x00089200
    void SetLastSelectableCameraMode(int delay, unsigned b, bool relative, bool notAutoDrive);  // 0x000894d0
    void EndMissileCamera();                                                                    // 0x000895e0
    void TriggerAnimationCamera(CARP::Instance *animation, int b, unsigned short delay, bool timePath,
                                int fov);                                                       // 0x00089630
    void TriggerAIPathAnimationCamera(void *path, int b, unsigned short delay, int fov);        // 0x000898d0
    void ForceCameraChange(unsigned id, int delay, int b, int unused);                          // 0x00089a40
    void EndCameraAnim(unsigned short delay, int b, bool c, unsigned id);                       // 0x00089ac0
};
static_assert(sizeof(RPlayerCamera) == 0x370, "RPlayerCamera is 880 bytes");

// One of the renderer's camera views (RRenderHigh sets the table): the view and its camera
struct CameraView {
    RViewCamera *view;
    RPlayerCamera *camera;
};

#define CameraViews (*(CameraView **)0x001ec488)        // (name ours)

// ---- the player camera's globals (names ours)

// Set by a lock-on (RPlayerCamera::CameraLockOn, RPlayerCamState's input), cleared with it; AICharacterHands'
// DoFiring, DoIdling and DoReloading read it
#define CameraLockOnFlag U8_AT(0x001dda98)
// Where the last shake that keeps its origin came from (ShakeCamera sets it; CheckForCameraShaking reads it)
#define ShakeOrigin (*(Coord4 *)0x001ec3a0)

#endif  // DRIVING_CAMERA_PLAYERCAMERA_H_
