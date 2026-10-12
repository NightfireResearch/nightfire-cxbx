#ifndef DRIVING_GAME_SIMULATIONOBJECTS_H_
#define DRIVING_GAME_SIMULATIONOBJECTS_H_

// Simulation's smaller methods: the pause state, the body slots and signatures, the simple bodies' collision
// pass, the objects' lookups, the tracked instances and the deletion of dead objects (0x000b25e0-0x000b4080,
// SimulationObjects.cpp). The class is declared in Simulation.h, the containers in SimulationContainers.h.

#include "Simulation.h"

#include <stdint.h>

extern uint32_t NextSignature;          // the counter in the low 24 bits of the next signature (name ours)

#endif // DRIVING_GAME_SIMULATIONOBJECTS_H_
