// How the drones move. The anims move the drone; NDrone2_Move only turns its heading (moveRotation) towards
// targetRot.y, at turnRate a frame, through NDrone2_Steer, which also turns it away from each drone within 3 m and
// from the players when NDrone2_EvaluateObstacle finds that walking on would bring them into contact (the sooner,
// the harder). A shot's push (pushOffset) is added to the position and dies away. In zero gravity (flies) the drone
// floats towards moveTarget instead. Also which ways are blocked, the nearest drone's side, the turn to an object,
// and running from a grenade or the like. docs/drone/architecture/README.md has the fields.

#include "DroneMove.h"
#include "NDrone2.h"
#include "BOT.h"                   // BOT_vars_t
#include "NDrone2_Route.h"        // NDrone2_MoveToGoalPosition
#include "DroneAnim.h"             // DroneAnim_CallAnim
#include "../../game.h"            // GameState, MPGame
#include "../mp/multiplayer.h"     // MPSettings
#include "../obj/build.h"          // build_FindCel
#include "../obj/Drone_AIHints.h"  // Drone_PointInAnyAIBox
#include "../../engine/AINetwork.h"
#include "../../engine/Collide.h"  // HITDATA_tag
#include "../../math/math.h"
#include "../../math/X87Ops.h"
#include "../../util/Random.h"

#include <bit>
#include <math.h>
#include <stdint.h>

#pragma fp_contract(off)

// Originals called by address
// Returns in ST0, unrounded

constexpr float kDroneRadius = 0.404f;          // what NDrone2_Steer gives a drone
constexpr float kPlayerRadius = 0.47500002f;    // and a player
constexpr float kHorizon = 7030.0f;             // NDrone2_EvaluateObstacle: no turn for a contact this far off
constexpr float kInvPi = 0.31830987f;
static_assert(std::bit_cast<uint32_t>(kDroneRadius) == 0x3eced917, "not the original's constant");
static_assert(std::bit_cast<uint32_t>(kPlayerRadius) == 0x3ef33334, "not the original's constant");
static_assert(std::bit_cast<uint32_t>(kHorizon) == 0x45dbb000, "not the original's constant");
static_assert(std::bit_cast<uint32_t>(kInvPi) == 0x3ea2f983, "not the original's constant");
static_assert(std::bit_cast<uint32_t>(0.005f) == 0x3ba3d70a, "not the original's constant");
static_assert(std::bit_cast<uint32_t>(0.9998f) == 0x3f7ff2e5, "not the original's constant");
static_assert(std::bit_cast<uint32_t>(0.95f) == 0x3f733333, "not the original's constant");
static_assert(std::bit_cast<uint32_t>(0.1f) == 0x3dcccccd, "not the original's constant");
static_assert(std::bit_cast<uint32_t>(1.8f) == 0x3fe66666, "not the original's constant");

// The game's ABS: it works its argument out twice
#define ABS(x) ((x) < 0.0f ? -(x) : (x))

// The size of an angle as the original's FCHS on "below or unordered" leaves it: a NaN's sign is flipped too
static double AngleSize(double angle) {
    return !(angle >= 0.0) ? std::bit_cast<double>(std::bit_cast<uint64_t>(angle) ^ 0x8000000000000000ull) : angle;
}

// ---- walking and steering

// Each frame: turns the heading towards targetRot.y (unless unknown3f0 has 0x8000000), or in zero gravity
// speeds up towards moveTarget and turns the object a tenth of the way a frame. Last frame's push comes off the
// position, this frame's goes on, and it dies away by 5%.
// AUTOINJECT
void NDrone2_Move(DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;
    float heading = drone->targetRot.y;
    obj_tag *obj = dcv->gameObj;
    drone->avoidingPlayer = 0;
    if (!drone->flies) {
        if (!(dcv->drone->unknown3f0 & 0x8000000))
            NDrone2_Steer(dcv, Vec_AngleDifference(drone->moveRotation.y, heading));
    } else if (drone->floatsToMoveTarget) {
        _VECTOR dir;
        Vec_Subtract(&drone->moveTarget, &obj->position, &dir);
        Vec_Normalise(&dir, &dir);
        auxVec_AddMulR32(&drone->velocity, &dir, REC_FRAME_RATE * drone->floatAcceleration, &drone->velocity);
        double turn = Vec_AngleDifference(obj->rotation.y, heading);
        obj->rotation.y = (float)(0.1f / (double)FRAME_RATE_DIV * turn + obj->rotation.y);
    }
    Vec_Subtract(&obj->position, &drone->prevPushOffset, &obj->position);
    Vec_Add2(&obj->position, &drone->pushOffset, &obj->position);
    Vec_Copy(&drone->pushOffset, &drone->prevPushOffset);
    Vec_MulR32(&drone->pushOffset, &drone->pushOffset, 0.95f);
}

