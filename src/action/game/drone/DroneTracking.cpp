// Where a drone looks and aims. Each frame NDrone2_DoTracking either aims the drone's arms at targetPos, while its
// anim aims (NDrone2_AimTest), letting its head go, or stops aiming and lets NDrone2_DoHeadTrackingBehaviour choose
// whom its head follows: its opponent once it has reacted to it, or the nearest drone in its cel. The head stays on a
// choice for the time NDrone2_HeadTrackObj gives it.

#include "DroneTracking.h"
#include "BOT.h"
#include "Behaviour.h"                 // behaviour_util_getProperty
#include "DroneTables.h"               // DRONE_PROP_HEAD_TRACK
#include "../../game.h"                // GameState
#include "../../engine/Anim.h"         // AnimObjectIsAiming, AnimObjectIsHeadTracking, AnimObjectSetAiming
#include "../../math/math.h"
#include "../../util/Random.h"
#include "../mp/multiplayer.h"         // MPSettings
#include "../../math/X87Ops.h"

#include <math.h>
#include <stdint.h>

#pragma fp_contract(off)

// Drone_tag.aimMode
#define AIM_ALWAYS 2
#define AIM_FROM_HALF_WAY 3     // or as the anim's events say (aimByEvent)
#define AIM_NO_LOOK 0xd         // with 0, the head does not follow anyone

// Drone_tag.headMode: looks further and holds on to its choice
#define HEAD_MODE_WIDE 2

// The skin class whose head follows only the player

// The bone AnimObjectSetAiming is handed when the aiming stops
#define AIM_BONE 5

// Whether the drone's anim aims: the AIM_ALWAYS rows, and the AIM_FROM_HALF_WAY rows once the script is half way
// through (or, outside multiplayer, as the anim's events say). Never without an anim script, or for a recovering bot.
// The event byte is returned as it is.
// AUTOINJECT
uchar NDrone2_AimTest(DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;
    sAnimScript_tag *script = drone->animScript;
    if (script == NULL)
        return 0;
    if (MPSettings.dronesAreBots) {
        if (drone->botVars->flags & BOT_FLAG_RECOVERING)
            return 0;
    } else if (drone->aimMode == AIM_FROM_HALF_WAY && drone->aimByEvent) {
        return drone->aimEvent;
    }
    if (drone->aimMode == AIM_ALWAYS)
        return 1;
    if (drone->aimMode != AIM_FROM_HALF_WAY)
        return 0;
    return Ftol(script->curFrame) > script->numFrames >> 1;
}

// Aims the drone's arms at targetPos while aim is set and it has an opponent, or stops them. Inline in their reading
// (our name).
// FUNC_AT(0x0003ab00)
void NDrone2_DoAiming(char aim, DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;
    obj_tag *obj = dcv->gameObj;
    if (drone->opponent == NULL)
        aim = 0;
    drone->aiming = aim;
    if (aim) {
        // Compared with the opponent, but aimed at targetPos
        if (obj->animState->animObj.aimTarget != drone->opponent || !AnimObjectIsAiming(obj))
            AnimObjectSetAiming(obj, &drone->targetPos, 100.0f, 0.2f, 0, 0);
    } else if (AnimObjectIsAiming(obj)) {
        AnimObjectSetAiming(obj, NULL, 100.0f, 0.2f, 1, AIM_BONE);
    }
}

// Turns the head to target for 2 to 6 seconds (with on, for drones with property 0x4c), or lets it go after 5 to 14,
// at 0.14 when alert past 0.66, else 0.07. Skin class 0x11 follows only the player; nothing changes while it already
// follows target.
// AUTOINJECT
void NDrone2_DoHeadTracking(char on, obj_tag *target, DCVars_tag *dcv) {
    obj_tag *obj = dcv->gameObj;
    Drone_tag *drone = dcv->drone;
    float speed = drone->alertness > 0.66f ? 0.14f : 0.07f;
    if (!on || behaviour_util_getProperty(DRONE_PROP_HEAD_TRACK, drone->currentBehaviour) == 0) {
        if (AnimObjectIsHeadTracking(obj))
            NDrone2_HeadTrackObj(NULL, (Rand_Rand(10) + 5) * FRAME_RATE_INT, speed, dcv);
        return;
    }
    if (drone->skinClass == SKINCLASS_CASTLE_GUARD && target != NDrone2_Player())
        return;
    if (AnimObjectIsHeadTracking(obj) && drone->headTarget == target)
        return;
    NDrone2_HeadTrackObj(target, (Rand_Rand(5) + 2) * FRAME_RATE_INT, speed, dcv);
}

