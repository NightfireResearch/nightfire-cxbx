#ifndef DRIVING_DEVTOOLS_ORIGCONSTANTS_H_
#define DRIVING_DEVTOOLS_ORIGCONSTANTS_H_

// The originals' copies of the constants our ports give directly, whose static initialisers no longer run (see
// engine/StaticInitTable.cpp): a shadow that runs an original reading one fills it first by running the original
// initialiser, as the game's start-up did.

typedef void (*StaticInitialiserFn)();

// camera/CameraIniLoader.cpp's TwoPi and TurnsPerRadian, CameraSpline.cpp's and PlayerCameraA.cpp's
// RadiansToTurns, PlayerCameraB.cpp's DiagonalBoost
inline void FillOrigCameraConstants() {
    ((StaticInitialiserFn)0x00157ab0)();    // TwoPi, 0x001ebc90
    ((StaticInitialiserFn)0x00157ac0)();    // TurnsPerRadian, 0x001ebc94
    ((StaticInitialiserFn)0x00157b90)();    // RadiansToTurns, 0x001ebcb0 (CameraSpline.cpp)
    ((StaticInitialiserFn)0x00157e80)();    // RadiansToTurns, 0x001ec3b8 (PlayerCameraA.cpp)
    ((StaticInitialiserFn)0x00157ee0)();    // DiagonalBoost, 0x001ec3c4
}

// physics/RigidBodyBasics.cpp's RigidGroundRestitutionRate, RigidRestitutionRateC8, RigidBuildingRestitutionRate
inline void FillOrigRigidConstants() {
    ((StaticInitialiserFn)0x00158c80)();    // 0x00233fbc
    ((StaticInitialiserFn)0x00158ca0)();    // 0x00233fc0
    ((StaticInitialiserFn)0x00158cc0)();    // 0x00233fc4
}

#endif // DRIVING_DEVTOOLS_ORIGCONSTANTS_H_
