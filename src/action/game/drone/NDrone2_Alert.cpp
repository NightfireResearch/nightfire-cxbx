// Going to a goal position (an alert's, a hostage's, an ally's), alert routes, the door guards' partners, searching and
// investigating, and the states for what a drone has seen (a dead body, a surrendered drone, a drone shot, an
// explosive) or heard (a noise, through aware, suspect and alert).

#include "NDrone2_Alert.h"
#include "DroneAnim.h"
#include "DroneModes.h"                 // NDrone2_ChangeToAttackMode
#include "DroneTables.h"
#include "DroneVision_Alerts.h"
#include "DroneWeapon.h"
#include "NDrone2_Damage.h"
#include "NDrone2_Route.h"
#include "Behaviour.h"
#include "../../game.h"                 // GameState
#include "../../util/Random.h"          // Rand_Rand
#include "../../math/X87Ops.h"          // Negate
#include "../mp/multiplayer.h"          // MPSettings

#include <bit>
#include <math.h>
#include <string.h>

// The originals, called by address
#define DroneFunc_InRangeOfTarget ((bool (__cdecl *)(DCVars_tag *dcv, float range))0x00035f50)
// Return in ST0, unrounded
#define Vec_Dist2D ((double (__cdecl *)(_VECTOR *a, _VECTOR *b))0x000d4e20)
#define Vec_SqDist2D ((double (__cdecl *)(_VECTOR *a, _VECTOR *b))0x000d4e50)

// Anim states (our names)
#define DANIM_RUN 0x1e
#define DANIM_STAND_AIM 10
#define DANIM_STAND_ALERT 0x21
#define DANIM_LOOK_AROUND 0x22
#define DANIM_LOOK_BEHIND 0x61
#define DANIM_ALERT_WALK 0x35
#define DANIM_ALERT_WALK_START 0xf
#define DANIM_SEARCH_LOOK 0xb

// Messages the search and heard-noise states send themselves at the end of an anim (our names)
#define DRONE_MSG_TurnedBehind 0x23
#define DRONE_MSG_SearchLooked 0x24
#define DRONE_MSG_SearchDone 0x25

#define SFX_HEARD_THAT 0x26
#define SFX_SURRENDERED_DRONE 0x23
#define SFX_CIVILIAN_SCARED 0x48

// A sound this loud makes an alerted or searching drone attack, once (DRONE_FLAG 0x4000000)
#define LOUD_SOUND_STEP 0.2f
// And a suspicious one
#define SUSPECT_SOUND_STEP 0.1f

// A turn of more than this to face the noise is made with a look behind first
constexpr float kThreeQuarterPi = 2.35619450f;
static_assert(std::bit_cast<uint32_t>(kThreeQuarterPi) == 0x4016cbe4, "not the original's 3pi/4");
static_assert(std::bit_cast<uint32_t>(LOUD_SOUND_STEP) == 0x3e4ccccd, "not the original's 0.2");
static_assert(std::bit_cast<uint32_t>(SUSPECT_SOUND_STEP) == 0x3dcccccd, "not the original's 0.1");
static_assert(std::bit_cast<uint32_t>(0.66f) == 0x3f28f5c3, "not the original's 0.66");

// MSVC's __ftol2 as the original uses it: the low word of a truncation to 64 bits. NaN, values out of the 64-bit range
// and values that round to a multiple of 2^32 give 0 (as src/driving/platform/X87.h has it).
static int32_t Ftol(double x) {
    if (!(fabs(x) < 9223372036854775808.0))
        return 0;
    long long nearest = llrint(x);
    if (uint32_t(nearest) == 0)
        return 0;
    return int32_t(int64_t(x));
}

static void SetTimer(DroneTimer *timer, uint dueFrame) {
    timer->dueFrame = dueFrame;
    timer->unknown8 = 0;
    timer->on = 1;
}

// How far the drone's feet are from the point, in plan, kept in it; returned unrounded too
// AUTOINJECT
double NDrone2_DistanceToAIPoint(DCVars_tag *dcv, AIPoint_tag *point) {
    _VECTOR feet;
    NDrone2_FeetPos(dcv->gameObj, &feet);
    double distance = Vec_Dist2D(&feet, &point->pos);
    point->distance = (float)distance;
    return distance;
}

// Placement mode 100 has a second behaviour block; these say which is in use and switch between them
// AUTOINJECT
uchar DroneFunc_Using2ndBehaviour(Drone_tag *drone) {
    return drone->usingSecondBehaviour;
}

// AUTOINJECT
bool DroneFunc_Set1stBehaviour(Drone_tag *drone) {
    if (drone->diVars.keys.mode != 100)
        return false;
    drone->currentBehaviour = drone->behaviour1;
    drone->usingSecondBehaviour = 0;
    return true;
}

// AUTOINJECT
bool DroneFunc_Set2ndBehaviour(Drone_tag *drone) {
    if (drone->diVars.keys.mode != 100)
        return false;
    drone->currentBehaviour = drone->behaviour2;
    drone->usingSecondBehaviour = 1;
    return true;
}

// AUTOINJECT
bool NDrone2_IsCivilian(DCVars_tag *dcv) {
    return dcv->drone->side != 1;
}

// A scared cry from a drone with unknown15 set, while it is not talking and alive: in the Tower levels first one of
// three lines (0x9f-0xa1), then always sound 0x48
// AUTOINJECT
void NDrone2_CivilianScaredTalk(DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;
    if (Drone_IsTalking(drone) || drone->health <= 0.0f)
        return;
    if (!dcv->drone->unknown15)
        return;
    switch (GameState.CurrentLevelHashcode) {
    case HT_Level_TowerA:
    case HT_Level_TowerB:
    case HT_Level_TowerC:
    case HT_Level_Tower2A:
    case HT_Level_Tower2B:
    case HT_Level_Tower2C:
    case HT_Level_Tower2Elevator:
        NDrone2_SpeechSFX(dcv, 0x9f + Rand_Rand(3), 0, 1, 0);
        break;
    default:
        break;
    }
    NDrone2_SpeechSFX(dcv, SFX_CIVILIAN_SCARED, 0, 1, 0);
}

// AUTOINJECT
uint NDrone2_MoveToAlertPosition(DCVars_tag *dcv) {
    return NDrone2_MoveToGoalPosition(dcv, (CelPos_tag *)&dcv->drone->aiPoint.pos, dcv->drone->aiPoint.radius, 0);
}

