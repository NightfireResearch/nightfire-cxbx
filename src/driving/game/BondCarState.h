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
#define BondCar_FarDistanceSq FLOAT_AT(0x001c36c0)          // 2500: class 3 cars beyond it simulate every other step
#define BondCar_ENABLE_ROLL_STOPS_THRESHOLD FLOAT_AT(0x001c36cc)
#define BondCar_BASE_FRICTION_MASS FLOAT_AT(0x001c36d0)
#define BondCar_SHRED_DRAG FLOAT_AT(0x001c36d4)
#define BondCar_PAD_DEAD_ZONE FLOAT_AT(0x001c36d8)
#define BondCar_TWO_WHEEL_ANGLE FLOAT_AT(0x001c36dc)
#define BondCar_TWO_WHEEL_SCALE FLOAT_AT(0x001c36e0)
#define BondCar_TWO_WHEEL_LIMIT FLOAT_AT(0x001c36e4)
#define BondCar_TWO_WHEEL_OPPOSITE_SCALE FLOAT_AT(0x001c36e8)
#define BondCar_TWO_WHEEL_TENSOR_SCALE FLOAT_AT(0x001c36ec)
#define BondCar_SKID_AUDIO_SCALE FLOAT_AT(0x001c36f0)
#define BondCar_ROLLING_RESISTANCE FLOAT_AT(0x001c36f4)
#define BondCar_MIN_BUTTON_VALUE FLOAT_AT(0x001c36f8)
#define BondCar_WHEEL_SPIN_EXTRA_RPM FLOAT_AT(0x001c36fc)
#define BondCar_DAMAGE_SCALE_COLLISION FLOAT_AT(0x001c3704)
#define BondCar_EMP_LIFETIME I32_AT(0x001c3708)                 // simulation steps
#define BondCar_TYRE_DAMAGE_RADIUS FLOAT_AT(0x001c370c)
#define BondCar_MAX_WHEEL_SPIN_RATE FLOAT_AT(0x001c3710)
#define BondCar_MAX_WHEEL_SPIN_RATE_AI FLOAT_AT(0x001c3714)
#define BondCar_POST_BRAKE_ACCEL_COUNT I32_AT(0x001c3718)
#define BondCar_POST_BRAKE_ACCEL_SCALE FLOAT_AT(0x001c371c)
#define BondCar_POST_BRAKE_ACCEL_MIN FLOAT_AT(0x001c3720)
#define BondCar_friction ((float *)0x001c3728)                  // [kCarSurfaces]
#define BondCar_lateralLoss ((float *)0x001c3768)               // [kCarSurfaces]

// Beside them, two values no code writes (0x001c36c4, 0x001c36c8): the steps a body is off the ground before
// ImproveLanding turns it, and the steps of landingFlag after which ImproveLanding damps its spin
constexpr unsigned kLandingAirSteps = 50;
constexpr int kLandingDampSteps = 10;

#endif // DRIVING_GAME_BONDCARSTATE_H_
