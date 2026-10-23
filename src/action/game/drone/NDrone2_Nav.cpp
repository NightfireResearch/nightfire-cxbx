// What drones ask of the AI network: whether they can walk straight from one place to another, the nav node nearest
// them, how far an emitter is, who is standing near a point, and the shared target of every route to the player.
// See docs/drone/bots-and-navigation/README.md.

#include "NDrone2.h"
#include "../../math/math.h"    // Vec_* (ours)
#include "../../math/X87Ops.h"
#include "BOT.h"                        // BOT_getMovementSpeedMul
#include "../obj/build.h"               // build_FindCel

#include <bit>
#include <stdint.h>

#include <math.h>

#pragma fp_contract(off)

// Where NDrone2_CanWalkStraightTo last tested a walk to (BOTSTATE_checkAttackMove reads it)
// XBE_GLOBAL(0x001ee420, 0xc)
_VECTOR LastWalkTarget;

constexpr float kMoveTargetRadius = 0.3f;
static_assert(std::bit_cast<uint32_t>(kMoveTargetRadius) == 0x3e99999a, "not the original's 0.3");

// How near (squared, in plan) the player counts as at a point in NDrone2_DroneNearPos: 0.55 m
#define PLAYER_NEAR_SQ 0.3025f

// Whether a mover can walk straight from one place to another: not too far, not too steep a rise (one up to the step
// height is fine), and across no bound (AINetwork_BoundsTest, which with `hit` returns 0xff and the crossing). The
// objects are not used. The original passes BoundsTest a flag when checkBlocked is set and fails if it comes back set;
// BoundsTest never writes it.
// AUTOINJECT
char NDrone2_MoveTest(_VECTOR *from, cel_tag *fromCel, _VECTOR *to, cel_tag *toCel, obj_tag *objA, obj_tag *objB, _VECTOR *hit, char strict, char checkBlocked) {
    _VECTOR delta;
    Vec_Subtract(to, from, &delta);
    float flatSq = (float)((double)delta.z * delta.z + (double)delta.x * delta.x);
    if ((double)delta.y * delta.y + flatSq > MoveTest_MaxDistanceSq)
        return 0;

    double rise = (double)from->y - to->y;
    if (rise < 0.0)
        rise = -rise;
    if (rise > MoveTest_MaxRise)
        return 0;
    if (rise > MoveTest_StepHeight) {
        // FPATAN, unrounded
        Extended slope = AbsFchs(-FPatan(delta.y, sqrt((double)flatSq)));
        if (slope > Extended(MoveTest_MaxSlope))
            return 0;
    }

    if (AIBoundCount == 0)
        return 1;
    return AINetwork_BoundsTest(from, fromCel, to, toCel, hit, strict);
}

// The same between two cel positions
// AUTOINJECT
char NDrone2_MoveTest(CelPos_tag *from, CelPos_tag *to, obj_tag *objA, obj_tag *objB, _VECTOR *hit, char strict, char checkBlocked) {
    return NDrone2_MoveTest(&from->pos, from->cel, &to->pos, to->cel, objA, objB, hit, strict, checkBlocked);
}

// AUTOINJECT
char NDrone2_MoveTestFromNode(MAPNODE_tag *node, CelPos_tag *to, obj_tag *objA, obj_tag *objB, _VECTOR *hit,
                              char strict, char checkBlocked) {
    return NDrone2_MoveTest(&node->pos, node->cel, &to->pos, to->cel, objA, objB, hit, strict, checkBlocked);
}

// AUTOINJECT
char NDrone2_MoveTestToNode(CelPos_tag *from, MAPNODE_tag *node, obj_tag *objA, obj_tag *objB, _VECTOR *hit,
                            char strict, char checkBlocked) {
    return NDrone2_MoveTest(&from->pos, from->cel, &node->pos, node->cel, objA, objB, hit, strict, checkBlocked);
}

