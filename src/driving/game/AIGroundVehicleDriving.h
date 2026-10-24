#ifndef DRIVING_GAME_AIGROUNDVEHICLEDRIVING_H_
#define DRIVING_GAME_AIGROUNDVEHICLEDRIVING_H_

// ---------------------------------------------------------------------------------------------------------------
// AIGroundVehicle, part 2 (AIGroundVehicleDriving.cpp): the driving, the collision checks, traffic spawning, the
// weapons and Update. The methods are declared in AIGroundVehicle.h; here are the free functions among them.
// ---------------------------------------------------------------------------------------------------------------

#include "AIGroundVehicle.h"
#include "../world/RoadNav.h"

struct PhysicsObject;

// The point pulled back towards the object to stop short of the obstacle: along the line to the point, the obstacle's
// distance along it less `obstacleReach` and 1.5 of the object's half length, at least 0
void GetCollisionStopPoint(PhysicsObject *object, Coord3 *point, const Coord3 *obstacle, float obstacleReach);
                                                                                                // 0x0002e580
// `to` is within reach of a car at `from` going `speed` along `heading`, and not behind it: twice the speed, kept
// between 20 and 35 (15 when the other's speed is more than ours by 1); unless anyHeight, within 5 in height
bool InCollisionRange(const Coord3 *from, const Coord3 *to, const Coord3 *heading, float speed, float otherSpeed,
                      bool anyHeight);                                                          // 0x0002e6b0
// No agent car (but the one whose body is in `slot`) lies within `range` of `from` in line with the heading
bool ClearShot(int slot, const Coord3 *heading, const Coord3 *from, float range);              // 0x0002e810


#endif // DRIVING_GAME_AIGROUNDVEHICLEDRIVING_H_