// Another drone to steer round: enabled, neither dead nor dying, alive (a NaN health counts), not a
// dead drone, not marked for deletion
static bool IsObstacle(Drone_tag *drone, Drone_tag *other) {
    if (other == drone)
        return false;
    obj_tag *obj = other->gameObj;
    if (obj == NULL)
        return false;
    Drone_tag *data = (Drone_tag *)obj->extraObjectData;
    if ((data->flags & (DRONE_FLAG_DEAD | DRONE_FLAG_DYING)) || !(data->flags & DRONE_FLAG_ENABLED))
        return false;
    if (data->health <= 0.0f)
        return false;
    if (obj->objectType == OBJECTTYPE_DEAD_DRONE || (obj->flags & 1))
        return false;
    return true;
}

// A player as the obstacle: standing, and not a drone
static bool SteerRoundPlayer(Drone_tag *drone, ObstacleEval_tag *eval, obj_tag *player) {
    eval->other->pos = &player->position;
    eval->other->rot = &player->rotation;
    eval->self.flags = 0;
    eval->other->speed = 0.0f;
    eval->other->radius = kPlayerRadius;
    return NDrone2_EvaluateObstacle(drone, NULL, eval);
}

// Turns the heading by turnRate of angle, and finds the nearest drone within 3 m (nearestDrone). Unless the drone is
// of skin class 0xb or its anim does not move it, the turn is the average of twice the angle and the turn each
// obstacle in the way asks for.
// AUTOINJECT
void NDrone2_Steer(DCVars_tag *dcv, float angle) {
    obj_tag *obj = dcv->gameObj;
    Drone_tag *drone = dcv->drone;
    if (drone->skinClass == SKINCLASS_NINJA || drone->animMoveSpeed == 0.0f) {
        drone->nearestDroneDist = 3.0f;
        drone->nearestDrone = NULL;
        drone->unknown28c = 0;
        drone->moveRotation.y = (float)((double)angle * drone->turnRate + drone->moveRotation.y);
        for (Drone_tag *other = NPCGlobals.NDrone2List; other != NULL; other = other->next) {
            if (!IsObstacle(drone, other))
                continue;
            double dist = Vec_Dist3D(&obj->position, &other->gameObj->position);
            if (dist < drone->nearestDroneDist) {
                drone->nearestDroneDist = (float)dist;
                drone->nearestDrone = other;
            }
        }
        return;
    }

    ObstacleEval_tag eval;
    SteerBody obstacle;
    obstacle.flags = 0;     // uninitialised stack in the original
    eval.self.pos = &obj->position;
    eval.self.rot = &drone->moveRotation;
    eval.self.speed = drone->moved;
    eval.self.radius = kDroneRadius;
    eval.self.vel.x = (float)(FSin((double)drone->moveRotation.y) * eval.self.speed);
    eval.self.vel.y = 0.0f;
    eval.self.vel.z = (float)(FCos((double)drone->moveRotation.y) * eval.self.speed);
    eval.other = &obstacle;
    float sum = 0.0f;
    int count = 1;
    drone->nearestDroneDist = 3.0f;
    drone->nearestDrone = NULL;
    drone->unknown28c = 0;

    for (Drone_tag *other = NPCGlobals.NDrone2List; other != NULL; other = other->next) {
        if (!IsObstacle(drone, other))
            continue;
        eval.self.flags = drone->sm.id > other->sm.id;
        obstacle.pos = &other->gameObj->position;
        obstacle.rot = &other->moveRotation;
        obstacle.speed = other->moved;
        obstacle.radius = kDroneRadius;
        if (NDrone2_EvaluateObstacle(drone, other, &eval)) {
            sum = eval.steer + sum;
            count++;
        }
    }

    if (!MPSettings.dronesAreBots) {
        obj_tag *player = NDrone2_Player();
        if (player != NULL && SteerRoundPlayer(drone, &eval, player)) {
            drone->avoidingPlayer = 1;
            sum = eval.steer + sum;
            count++;
        }
    } else {
        NDrone2_Player();
        for (uint i = 0; i < MPSettings.numPlayers; i++) {
            obj_tag *player = MPGame.players[i].playerObj;
            if (player != NULL && SteerRoundPlayer(drone, &eval, player)) {
                drone->avoidingPlayer = 1;
                sum = eval.steer + sum;
                count++;
            }
        }
    }

    // Where two NaNs can meet, the x87's choice between them is kept (X87NaNs)
    double turn = angle;
    if (count > 1)
        turn = X87NaNs(turn + turn + sum, turn, sum) / (count + 1);
    double scaled = X87NaNs(turn * drone->turnRate, turn, drone->turnRate);
    drone->moveRotation.y = (float)X87NaNs(scaled + drone->moveRotation.y, scaled, drone->moveRotation.y);
}

