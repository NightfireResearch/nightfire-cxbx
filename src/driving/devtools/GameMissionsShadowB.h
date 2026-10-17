#ifndef DRIVING_DEVTOOLS_GAMEMISSIONSSHADOWB_H_
#define DRIVING_DEVTOOLS_GAMEMISSIONSSHADOWB_H_

// NIGHTFIRE_GAMEMISSIONSSHADOWB=1: Simulation part 2 (game/SimulationSpawn.cpp) against the originals, on deep
// copies of the live Simulation and synthetic containers, the callees outside the package recorded by fakes, once,
// from the first simulation tick. See GameMissionsShadowB.cpp.
void GameMissionsShadowB_Run(void);

#endif // DRIVING_DEVTOOLS_GAMEMISSIONSSHADOWB_H_