// The goal becomes the last alert broadcast (NPCGlobals.alertTarget, rebuilt first), within 2 m. The copy takes the
// eight bytes after the target too.
// AUTOINJECT
bool NDrone2_SetDroneAlertRoute(DCVars_tag *dcv) {
    AINetwork_InvalidateRoute(&dcv->drone->route1);
    AINetwork_BuildAITarget(&NPCGlobals.alertTarget, NULL, 0.0f);
    memcpy(&dcv->drone->goalTarget, &NPCGlobals.alertTarget, sizeof(AITarget_tag) + 8);
    Drone_tag *drone = dcv->drone;
    AINetwork_SetupGoalPosition(&drone->goalTarget, &drone->aiPoint, &drone->goalTarget.goal, 2.0f);
    return true;
}

// The goal becomes the last sound heard (soundTarget, with its frame and level), within 2 m
// AUTOINJECT
bool NDrone2_SetSoundAlertRoute(DCVars_tag *dcv) {
    memcpy(&dcv->drone->goalTarget, &dcv->drone->soundTarget, sizeof(AITarget_tag) + 8);
    AINetwork_InvalidateRoute(&dcv->drone->route1);
    AINetwork_BuildAITarget(&dcv->drone->goalTarget, NULL, 0.0f);
    Drone_tag *drone = dcv->drone;
    AINetwork_SetupGoalPosition(&drone->goalTarget, &drone->aiPoint, &drone->goalTarget.goal, 2.0f);
    return true;
}

// Turns the drone towards the last sound heard (and offset more), unless flag 0x8000000 of unknown3f0 is set
// AUTOINJECT
void NDrone2_SetAngleToAlert(DCVars_tag *dcv, float offset) {
    if (dcv->drone->unknown3f0 & 0x8000000)
        return;
    _VECTOR toSound;
    Vec_Subtract(&dcv->drone->soundTarget.goal.pos, &dcv->gameObj->position, &toSound);
    dcv->drone->angleToSomePoint = maybeAtan2(toSound.x, toSound.z) + offset;
}

// How far the drone must turn to face the last sound heard
// AUTOINJECT
float NDrone2_GetAngleDifferenceToAlert(DCVars_tag *dcv) {
    _VECTOR toSound;
    Vec_Subtract(&dcv->drone->soundTarget.goal.pos, &dcv->gameObj->position, &toSound);
    float yaw = maybeAtan2(toSound.x, toSound.z);
    return Vec_AngleDifference(dcv->gameObj->rotation.y, yaw);
}

// "Heard that?" from skin class 1, while no drone saw its opponent last frame, and it is not talking and alive
// AUTOINJECT
void DroneFunc_HeardNoiseTalk(DCVars_tag *dcv) {
    if (NPCGlobals.unknown174 != 0)
        return;
    Drone_tag *drone = dcv->drone;
    if (Drone_IsTalking(drone) || drone->health <= 0.0f)
        return;
    if (dcv->drone->skinClass == 1)
        NDrone2_SpeechSFX(dcv, SFX_HEARD_THAT, 0, 1, 0);
}

// The plane through the point along a and b (the normal a x b, not normalised). The offset is worked out with the
// normal's z as computed, before it is rounded to a float.
// AUTOINJECT
void plane_from_two_vectors_and_point(plane_equ_tag *plane, _VECTOR *a, _VECTOR *b, _VECTOR *point) {
    plane->a = (float)((double)b->z * a->y - (double)a->z * b->y);
    plane->b = (float)((double)a->z * b->x - (double)a->x * b->z);
    double c = (double)b->y * a->x - (double)a->y * b->x;
    plane->c = (float)c;
    plane->d = (float)(Negate((double)plane->a * point->x) - (double)point->y * plane->b - c * point->z);
}

// The door guard's planes (our name): the upright plane through pos along dir (the way to its partner), and the two
// across it, through pos and through the partner
// AUTOINJECT
void NDrone2_SetDoorGuardPlanes(_VECTOR *partnerPos, _VECTOR *pos, _VECTOR *dir, Drone_tag *drone) {
    _VECTOR up = {0.0f, 1.0f, 0.0f};
    plane_equ_tag *planes = drone->guardPlanes;
    plane_from_two_vectors_and_point(&planes[0], dir, &up, pos);
    float a = planes[0].a;
    float c = planes[0].c;
    planes[2].c = (float)Negate(a);
    planes[2].a = c;
    planes[2].b = planes[0].b;
    planes[1].c = a;
    planes[1].b = planes[0].b;
    planes[2].d = (float)(((double)a - planes[2].a) * pos->x + ((double)c - Negate(a)) * pos->z + planes[0].d);
    planes[1].a = (float)Negate(c);
    planes[1].d = (float)((((double)pos->x * a + planes[0].d) - Negate(c) * partnerPos->x) + (double)pos->z * c -
                          (double)partnerPos->z * planes[1].c);
}

// Pairs the drone with the nearest live drone of the DTYPE (in plan, within 2 m of its height) that has no partner yet.
// The one on the right, as the drone faces, keeps the door's planes; the other's guardZone is 0xff (it reads its
// partner's). Always true.
// AUTOINJECT
bool NDrone2_SetDronePartner(uchar dtype, DCVars_tag *dcv) {
    obj_tag *gameObj = dcv->gameObj;
    Drone_tag *drone = dcv->drone;
    Drone_tag *partner = NULL;
    float nearestSq = 1000000.0f;
    for (Drone_tag *other = NPCGlobals.NDrone2List; other != NULL; other = other->next) {
        if (other == drone || other->dtype != dtype)
            continue;
        obj_tag *obj = other->gameObj;
        if (obj == NULL)
            continue;
        Drone_tag *otherDrone = (Drone_tag *)obj->extraObjectData;
        if ((otherDrone->flags & 0x600) || !(otherDrone->flags & 0x100) || otherDrone->health <= 0.0f)
            continue;
        if (obj->objectType == OBJECTTYPE_DEAD_DRONE || (obj->flags & 1))
            continue;
        if (other->unknown23c != NULL)
            continue;
        double dy = (double)gameObj->position.y - obj->position.y;
        if (dy < 0.0)
            dy = -dy;
        if (!(dy < 2.0f))
            continue;
        double sq = Vec_SqDist2D(&gameObj->position, &obj->position);
        if (sq < nearestSq) {
            nearestSq = (float)sq;
            partner = other;
            drone->unknown23c = other->gameObj;
        }
    }
    if (partner == NULL)
        return true;

    partner->unknown23c = gameObj;
    _VECTOR *partnerPos = &partner->gameObj->position;
    _VECTOR dir;
    Vec_Subtract(partnerPos, &gameObj->position, &dir);
    Vec_Normalise(&dir, &dir);
    float yaw = maybeAtan2(dir.x, dir.z);
    if (Vec_AngleDifference(gameObj->rotation.y, yaw) < 0.0f) {
        dir.x = (float)Negate(dir.x);
        dir.y = (float)Negate(dir.y);
        drone->guardZone = 0xff;
        dir.z = (float)Negate(dir.z);
        NDrone2_SetDoorGuardPlanes(&gameObj->position, partnerPos, &dir, partner);
    } else {
        partner->guardZone = 0xff;
        NDrone2_SetDoorGuardPlanes(partnerPos, &gameObj->position, &dir, drone);
    }
    return true;
}

