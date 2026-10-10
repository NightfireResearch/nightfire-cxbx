#ifndef DRIVING_DEVTOOLS_VEHICLESHADOW_H_
#define DRIVING_DEVTOOLS_VEHICLESHADOW_H_

// NIGHTFIRE_VEHICLESHADOW=1: PVehicle, its car name map, PhysicsData's constructor and PHelicopter (game/Vehicle.cpp,
// game/Helicopter.cpp) against the originals. See VehicleShadow.cpp.
//
// VehicleShadow_Run: from the first simulation tick (the cars and their attributes exist then): everything, on
// copies of the player's car's render object and any live helicopters.
// VehicleShadow_Tick: from every tick after; runs the helicopter tests once more, the first tick a live
// helicopter exists (they are spawned during play), then does nothing.
void VehicleShadow_Run(void);
void VehicleShadow_Tick(void);

#endif // DRIVING_DEVTOOLS_VEHICLESHADOW_H_
