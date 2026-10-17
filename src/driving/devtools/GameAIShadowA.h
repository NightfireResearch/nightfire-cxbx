#ifndef DRIVING_DEVTOOLS_GAMEAISHADOWA_H_
#define DRIVING_DEVTOOLS_GAMEAISHADOWA_H_

// NIGHTFIRE_GAMEAISHADOWA=1: AIGroundVehicle's part 1 (game/AIGroundVehicle.cpp: the constructor, Reset, the modes,
// the navigators, the lane weights, the attack speed, the turret, the auto-drive and agent updates, UnSpawn) against
// the originals, on synthetic cars round the live player car and road network, every call out recorded by fakes,
// once, from the first simulation tick. See GameAIShadowA.cpp.
void GameAIShadowA_Run(void);

#endif // DRIVING_DEVTOOLS_GAMEAISHADOWA_H_
