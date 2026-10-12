#ifndef DRIVING_DEVTOOLS_GAMEAISHADOWD_H_
#define DRIVING_DEVTOOLS_GAMEAISHADOWD_H_

// NIGHTFIRE_GAMEAISHADOWD=1: the AI vehicles' base, the helicopters, the vehicle controller, the road spawns and the
// spline paths (game/AIVehicle.cpp, AIHelicopter.cpp, AIVehicleController.cpp, AIRoadSpawn.cpp, AISplinePath.cpp)
// against the originals, on synthetic vehicles round the live player car and road network, every call out recorded
// by fakes, once, from the first simulation tick. See GameAIShadowD.cpp.
void GameAIShadowD_Run(void);

#endif // DRIVING_DEVTOOLS_GAMEAISHADOWD_H_
