#ifndef NDRONE2_ROUTE_H_
#define NDRONE2_ROUTE_H_

#include "NDrone2.h"

// The drones' routes over the AI paths (NDrone2_Route.cpp): the patrol or mission path a drone walks (route2), the
// route it plans to a position (route1), and the AI points it goes to.

struct DroneAIPoint_tag;

DSTATE NDrone2_ObstructedPatrolPath(DCVars_tag *dcv);

// The patrol and mission paths
bool NDrone2_InitAIPath(DCVars_tag *dcv, AIPath_tag *path, AINode_tag *start);
bool NDrone2_StartRouteCreep(int unused, AIRoute_tag *route, DCVars_tag *dcv, char fromEnd);
bool NDrone2_AssignAIPath(DCVars_tag *dcv, uint pathType);
uint NDrone2_UpdatePatrolRoute(DCVars_tag *dcv);
uint NDrone2_UpdateMissionRoute(DCVars_tag *dcv);

// Following a route, and going to a position
uint NDrone2_FollowRoute(AIRoute_tag *route, DCVars_tag *dcv);
uint NDrone2_MoveToGoalPosition(DCVars_tag *dcv, CelPos_tag *dest, float radius, ushort clearFlags);

// AI points
struct DroneAIPoint_tag* NDrone2_ClosestAIPoint(DCVars_tag *dcv, ushort id, _VECTOR *pos, cel_tag *cel);
bool NDrone2_GoToPoint(DCVars_tag *dcv, CelPos_tag *pos, float radius);
bool NDrone2_FindRunToPoint(DCVars_tag *dcv);
bool NDrone2_FindAlarmPoint(DCVars_tag *dcv);

#endif // NDRONE2_ROUTE_H_