// One obstacle for the drone in eval->self: notes the nearest drone (other, NULL for a player); then, within 3 m, if
// a frame's walk brings them into contact, and before kHorizon over their closing speed, the turn away (steer): up to
// a quarter turn of the drone's share, less the further off the contact. True with a turn.
bool NDrone2_EvaluateObstacle(Drone_tag *drone, Drone_tag *other, ObstacleEval_tag *eval) {
    SteerBody *self = &eval->self;
    SteerBody *obstacle = eval->other;
    _VECTOR apart;
    apart.x = self->pos->x - obstacle->pos->x;
    apart.y = self->pos->y - obstacle->pos->y;
    apart.z = self->pos->z - obstacle->pos->z;
    double distance = Vec_Magnitude(&apart);
    if (other != NULL && distance < drone->nearestDroneDist) {
        drone->nearestDroneDist = (float)distance;
        drone->nearestDrone = other;
    }

    _VECTOR selfTo;
    selfTo.x = self->vel.x + self->pos->x;
    selfTo.y = 0.0f;
    selfTo.z = self->pos->z + self->vel.z;
    if (Vec_SqDist3D(self->pos, obstacle->pos) > 9.0f)
        return false;
    _VECTOR obstacleTo;
    if (obstacle->speed > 0.005f) {
        obstacle->vel.x = (float)(FSin((double)obstacle->rot->y) * obstacle->speed);
        obstacle->vel.y = 0.0f;
        obstacle->vel.z = (float)(FCos((double)obstacle->rot->y) * obstacle->speed);
        obstacleTo.x = obstacle->vel.x + obstacle->pos->x;
        obstacleTo.y = 0.0f;
        obstacleTo.z = obstacle->vel.z + obstacle->pos->z;
    } else {
        obstacleTo.x = obstacle->pos->x;
        obstacleTo.y = 0.0f;
        obstacleTo.z = obstacle->pos->z;
        obstacle->flags |= 2;
    }
    float second;
    if (!SphereSphereSweep(self->radius, self->pos, &selfTo, obstacle->radius, obstacle->pos, &obstacleTo, &eval->t,
                           &second))
        return false;

    if (!(obstacle->flags & 2))
        eval->limit = (float)(self->speed -
                              FCos((double)Vec_AngleDifference(obstacle->rot->y, self->rot->y)) * obstacle->speed);
    else
        eval->limit = self->speed;
    if (eval->t < 0.0f)
        return false;
    if ((double)eval->limit * kHorizon <= eval->t)
        return false;

    if (eval->t == 0.0f) {
        // Already touching: the turn is the larger the more nearly ahead the obstacle is
        Vec_Copy(self->pos, &self->contact);
        Vec_Copy(obstacle->pos, &obstacle->contact);
        eval->radiusSum = obstacle->radius + self->radius;
        Vec_Subtract(&obstacle->contact, &self->contact, &eval->delta);
        float bearing = (float)FPatan(eval->delta.x, eval->delta.z);
        eval->delta.y = 0.0f;
        double angle = Vec_AngleDifference(bearing, self->rot->y);
        eval->angle = (float)angle;
        eval->dist = 0.0f;
        eval->share = (float)(self->speed / ((double)obstacle->speed + self->speed));
        double weight = 1.0f - AngleSize(angle) * kInvPi;
        eval->weight = (float)weight;
        eval->steer = (float)(weight * eval->share * HALF_PI);
        if (angle < 0.0f)
            eval->steer = -eval->steer;
        return true;
    }

    Vec_Scalef(&self->vel, eval->t, &self->contact);
    Vec_Add2(self->pos, &self->contact, &self->contact);
    if (obstacle->flags & 2) {
        Vec_Copy(obstacle->pos, &obstacle->contact);
    } else {
        Vec_Scalef(&obstacle->vel, eval->t, &obstacle->contact);
        Vec_Add2(obstacle->pos, &obstacle->contact, &obstacle->contact);
    }
    eval->radiusSum = obstacle->radius + self->radius;
    Vec_Subtract(&obstacle->contact, &self->contact, &eval->delta);
    eval->delta.y = 0.0f;
    eval->dist = Vec_Magnitude(&eval->delta);
    eval->share = (float)(self->speed / ((double)obstacle->speed + self->speed));
    double weight = 1.0f - 1.0f / ((double)eval->limit * kHorizon) * eval->t;
    eval->weight = (float)weight;
    eval->steer = (float)(weight * eval->share * HALF_PI);
    float bearing = (float)FPatan(eval->delta.x, eval->delta.z);
    double angle = Vec_AngleDifference(bearing, self->rot->y);
    eval->angle = (float)angle;
    if (angle < 0.0f)
        eval->steer = -eval->steer;
    return true;
}

