#ifndef DRIVING_GAME_BONDCARPHYSICS_H_
#define DRIVING_GAME_BONDCARPHYSICS_H_

// ---------------------------------------------------------------------------------------------------------------
// PBondCar's driving (0x00066060-0x0006a840, BondCarPhysics.cpp): the controller input, the wheels' forces and the
// two ways a car's physics are stepped (the full one, and the simpler one the AI's classes use), the two-wheel
// stunt, tyre tracks, a reset, a change of model, visual damage, the scalar deleting destructor and the small
// accessors at the start of the range. The methods are declared in BondCar.h.
// ---------------------------------------------------------------------------------------------------------------

#include "BondCar.h"
#include "../data/CoordConvert.h"       // Coord3

struct RigidBody;

// The impact a hard landing raises (ProcessPhysics, ProcessSimplePhysics, ProcessSnowmobilePhysics: the same
// code inlined in each): an ECollision of the body at `position`, straight up, of `strength` (the name is ours)
void RaiseLandingImpact(const RigidBody *body, const Coord3 *position, float strength);

#endif // DRIVING_GAME_BONDCARPHYSICS_H_
