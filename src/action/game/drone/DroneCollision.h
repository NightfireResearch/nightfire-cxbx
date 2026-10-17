#ifndef DRONE_COLLISION_H
#define DRONE_COLLISION_H

#include "NDrone2.h"

// A drone's body against the world: NDrone2_Collision sets up the capsule the collision update tests, and
// Drone_CollisionHandler (the drone's collide function) takes what it hit, finds the floor (Drone_FeetOnPoint) and
// moves the drone by its velocity, with gravity (NDrone2_Fall).

bool DroneFunc_NearPlayerCollision(Drone_tag *drone);
bool NDrone2_DoCollision(DCVars_tag *dcv);
bool NDrone2_DoGravity(DCVars_tag *dcv);
void NDrone2_Fall(obj_tag *obj);
void NDrone2_Collision(obj_tag *obj);
obj_tag* Drone_FeetOnPoint(obj_tag *obj, HITTEST_tag *test, float *slope, int unused, float *height);
void Drone_CollisionHandler(obj_tag *obj);

#endif // DRONE_COLLISION_H
