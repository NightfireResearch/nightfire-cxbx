#ifndef DRIVING_DEVTOOLS_GAMEMISSIONSSHADOWA_H_
#define DRIVING_DEVTOOLS_GAMEMISSIONSSHADOWA_H_

// NIGHTFIRE_GAMEMISSIONSSHADOWA=1: the Simulation's smaller methods and its containers' code
// (game/SimulationObjects.cpp, SimulationContainers.cpp) against the originals, on a copy of the live Simulation
// and on synthetic bodies, objects, trees and vectors. Call at the first simulation tick; returns at once unless
// the variable is set.
void GameMissionsShadowA_Run(void);

#endif // DRIVING_DEVTOOLS_GAMEMISSIONSSHADOWA_H_
