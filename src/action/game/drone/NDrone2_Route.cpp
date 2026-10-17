// The drones' routes over the AI paths (engine/AINetwork.cpp): the patrol or mission path a drone walks (route2, set up
// by NDrone2_AssignAIPath and NDrone2_InitAIPath), following a route (NDrone2_FollowRoute steers moveTarget at its
// waypoint), going to a position over route1 (NDrone2_MoveToGoalPosition), and choosing the AI points to go to.

#include "NDrone2_Route.h"
#include "Behaviour.h"
#include "DroneAnim.h"
#include "DroneMove.h"              // NDrone2_NearestNodeDCV, NDrone2_CanReachGoalPosition
#include "DroneTables.h"            // DRONE_PROP
#include "../../game.h"             // GameState
#include "../../engine/viewer.h"     // cel_tag
#include "../../math/math.h"
#include "../obj/Drone_AIHints.h"   // DroneAIPoint_tag

#include <bit>
#include <stdint.h>

#pragma fp_contract(off)

// Originals called by address
// Returns in ST0, unrounded
// Steers moveTarget at a position (finding its cel), and works out the distance and the angle to it
#define NDrone2_maybeCalcDistanceAndDirToTarget ((void (__cdecl *)(DCVars_tag *dcv, CelPos_tag *target))0x00046080)

// Path flag 0x10 (AIPath_tag.flags): a patrol from its first node takes the first link, not the second
#define AIPATH_FIRST_LINK 0x10

// The anim states NDrone2_UpdatePatrolRoute plays for an alert patroller (DRONE_PROP_25): walking on, and otherwise
#define DANIM_21 0x21
#define DANIM_35 0x35

// AI point ids (DroneAIPoint_tag.id) the drones look for
#define AIPOINT_ENEMY 0             // side 1
#define AIPOINT_RUN 1               // DRONE_PROP_4E, and a civilian with no point of its own
#define AIPOINT_CIVILIAN 2          // side 3
#define AIPOINT_ALARM 8             // DRONE_PROP_56; the one nearest a mission path's end
#define AIPOINT_ALLY 9              // side 2

constexpr float kNodeReach = 0.3f;          // NDrone2_AssignAIPath: how near in plan the path's node must be
constexpr float kPastEnd = 0.5f;            // how far a mission route goes on past its last node, and its arrive radius
constexpr float kAlert = 0.66f;             // an alertness at which a patroller with DRONE_PROP_25 is alert
constexpr float kFarthestPoint = 255.0f;    // an emitter's byte for a node it did not reach
static_assert(std::bit_cast<uint32_t>(kNodeReach) == 0x3e99999a, "not the original's constant");
static_assert(std::bit_cast<uint32_t>(kAlert) == 0x3f28f5c3, "not the original's constant");

// The state a patrol in DSTATE_Patrol goes to when its path is obstructed (message 0x1b)
// AUTOINJECT
DSTATE NDrone2_ObstructedPatrolPath(DCVars_tag *dcv) {
    return DSTATE_Obstructed;
}

// ---- the patrol and mission paths

