#ifndef DRIVING_DEVTOOLS_RIGIDORIGGLOBALS_H_
#define DRIVING_DEVTOOLS_RIGIDORIGGLOBALS_H_

// The originals' copies of the rigid body system's globals (physics/RigidBody.h): the scratch pad, the underwater
// flag, the restitution rates and the Rigid_* tuning. The rigid body shadows lend ours to the originals before
// they run.

#include <stdint.h>

#include "../physics/RigidBody.h"

#define OrigRigidScratchPad (*(RigidScratchPadFields **)0x00233fac)
#define OrigRigidUnderwater (*(bool *)0x00233fb0)

struct RigidOrigCopy {
    uintptr_t at;               // the original's copy
    const float *ours;
};

const RigidOrigCopy kRigidOrigCopies[] = {
    { 0x00233fbc, &RigidGroundRestitutionRate },
    { 0x00233fc0, &RigidRestitutionRateC8 },
    { 0x00233fc4, &RigidBuildingRestitutionRate },
    { 0x001c8f2c, &Rigid_GRAVITY },
    { 0x001c8f30, &Rigid_BODGE_PLAYER_GRAVITY },
    { 0x001c8f34, &Rigid_MICRO_THRESHOLD },
    { 0x001c8f38, &Rigid_GROUND_RESTITUTION },
    { 0x001c8f40, &Rigid_GROUND_FRICTION_COEFF },
    { 0x001c8f44, &Rigid_NATURAL_ANGULAR_DAMPING },
    { 0x001c8f48, &Rigid_SLEEP_VEL },
    { 0x001c8f4c, &Rigid_AM_LIMIT },
    { 0x001c8f50, &Rigid_M_LIMIT },
    { 0x001c8f54, &Rigid_SPEED_FORCE_LIMIT },
    { 0x001c8f5c, &Rigid_SPEED_SCALE },
    { 0x001c8f60, &Rigid_DEPTH_FORCE_SCALE },
    { 0x001c8f64, &Rigid_TOTAL_FORCE_LIMIT },
    { 0x001c8f68, &Rigid_ForceCapDivisor },
    { 0x001c8f6c, &Rigid_ForceCapFloor },
    { 0x001c8f70, &Rigid_BoxSpeedScale },
    { 0x001c8f74, &Rigid_BoxDepthScale },
    { 0x001c8f78, &Rigid_BoxForceCapScale },
    { 0x001c8f7c, &Rigid_BUILDING_MICRO_THRESHOLD },
    { 0x001c8f80, &Rigid_BUILDING_COEFF_RESTITUTION },
    { 0x001c8f84, &Rigid_BUILDING_FRICTION_FACTOR },
    { 0x001c8f88, &Rigid_BUILDING_FRICTION_LIMIT },
    { 0x001c8f8c, &Rigid_BUILDING_COLLIDE_COG_SCALE },
    { 0x001c8f90, &Rigid_BUILDING_FORCE_LIMIT },
    { 0x001c8f94, &Rigid_CornerLift },
    { 0x001c8f98, &Rigid_PF_SCALE },
    { 0x001c8f9c, &Rigid_WorldDamageFactor },
    { 0x001c8fa0, &Rigid_CAR_COLLIDE_COG_SCALE },
    { 0x001c8fa4, &Rigid_UnknownCogScale },
    { 0x001c8fa8, &Rigid_ObjectDamageFactor },
    { 0x001c8fac, &Rigid_DamageScale },
    { 0x001c8fb0, &Rigid_VehicleWorldDamageScale },
};

// The tuning and the restitution rates lent to the originals
inline void LendRigidTuning() {
    for (const RigidOrigCopy &copy : kRigidOrigCopies)
        *reinterpret_cast<float *>(copy.at) = *copy.ours;
}

// ... and the scratch pad and the underwater flag
inline void LendRigidGlobals() {
    LendRigidTuning();
    OrigRigidScratchPad = RigidScratchPad;
    OrigRigidUnderwater = RigidUnderwater;
}

#endif // DRIVING_DEVTOOLS_RIGIDORIGGLOBALS_H_