// Where the opponent is from the guarded door (kept in guardZone): 3 through it, 2 within 2 m in front, 1 within 6 m,
// else 0 (and 0 beside it, or more than 2 m above or below). A drone with no partner or no opponent says 0; the one
// without the planes reads its partner's.
// AUTOINJECT
uint NDrone2_DoorGuardHandler(DCVars_tag *dcv) {
    obj_tag *gameObj = dcv->gameObj;
    Drone_tag *drone = dcv->drone;
    if (drone->unknown23c == NULL || drone->opponent == NULL)
        return 0;
    obj_tag *opponent = drone->opponent;
    if (drone->guardZone == 0xff)
        return ((Drone_tag *)drone->unknown23c->extraObjectData)->guardZone;
    double dy = (double)opponent->position.y - gameObj->position.y;
    if (dy < 0.0)
        dy = -dy;
    if (dy > 2.0f)
        return 0;
    float along = (float)DistancePointToPlane(&opponent->position, &drone->guardPlanes[0]);
    float side1 = (float)DistancePointToPlane(&opponent->position, &drone->guardPlanes[1]);
    double side2 = DistancePointToPlane(&opponent->position, &drone->guardPlanes[2]);
    uint zone = 0;
    if (!(side1 < 0.0f) && !(side2 < 0.0) && !(along < -6.0f)) {
        if (along < 0.0f)
            zone = 3;
        else if (along < 2.0f)
            zone = 2;
        else if (along < 6.0f)
            zone = 1;
    }
    dcv->drone->guardZone = zone;
    return zone;
}

// ---- going to a goal position

// Runs to the goal (aiPoint), looking for its opponent on the way (not in TowerA and TowerB, nor once it knows of it
// out of range, with flags2 0x200000 or unknown16); armed and held up, it fires at a seen opponent. There it deletes
// itself (property 0x59), or as DTYPE 7 switches between its two behaviours and attacks, or goes to its initial state.
// AUTOINJECT
bool NDrone2_DSTATE_GoToGoalPosition(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= 0x10000;
        dcv->drone->flags |= 0x400000;
        return true;

    case DRONE_MSG_Exit:
        dcv->drone->flags &= ~0x400000;
        return true;

    case DRONE_MSG_Update: {
        NDrone2_SeenAndAttacking(dcv);
        if (GameState.CurrentLevelHashcode < HT_Level_TowerA || GameState.CurrentLevelHashcode > HT_Level_TowerB) {
            Drone_tag *self = dcv->drone;
            uint sight = self->sightFlags;
            if ((sight & DRONE_SIGHT_SEES_OPPONENT) &&
                (!(sight & DRONE_SIGHT_REACTED) || DroneFunc_InRangeOfTarget(dcv, self->combatRanges[1])) &&
                !(self->flags2 & 0x200000) && !drone->unknown16) {
                dcv->aiStateMachine->scratch1c = (short)DroneVision_EnemyLookForOpponent(dcv);
                if (dcv->aiStateMachine->scratch1c != 0)
                    return Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)dcv->aiStateMachine->scratch1c, 0);
            }
        }
        NDrone2_DistanceToAIPoint(dcv, &dcv->drone->aiPoint);
        Drone_tag *self = dcv->drone;
        switch (NDrone2_MoveToGoalPosition(dcv, (CelPos_tag *)&self->aiPoint.pos, self->aiPoint.radius, 0)) {
        case AIROUTE_FOLLOWING:
        case AIROUTE_PARTIAL:
        case AIROUTE_DIRECT:
            DroneAnim_CallAnim(DANIM_RUN, 0, 0, 0, 0.0f, 0, dcv);
            return true;
        case AIROUTE_CREEP_FAILED:
        case AIROUTE_CREEP_FAILED2:
            if (drone->side == 1 && NDrone2_ObjectIsArmed(gameObj)) {
                NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, 0.0f, 0);
                if (!(dcv->drone->sightFlags & DRONE_SIGHT_SEES_OPPONENT)) {
                    drone->firing = 0;
                    DroneAnim_CallAnim(DANIM_STAND_ALERT, 0, 0, 0, 0.0f, 0, dcv);
                    return true;
                }
                drone->firing = drone->gunThroughWall == 0;
                DroneAnim_CallAnim(DANIM_STAND_AIM, 0, 0, 0, 0.0f, 0, dcv);
                if (dcv->drone->burstOver) {
                    if (drone->firing)
                        DroneWeap_Fire(1, dcv);
                } else if (drone->firing) {
                    DroneAnim_PlayFiringAnim(dcv);
                }
                return true;
            }
            DroneAnim_CallAnim(DANIM_STAND_ALERT, 0, 0, 0, 0.0f, 0, dcv);
            return true;
        case AIROUTE_DOOR:
            return true;
        default:
            if (behaviour_util_getProperty(DRONE_PROP_59, dcv->drone->currentBehaviour) != 0)
                return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_DeleteMe, 0);
            dcv->drone->unknown33 = 0;
            if (MPSettings.maybeDroneAIEnabled == 0 && dcv->drone->dtype == DTYPE_RunToPoint) {
                if (DroneFunc_Using2ndBehaviour(dcv->drone)) {
                    DroneFunc_Set1stBehaviour(dcv->drone);
                    return true;
                }
                dcv->aiStateMachine->scratch1c = NDrone2_ChangeToAttackMode(dcv->drone);
                if (dcv->aiStateMachine->scratch1c != 0)
                    return Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)dcv->aiStateMachine->scratch1c, 0);
                DroneFunc_Set2ndBehaviour(dcv->drone);
                dcv->drone->dtype = dcv->drone->dtypeBase;
                return true;
            }
            Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)dcv->drone->initialState, 0);
            return true;
        }
    }

    case DRONE_MSG_ImpactPunch:
    case DRONE_MSG_ImpactBullet:
        DroneFunc_HandleImpact(dcv, msg, dcv->aiStateMachine->curState, 0);
        return true;

    case DRONE_MSG_ImpactTaser:
    case DRONE_MSG_ImpactExplosive:
    case DRONE_MSG_ImpactSmoke:
    case DRONE_MSG_ImpactStunGrenade:
    case DRONE_MSG_ImpactStunDart:
        DroneFunc_HandleImpact(dcv, msg, dcv->aiStateMachine->curState, 1);
        return true;

    case 0x1b:
        Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)dcv->drone->initialState, 0);
        return true;

    case DRONE_MSG_ExplosiveSeen:
        DroneFunc_ConsiderExplosive(dcv, (obj_tag *)msg->extraData, (short)dcv->aiStateMachine->curState);
        return true;

    default:
        return false;
    }
}

