#ifndef DRIVING_GAME_AIVEHICLE_H_
#define DRIVING_GAME_AIVEHICLE_H_

// ---------------------------------------------------------------------------------------------------------------
// AIGroundVehicle (not ported): a car's AI, what the vehicle code uses of it under Ghidra's names. Provisional,
// until the AI is ported.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

class AISplinePath;
class WRoadNav;

class AIGroundVehicle {
public:
    uint8_t unknown00[0x64];
    WRoadNav *driveToNav;           // +0x64
    uint8_t active;                 // +0x68
    uint8_t unknown69[0x17];
    int32_t type;                   // +0x80 kAIDrivingOnRoad: PBondCar::ResetCar puts the car back on its road
    uint8_t unknown84[4];
    int32_t unknown88;              // +0x88 1: PBondCar::Simulate turns the engine sound off
};
static_assert(offsetof(AIGroundVehicle, driveToNav) == 0x64 && offsetof(AIGroundVehicle, type) == 0x80 &&
              offsetof(AIGroundVehicle, unknown88) == 0x88, "AIGroundVehicle layout");

constexpr int32_t kAIDrivingOnRoad = 2;

// AIVehicle::GetSplinePath: the spline path the AI carries the car along, NULL if none
#define AIVehicle_GetSplinePath ((AISplinePath *(__fastcall *)(AIGroundVehicle *, int))0x000359d0)

#endif // DRIVING_GAME_AIVEHICLE_H_