// Builds route2 along a path from the node start, taking at each node with two links the one that does not lead back.
// A patrol path loops; a mission path stops at a node with one link (other than the first). A patrol node with another
// number of links is put in again, until the route has as many entries as the path has nodes.
// AUTOINJECT
bool NDrone2_InitAIPath(DCVars_tag *dcv, AIPath_tag *path, AINode_tag *start) {
    Drone_tag *drone = dcv->drone;
    obj_tag *obj = dcv->gameObj;
    AIRoute_tag *route = &drone->route2;
    if (path->flags & AIPATH_PATROL)
        route->flags = (route->flags & ~AIROUTE_MODE) | AIROUTE_LOOP;
    else
        route->flags = (route->flags & ~AIROUTE_MODE) | AIROUTE_ONCE;
    if (path->flags & AIPATH_FIRST_LINK)
        route->flags |= AIROUTE_PATROL_MOD;
    route->path = path;
    route->count = 0;

    ushort prev = 0xffff;
    AINode_tag *node = start;
    for (uint i = 0; i < path->nodeCount; i++) {
        route->nodes[i] = node->fileNode->index;
        route->count++;
        if (node->linkCount != 2 && !((path->flags & AIPATH_MISSION) && node->linkCount == 1 && route->count == 1)) {
            if (path->flags & AIPATH_MISSION)
                break;
            NF_WARN("Patrol Path '%s' is NOT looping", (char *)path->record);   // GC check (0x800a0870)
            continue;
        }
        ushort *links = &path->nodeLinks[node->firstLink];
        AILink_tag *link = &path->links[links[0]];
        if (link->a == prev || link->b == prev
            || (node->linkCount != 1 && (path->flags & AIPATH_PATROL) && prev == 0xffff
                && !(path->flags & AIPATH_FIRST_LINK)))
            link = &path->links[links[1]];
        prev = node->fileNode->index;
        node = &path->runtimeNodes[link->a != prev ? link->a : link->b];
    }

    route->current = 0;
    drone->activeRoute = route;
    NDrone2_FeetPos(obj, &route->start.pos);
    route->start.cel = obj->inCel;
    if (route->count > 1) {
        // Half a metre on past the last node
        MAPNODE_tag *last = path->runtimeNodes[route->nodes[route->count - 1]].fileNode;
        MAPNODE_tag *before = path->runtimeNodes[route->nodes[route->count - 2]].fileNode;
        _VECTOR beyond;
        Vec_Subtract(&last->pos, &before->pos, &beyond);
        Vec_Normalise(&beyond, &beyond);
        Vec_MulR32(&beyond, &beyond, kPastEnd);
        route->dest.cel = last->cel;
        Vec_Add2(&last->pos, &beyond, &route->dest.pos);
        route->arriveRadius = kPastEnd;
    } else {
        Vec_Copy(&start->fileNode->pos, &route->dest.pos);
        route->dest.cel = start->fileNode->cel;
    }
    route->targetFrame = GameState.NumFramesUnpaused;
    AINetwork_ValidateRoute(route);
    return true;
}

// Sets up the creep along the route's current link and, when there is one, steers at its first step. The first
// argument is not read. Our name.
// FUNC_AT(0x00046220)
bool NDrone2_StartRouteCreep(int unused, AIRoute_tag *route, DCVars_tag *dcv, char fromEnd) {
    if (!LinkCreep_Calc(route, fromEnd))
        return false;
    LinkCreep_Dest(route, 0, &route->waypoint);
    NDrone2_maybeCalcDistanceAndDirToTarget(dcv, &route->waypoint);
    return true;
}

// Puts the drone on the path of the type asked for (AIPATH_PATROL or AIPATH_MISSION) whose node in its cel is nearest
// it in plan, within 0.3 m. On a mission path it keeps the alarm point nearest the path's last node.
// AUTOINJECT
bool NDrone2_AssignAIPath(DCVars_tag *dcv, uint pathType) {
    obj_tag *obj = dcv->gameObj;
    dcv->drone->route2.count = 0;
    if (obj->inCel == NULL)
        return false;
    AINode_tag *nearest = NULL;
    short pathIndex = -1;
    float best = kNodeReach;
    for (AINode_tag *node = obj->inCel->aiNodes; node != NULL; node = node->nextInCel) {
        if (AIPaths[node->fileNode->path].flags & pathType) {
            double distance = Vec_Dist2D(&obj->position, &node->fileNode->pos);
            if (distance < best) {
                best = (float)distance;
                pathIndex = node->fileNode->path;
                nearest = node;
            }
        }
    }
    if (pathIndex < 0)
        return false;
    AIPath_tag *path = &AIPaths[pathIndex];
    if (!NDrone2_InitAIPath(dcv, path, nearest))
        return false;
    if ((path->flags & AIPATH_MISSION) && path->nodeCount > 1) {
        dcv->drone->route2.current = 0;
        Drone_tag *drone = dcv->drone;
        AIRoute_tag *route = &drone->route2;
        MAPNODE_tag *last = route->path->runtimeNodes[route->nodes[route->count - 1]].fileNode;
        drone->missionPoint = NDrone2_ClosestAIPoint(dcv, AIPOINT_ALARM, &last->pos, last->cel);
    }
    NDrone2_StartRouteCreep(0, &dcv->drone->route2, dcv, 0);
    return true;
}

