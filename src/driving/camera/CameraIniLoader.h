#ifndef DRIVING_CAMERA_CAMERAINILOADER_H_
#define DRIVING_CAMERA_CAMERAINILOADER_H_

// ---------------------------------------------------------------------------------------------------------------
// RCameraIniLoader: the camera tuning file, data/render/camera.ini, read once into the tables the player camera
// runs from. Each section is a camera mode, its type found in its name ("Heli", "Spline", "Ellipse", "Bumper",
// "Dashboard", "Fixed", "Tumble", "WorldAnim", "RelativeAnim", "AIPathAnim", "Collision", "AutoDrive"), taken only
// when its "car" list names the player's car (or it has none); "<section>:Arm<n>" sections hold a heli or auto-drive
// mode's arms, [Global] the constants. Every mode gets an entry in the mode table, and most a record in their type's
// table as well. Field names are the file's keys; the struct names are ours.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "Camera.h"                     // CameraAnchorInfo
#include "../data/CoordConvert.h"       // Coord3, Coord4
#include "../../helpers.h"

class RPlayerCamera;

// ---- the mode table

// What a mode is (its section's name)
enum CameraModeType : uint16_t {
    kCameraBumper = 0x001,
    kCameraHeli = 0x002,
    kCameraSpline = 0x004,
    kCameraEllipse = 0x008,
    kCameraFixed = 0x010,
    kCameraTumble = 0x020,
    kCameraAnimation = 0x040,           // WorldAnim and RelativeAnim
    kCameraAIPathAnimation = 0x080,
    kCameraCollision = 0x100,
    kCameraAutoDrive = 0x200,
    kCameraDashboard = 0x400,
};

// The RPlayerCamera methods the modes run (CameraModeInfo::update: the original's address, called thiscall with no
// arguments)
enum CameraModeUpdate : uint32_t {
    kUpdateBumperCam = 0x00080b90,
    kUpdateAIPathAnimationCam = 0x00080cf0,
    kUpdateDashboardCam = 0x00081d10,
    kUpdateWorldAnimationCam = 0x00082400,
    kUpdateRelativeAnimationCam = 0x00082840,
    kUpdateSplineCam = 0x00084a60,
    kUpdateFixedCam = 0x00085160,
    kUpdateAutoDriveCam = 0x000857d0,
    kUpdateMomentumHeliCam = 0x00087880,
    kUpdateTumbleCam = 0x00088380,
    kUpdateEllipseCam = 0x000886f0,
    kUpdateNothing = 0x000d3580,        // dummyNullFunction (Collision)
};

// A mode (0x18 bytes). The table has one entry more than there are modes, a copy of the first.
struct CameraModeInfo {
    uint8_t tumble;                     // +0x00
    uint8_t shake;                      // +0x01
    int8_t smoothTrans;                 // +0x02 also the default of its arms' armTransition
    uint8_t lookBack;                   // +0x03
    int8_t index;                       // +0x04 its record in its type's table (0 for the types without one)
    uint8_t selectable;                 // +0x05
    uint8_t lerpRotation;               // +0x06
    uint8_t interiorView;               // +0x07
    int16_t camID;                      // +0x08
    uint16_t type;                      // +0x0a CameraModeType
    float defaultFov;                   // +0x0c
    float explosionShakeScale;          // +0x10
    uint32_t update;                    // +0x14 CameraModeUpdate
};
static_assert(sizeof(CameraModeInfo) == 0x18, "a camera mode is 24 bytes");

// ---- the types' tables

// A Bumper mode (0x40 bytes)
struct BumperCamInfo {
    Coord4 forwardArm;                  // +0x00 x, y, z, panUp
    Coord4 backwardsArm;                // +0x10 x, y, z, panUp
    CameraAnchorInfo anchor;            // +0x20
    uint8_t unknown38[8];
};
static_assert(sizeof(BumperCamInfo) == 0x40, "a bumper camera is 64 bytes");

// One of a Heli mode's arms ("<section>:Arm<n>", 0x40 bytes)
struct HeliArmInfo {
    CameraAnchorInfo anchor;            // +0x00
    uint32_t weapons[2];                // +0x18 ResolveWeaponNames' mask
    int32_t armTransition;              // +0x20
    uint8_t unknown24[0xc];
    float sideways;                     // +0x30 Heli_Sideways
    float height;                       // +0x34 Heli_Height
    float distance;                     // +0x38 Heli_Distance
    uint8_t unknown3C[4];
};
static_assert(sizeof(HeliArmInfo) == 0x40, "a heli arm is 64 bytes");
static_assert(offsetof(HeliArmInfo, weapons) == 0x18 && offsetof(HeliArmInfo, armTransition) == 0x20 &&
              offsetof(HeliArmInfo, sideways) == 0x30, "heli arm layout");