// A hostage running to the goal, crying out on the way; there it goes to its initial state. Neither Enter nor Exit is
// handled.
// AUTOINJECT
bool NDrone2_DSTATE_HostageGoToGoalPosition(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Update: {
        NDrone2_DistanceToAIPoint(dcv, &dcv->drone->aiPoint);
        NDrone2_CivilianScaredTalk(dcv);
        Drone_tag *self = dcv->drone;
        switch (NDrone2_MoveToGoalPosition(dcv, (CelPos_tag *)&self->aiPoint.pos, self->aiPoint.radius, 0)) {
        case AIROUTE_FOLLOWING:
        case AIROUTE_PARTIAL:
        case AIROUTE_DIRECT:
            DroneAnim_CallAnim(DANIM_RUN, 0, 0, 0, 0.0f, 0, dcv);
            return true;
        case AIROUTE_ARRIVED:
            return Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)dcv->drone->initialState, 0);
        case AIROUTE_CREEP_FAILED:
        case AIROUTE_CREEP_FAILED2:
            DroneAnim_CallAnim(DANIM_STAND_ALERT, 0, 0, 0, 0.0f, 0, dcv);
            return true;
        default:
            return true;
        }
    }

    case DRONE_MSG_ImpactPunch:
    case DRONE_MSG_ImpactBullet:
        DroneFunc_HandleImpact(dcv, msg, DSTATE_HostageGoToGoalPosition, 0);
        return true;

    case DRONE_MSG_ImpactTaser:
    case DRONE_MSG_ImpactExplosive:
    case DRONE_MSG_ImpactSmoke:
    case DRONE_MSG_ImpactStunGrenade:
    case DRONE_MSG_ImpactStunDart:
        DroneFunc_HandleImpact(dcv, msg, DSTATE_HostageGoToGoalPosition, 1);
        return true;

    case 0x1b:
        Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)dcv->drone->initialState, 0);
        return true;

    case DRONE_MSG_ExplosiveSeen:
        DroneFunc_ConsiderExplosive(dcv, (obj_tag *)msg->extraData, DSTATE_HostageGoToGoalPosition);
        return true;

    default:
        return false;
    }
}

// An ally running to the goal; there, or when it cannot get there, it goes to its initial state. Neither Enter nor
// Exit is handled.
// AUTOINJECT
bool NDrone2_DSTATE_AllyGoToGoalPosition(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Update: {
        dcv->drone->flags |= 0x10000;
        Drone_AlertStatusSet(2, dcv);
        NDrone2_DistanceToAIPoint(dcv, &dcv->drone->aiPoint);
        Drone_tag *self = dcv->drone;
        switch (NDrone2_MoveToGoalPosition(dcv, (CelPos_tag *)&self->aiPoint.pos, self->aiPoint.radius, 0)) {
        case AIROUTE_FOLLOWING:
        case AIROUTE_PARTIAL:
        case AIROUTE_DIRECT:
            DroneAnim_CallAnim(DANIM_RUN, 0, 0, 0, 0.0f, 0, dcv);
            return true;
        case AIROUTE_ARRIVED:
            return Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)dcv->drone->initialState, 0);
        case AIROUTE_DOOR:
            return true;
        default:
            Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)dcv->drone->initialState, 0);
            return true;
        }
    }

    case DRONE_MSG_ImpactPunch:
    case DRONE_MSG_ImpactBullet:
        DroneFunc_HandleImpact(dcv, msg, DSTATE_AllyGoToGoalPosition, 0);
        return true;

    case DRONE_MSG_ImpactTaser:
    case DRONE_MSG_ImpactExplosive:
    case DRONE_MSG_ImpactSmoke:
    case DRONE_MSG_ImpactStunGrenade:
    case DRONE_MSG_ImpactStunDart:
        DroneFunc_HandleImpact(dcv, msg, DSTATE_AllyGoToGoalPosition, 1);
        return true;

    case 0x1b:
        Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)dcv->drone->initialState, 0);
        return true;

    case DRONE_MSG_ExplosiveSeen:
        DroneFunc_ConsiderExplosive(dcv, (obj_tag *)msg->extraData, DSTATE_AllyGoToGoalPosition);
        return true;

    default:
        return false;
    }
}