// Follows route2 and picks the anim: standing for status 7, else walking, or with DRONE_PROP_25 when alert, anim 0x35
// on a full route and 0x21 otherwise. Returns 0 on a full route, 7, or 1 for anything else.
// AUTOINJECT
uint NDrone2_UpdatePatrolRoute(DCVars_tag *dcv) {
    uint status = NDrone2_FollowRoute(&dcv->drone->route2, dcv);
    if (status == AIROUTE_NO_PATH) {
        DroneAnim_SetStandIdleAnim(dcv, 1);
        return status;
    }
    if (behaviour_util_getProperty(DRONE_PROP_25, dcv->drone->currentBehaviour) && dcv->drone->alertness >= kAlert)
        DroneAnim_CallAnim(status == AIROUTE_FOLLOWING ? DANIM_35 : DANIM_21, 0, 0, 0, 0.0f, 0, dcv);
    else
        DroneAnim_SetWalkIdleAnim(dcv, 1);
    return status == AIROUTE_FOLLOWING ? AIROUTE_FOLLOWING : 1;
}

// AUTOINJECT
uint NDrone2_UpdateMissionRoute(DCVars_tag *dcv) {
    return NDrone2_FollowRoute(&dcv->drone->route2, dcv);
}

// ---- following a route

// A step along the route (AINetwork_FollowRoute), with moveTarget steered at its waypoint. A full route with no nodes
// counts as walking straight there.
// AUTOINJECT
uint NDrone2_FollowRoute(AIRoute_tag *route, DCVars_tag *dcv) {
    dcv->drone->activeRoute = route;
    uint status = AINetwork_FollowRoute(route, dcv);
    if (status == AIROUTE_FOLLOWING && route->count == 0)
        status = AIROUTE_DIRECT;
    NDrone2_maybeCalcDistanceAndDirToTarget(dcv, &route->waypoint);
    return status;
}

// Goes to a position over route1, planning it first when it is not valid (and keeping where it started). Returns the
// plan's failure, AIROUTE_ARRIVED within the route's arrive radius, AIROUTE_DOOR when the drone is to open a door,
// else what the step came to: AIROUTE_ARRIVED, AIROUTE_CREEP_FAILED2 or AIROUTE_FOLLOWING.
// AUTOINJECT
uint NDrone2_MoveToGoalPosition(DCVars_tag *dcv, CelPos_tag *dest, float radius, ushort clearFlags) {
    Drone_tag *drone = dcv->drone;
    ushort flags = (drone->unknown824 | drone->unknown825 << 8) & ~clearFlags;
    AIRoute_tag *route = &drone->route1;
    drone->activeRoute = route;
    if (!AINetwork_RouteIsValid(route)) {
        uint status = AINetwork_CalcRouteToPosition(dcv->gameObj, dest, route, &dcv->drone->goalTarget,
                                                    &dcv->drone->aiPoint, radius, flags);
        if (status > AIROUTE_DIRECT)
            return status;
        Vec_Copy(&route->start.pos, &dcv->drone->goalFrom.pos);
        dcv->drone->goalFrom.cel = route->start.cel;
    }
    double remaining = AINetwork_GetRouteDistance(route);
    route->remaining = (float)remaining;
    if (remaining < route->arriveRadius)
        return AIROUTE_ARRIVED;
    uint status = NDrone2_FollowRoute(route, dcv);
    if (status != AIROUTE_ARRIVED && status != AIROUTE_CREEP_FAILED2)
        status = AIROUTE_FOLLOWING;
    if (dcv->aiStateMachine->nextState == DSTATE_OpenDoor)
        status = AIROUTE_DOOR;
    return status;
}

// ---- AI points