// The node of a path nearest a position (within 25 m) that it can be walked to; -1 for none
// AUTOINJECT
short NDrone2_NearestNode(CelPos_tag *pos, AIPath_tag *path, obj_tag *obj) {
    AINodeSearch_tag search;
    search.origin = pos;
    search.heuristicGoal = NULL;
    search.path = path;
    search.toNode = 1;
    search.openList = NULL;
    search.objA = obj;
    search.objB = NULL;
    search.targetBit = 0;
    search.resultNode = -1;
    search.radiusSq = 625.0f;
    search.found = 0;
    search.obstructed = 0;
    AINetwork_NodeSearch(&search);
    if (search.found <= 0 || search.resultNode == -1)
        return -1;
    return search.resultNode;
}

// How far an emitter is from a position: the emitter's byte at the position's nearest node (cached in *nearest when
// given; 0xffff there means not known yet), in *distance. False when the emitter is over another path or has no data
// (the distance is then 255), or when the flood did not reach the node.
// AUTOINJECT
bool NDrone2_DistanceToEmitter(CelPos_tag *pos, AIPath_tag *path, ushort *nearest, AIEmitter_tag *emitter,
                               obj_tag *obj, float *distance) {
    if (emitter->path != path || emitter->data == NULL) {
        if (distance != NULL)
            *distance = 255.0f;
        return false;
    }
    ushort node;
    if (nearest == NULL || *nearest == 0xffff)
        node = NDrone2_NearestNode(pos, path, obj);
    else
        node = *nearest;
    if (nearest != NULL)
        *nearest = node;
    uchar value = emitter->data[node];
    if (distance != NULL)
        *distance = value;
    return value != 0xff;
}

// Within 2 m of its height
static bool NearHeight(float y, float otherY) {
    double difference = (double)y - otherY;
    if (difference < 0.0)
        difference = -difference;
    return difference < 2.0f;
}

// Whether anyone but the drone stands within radius of it (NDrone2_DroneNearPos)
// AUTOINJECT
bool NDrone2_NearDrone(DCVars_tag *dcv, float radius) {
    return NDrone2_DroneNearPos(&dcv->gameObj->position, radius, dcv->gameObj);
}

// Whether anyone stands near a point: the player within 0.55 m in plan, or a live, active drone (other than `except`)
// within the radius; either within 2 m of its height
// AUTOINJECT
bool NDrone2_DroneNearPos(_VECTOR *pos, float radius, obj_tag *except) {
    float radiusSq = radius * radius;
    obj_tag *player = NDrone2_Player();
    if (player != NULL && Vec_SqDist2D(pos, &player->position) < PLAYER_NEAR_SQ && NearHeight(pos->y, player->position.y))
        return true;

    for (Drone_tag *drone = NPCGlobals.NDrone2List; drone != NULL; drone = drone->next) {
        obj_tag *obj = drone->gameObj;
        if (obj == except || obj == NULL)
            continue;
        Drone_tag *other = (Drone_tag *)obj->extraObjectData;
        if ((other->flags & 0x600) || !(other->flags & 0x100) || other->health <= 0.0f
            || obj->objectType == OBJECTTYPE_DEAD_DRONE || (obj->flags & 1))
            continue;
        if (Vec_SqDist2D(pos, &obj->position) < radiusSq && NearHeight(pos->y, obj->position.y))
            return true;
    }
    return false;
}

// Each NPC frame: the player's feet as the goal of the shared player target, its nodes marked again when it has moved
// 2 m (AINetwork_BuildAITarget)
// AUTOINJECT
void NDrone2_NavNodeCache(void) {
    obj_tag *player = NDrone2_Player();
    if (player == NULL)
        return;
    AITarget_tag *target = &NPCGlobals.playerTarget;
    NDrone2_FeetPos(player, &target->goal.pos);
    target->goal.cel = player->inCel;
    target->obj = player;
    target->targetBit = AITARGET_PLAYER;
    AINetwork_BuildAITarget(target, NULL, 0.0f);
}