// Going to where an alert was (aiPoint), at a walk within 10 m of the end of the route, looking for its opponent; there
// it goes to its initial state, and with no route to its return state. Every five seconds (timer A) it runs while
// still over 8 m beyond the point's radius. A loud sound makes it attack.
// AUTOINJECT
bool NDrone2_DSTATE_AlertToPosition(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        Drone_AlertStatusSet(1, dcv);
        dcv->drone->flags |= 0x10000;
        dcv->drone->flags |= 0x2000;
        DroneAnim_CallAnim(DANIM_ALERT_WALK_START, 0, 0, 0, 0.0f, 0, dcv);
        SetTimer(&dcv->drone->timerA, GameState.NumFramesUnpaused + FRAME_RATE_INT * 5);
        return true;

    case DRONE_MSG_Update:
        if (dcv->drone->alertness >= 1.0f)
            NDrone2_SeenAndAttacking(dcv);
        dcv->aiStateMachine->scratch1c = (short)DroneVision_EnemyLookForOpponent(dcv);
        if (dcv->aiStateMachine->scratch1c != 0)
            return Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)dcv->aiStateMachine->scratch1c, 0);
        switch (NDrone2_MoveToAlertPosition(dcv)) {
        case AIROUTE_FOLLOWING:
        case AIROUTE_PARTIAL:
        case AIROUTE_DIRECT:
            if (drone->route1.remaining < 10.0f) {
                short animState = dcv->drone->animState;
                if (animState == DANIM_ALERT_WALK || animState == DANIM_ALERT_WALK_START)
                    return true;
                DroneAnim_CallAnim(DANIM_ALERT_WALK, 0, 0, 0, 0.0f, 0, dcv);
                return true;
            }
            DroneAnim_CallAnim(DANIM_RUN, 0, 0, 0, 0.0f, 0, dcv);
            return true;
        case AIROUTE_ARRIVED:
            return Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)dcv->drone->initialState, 0);
        case AIROUTE_NO_ROUTE:
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_CombatNoRoute, 0);
        case AIROUTE_DOOR:
            return true;
        default:
            dcv->drone->unknown33 = 0;
            Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)dcv->drone->returnState, 0);
            return true;
        }

    case DRONE_MSG_TimerA:
        if ((double)dcv->drone->aiPoint.radius + 8.0f > dcv->drone->aiPoint.distance)
            return true;
        DroneAnim_CallAnim(DANIM_RUN, 0, 0, 0, 0.0f, 0, dcv);
        return true;

    case DRONE_MSG_ConsideredAlerted:
        DroneFunc_DoForcedAttack(dcv);
        return true;

    case DRONE_MSG_HeardNoise:
        if (drone->soundAlertStep > LOUD_SOUND_STEP && !(dcv->drone->flags & 0x4000000)) {
            dcv->drone->flags |= 0x4000000;
            dcv->drone->flags |= 0x10000000;
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Attack, 0);
        }
        [[fallthrough]];
    case 0x11:
    case 0x12:
    case DRONE_MSG_DroneAlert:
    case 0x16:
    case DRONE_MSG_GlobalAlarm:
        dcv->aiStateMachine->scratch1c = (short)DroneVision_EnemyAlerts(dcv, msg);
        if (dcv->aiStateMachine->scratch1c != 0)
            return Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)dcv->aiStateMachine->scratch1c, 0);
        return true;

    case DRONE_MSG_ImpactPunch:
    case DRONE_MSG_ImpactBullet:
        DroneFunc_HandleImpact(dcv, msg, DSTATE_Attack, 0);
        return true;

    case DRONE_MSG_ImpactTaser:
    case DRONE_MSG_ImpactExplosive:
    case DRONE_MSG_ImpactSmoke:
    case DRONE_MSG_ImpactStunGrenade:
    case DRONE_MSG_ImpactStunDart:
        DroneFunc_HandleImpact(dcv, msg, DSTATE_Attack, 1);
        return true;

    case 0x1b:
        Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)dcv->drone->initialState, 0);
        return true;

    case DRONE_MSG_ExplosiveSeen:
        DroneFunc_ConsiderExplosive(dcv, (obj_tag *)msg->extraData, DSTATE_Attack);
        return true;

    default:
        return false;
    }
}

// The search's end: calm again, it goes back to where it was before the alert (returnPos), to the state it was in
static bool SearchArea_GoBack(DCVars_tag *dcv) {
    dcv->drone->initialState = dcv->drone->returnState;
    AINetwork_InvalidateRoute(&dcv->drone->route1);
    Drone_tag *drone = dcv->drone;
    AINetwork_SetupGoalPosition(&drone->goalTarget, &drone->aiPoint, (CelPos_tag *)&drone->returnPos, 2.0f);
    Drone_SM_SetState(dcv->aiStateMachine, DSTATE_GoToGoalPosition, 0);
    return true;
}

// Searching about: after three seconds (timer A) it looks behind, then around, then calms down and goes back to where
// it was. Seeing its opponent or an alert ends it; a loud sound makes it attack.
// AUTOINJECT
bool NDrone2_DSTATE_SearchArea(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= 0x10000;
        dcv->drone->flags |= 0x2000;
        Drone_AlertStatusSet(1, dcv);
        DroneAnim_CallAnim(DANIM_LOOK_AROUND, 0, 0, 0, 0.0f, 0, dcv);
        SetTimer(&dcv->drone->timerA, GameState.NumFramesUnpaused + FRAME_RATE_INT * 3);
        return true;

    case DRONE_MSG_Update:
        dcv->aiStateMachine->scratch1c = (short)DroneVision_EnemyLookForOpponent(dcv);
        if (dcv->aiStateMachine->scratch1c != 0)
            return Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)dcv->aiStateMachine->scratch1c, 0);
        return true;

    case DRONE_MSG_TimerA:
        DroneAnim_CallAnim(DANIM_LOOK_BEHIND, 0, 0, DRONE_MSG_SearchLooked, 0.0f, 0, dcv);
        return true;

    case DRONE_MSG_SearchLooked:
        DroneAnim_CallAnim(DANIM_SEARCH_LOOK, 0, 0, DRONE_MSG_SearchDone, 0.0f, 0, dcv);
        return true;

    case DRONE_MSG_SearchDone:
        Drone_AlertStatusSet(0, dcv);
        dcv->drone->alertness = 0.0f;
        dcv->drone->turnedToAlert = 0;
        return SearchArea_GoBack(dcv);

    case DRONE_MSG_ConsideredAlerted:
        DroneFunc_DoForcedAttack(dcv);
        return true;

    case DRONE_MSG_HeardNoise:
        if (drone->soundAlertStep > LOUD_SOUND_STEP && !(dcv->drone->flags & 0x4000000)) {
            dcv->drone->flags |= 0x4000000;
            dcv->drone->flags |= 0x10000000;
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Attack, 0);
        }
        [[fallthrough]];
    case 0x11:
    case 0x12:
    case DRONE_MSG_DroneAlert:
    case 0x16:
    case DRONE_MSG_GlobalAlarm:
        dcv->aiStateMachine->scratch1c = (short)DroneVision_EnemyAlerts(dcv, msg);
        if (dcv->aiStateMachine->scratch1c != 0)
            return Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)dcv->aiStateMachine->scratch1c, 0);
        return true;

    case DRONE_MSG_ImpactPunch:
    case DRONE_MSG_ImpactBullet:
        DroneFunc_HandleImpact(dcv, msg, DSTATE_Attack, 0);
        return true;

    case DRONE_MSG_ImpactTaser:
    case DRONE_MSG_ImpactExplosive:
    case DRONE_MSG_ImpactSmoke:
    case DRONE_MSG_ImpactStunGrenade:
    case DRONE_MSG_ImpactStunDart:
        DroneFunc_HandleImpact(dcv, msg, DSTATE_Attack, 1);
        return true;

    case 0x1b:
        return SearchArea_GoBack(dcv);

    case DRONE_MSG_ExplosiveSeen:
        DroneFunc_ConsiderExplosive(dcv, (obj_tag *)msg->extraData, DSTATE_Attack);
        return true;

    default:
        return false;
    }
}