// When two spheres moving straight in the plane, from1 to to1 and from2 to to2, come within their radii of each
// other: the times (0 to 1 is the move) in first and second, both 0 when they already touch. With the first the
// later, both are the earlier. False when they never meet.
// AUTOINJECT
bool SphereSphereSweep(float radius1, _VECTOR *from1, _VECTOR *to1, float radius2, _VECTOR *from2, _VECTOR *to2,
                       float *first, float *second) {
    _VECTOR move1, move2, apart, closing;
    Vec_Subtract(to1, from1, &move1);
    Vec_Subtract(to2, from2, &move2);
    Vec_Subtract(from2, from1, &apart);
    Vec_Subtract(&move2, &move1, &closing);
    double reach = (double)radius1 + radius2;
    reach = reach * reach;
    double apartSq = (double)apart.z * apart.z + (double)apart.x * apart.x;
    if (apartSq <= reach) {
        *first = 0.0f;
        *second = 0.0f;
        return true;
    }
    float a = (float)((double)closing.z * closing.z + (double)closing.x * closing.x);
    float b = (float)(((double)apart.z * closing.z + (double)apart.x * closing.x) * 2.0);
    float c = (float)(apartSq - reach);
    if (!SolveQuadratic(a, b, c, first, second))
        return false;
    if (*first > *second)
        *first = *second;
    return true;
}

// The roots of a t^2 + b t + c = 0: (sqrt(d) - b) / 2a and (-b - sqrt(d)) / 2a. A discriminant of exactly 0 gives
// both as 0. False when it is negative (or a NaN). Only SphereSphereSweep calls it (our name).
// FUNC_AT(0x00042b20)
bool SolveQuadratic(float a, float b, float c, float *root1, float *root2) {
    double discriminant = (double)b * b - (double)a * c * 4.0f;
    if (discriminant == 0.0) {
        *root1 = 0.0f;
        *root2 = 0.0f;
        return true;
    }
    // Tested again, and its root taken, as stored to a float
    float stored = (float)discriminant;
    if (!(stored > 0.0f))
        return false;
    double root = sqrt((double)stored);
    double inverse = 1.0f / ((double)a + a);
    *root1 = (float)((root - b) * inverse);
    *root2 = (float)((-(double)b - root) * inverse);
    return true;
}