// A Heli mode (0x4c bytes)
struct HeliCamInfo {
    float minRate;                      // +0x00 Min_Rate
    float maxRate;                      // +0x04 Max_Rate
    float speedRateDiff;                // +0x08 Speed_Rate_Diff
    float heightFactor;                 // +0x0c Height_Factor
    float fallbackFactor;               // +0x10 Fallback_Factor
    uint8_t unknown14[4];
    float tumbleArmScale;               // +0x18 Tumble_Arm_Scale
    float maxFallback;                  // +0x1c Max_Fallback
    float vertigoLerp;                  // +0x20 Vertigo_Lerp
    int32_t armCount;                   // +0x24
    uint8_t rigidArm;                   // +0x28
    uint8_t checkCollisions;            // +0x29
    uint8_t pad2A[2];
    HeliArmInfo *arms;                  // +0x2c new[armCount]
    float maxVertigoDownhill;           // +0x30 MaxVertigoDownhill, 0-1
    float maxVertigoUphill;             // +0x34 MaxVertigoUphill, 0-1
    float noisePace;                    // +0x38
    float noiseAmount;                  // +0x3c
    float noiseFrequency;               // +0x40
    float upRate;                       // +0x44
    float lookUp;                       // +0x48
};
static_assert(sizeof(HeliCamInfo) == 0x4c, "a heli camera is 76 bytes");

// A Spline mode (0x30 bytes)
struct SplineCamInfo {
    CameraAnchorInfo anchor;            // +0x00
    float splineSpeed;                  // +0x18
    float heightOffset;                 // +0x1c
    float maxSplineCamDist;             // +0x20
    float expLifeTime;                  // +0x24
    float minZoomDist;                  // +0x28
    float minFov;                       // +0x2c
};
static_assert(sizeof(SplineCamInfo) == 0x30, "a spline camera is 48 bytes");

// An Ellipse mode (0x2c bytes)
struct EllipseCamInfo {
    CameraAnchorInfo anchor;            // +0x00
    float xRad;                         // +0x18
    float zRad;                         // +0x1c
    int32_t facets;                     // +0x20 rounded down to a multiple of 4
    int32_t heightCount;                // +0x24
    int8_t *heights;                    // +0x28 new[heightCount]: "heights", each doubled (at most 30)
};
static_assert(sizeof(EllipseCamInfo) == 0x2c, "an ellipse camera is 44 bytes");

// A Fixed mode (0x28 bytes)
struct FixedCamInfo {
    CameraAnchorInfo anchor;            // +0x00
    float minZoomDist;                  // +0x18
    float minFov;                       // +0x1c
    float maxZoomDist;                  // +0x20
    float rotSpeed;                     // +0x24
};
static_assert(sizeof(FixedCamInfo) == 0x28, "a fixed camera is 40 bytes");

// A Dashboard mode (0xc0 bytes). The vectors' fourth words are zero.
struct DashboardCamInfo {
    Coord4 forwardArm;                  // +0x00
    Coord4 backwardsArm;                // +0x10
    Coord4 forceScale;                  // +0x20
    Coord4 forceMax;                    // +0x30
    Coord4 torqueScale;                 // +0x40
    Coord4 torqueMax;                   // +0x50
    float forcePace;                    // +0x60
    float torquePace;                   // +0x64
    float forwardPitch;                 // +0x68
    float forwardYaw;                   // +0x6c
    float intertiaScale;                // +0x70 (the file's spelling)
    float intertiaMin;                  // +0x74
    float intertiaMax;                  // +0x78
    float steerScale;                   // +0x7c
    float steerMax;                     // +0x80
    float steerPace;                    // +0x84
    float glanceScale;                  // +0x88
    float glanceMax;                    // +0x8c
    float glancePace;                   // +0x90
    float noiseAmount;                  // +0x94
    float noiseFrequency;               // +0x98
    float vertigoLerp;                  // +0x9c Vertigo_Lerp
    float maxVertigoDownhill;           // +0xa0 0-1
    float maxVertigoUphill;             // +0xa4 0-1
    CameraAnchorInfo anchor;            // +0xa8
};
static_assert(sizeof(DashboardCamInfo) == 0xc0, "a dashboard camera is 192 bytes");

constexpr int kAutoDriveLimits = 5;     // maxYaw, maxYaw1 ... maxYaw4 and so on

