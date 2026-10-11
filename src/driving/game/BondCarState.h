#ifndef DRIVING_GAME_BONDCARSTATE_H_
#define DRIVING_GAME_BONDCARSTATE_H_

// ---------------------------------------------------------------------------------------------------------------
// PBondCar's set-up and state (0x00061a50-0x00062fe0, BondCarState.cpp): the cars' tuning globals, tyre tracks,
// controls and variables, damage, shock, ResetCar, the engine's RPM, the weapons (laser, EMP, rocket boost), the
// target beacon, glares and turn signals, and the two-wheel stunt. The methods are declared in BondCar.h.
// ---------------------------------------------------------------------------------------------------------------

#include <stdint.h>

#include "BondCar.h"
#include "../../helpers.h"
#include "VehicleSound.h"               // AVehicle
#include "../data/CoordConvert.h"       // Coord3

// The ground's kinds as the friction tables index them (each named as in its attribute; 11, 14 and 15 have none)
enum CarSurface {
    kNODRIVE,
    kPAVED,
    kGRAVEL,
    kGRASS,
    kCOBBLE,
    kDIRT,
    kWATER,
    kWOOD,
    kICE,
    kSNOW,
    kPAVED_ROUGH,
    kSurface11,
    kRAILROAD,
    kMETAL,
    kSurface14,
    kSurface15,
    kCarSurfaces
};

// The cars' tuning, loaded by PBondCar::InitializeBondCarGlobals from "Physics:Physical" and "Physics:Friction",
// each named as its attribute; the first is initialised data no code writes (the name is ours)
extern float BondCar_FarDistanceSq;             // class 3 cars beyond it simulate every other step
extern float BondCar_ENABLE_ROLL_STOPS_THRESHOLD;
extern float BondCar_BASE_FRICTION_MASS;
extern float BondCar_SHRED_DRAG;
extern float BondCar_PAD_DEAD_ZONE;
extern float BondCar_TWO_WHEEL_ANGLE;
extern float BondCar_TWO_WHEEL_SCALE;
extern float BondCar_TWO_WHEEL_LIMIT;
extern float BondCar_TWO_WHEEL_OPPOSITE_SCALE;
extern float BondCar_TWO_WHEEL_TENSOR_SCALE;
extern float BondCar_SKID_AUDIO_SCALE;
extern float BondCar_ROLLING_RESISTANCE;
extern float BondCar_MIN_BUTTON_VALUE;
extern float BondCar_WHEEL_SPIN_EXTRA_RPM;
extern float BondCar_DAMAGE_SCALE_COLLISION;
extern int32_t BondCar_EMP_LIFETIME;            // simulation steps
extern float BondCar_TYRE_DAMAGE_RADIUS;
extern float BondCar_MAX_WHEEL_SPIN_RATE;
extern float BondCar_MAX_WHEEL_SPIN_RATE_AI;
extern int32_t BondCar_POST_BRAKE_ACCEL_COUNT;
extern float BondCar_POST_BRAKE_ACCEL_SCALE;
extern float BondCar_POST_BRAKE_ACCEL_MIN;
extern float BondCar_friction[kCarSurfaces];
extern float BondCar_lateralLoss[kCarSurfaces];

// Beside them, two values no code writes (0x001c36c4, 0x001c36c8): the steps a body is off the ground before
// ImproveLanding turns it, and the steps of landingFlag after which ImproveLanding damps its spin
constexpr unsigned kLandingAirSteps = 50;
constexpr int kLandingDampSteps = 10;

#endif // DRIVING_GAME_BONDCARSTATE_H_