// Which side the nearest drone (from NDrone2_Steer) is on, off the way the drone faces (its anim's turn, animFlags
// 8, 0x10, 0x20, added): bunchFlags gets 0x10 right or 8 left, and 2 within 45 degrees of ahead or 4. False with no
// nearest drone, or a stationary one.
// AUTOINJECT
bool DroneMove_NoBunching(DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;
    obj_tag *obj = dcv->gameObj;
    Drone_tag *nearest = drone->nearestDrone;
    if (nearest == NULL || (nearest->flags & DRONE_FLAG_STATIONARY))
        return false;
    _VECTOR toNearest;
    Vec_Subtract(&nearest->gameObj->position, &obj->position, &toNearest);
    float bearing = Atan2(toNearest.x, toNearest.z);
    float facing = obj->rotation.y;
    if (drone->animFlags & 8)
        facing = facing + HALF_PI;
    if (drone->animFlags & 0x10)
        facing = facing + PI;
    if (drone->animFlags & 0x20)
        facing = facing - HALF_PI;
    double turn = Vec_AngleDifference(facing, bearing);
    drone->bunchFlags |= turn < 0.0f ? 0x10 : 8;
    drone->bunchFlags |= AngleSize(turn) < QUARTER_PI ? 2 : 4;
    return true;
}

// Whether the drone could not step 0.5 m along x and z in its own frame
static bool StepBlocked(Drone_tag *drone, obj_tag *obj, float x, float z) {
    _VECTOR step = {x, 0.0f, z};
    return !NDrone2_MoveTestRelative(&step, drone, obj, 0, 0);
}

// Which ways half a metre's step is blocked (boundaryFlags), for a drone with DRONE_FLAG_BOUNDARY_STOPS
// AUTOINJECT
void DroneMove_SetBoundryFlags(DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;
    obj_tag *obj = dcv->gameObj;
    drone->boundaryFlags = 0;
    if (!(dcv->drone->flags & DRONE_FLAG_BOUNDARY_STOPS))
        return;
    if (StepBlocked(drone, obj, 0.0f, 0.5f))
        drone->boundaryFlags |= 2;
    if (StepBlocked(drone, obj, 0.0f, -0.5f))
        drone->boundaryFlags |= 4;
    if (StepBlocked(drone, obj, 0.5f, 0.0f))
        drone->boundaryFlags |= 8;
    if (StepBlocked(drone, obj, -0.5f, 0.0f))
        drone->boundaryFlags |= 0x10;
}

// ---- turning and routes

// Turns targetRot.y to face obj (plus offset) when it is more than tolerance off. Not while unknown3f0 has
// 0x8000000 or the anim turns the drone (animFlags 0x380).
// AUTOINJECT
void DroneMove_TurnToObj(DCVars_tag *dcv, obj_tag *obj, float offset, float tolerance) {
    Drone_tag *drone = dcv->drone;
    if (obj == NULL || (drone->unknown3f0 & 0x8000000) || (drone->animFlags & 0x380))
        return;
    _VECTOR toObj;
    Vec_Subtract(&obj->position, &dcv->gameObj->position, &toObj);
    float heading = Atan2(toObj.x, toObj.z) + offset;
    if (ABS(Vec_AngleDifference(heading, drone->targetRot.y)) > tolerance)
        drone->targetRot.y = heading;
}