// Looking about, as an ally, until its alertness falls below 0.66 (Idle), it sees its opponent or there is an alert.
// AUTOINJECT
bool NDrone2_DSTATE_Investigate(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= 0x10000;
        dcv->drone->flags |= 0x2000;
        Drone_AlertStatusSet(2, dcv);
        DroneAnim_CallAnim(DANIM_STAND_ALERT, 0, 0, 0, 0.0f, 0, dcv);
        return true;

    case DRONE_MSG_Update:
        dcv->aiStateMachine->scratch1c = (short)DroneVision_EnemyLookForOpponent(dcv);
        if (dcv->aiStateMachine->scratch1c != 0)
            return Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)dcv->aiStateMachine->scratch1c, 0);
        if (dcv->drone->alertness < 0.66f)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Idle, 0);
        return true;

    case DRONE_MSG_ConsideredAlerted:
        DroneFunc_DoForcedAttack(dcv);
        return true;

    case 0x11:
    case 0x12:
    case DRONE_MSG_HeardNoise:
    case DRONE_MSG_DroneAlert:
    case 0x16:
    case DRONE_MSG_GlobalAlarm:
        dcv->aiStateMachine->scratch1c = (short)DroneVision_EnemyAlerts(dcv, msg);
        if (dcv->aiStateMachine->scratch1c != 0)
            return Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)dcv->aiStateMachine->scratch1c, 0);
        return true;

    case DRONE_MSG_ImpactPunch:
    case DRONE_MSG_ImpactBullet:
        DroneFunc_HandleImpact(dcv, msg, DSTATE_Attack, 0);
        return true;

    case DRONE_MSG_ImpactTaser:
    case DRONE_MSG_ImpactExplosive:
    case DRONE_MSG_ImpactSmoke:
    case DRONE_MSG_ImpactStunGrenade:
    case DRONE_MSG_ImpactStunDart:
        DroneFunc_HandleImpact(dcv, msg, DSTATE_Attack, 1);
        return true;

    case DRONE_MSG_ExplosiveSeen:
        DroneFunc_ConsiderExplosive(dcv, (obj_tag *)msg->extraData, DSTATE_Attack);
        return true;

    default:
        return false;
    }
}

// ---- what a drone has seen

// Fully alert, it heads for the last alert broadcast (AlertToPosition after the anim), and attacks when it gets there.
// Its update is not handled.
// AUTOINJECT
bool NDrone2_DSTATE_SeenDeadBody(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= 0x10000;
        Drone_AlertStatusSet(1, dcv);
        dcv->drone->alertness = 1.0f;
        dcv->drone->initialState = DSTATE_Attack;
        NDrone2_SetDroneAlertRoute(dcv);
        DroneAnim_CallAnim(DANIM_STAND_ALERT, 0, DSTATE_AlertToPosition, 0, 0.0f, 0, dcv);
        return true;

    case DRONE_MSG_ImpactPunch:
    case DRONE_MSG_ImpactBullet:
        DroneFunc_HandleImpact(dcv, msg, DSTATE_Attack, 0);
        return true;

    case DRONE_MSG_ImpactTaser:
    case DRONE_MSG_ImpactExplosive:
    case DRONE_MSG_ImpactSmoke:
    case DRONE_MSG_ImpactStunGrenade:
    case DRONE_MSG_ImpactStunDart:
        DroneFunc_HandleImpact(dcv, msg, DSTATE_Attack, 1);
        return true;

    case DRONE_MSG_ExplosiveSeen:
        DroneFunc_ConsiderExplosive(dcv, (obj_tag *)msg->extraData, DSTATE_Attack);
        return true;

    default:
        return false;
    }
}

// As SeenDeadBody, saying so one time in six.
// AUTOINJECT
bool NDrone2_DSTATE_SeenSurrenderedDrone(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= 0x10000;
        Drone_AlertStatusSet(1, dcv);
        dcv->drone->alertness = 1.0f;
        dcv->drone->initialState = DSTATE_Attack;
        NDrone2_SetDroneAlertRoute(dcv);
        if (Rand_Rand(6) == 0)
            NDrone2_PlaySFX(dcv, SFX_SURRENDERED_DRONE, NULL, 1, 1.0f);
        DroneAnim_CallAnim(DANIM_STAND_ALERT, 0, DSTATE_AlertToPosition, 0, 0.0f, 0, dcv);
        return true;

    case DRONE_MSG_ImpactPunch:
    case DRONE_MSG_ImpactBullet:
        DroneFunc_HandleImpact(dcv, msg, DSTATE_Attack, 0);
        return true;

    case DRONE_MSG_ImpactTaser:
    case DRONE_MSG_ImpactExplosive:
    case DRONE_MSG_ImpactSmoke:
    case DRONE_MSG_ImpactStunGrenade:
    case DRONE_MSG_ImpactStunDart:
        DroneFunc_HandleImpact(dcv, msg, DSTATE_Attack, 1);
        return true;

    case DRONE_MSG_ExplosiveSeen:
        DroneFunc_ConsiderExplosive(dcv, (obj_tag *)msg->extraData, DSTATE_Attack);
        return true;

    default:
        return false;
    }
}