// The active AI point with the id asked for nearest by the drone's path (by its emitter's byte at the node nearest the
// drone, or nearest pos when given), within 255. A point of the drone's own group beats any other. NULL when there is
// none, or no node near.
// AUTOINJECT
DroneAIPoint_tag* NDrone2_ClosestAIPoint(DCVars_tag *dcv, ushort id, _VECTOR *pos, cel_tag *cel) {
    Drone_tag *drone = dcv->drone;
    obj_tag *obj = dcv->gameObj;
    uint group = drone->group;
    float distance = 0.0f;
    float best = kFarthestPoint;
    uint bestGroup = 0;
    CelPos_tag from;
    ushort node;
    if (pos == NULL) {
        node = NDrone2_NearestNodeDCV(dcv);
        Vec_Copy(&obj->position, &from.pos);
        from.cel = obj->inCel;
    } else {
        from.pos = *pos;
        from.cel = cel;
        node = NDrone2_NearestNode(&from, drone->route1.path, obj);
    }
    if (node == 0xffff)
        return NULL;

    DroneAIPoint_tag *closest = NULL;
    for (uint i = 0; i < NPCGlobals.numAIPoints; i++) {
        DroneAIPoint_tag *point = &NPCGlobals.aiPoints[i];
        if (!point->active || point->id != id)
            continue;
        if (!NDrone2_DistanceToEmitter(&from, drone->route1.path, &node, &point->emitter, obj, &distance))
            continue;
        // The comparisons let a NaN through, as the original's do
        if (distance >= kFarthestPoint)
            continue;
        if (bestGroup != 0) {
            if (point->group != group || distance >= best)
                continue;
            bestGroup = point->group;
        } else if (point->group != 0 && point->group == group) {
            bestGroup = point->group;
        } else {
            if (distance >= best)
                continue;
            bestGroup = 0;
        }
        best = distance;
        closest = point;
    }
    return closest;
}

// Plans route1 afresh to a position with the arrive radius given (the goal in goalTarget and aiPoint), and answers
// whether it can be walked (NDrone2_CanReachGoalPosition). Our name.
// FUNC_AT(0x000470b0)
bool NDrone2_GoToPoint(DCVars_tag *dcv, CelPos_tag *pos, float radius) {
    AINetwork_InvalidateRoute(&dcv->drone->route1);
    AINetwork_SetupGoalPosition(&dcv->drone->goalTarget, &dcv->drone->aiPoint, pos, radius);
    return NDrone2_CanReachGoalPosition(dcv);
}

// Where to run to when alerted: the alarm point with DRONE_PROP_56, the run point with DRONE_PROP_4E, else the side's
// own (a civilian without one takes a run point). True when there is one and it can be reached.
// AUTOINJECT
bool NDrone2_FindRunToPoint(DCVars_tag *dcv) {
    ushort id;
    if (behaviour_util_getProperty(DRONE_PROP_56, dcv->drone->currentBehaviour)) {
        id = AIPOINT_ALARM;
    } else if (behaviour_util_getProperty(DRONE_PROP_4E, dcv->drone->currentBehaviour)) {
        id = AIPOINT_RUN;
    } else {
        switch (dcv->drone->side) {
        case 1:
            id = AIPOINT_ENEMY;
            break;
        case 2:
            id = AIPOINT_ALLY;
            break;
        case 3:
            id = AIPOINT_CIVILIAN;
            break;
        default:
            return false;
        }
    }
    // A point's position and cel are a CelPos_tag
    DroneAIPoint_tag *point = NDrone2_ClosestAIPoint(dcv, id, NULL, NULL);
    if (point != NULL)
        return NDrone2_GoToPoint(dcv, (CelPos_tag *)&point->pos, point->radius);
    if (id == AIPOINT_CIVILIAN) {
        point = NDrone2_ClosestAIPoint(dcv, AIPOINT_RUN, NULL, NULL);
        if (point != NULL)
            return NDrone2_GoToPoint(dcv, (CelPos_tag *)&point->pos, point->radius);
    }
    return false;
}

// The alarm point to run to (DSTATE_RunToAlarm): the mission path's, which counts as found whether or not it can be
// reached, else the nearest, whose rotation goes to aiPointRot. True when it is found and can be reached.
// AUTOINJECT
bool NDrone2_FindAlarmPoint(DCVars_tag *dcv) {
    DroneAIPoint_tag *point = dcv->drone->missionPoint;
    if (point != NULL && point->id == AIPOINT_ALARM) {
        NDrone2_GoToPoint(dcv, (CelPos_tag *)&point->pos, point->radius);
        return true;
    }
    point = NDrone2_ClosestAIPoint(dcv, AIPOINT_ALARM, NULL, NULL);
    if (point == NULL)
        return false;
    dcv->drone->aiPointRot.y = point->rot.y;
    return NDrone2_GoToPoint(dcv, (CelPos_tag *)&point->pos, point->radius);
}