// The angle between the way from moveTarget to the drone and the segment the active route creeps along; 0 within
// about a degree. The XBE's acos, left unrounded.
static Extended RouteTurn(DCVars_tag *dcv) {
    _VECTOR fromTarget, along;
    Vec_Subtract(&dcv->gameObj->position, &dcv->drone->moveTarget, &fromTarget);
    AICreep_tag *creep = &dcv->drone->activeRoute->creep;
    Vec_Subtract(&creep->segmentEnd, &creep->segmentStart, &along);
    Vec_Normalise(&fromTarget, &fromTarget);
    Vec_Normalise(&along, &along);
    // Summed z first, and compared before it is rounded to a float
    double dot = (double)along.z * fromTarget.z + (double)along.y * fromTarget.y + (double)along.x * fromTarget.x;
    if (dot > 0.9998f)
        return 0.0f;
    return CIAcos((float)dot);
}

#if NIGHTFIRE_X87
static void StoreRouteTurn(DCVars_tag *dcv, Extended *turn) {
    *turn = RouteTurn(dcv);
}

// For the original's callers: the turn in ST0, unrounded
// AUTOINJECT
double __declspec(naked) DroneMove_NextRouteAngle(DCVars_tag *dcv) {
    _asm {
        sub esp, 12
        mov eax, esp
        push eax
        push dword ptr [esp + 20]
        call StoreRouteTurn
        add esp, 8
        fld tbyte ptr [esp]
        add esp, 12
        ret
    }
}

// DroneMove_NextRouteAngle's result as it leaves it in ST0
Extended DroneMove_RouteTurn(DCVars_tag *dcv) {
    Extended turn;
    Extended *at = &turn;
    _asm {
        push dcv
        call DroneMove_NextRouteAngle
        add esp, 4
        mov eax, at
        fstp tbyte ptr [eax]
    }
    return turn;
}
#else
double DroneMove_NextRouteAngle(DCVars_tag *dcv) {
    return (double)RouteTurn(dcv);
}

Extended DroneMove_RouteTurn(DCVars_tag *dcv) {
    return RouteTurn(dcv);
}
#endif

// AUTOINJECT
float DroneMove_DistanceToNextRouteNode(DCVars_tag *dcv) {
    return dcv->drone->moveTargetDistance;
}

// How far along the mission route (route2) from where the drone is to its node nearest obj: the nearest within
// radius (in plan) that obj's feet can walk straight to. 0 for none.
// AUTOINJECT
double DroneMove_ObjectMissionPathDistance(DCVars_tag *dcv, obj_tag *obj, float radius) {
    float bestSq = radius * radius;
    obj_tag *gameObj = dcv->gameObj;
    AIRoute_tag *route = &dcv->drone->route2;
    uint best = 0xffff;
    CelPos_tag from;
    NDrone2_FeetPos(obj, &from.pos);
    from.cel = obj->inCel;
    for (uint i = 0; i < route->count; i++) {
        MAPNODE_tag *node = &route->path->fileNodes[route->nodes[i]].node;
        float sq = (float)Vec_SqDist2D(&node->pos, &obj->position);
        if (best != 0xffff && !(sq < bestSq))
            continue;
        CelPos_tag to;
        Vec_Copy(&node->pos, &to.pos);
        to.cel = node->cel;
        if (NDrone2_MoveTest(&from.pos, from.cel, &to.pos, to.cel, NULL, gameObj, NULL, 0, 0) == 1) {
            best = i;
            bestSq = sq;
        }
    }
    if (best == 0xffff)
        return 0.0f;
    return AINetwork_GetRouteDistanceBetweenNodes(route, route->current, (short)best);
}

// The node of the drone's route path nearest it that it can walk to (NDrone2_NearestNode); -1 for none
// AUTOINJECT
short NDrone2_NearestNodeDCV(DCVars_tag *dcv) {
    CelPos_tag here;
    Vec_Copy(&dcv->gameObj->position, &here.pos);
    here.cel = dcv->gameObj->inCel;
    return NDrone2_NearestNode(&here, dcv->drone->route1.path, dcv->gameObj);
}

// Whether a route goes to aiPoint: NDrone2_MoveToGoalPosition answers 0 to 3
// AUTOINJECT
bool NDrone2_CanReachGoalPosition(DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;
    switch (NDrone2_MoveToGoalPosition(dcv, (CelPos_tag *)&drone->aiPoint.pos, drone->aiPoint.radius, 0)) {
    case 0:
    case 1:
    case 2:
    case 3:
        return true;
    default:
        return false;
    }
}

