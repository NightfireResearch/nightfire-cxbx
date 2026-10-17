#ifndef DRIVING_DEVTOOLS_GAMEAISHADOWB_H_
#define DRIVING_DEVTOOLS_GAMEAISHADOWB_H_

// NIGHTFIRE_GAMEAISHADOWB=1: AIGroundVehicle's part 2 (game/AIGroundVehicleDriving.cpp: the driving, the collision
// checks, traffic spawning, the weapons, DoMoveMode and Update) against the originals, on synthetic cars round the
// live player car and road network, every call out recorded by fakes, once, from the first simulation tick. See
// GameAIShadowB.cpp.
void GameAIShadowB_Run(void);

#endif // DRIVING_DEVTOOLS_GAMEAISHADOWB_H_
