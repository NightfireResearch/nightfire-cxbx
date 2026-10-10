#ifndef DRIVING_GAME_MISSILE_H_
#define DRIVING_GAME_MISSILE_H_

// ---------------------------------------------------------------------------------------------------------------
// Missile (not ported): what the vehicles' ApplyDamage reads of the Simulation's missiles when it counts a kill's
// hits. Provisional, until the weapons are ported.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "../../common/xbeClass.h"      // offsetof on a derived class without clang's warning
#include "../physics/PhysicsObject.h"

struct WTargetable;

class Missile : public PhysicsObject {
public:
    WTargetable *target;            // +0x6c
    uint8_t unknown70[0x69a];
    uint8_t unknown70A;             // +0x70a set: its hit does not count
};
static_assert(offsetof(Missile, target) == 0x6c && offsetof(Missile, unknown70A) == 0x70a, "Missile layout");

// The Simulation's missiles (a vector's first and last)
#define SimulationMissilesFirst (*(Missile ***)0x00234e80)
#define SimulationMissilesLast (*(Missile ***)0x00234e84)

#endif // DRIVING_GAME_MISSILE_H_