// ---- running from what scares them

// Sets aiPoint where the drone runs to, dist from pos (where obj is, if anything): a node the drone can reach dist on
// from it over the route path (safetyEmitter's flood from pos, to 1.8 x dist), or else the point dist beyond pos
// from the drone, as far as the bounds let it go. The route is reset either way. The drone then starts again in
// explosiveState, or in its state machine's resumeState, and returnState is resumeState. Always true.
// AUTOINJECT
bool DroneMove_FindSafetyFromScaryPosition(DCVars_tag *dcv, _VECTOR *pos, obj_tag *obj, float dist) {
    Drone_tag *drone = dcv->drone;
    obj_tag *gameObj = dcv->gameObj;
    CelPos_tag goal;
    AIEmitter_tag *emitter = &NPCGlobals.safetyEmitter;
    if (AINetwork_InitEmitter2(obj, pos, obj != NULL ? obj->inCel : NULL, emitter, drone->route1.path) &&
        drone->route1.path == emitter->path && AINetwork_EmitPath(emitter, dist * 1.8f) != 0) {
        MAPNODE_tag *node = NULL;
        short start = NDrone2_NearestNodeDCV(dcv);
        if (AINetwork_Emitter_GetNodeAtDistance(start, dist, emitter, &node) && node != NULL) {
            Vec_Copy(&node->pos, &goal.pos);
            goal.cel = node->cel;
            AINetwork_SetupGoalPosition(&drone->goalTarget, &drone->aiPoint, &goal, 1.0f);
            AINetwork_ResetRoute(&drone->route1, gameObj, NULL);
            AINetwork_InvalidateRoute(&drone->route1);
            if (NDrone2_CanReachGoalPosition(dcv)) {
                if (drone->explosiveState != 0)
                    dcv->drone->initialState = drone->explosiveState;
                else
                    dcv->drone->initialState = (short)dcv->aiStateMachine->resumeState;
                dcv->drone->returnState = (short)dcv->aiStateMachine->resumeState;
                return true;
            }
        }
    }

    _VECTOR away;
    Vec_Subtract(&gameObj->position, pos, &away);
    Vec_Normalise(&away, &away);
    auxVec_AddMulR32(pos, &away, dist, &goal.pos);
    goal.pos.y = gameObj->position.y + 0.5f;
    goal.cel = build_FindCel(&goal.pos, glb_world);
    AINetwork_FurthestPosition(&gameObj->position, &goal.pos, &goal.pos);
    goal.pos.y = pos->y + 0.5f;
    AINetwork_SetupGoalPosition(&dcv->drone->goalTarget, &dcv->drone->aiPoint, &goal, 1.0f);
    AINetwork_ResetRoute(&drone->route1, gameObj, NULL);
    AINetwork_InvalidateRoute(&drone->route1);
    if (drone->explosiveState != 0)
        dcv->drone->initialState = drone->explosiveState;
    else
        dcv->drone->initialState = (short)dcv->aiStateMachine->resumeState;
    dcv->drone->returnState = (short)dcv->aiStateMachine->resumeState;
    return true;
}

// Runs from the explosive DroneFunc_ConsiderExplosive noted, if any
// AUTOINJECT
bool DroneMove_FindSafetyFromScaryObject(DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;
    if (!drone->explosiveSeen)
        return false;
    return DroneMove_FindSafetyFromScaryPosition(dcv, &drone->explosivePos, NULL, drone->explosiveRadius);
}

// ---- zero gravity