// Straight to the attack. The state table has it for SeenOpponent too.
// AUTOINJECT
bool NDrone2_DSTATE_SeenDroneShot(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Attack, 0);
        return true;

    default:
        return false;
    }
}

// Does nothing: it only says Null, Enter and Update are handled.
// AUTOINJECT
bool NDrone2_DSTATE_SeenExplosive(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
    case DRONE_MSG_Enter:
    case DRONE_MSG_Update:
        return true;

    default:
        return false;
    }
}

// ---- what a drone has heard

// AUTOINJECT
bool NDrone2_DSTATE_HeardNoise(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        Drone_AlertStatusSet(1, dcv);
        dcv->drone->unknown33 = 1;
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_HeardNoiseAware, 0);
        return true;

    default:
        return false;
    }
}

// The first time, it looks around and goes back to its return state; heard again, or a suspicious sound, and it is
// suspicious (HeardNoiseSuspect).
// AUTOINJECT
bool NDrone2_DSTATE_HeardNoiseAware(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->unknown33 = 1;
        if (drone->heardNoiseStage >= 2)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_HeardNoiseSuspect, 0);
        drone->heardNoiseStage = 2;
        dcv->drone->flags |= 0x10000;
        dcv->drone->flags |= 0x2000;
        DroneAnim_CallAnim(DANIM_LOOK_AROUND, 1, dcv->drone->returnState, 0, 0.0f, 0, dcv);
        return true;

    case DRONE_MSG_Update:
        dcv->aiStateMachine->scratch1c = (short)DroneVision_EnemyLookForOpponent(dcv);
        if (dcv->aiStateMachine->scratch1c != 0)
            return Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)dcv->aiStateMachine->scratch1c, 0);
        return true;

    case DRONE_MSG_ConsideredAlerted:
        DroneFunc_DoForcedAttack(dcv);
        return true;

    case DRONE_MSG_HeardNoise:
        if (drone->soundAlertStep > SUSPECT_SOUND_STEP)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_HeardNoiseSuspect, 0);
        return true;

    case 0x11:
    case 0x12:
    case DRONE_MSG_DroneAlert:
    case 0x16:
    case DRONE_MSG_GlobalAlarm:
        dcv->aiStateMachine->scratch1c = (short)DroneVision_EnemyAlerts(dcv, msg);
        if (dcv->aiStateMachine->scratch1c != 0)
            return Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)dcv->aiStateMachine->scratch1c, 0);
        return true;

    case DRONE_MSG_ImpactPunch:
    case DRONE_MSG_ImpactBullet:
        DroneFunc_HandleImpact(dcv, msg, DSTATE_Attack, 0);
        return true;

    case DRONE_MSG_ImpactTaser:
    case DRONE_MSG_ImpactExplosive:
    case DRONE_MSG_ImpactSmoke:
    case DRONE_MSG_ImpactStunGrenade:
    case DRONE_MSG_ImpactStunDart:
        DroneFunc_HandleImpact(dcv, msg, DSTATE_Attack, 1);
        return true;

    case DRONE_MSG_ExplosiveSeen:
        DroneFunc_ConsiderExplosive(dcv, (obj_tag *)msg->extraData, DSTATE_Attack);
        return true;

    default:
        return false;
    }
}

// Turns to the noise: with property 0x21 it then goes to look (AlertToPosition, then SearchArea), otherwise it looks
// around and goes back. A noise more than three quarters of a turn round takes a look behind first. A drone with flag
// 0x10 or property 0x40 only looks around. Suspicious once before, it is alert (HeardNoiseAlert); any noise heard here
// makes it attack.
// AUTOINJECT
bool NDrone2_DSTATE_HeardNoiseSuspect(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter: {
        Drone_tag *self = dcv->drone;
        if (self->heardNoiseStage >= 3)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_HeardNoiseAlert, 0);
        self->heardNoiseStage = 3;
        Drone_AlertStatusSet(1, dcv);
        DroneFunc_HeardNoiseTalk(dcv);
        dcv->drone->unknown33 = 1;
        dcv->drone->flags |= 0x2000;
        if ((dcv->drone->flags & 0x10) ||
            behaviour_util_getProperty(DRONE_PROP_40, dcv->drone->currentBehaviour) != 0)
            break;
        dcv->drone->unknown33 = 1;
        NDrone2_SetSoundAlertRoute(dcv);
        if (dcv->drone->turnedToAlert &&
            behaviour_util_getProperty(DRONE_PROP_21, dcv->drone->currentBehaviour) != 0)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_AlertToPosition, 0);
        dcv->drone->flags |= 0x10000;
        double turn = NDrone2_GetAngleDifferenceToAlert(dcv) < 0.0f ? -NDrone2_GetAngleDifferenceToAlert(dcv)
                                                                      : NDrone2_GetAngleDifferenceToAlert(dcv);
        if (turn > kThreeQuarterPi) {
            DroneAnim_CallAnim(DANIM_LOOK_BEHIND, 0, 0, DRONE_MSG_TurnedBehind, 0.0f, 0, dcv);
            return true;
        }
        dcv->drone->turnedToAlert = 1;
        NDrone2_SetAngleToAlert(dcv, 0.0f);
        if (behaviour_util_getProperty(DRONE_PROP_21, dcv->drone->currentBehaviour) != 0) {
            dcv->drone->initialState = DSTATE_SearchArea;
            DroneAnim_CallAnim(DANIM_LOOK_AROUND, 0, DSTATE_AlertToPosition, 0, 0.0f, 0, dcv);
            return true;
        }
        break;
    }

    case DRONE_MSG_TurnedBehind:
        if (dcv->drone->turnedToAlert &&
            behaviour_util_getProperty(DRONE_PROP_21, dcv->drone->currentBehaviour) != 0)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_AlertToPosition, 0);
        dcv->drone->turnedToAlert = 1;
        NDrone2_SetAngleToAlert(dcv, 0.0f);
        if (behaviour_util_getProperty(DRONE_PROP_21, dcv->drone->currentBehaviour) == 0)
            break;
        dcv->drone->initialState = DSTATE_SearchArea;
        if (dcv->drone->animState == DANIM_STAND_ALERT) {
            DroneAnim_CallAnim(DANIM_SEARCH_LOOK, 0, DSTATE_AlertToPosition, 0, 0.0f, 0, dcv);
            return true;
        }
        return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_AlertToPosition, 0);

    case DRONE_MSG_Update:
        dcv->aiStateMachine->scratch1c = (short)DroneVision_EnemyLookForOpponent(dcv);
        if (dcv->aiStateMachine->scratch1c != 0)
            return Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)dcv->aiStateMachine->scratch1c, 0);
        return true;

    case DRONE_MSG_ConsideredAlerted:
        DroneFunc_DoForcedAttack(dcv);
        return true;

    case DRONE_MSG_HeardNoise:
        if (drone->soundAlertStep > SUSPECT_SOUND_STEP) {
            dcv->drone->flags |= 0x4000000;
            dcv->drone->flags |= 0x10000000;
        }
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Attack, 0);
        return true;

    case 0x11:
    case 0x12:
    case DRONE_MSG_DroneAlert:
    case 0x16:
    case DRONE_MSG_GlobalAlarm:
        dcv->aiStateMachine->scratch1c = (short)DroneVision_EnemyAlerts(dcv, msg);
        if (dcv->aiStateMachine->scratch1c != 0)
            return Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)dcv->aiStateMachine->scratch1c, 0);
        return true;

    case DRONE_MSG_ImpactPunch:
    case DRONE_MSG_ImpactBullet:
        DroneFunc_HandleImpact(dcv, msg, DSTATE_Attack, 0);
        return true;

    case DRONE_MSG_ImpactTaser:
    case DRONE_MSG_ImpactExplosive:
    case DRONE_MSG_ImpactSmoke:
    case DRONE_MSG_ImpactStunGrenade:
    case DRONE_MSG_ImpactStunDart:
        DroneFunc_HandleImpact(dcv, msg, DSTATE_Attack, 1);
        return true;

    case DRONE_MSG_ExplosiveSeen:
        DroneFunc_ConsiderExplosive(dcv, (obj_tag *)msg->extraData, DSTATE_Attack);
        return true;

    default:
        return false;
    }

    // Looks around and goes back to its return state
    dcv->drone->flags |= 0x10000;
    DroneAnim_CallAnim(DANIM_LOOK_AROUND, 1, dcv->drone->returnState, 0, 0.0f, 0, dcv);
    return true;
}