// One of the AutoDrive mode's arms ("<section>:Arm<n>", 0x100 bytes). The angles are in turns (the file's in
// radians); the limits are clamped to +-0.5 (yaw) and +-0.999 (pitch). The five pairs of limits are bands of yaw,
// each with its own pitch limits; the yaw limits are in turns, the pitch limits sines. The player camera keeps its
// run-time state of the arm in it too (the rotations, the pitch, the blend back to the rest position).
struct AutoDriveArmInfo {
    Coord4 rotation;                    // +0x00 a quaternion, (0, 0, 0, 1) at load: where the arm is turned to now
    Coord4 rotationFrom;                // +0x10 (0, 0, 0, 1) at load: the turns it blends between
    Coord4 rotationTo;                  // +0x20 (0, 0, 0, 1) at load
    Coord3 relPos;                      // +0x30
    uint8_t unknown3C[4];
    float maxDeadzonePitch;             // +0x40
    float maxDeadzoneYaw;               // +0x44
    float maxAutoaimPitch;              // +0x48
    float maxAutoaimYaw;                // +0x4c
    float autoaimInterpolSpeed;         // +0x50
    bool allowDeadzone;                 // +0x54 AllowDeadzone
    uint8_t pad55[3];
    float aimFov;                       // +0x58
    float minFov;                       // +0x5c
    bool preserveTransform;             // +0x60 PreserveTransform
    bool hasRestPos;                    // +0x61 HasRestPos
    bool lockArmToCar;                  // +0x62 LockArmToCar
    uint8_t unknown63;
    float restPitchChange;              // +0x64 the pitch the blend back to the rest position covers
    float restInterpolFallScale;        // +0x68
    int32_t restStartStep;              // +0x6c the simulation step that blend began
    float pitch;                        // +0x70 the arm's pitch, in turns
    float normCursorMoveSpeed;          // +0x74 NormCursorMoveSpeed, times pi / 2
    float normCursorEndMoveSpeed;       // +0x78 NormCursorEndMoveSpeed, times pi / 2
    float targetCursorMoveSpeed;        // +0x7c TargetCursorMoveSpeed
    float targetCursorEndMoveSpeed;     // +0x80 TargetCursorEndMoveSpeed
    CameraAnchorInfo anchor;            // +0x84
    uint32_t weapons[2];                // +0x9c ResolveWeaponNames' mask
    float minYaw[kAutoDriveLimits];     // +0xa4
    float maxYaw[kAutoDriveLimits];     // +0xb8
    float minPitch[kAutoDriveLimits];   // +0xcc
    float maxPitch[kAutoDriveLimits];   // +0xe0
    float restPitch;                    // +0xf4 the rest position's pitch, in turns
    uint8_t unknownF8[8];
};
static_assert(sizeof(AutoDriveArmInfo) == 0x100, "an auto-drive arm is 256 bytes");
static_assert(offsetof(AutoDriveArmInfo, maxDeadzonePitch) == 0x40 && offsetof(AutoDriveArmInfo, aimFov) == 0x58 &&
              offsetof(AutoDriveArmInfo, restInterpolFallScale) == 0x68 &&
              offsetof(AutoDriveArmInfo, normCursorMoveSpeed) == 0x74 && offsetof(AutoDriveArmInfo, anchor) == 0x84 &&
              offsetof(AutoDriveArmInfo, minYaw) == 0xa4 && offsetof(AutoDriveArmInfo, maxPitch) == 0xe0 &&
              offsetof(AutoDriveArmInfo, restPitchChange) == 0x64 && offsetof(AutoDriveArmInfo, restStartStep) == 0x6c &&
              offsetof(AutoDriveArmInfo, pitch) == 0x70 && offsetof(AutoDriveArmInfo, restPitch) == 0xf4,
              "auto-drive arm layout");

constexpr int kMaxCameraArms = 32;      // "<section>:Arm0" ... "Arm31"

// ---- the tables (at 0x001ec310)
struct CameraTables {
    CameraAnchorInfo animationAnchor;   // +0x00 the last WorldAnim, RelativeAnim or AIPathAnim section's
    CameraAnchorInfo tumbleAnchor;      // +0x18
    float tumbleVectorLerp;             // +0x30 vectorLerp
    float tumbleRelPosLerp;             // +0x34 relPosLerp
    float maxWeapTransTime;             // +0x38 MaxWeapTransTime (AutoDrive)
    float autoDriveInertia;             // +0x3c inertia
    float autoDriveZoomFactor;          // +0x40 zoomFactor
    int32_t autoDriveArmCount;          // +0x44
    AutoDriveArmInfo *autoDriveArms;    // +0x48
    BumperCamInfo *bumpers;             // +0x4c
    DashboardCamInfo *dashboards;       // +0x50
    HeliCamInfo *helis;                 // +0x54
    CameraModeInfo *modes;              // +0x58 also what says the file is loaded
    SplineCamInfo *splines;             // +0x5c
    EllipseCamInfo *ellipses;           // +0x60
    FixedCamInfo *fixeds;               // +0x64
    int32_t previousAutoDriveArm;       // +0x68 the arm before a weapon's change of arm, -1 none; zeroed with the arms
    float weaponArmTurn;                // +0x6c twice the turn between the two arms of that change
    int32_t heliCount;                  // +0x70
    int32_t splineCount;                // +0x74
    int32_t ellipseCount;               // +0x78
    int32_t fixedCount;                 // +0x7c
    int32_t bumperCount;                // +0x80
    int32_t dashboardCount;             // +0x84
    int32_t modeCount;                  // +0x88
};
static_assert(sizeof(CameraTables) == 0x8c, "the camera tables are 0x8c bytes");
static_assert(offsetof(CameraTables, modes) == 0x58 && offsetof(CameraTables, previousAutoDriveArm) == 0x68 &&
              offsetof(CameraTables, modeCount) == 0x88, "camera tables layout");