// The nearest other drone in the drone's cel within 30 m, for drones with property 0x4c. One that is already the
// head's target is kept, 1 time in 4, rather than counted. Inline in their reading (our name).
// FUNC_AT(0x0003a780)
obj_tag* NDrone2_HeadTrackNearest(DCVars_tag *dcv) {
    if (behaviour_util_getProperty(DRONE_PROP_HEAD_TRACK, dcv->drone->currentBehaviour) == 0)
        return NULL;
    Drone_tag *nearest = NULL;
    bool keep = false;
    float nearestSq = 900.0f;
    for (Drone_tag *other = NPCGlobals.NDrone2List; other != NULL; other = other->next) {
        if (other == dcv->drone || other->gameObj->inCel != dcv->gameObj->inCel)
            continue;
        double distSq = Vec_SqDist2D(&dcv->gameObj->position, &other->gameObj->position);
        if (!(distSq < nearestSq))
            continue;
        if (dcv->drone->headTarget == other->gameObj && Rand_Rand(4) == 0) {
            keep = true;
            continue;
        }
        nearest = other;
        nearestSq = (float)distSq;
    }
    if (nearest != NULL)
        return nearest->gameObj;
    if (keep)
        return dcv->drone->headTarget;
    return NULL;
}

// Whom the head follows while the drone does not aim. Nobody while attacking or for the anims that do not look; its
// opponent once it has reacted to it; otherwise, when the last choice has run out, the nearest drone
// (NDrone2_HeadTrackNearest): for an enemy below 0.66 alert that has not reacted, for allies and civilians when they
// have no opponent within range (16, or 50 for HEAD_MODE_WIDE) or already follow it. HEAD_MODE_WIDE holds the
// opponent for 5 to 9 seconds, anyone else for one.
// AUTOINJECT
void NDrone2_DoHeadTrackingBehaviour(DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;
    float range = drone->headMode == HEAD_MODE_WIDE ? 50.0f : 16.0f;
    obj_tag *target = NULL;
    if (drone->sightFlags & DRONE_SIGHT_REACTED)
        target = drone->opponent;
    if ((drone->flags & DRONE_FLAG_ATTACK_MODE) || drone->aimMode == 0 || drone->aimMode == AIM_NO_LOOK) {
        NDrone2_DoHeadTracking(0, NULL, dcv);
        return;
    }

    float distSq = 0.0f;
    if (target != NULL)
        distSq = (float)Vec_SqDist2D(&target->position, &dcv->gameObj->position);
    if (drone->headTrackUntil != 0 && GameState.NumFramesUnpaused <= drone->headTrackUntil)
        return;

    switch (drone->side) {
    case 1:
        if (drone->alertness >= 0.66f)
            return;
        if (!(dcv->drone->sightFlags & DRONE_SIGHT_REACTED))
            target = NDrone2_HeadTrackNearest(dcv);
        break;
    case 2:
    case 3:
        if (target == NULL || distSq > range || drone->headTarget == target)
            target = NDrone2_HeadTrackNearest(dcv);
        break;
    }

    if (target == NULL || target == drone->headTarget)
        return;
    NDrone2_DoHeadTracking(1, target, dcv);
    if (drone->headMode != HEAD_MODE_WIDE)
        return;
    if (target == drone->opponent)
        drone->headTrackUntil = (Rand_Rand(5) + 5) * FRAME_RATE_INT + GameState.NumFramesUnpaused;
    else
        drone->headTrackUntil = FRAME_RATE_INT + GameState.NumFramesUnpaused;
}

// Each frame: aims while NDrone2_AimTest says so, the head let go, and otherwise the head follows
// NDrone2_DoHeadTrackingBehaviour's choice. Without on, neither.
// AUTOINJECT
void NDrone2_DoTracking(char on, DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;
    drone->aiming = 0;
    if (!on) {
        NDrone2_DoAiming(0, dcv);
        NDrone2_DoHeadTracking(0, NULL, dcv);
        drone->headTrackUntil = 0;
        return;
    }
    if (NDrone2_AimTest(dcv)) {
        NDrone2_DoHeadTracking(0, NULL, dcv);
        drone->headTrackUntil = 0;
        NDrone2_DoAiming(1, dcv);
    } else {
        NDrone2_DoAiming(0, dcv);
        NDrone2_DoHeadTrackingBehaviour(dcv);
    }
}