// Alert: it turns to its opponent and looks (the attack after it), with a look behind first when the opponent is more
// than three quarters of a turn round; after a minute (timer A) it attacks. A drone with flag 0x10 or property 0x40
// only looks around, and attacks.
// AUTOINJECT
bool NDrone2_DSTATE_HeardNoiseAlert(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter: {
        dcv->drone->heardNoiseStage = 4;
        Drone_AlertStatusSet(1, dcv);
        dcv->drone->unknown33 = 1;
        DroneFunc_HeardNoiseTalk(dcv);
        if ((dcv->drone->flags & 0x10) ||
            behaviour_util_getProperty(DRONE_PROP_40, dcv->drone->currentBehaviour) != 0) {
            dcv->drone->flags |= 0x10000;
            dcv->drone->flags |= 0x2000;
            DroneAnim_CallAnim(DANIM_LOOK_AROUND, 1, DSTATE_Attack, 0, 0.0f, 0, dcv);
            return true;
        }
        dcv->drone->unknown33 = 1;
        dcv->drone->flags |= 0x10000;
        dcv->drone->flags |= 0x2000;
        SetTimer(&dcv->drone->timerA, Ftol((double)FRAME_RATE_DIV * 60.0f + (double)GameState.NumFramesUnpaused));
        Drone_tag *self = dcv->drone;
        float bearing = self->opponentBearing;
        if (bearing < 0.0f)
            bearing = -bearing;
        if (bearing > kThreeQuarterPi) {
            DroneAnim_CallAnim(DANIM_LOOK_BEHIND, 0, 0, DRONE_MSG_TurnedBehind, 0.0f, 0, dcv);
            return true;
        }
        self->turnedToAlert = 1;
        NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, 0.0f, 0);
        DroneAnim_CallAnim(DANIM_SEARCH_LOOK, 0, DSTATE_Attack, 0, 0.0f, 0, dcv);
        return true;
    }

    case DRONE_MSG_TurnedBehind:
        dcv->drone->turnedToAlert = 1;
        NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, 0.0f, 0);
        DroneAnim_CallAnim(DANIM_SEARCH_LOOK, 0, DSTATE_Attack, 0, 0.0f, 0, dcv);
        return true;

    case DRONE_MSG_Update:
        dcv->aiStateMachine->scratch1c = (short)DroneVision_EnemyLookForOpponent(dcv);
        if (dcv->aiStateMachine->scratch1c != 0)
            return Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)dcv->aiStateMachine->scratch1c, 0);
        return true;

    case DRONE_MSG_TimerA:
        dcv->drone->flags |= 0x4000000;
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Attack, 0);
        return true;

    case DRONE_MSG_ConsideredAlerted:
        DroneFunc_DoForcedAttack(dcv);
        return true;

    case 0x11:
    case 0x12:
    case DRONE_MSG_DroneAlert:
    case 0x16:
    case DRONE_MSG_GlobalAlarm:
        dcv->aiStateMachine->scratch1c = (short)DroneVision_EnemyAlerts(dcv, msg);
        if (dcv->aiStateMachine->scratch1c != 0)
            return Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)dcv->aiStateMachine->scratch1c, 0);
        return true;

    case DRONE_MSG_ImpactPunch:
    case DRONE_MSG_ImpactBullet:
        DroneFunc_HandleImpact(dcv, msg, DSTATE_Attack, 0);
        return true;

    case DRONE_MSG_ImpactTaser:
    case DRONE_MSG_ImpactExplosive:
    case DRONE_MSG_ImpactSmoke:
    case DRONE_MSG_ImpactStunGrenade:
    case DRONE_MSG_ImpactStunDart:
        DroneFunc_HandleImpact(dcv, msg, DSTATE_Attack, 1);
        return true;

    case DRONE_MSG_ExplosiveSeen:
        DroneFunc_ConsiderExplosive(dcv, (obj_tag *)msg->extraData, DSTATE_Attack);
        return true;

    default:
        return false;
    }
}