// Faces the opponent (not while unknown3f0 has 0x8000000, the anim turns the drone, or a bot recovers), fires while it
// sees it, and once the state's time is up picks a point 10 to 20 m from the opponent in any direction: inside an AI
// box of type 3 it floats there (DSTATE_AstronautCombatMove) for 2 to 7 seconds. Within 3 m of it, it goes back to
// DSTATE_AstronautCombat.
// AUTOINJECT
void DroneMove_AstronautCombat(DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;
    obj_tag *obj = dcv->gameObj;
    if (drone->opponent == NULL)
        return;
    if (!(drone->unknown3f0 & 0x8000000) && (!MPSettings.dronesAreBots || !(drone->botVars->flags & 2)) &&
        !(drone->animFlags & 0x380)) {
        _VECTOR toOpponent;
        Vec_Subtract(&drone->opponent->position, &obj->position, &toOpponent);
        dcv->drone->targetRot.y = Atan2(toOpponent.x, toOpponent.z);
    }
    drone->firing = (dcv->drone->sightFlags & DRONE_SIGHT_SEES_OPPONENT) != 0;

    if (drone->stateScratch + drone->stateScratch2 < GameState.NumFramesUnpaused) {
        _VECTOR to;
        to.x = Float_FRand(2.0f) - 1.0f;
        to.y = Float_FRand(2.0f) - 1.0f;
        to.z = Float_FRand(2.0f) - 1.0f;
        Vec_Normalise(&to, &to);
        Vec_MulR32(&to, &to, Float_FRand(10.0f) + 10.0f);
        Vec_Add2(&drone->opponent->position, &to, &to);
        if (Drone_PointInAnyAIBox(&to, 3)) {
            drone->moveTarget = to;
            drone->floatsToMoveTarget = 1;
            drone->stateScratch = GameState.NumFramesUnpaused;
            drone->stateScratch2 = Rand_Rand(FRAME_RATE_INT * 5) + FRAME_RATE_INT * 2;
            Drone_SM_SetState(dcv->aiStateMachine, DSTATE_AstronautCombatMove, 0);
        }
    } else if (Vec_Dist3D(&obj->position, &drone->moveTarget) < 3.0f) {
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_AstronautCombat, 0);
    }
}

// The float anim for where moveTarget is: 0x72 within 45 degrees of ahead, 0x71 beyond 135, else 0x73 for a positive
// turn or 0x74
// AUTOINJECT
void DroneMove_AstronautAnim(DCVars_tag *dcv) {
    obj_tag *obj = dcv->gameObj;
    _VECTOR toTarget;
    Vec_Subtract(&dcv->drone->moveTarget, &obj->position, &toTarget);
    float bearing = Atan2(toTarget.x, toTarget.z);
    double turn = Vec_AngleDifference(obj->rotation.y, bearing);
    ushort anim;
    if (AngleSize(turn) < QUARTER_PI)
        anim = 0x72;
    else if (AngleSize(turn) > THREE_QUARTER_PI)
        anim = 0x71;
    else if (turn > 0.0f)
        anim = 0x73;
    else
        anim = 0x74;
    DroneAnim_CallAnim(anim, 0, 0, 0, 0.0f, 0, dcv);
}

// Hit while floating: 5 m/s more along the shot (away from the opponent without one), and no more floating to
// moveTarget
// AUTOINJECT
void DroneMove_AstronautHitMove(DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;
    _VECTOR push;
    if (drone->lastHit != NULL) {
        Vec_Copy(&drone->lastHit->hitDirection, &push);
        Vec_Normalise(&push, &push);
        Vec_MulR32(&push, &push, 5.0f);
    } else {
        Vec_Copy(&drone->opponentDelta, &push);
        Vec_Normalise(&push, &push);
        Vec_MulR32(&push, &push, -5.0f);
    }
    Vec_Add2(&drone->velocity, &push, &drone->velocity);
    drone->floatsToMoveTarget = 0;
}

// Killed while floating: 5 m/s more away from the opponent, DRONE_FLAG_DAMPED cleared, and no more floating to
// moveTarget
// AUTOINJECT
void DroneMove_AstronautDeathMove(DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;
    _VECTOR push;
    Vec_Copy(&drone->opponentDelta, &push);
    Vec_Normalise(&push, &push);
    Vec_MulR32(&push, &push, -5.0f);
    Vec_Add2(&drone->velocity, &push, &drone->velocity);
    dcv->drone->flags &= ~DRONE_FLAG_DAMPED;
    drone->floatsToMoveTarget = 0;
}