// Whether the drone could walk straight from its object's feet to pos (finding pos's cel when it has none); never
// while it is frozen (0x10). FUN_00045340 in Ghidra. Not named on PS2; the name is ours.
// AUTOINJECT
bool NDrone2_CanWalkStraightTo(Drone_tag *drone, obj_tag *obj, CelPos_tag *pos, char checkBlocked) {
    if (drone != NULL && (drone->flags & 0x10))
        return false;
    CelPos_tag from;
    NDrone2_FeetPos(obj, &from.pos);
    from.cel = obj->inCel;
    if (pos->cel == NULL)
        pos->cel = NDrone2_FindCel(&pos->pos);
    LastWalkTarget = pos->pos;
    return NDrone2_MoveTest(&from.pos, from.cel, &pos->pos, pos->cel, NULL, obj, NULL, 0, checkBlocked) == 1;
}

// Whether the drone could walk from its object's feet to `offset` in the object's own frame (a bot's offset scaled by
// its speed unless `unscaled`); the offset is left turned into the world. Never while it is frozen. FUN_000453e0 in
// Ghidra. Not named on PS2; the name is ours.
// AUTOINJECT
bool NDrone2_MoveTestRelative(_VECTOR *offset, Drone_tag *drone, obj_tag *obj, char unscaled, char checkBlocked) {
    if (drone != NULL && (drone->flags & 0x10))
        return false;
    CelPos_tag to;
    NDrone2_FeetPos(obj, &to.pos);
    if (!unscaled && drone->dtype == DTYPE_Bot) {
        float speed = BOT_getMovementSpeedMul(drone);
        Vec_Mul_F3(offset, offset, speed, 1.0f, speed);
    }
    ApplyMatrixLVI(&obj->transformMatrix, offset);
    Vec_Add2(offset, &to.pos, &to.pos);
    to.cel = build_FindCel(&to.pos, glb_world);
    return NDrone2_CanWalkStraightTo(drone, obj, &to, checkBlocked);
}

// Whether the drone is within moveTargetRadius of moveTarget, in plan, from its feet (keeping the distance)
// AUTOINJECT
bool NDrone2_AtDest(DCVars_tag *dcv) {
    _VECTOR feet;
    NDrone2_FeetPos(dcv->gameObj, &feet);
    dcv->drone->moveTargetDistance = Vec_Dist2D(&feet, &dcv->drone->moveTarget);
    return dcv->drone->moveTargetDistance <= dcv->drone->moveTargetRadius;
}

// Steers moveTarget at a position (finding its cel), within 0.3 m, and works out the distance in plan and the
// heading to it
// FUNC_AT(0x00046080)
void NDrone2_SetMoveTarget(DCVars_tag *dcv, CelPos_tag *target) {
    CelPos_tag from;    // set up and not used
    NDrone2_FeetPos(dcv->gameObj, &from.pos);
    from.cel = dcv->gameObj->inCel;
    Vec_Copy(&target->pos, &dcv->drone->moveTarget);
    if (target->cel == NULL)
        target->cel = NDrone2_FindCel(&target->pos);
    dcv->drone->moveTargetCel = target->cel;
    dcv->drone->moveTargetRadius = kMoveTargetRadius;
    _VECTOR feet;
    NDrone2_FeetPos(dcv->gameObj, &feet);
    dcv->drone->moveTargetDistance = Vec_Dist2D(&feet, &dcv->drone->moveTarget);
    Drone_tag *drone = dcv->drone;
    _VECTOR delta;
    Vec_Subtract(&drone->moveTarget, &dcv->gameObj->position, &delta);
    drone->moveTargetRot.x = 0.0f;
    drone->moveTargetRot.y = Atan2(delta.x, delta.z);
    drone->moveTargetRot.z = 0.0f;
}

// Whether the drone could walk to (x, y, z) in its object's own frame from its feet. Never while it is frozen (0x10).
// FUNC_AT(0x00045490)
bool NDrone2_CanMoveToRelativePos(Drone_tag *drone, obj_tag *obj, float x, float y, float z) {
    if (drone != NULL && (drone->flags & 0x10))
        return false;
    _VECTOR offset = {x, y, z};
    return NDrone2_MoveTestRelative(&offset, drone, obj, 1, 1);
}