// The modes found by name, -1 for none (at 0x001c4194)
struct CameraModeIndices {
    int8_t tumble;                      // +0x0
    int8_t worldAnimation;              // +0x1
    int8_t relativeAnimation;           // +0x2
    int8_t aiPathAnimation;             // +0x3
    int8_t collision;                   // +0x4
    int8_t autoDrive;                   // +0x5 the first AutoDrive section only
    int8_t pause;                       // +0x6 a section named "Pause"
    int8_t missile;                     // +0x7 ... "Missile"
    int8_t cinematic;                   // +0x8 the last Heli mode with "Cinematic"
    int8_t cinematicHeli;               // +0x9 one past its record in the heli table
    int8_t defaultCamera;               // +0xa the first with "defaultCamera"
};
static_assert(sizeof(CameraModeIndices) == 0xb, "eleven mode indices");

// The [Global] section (at 0x001c3e40; the game's data holds the defaults, which LoadFile passes again)
struct CameraConstants {
    int32_t kMaxTumble;                 // +0x00
    int32_t kMaxCollision;              // +0x04
    int32_t kDefualtTransition;         // +0x08 (the file's spelling)
    float kTransRate;                   // +0x0c
    float kTransRateLerpRate;           // +0x10
    float kBumperYLerpRate;             // +0x14
    float kCollideRadius;               // +0x18
    float kSplineOffsetLerp;            // +0x1c
    float kExplosionScale;              // +0x20
    float kExplosionMaxShake;           // +0x24
    float kExplosionAfterShock;         // +0x28
    float kExplosionTimeScale;          // +0x2c
    Coord4 kCameraObjectRadiusEx;       // +0x30 w 1 (not read)
    float kCameraObjectSphereRad;       // +0x40
    float kCollisionMinRate;            // +0x44
    float kCollisionMaxRate;            // +0x48
    float kCollisionRateDiff;           // +0x4c
    float kZoomIncSpeed;                // +0x50
    float kCenteringSpeed;              // +0x54
    int32_t kWeaponArmChangeLatency;    // +0x58
    int32_t kMissileCamLatency;         // +0x5c
    float kfWeaponAnimationLength;      // +0x60
    float kfWeaponAnimationAmplitude;   // +0x64
    float kfMaxAutoaimDistance;         // +0x68
};
static_assert(sizeof(CameraConstants) == 0x6c, "the camera constants are 0x6c bytes");

#define fgCameraTables (*(CameraTables *)0x001ec310)
#define fgCameraModeIndices (*(CameraModeIndices *)0x001c4194)
#define fgCameraConstants (*(CameraConstants *)0x001c3e40)
#define kExplosionShakePeriod I32_AT(0x001ebca4)   // [Global]'s; a static initialiser sets the default first
#define kAutoDriveLatency I32_AT(0x001ebc88)       // [Global]'s

// ---- RCameraIniLoader (no data: a byte)

class RCameraIniLoader {
public:
    // Keeps the player camera (a static, at 0x001ebc8c).                                           0x00078550
    RCameraIniLoader* Construct(RPlayerCamera *camera);
    // Reads the file into the tables, once (nothing when the mode table exists).                    0x00078770
    static void LoadFile();
    static void LoadFileThunk();        // a jump to it                                              0x00081880
    // The bit of each weapon whose name is in `names` (by the weapon manager's slots' names) set in the 64-bit
    // mask; nothing for a weapon numbered 64 or more, or after it.                                  0x000785f0
    static void ResolveWeaponNames(const char *names, uint32_t *mask);
    // Whether a "car" list (lower-cased here, in place) names the player's car: its type, or its secondary type,
    // or the override car type when its class is 0. True for no list.                               0x00078670
    static bool CheckCameraAgainstCar(char *cars);
};

// FUN_00078560: the value clamped to [min, max] (name ours)
float ClampCameraValue(float value, float min, float max);              // 0x00078560

// FUN_00078590: whether `name` is the first of the names in `list` it is found in, ended by a space, a comma or the
// end. The original takes the list in EDI: FUN_00078590 is the adaptor, CarListContains the C++ under it.
void FUN_00078590();
bool CarListContains(const char *list, const char *name);

#endif // DRIVING_CAMERA_CAMERAINILOADER_H_
