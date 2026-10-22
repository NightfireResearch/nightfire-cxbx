// What the drones see. Each frame Drone_InitComms runs DroneVision_ProcessDroneSight: drones that know something
// alert those that see them (DroneVision_FindAlertedDrones), each drone works out how well it sees its opponent
// (DroneVision_OpponentVisibility), lines of sight are tested in turn until one frame's first costly test
// (DroneVision_SeekOpponentLOS: one of Drone_View_Bones a frame), and after the reaction time a drone in sight
// registers its opponent (DroneVision_HaveOpponentSight). docs/drone/architecture/README.md has the fields.

#include "DroneVision.h"
#include "../obj/control.h"
#include "NDrone2.h"
#include "BOT.h"
#include "Behaviour.h"
#include "DroneTables.h"           // DRONE_PROP
#include "../../game.h"            // GameState
#include "../mp/multiplayer.h"     // MPSettings
#include "../obj/Drone_AIHints.h"  // Drone_VisibilityForPosition
#include "../sp/PlayerStats.h"     // PlrStat_LogEnemyDetectedPlayer
#include "../../sound/music.h"     // Music_Event
#include "../../engine/Anim.h"     // AnimGetBoneWorldTrans, AnimState
#include "../../engine/Collide.h"  // Collide_LineOfSight, Collide_RayIntersect
#include "../../math/math.h"
#include "../../math/X87Ops.h"

#include <bit>
#include <math.h>
#include <stdint.h>
#include "BOTSTATE.h"

#pragma fp_contract(off)

// The bones a drone's line of sight tries in turn (DroneVision_OpponentLOS)
// XBE_GLOBAL(0x00178088, 0x20) readonly relocate
uint Drone_View_Bones[8] = {
    2,
    3,
    5,
    49,
    55,
    19,
    35,
    0xffffffff,
};
// The "blind drones" cheat (C_CHCHBLIND)
// XBE_GLOBAL(0x001df9a0, 0x4)
uint32_t switch_BLIND_DRONES;

#define HEAD_BONE 5

// The flags every line of sight hands Collide_LineOfSight, and the muzzle's ray
#define SIGHT_RAY_FLAGS 0xa27
#define MUZZLE_RAY_FLAGS 0x927

constexpr float kCollisionRadius = 0.4f;    // DroneFunc_ClampPosToCollisionCylinder
constexpr float kViewMargin = 0.3926991f;   // pi / 8, beyond viewAngle
constexpr float kHalfPi = 1.5707964f;
constexpr float kPi = 3.1415927f;
static_assert(std::bit_cast<uint32_t>(kCollisionRadius) == 0x3ecccccd, "not the original's constant");
static_assert(std::bit_cast<uint32_t>(kViewMargin) == 0x3ec90fdb, "not the original's constant");
static_assert(std::bit_cast<uint32_t>(kHalfPi) == 0x3fc90fdb, "not the original's constant");
static_assert(std::bit_cast<uint32_t>(kPi) == 0x40490fdb, "not the original's constant");
static_assert(std::bit_cast<uint32_t>(0.7f) == 0x3f333333, "not the original's constant");

// The original's compare-and-FCHS: -0 keeps its sign, and a NaN is negated too
static double AbsByCompare(double x) {
    if (x >= 0.0)
        return x;
    return Negate(x);
}

// ---- lines of sight

// The weapon's bone in the drone's hand, where its muzzle is and the barrel's direction (the bone's -x). False
// without an anim, a weapon datum or an anim running (flag 1). A sound of 0 comes back as 0xffff.
// AUTOINJECT
bool DroneWeap_GetWeaponInfo(Drone_tag *drone, int *bone, uint *flash, ushort *sound, _VECTOR *muzzle,
                             _VECTOR *barrel) {
    obj_tag *obj = drone->gameObj;
    if (obj->animState == NULL)
        return false;
    *bone = AnimDatumGetWeaponInfo(obj, NULL, (HASHCODE *)flash, sound);
    if (*bone < 0)
        return false;
    if (obj->animState == NULL || !(obj->animState->animObj.flags & 1))
        return false;
    if (sound != NULL && *sound == 0)
        *sound = 0xffff;
    _MATRIX mtx;
    AnimGetBoneWorldTrans(obj, *bone, 1, muzzle, &mtx);
    _VECTOR back = {-1.0f, 0.0f, 0.0f};
    MatrixMultiplyVector(&mtx, &back, barrel);
    barrel->x = barrel->x - muzzle->x;
    barrel->y = barrel->y - muzzle->y;
    barrel->z = barrel->z - muzzle->z;
    return true;
}

// Brings pos in to 0.4 m of the object's centre in the plane
// AUTOINJECT
void DroneFunc_ClampPosToCollisionCylinder(obj_tag *obj, _VECTOR *pos) {
    _VECTOR offset;
    Vec_Subtract(pos, &obj->position, &offset);
    offset.y = 0.0f;
    if (Vec_Magnitude(&offset) > kCollisionRadius) {
        Vec_Normalise(&offset, &offset);
        Vec_MulR32(&offset, &offset, kCollisionRadius);
        pos->x = offset.x + obj->position.x;
        pos->z = offset.z + obj->position.z;
    }
}

// A line of sight between two places, tested against the level only if the cels can see each other. Counted in
// NPCGlobals.visionTests.
// AUTOINJECT
bool DroneVision_LineOfSightToObject(CelPos_tag *from, CelPos_tag *to, obj_tag *objA, obj_tag *objB, char unused,
                                     uint mask) {
    if (from->cel != to->cel && !AINetwork_TestRayCels(&from->pos, from->cel, &to->pos, to->cel))
        return false;
    NPCGlobals.visionTests++;
    return Collide_LineOfSight(&from->pos, &to->pos, from->cel, objA, objB, mask | SIGHT_RAY_FLAGS);
}

// A line of sight from `from` (in cel) to the object. To a drone or the player it runs from the bone (pulled in to
// the collision cylinder; with bots, the head) back to the eye; to anything else, from the eye to the object (1.75 m
// up in the protection and demolition games).
// AUTOINJECT
bool DroneVision_CanSeeObjectFrom(Drone_tag *drone, obj_tag *obj, uint bone, _VECTOR *from, cel_tag *cel, uint mask) {
    bool alert = drone->alertness >= 1.0f;
    CelPos_tag target, eye;
    eye.pos = *from;
    target.cel = obj->inCel;
    eye.cel = cel;
    if (obj->objectType == OBJECTTYPE_DRONE || obj->objectType == OBJECTTYPE_PLAYER) {
        if (!MPSettings.maybeDroneAIEnabled) {
            DroneFunc_GetBonePos(obj, bone, &target.pos);
            DroneFunc_ClampPosToCollisionCylinder(obj, &target.pos);
        } else {
            NDrone2_GetHeadPos(obj, &target.pos, NULL);
        }
        obj_tag *viewer = drone->gameObj;
        if (target.cel != eye.cel && !AINetwork_TestRayCels(&target.pos, target.cel, &eye.pos, eye.cel))
            return false;
        NPCGlobals.visionTests++;
        return Collide_LineOfSight(&target.pos, &eye.pos, target.cel, obj, viewer, mask | SIGHT_RAY_FLAGS);
    }

    Vec_Copy(&obj->position, &target.pos);
    if (MPSettings.maybeDroneAIEnabled &&
        (MPSettings.GameMode == GM_PROTECTION || MPSettings.GameMode == GM_DEMOLITION))
        target.pos.y = target.pos.y + 1.75f;
    // The mask is not passed on here
    return DroneVision_LineOfSightToObject(&eye, &target, drone->gameObj, obj, alert, 0);
}

// From the drone's eyes (pulled in to its collision cylinder, but not with bots) to the object's bone
// AUTOINJECT
bool NDrone2_CanSeeObject(Drone_tag *drone, obj_tag *obj, uint bone, uint mask) {
    _VECTOR eye;
    NDrone2_GetHeadPos(drone->gameObj, &eye, NULL);
    if (!MPSettings.maybeDroneAIEnabled)
        DroneFunc_ClampPosToCollisionCylinder(drone->gameObj, &eye);
    return DroneVision_CanSeeObjectFrom(drone, obj, bone, &eye, drone->gameObj->inCel, mask);
}

// From the eye (in eyeCel) to pos (in cel); the original has DroneVision_LineOfSightToObject inline. Our name.
// FUNC_AT(0x00064660)
bool DroneVision_CanSeePositionFrom(obj_tag *viewer, _VECTOR *pos, cel_tag *cel, obj_tag *ignore, _VECTOR *eye,
                                    cel_tag *eyeCel) {
    CelPos_tag from, to;
    from.pos = *eye;
    from.cel = eyeCel;
    Vec_Copy(pos, &to.pos);
    to.cel = cel;
    return DroneVision_LineOfSightToObject(&from, &to, viewer, ignore, 0, 0);
}

// From the viewer's eyes, pulled in to its collision cylinder, to pos
// AUTOINJECT
bool DroneVision_CanSeePosition(obj_tag *viewer, _VECTOR *pos, cel_tag *cel, obj_tag *ignore) {
    _VECTOR eye;
    NDrone2_GetHeadPos(viewer, &eye, NULL);
    DroneFunc_ClampPosToCollisionCylinder(viewer, &eye);
    return DroneVision_CanSeePositionFrom(viewer, pos, cel, ignore, &eye, viewer->inCel);
}

// Whether an aiming, armed drone's muzzle is through a wall: a ray from the muzzle pulled in to the collision
// cylinder out to the muzzle
// AUTOINJECT
bool DroneVision_GunThroughWall(Drone_tag *drone) {
    if (!drone->aiming || !NDrone2_ObjectIsArmed(drone->gameObj))
        return false;
    int bone;
    uint flash;
    ushort sound;
    _VECTOR muzzle, barrel;
    if (!DroneWeap_GetWeaponInfo(drone, &bone, &flash, &sound, &muzzle, &barrel))
        return false;
    _VECTOR end;
    Vec_Copy(&muzzle, &end);
    DroneFunc_ClampPosToCollisionCylinder(drone->gameObj, &muzzle);
    HITDATA_tag *hits = NULL;
    if (!Collide_RayIntersect(&muzzle, &end, drone->gameObj->inCel, drone->gameObj, NULL, &hits, 0, MUZZLE_RAY_FLAGS,
                              0))
        return false;
    Collide_FreeHitList(&hits);
    return true;
}

// ---- how well a drone sees

// 1 when the object is within 2 m, or within 50 m, the view angle (of the head's facing) and the view range;
// otherwise 0. No line of sight is tested.
// AUTOINJECT
float DroneVision_ObjectVisibility(Drone_tag *drone, obj_tag *obj) {
    if (drone == NULL || obj == NULL)
        return 0.0f;
    _VECTOR toObj;
    Vec_Subtract(&obj->position, &drone->gameObj->position, &toObj);
    double length = Vec_Magnitude(&toObj);
    float distance = (float)length;
    if (length < 2.0f)
        return 1.0f;
    double bearing = Vec_AngleDifference(drone->headYaw, maybeAtan2(toObj.x, toObj.z));
    if (distance > 50.0f)
        return 0.0f;
    if (drone->viewAngle != 0.0f && drone->viewAngle - AbsByCompare(bearing) < 0.0)
        return 0.0f;
    if (distance > drone->viewRange)
        return 0.0f;
    return 1.0f;
}

// How well the drone sees its opponent, kept in opponentVisibility: 0.7 at the view range scaled by 1 + 2 x
// alertness, more nearer; outside the view angle plus pi/8 nothing, and towards its edge less, as a sine; times
// the AI volumes' scale at the opponent unless the drone knows something and is fully alert. Within 2 m a line of
// sight is enough. The value is returned unrounded, as the original leaves it in ST0.
double _DroneVision_OpponentVisibility(Drone_tag *drone) {
    float halfAngle = drone->viewAngle + kViewMargin;
    float yaw = drone->headYaw;
    if (MPSettings.maybeDroneAIEnabled)
        yaw = drone->gameObj->rotation.y;
    float bearing = Vec_AngleDifference(yaw, drone->opponentAngles.y);
    if (drone->opponent == NULL) {
        drone->opponentVisibility = 0.0f;
        return 0.0f;
    }
    if (drone->distanceToTarget < 2.0f && drone->viewRange > 0.0f &&
        NDrone2_CanSeeObject(drone, drone->opponent, HEAD_BONE, 0)) {
        drone->opponentVisibility = 1.0f;
        return 1.0f;
    }

    // Where two NaNs can meet, the x87's choice between them is kept (X87NaNs)
    double alertScale = (double)drone->alertness + drone->alertness + 1.0f;
    double range = X87NaNs(alertScale * drone->viewRange, alertScale, drone->viewRange);
    double beyond = X87NaNs(drone->distanceToTarget - range, drone->distanceToTarget, range);
    double ratio = X87NaNs(beyond / range, beyond, range);
    // Negated, then 0.7 added, as the original has it (a NaN's sign)
    double visibility = Negate(ratio) + 0.7f;
    if (drone->viewAngle != 0.0f) {
        double offAngle = AbsByCompare(bearing);
        double margin = X87NaNs(halfAngle - offAngle, halfAngle, offAngle);
        if (margin < 0.0) {
            drone->opponentVisibility = 0.0f;
            return 0.0f;
        }
        double edge = X87NaNs(margin / drone->viewAngle, margin, drone->viewAngle);
        visibility = (double)(visibility * FSin(edge * kHalfPi));
    }
    float scale = 1.0f;
    if ((drone->flags2 == 0 || drone->alertness < 1.0f) && drone->opponentVisibilityScale != 1.0f)
        scale = drone->opponentVisibilityScale;
    visibility = X87NaNs(visibility * scale, visibility, scale);
    // Written so that a -0 becomes +0 too: MSVC compiles the plain `<= 0` clamp to a MAXSD, which keeps -0
    if (!(visibility > 0.0) && visibility == visibility)
        visibility = 0.0;
    drone->opponentVisibility = (float)visibility;
    return visibility;
}

#ifndef NIGHTFIRE_STANDALONE
// The original takes the drone in ESI and returns in ST0.
// AUTOLTCG
void __declspec(naked) DroneVision_OpponentVisibility(void) {
    _asm {
        push esi
        call _DroneVision_OpponentVisibility
        add esp, 4
        ret
    }
}
#endif

// Whether the drone may see its opponent this frame: it has one, the drones are not blinded, it is alive, the
// opponent is within range (when range is above 0) and well enough seen (0.7). Otherwise opponentVisibility is 0.
// Clears DRONE_SIGHT_SEES_OPPONENT either way. Our name.
bool _DroneVision_OpponentInView(Drone_tag *drone, float range) {
    drone->sightFlags &= ~DRONE_SIGHT_SEES_OPPONENT;
    if (drone->opponent != NULL && !switch_BLIND_DRONES && !(drone->health <= 0.0f) && !NPCGlobals.unknown1390 &&
        !(range > 0.0f && drone->distanceToTarget > range) && !(_DroneVision_OpponentVisibility(drone) < 0.7f))
        return true;
    drone->opponentVisibility = 0.0f;
    return false;
}

#ifndef NIGHTFIRE_STANDALONE
// The original takes the drone in EAX and the range on the stack.
// AUTOLTCG
void __declspec(naked) DroneVision_OpponentInView(float range) {
    _asm {
        push dword ptr [esp + 4]
        push eax
        call _DroneVision_OpponentInView
        add esp, 8
        ret
    }
}
#endif

// The line of sight to the opponent. Outside multiplayer, and for all but civilians, one of Drone_View_Bones a call,
// in turn, back to the first as soon as it is seen; while aiming, whether the gun is through a wall. Our name (the
// original's part of DroneVision_SeekOpponentLOS).
// FUNC_AT(0x00066030)
bool DroneVision_OpponentLOS(Drone_tag *drone) {
    obj_tag *opponent = drone->opponent;
    if (opponent == NULL)
        return false;
    if (MPSettings.maybeDroneAIEnabled || drone->side == 3)
        return NDrone2_CanSeeObject(drone, opponent, HEAD_BONE, 0);

    bool seen = false;
    if (drone->sightFlags & DRONE_SIGHT_HAD_LOS) {
        if (drone->viewBone != 0 && NDrone2_CanSeeObject(drone, opponent, Drone_View_Bones[0], 0)) {
            drone->viewBone = 0;
            seen = true;
        }
    } else {
        drone->viewBone++;
        if (drone->viewBone > 6)
            drone->viewBone = 0;
    }
    if (!seen)
        seen = NDrone2_CanSeeObject(drone, drone->opponent, Drone_View_Bones[drone->viewBone], 0);
    if (seen && drone->aiming) {
        drone->gunThroughWall = DroneVision_GunThroughWall(drone);
        return seen;
    }
    drone->gunThroughWall = 0;
    drone->unknown1d = 0;
    return seen;
}

// Looks for the opponent once it is seen well enough (0.7): DRONE_SIGHT_LOS when there is a line of sight, the last
// frame's in DRONE_SIGHT_HAD_LOS. A bot takes BOT_PLAYER_IN_SIGHT from what it knows of the opponent's player, while
// that is BOT_PLAYER_PRESENT. With secondEyeFlags 0x30000000 it looks from secondEyePos too, and ignores what it
// sees.
bool _DroneVision_SeekOpponentLOS(Drone_tag *drone) {
    uint flags = drone->sightFlags;
    if (flags & DRONE_SIGHT_LOS)
        flags |= DRONE_SIGHT_HAD_LOS;
    else
        flags &= ~DRONE_SIGHT_HAD_LOS;
    drone->sightFlags = flags & ~DRONE_SIGHT_LOS;
    if (drone->opponentVisibility < 0.7f || drone->opponent == NULL)
        return false;

    bool seen = false;
    bool known = false;
    if (MPSettings.maybeDroneAIEnabled) {
        short player = Control_Plr2Ind(drone->opponent);
        if (player != -1) {
            uchar record = drone->botVars->players[player].flags;
            if (record & BOT_PLAYER_PRESENT) {
                seen = (record & BOT_PLAYER_IN_SIGHT) != 0;
                known = true;
            }
        }
    }
    if (!known)
        seen = DroneVision_OpponentLOS(drone);
    if ((drone->secondEyeFlags & 0x20000000) && (drone->secondEyeFlags & 0x10000000))
        DroneVision_CanSeeObjectFrom(drone, drone->opponent, drone->viewBone, &drone->secondEyePos,
                                     drone->secondEyeCel, 0);
    if (!seen)
        return false;
    drone->sightFlags |= DRONE_SIGHT_LOS;
    return true;
}

#ifndef NIGHTFIRE_STANDALONE
// The original takes the drone in ESI.
// AUTOLTCG
void __declspec(naked) DroneVision_SeekOpponentLOS(void) {
    _asm {
        push esi
        call _DroneVision_SeekOpponentLOS
        add esp, 4
        ret
    }
}
#endif

// The first time an enemy (or, in TowerA and TowerB, a civilian) sees the player, it counts in the player's stats;
// in CastleIndoors1 only while the player is armed
// AUTOINJECT
void DroneVision_LogSeenOpponent(Drone_tag *drone) {
    if (drone->opponent != glb_players[0] || drone->seenPlayerLogged)
        return;
    if (drone->side == 2)
        return;
    if (drone->side == 3 && GameState.CurrentLevelHashcode != HT_Level_TowerA &&
        GameState.CurrentLevelHashcode != HT_Level_TowerB)
        return;
    if (GameState.CurrentLevelHashcode == HT_Level_CastleIndoors1 && !NDrone2_ObjectIsArmed(drone->opponent))
        return;
    drone->seenPlayerLogged = 1;
    PlrStat_LogEnemyDetectedPlayer(0);
}

// The drone registers its opponent, after the reaction time outside multiplayer: DRONE_SIGHT_SEES_OPPONENT, when and
// where it was seen. The first sighting (with DRONE_PROP_3C) sends the drone DRONE_MSG_OpponentSighted; a
// DTYPE_HostageKiller also DRONE_MSG_AttackNow 8 seconds later and the music; with DRONE_PROP_55 the mission fails
// (reason 10). A bot's second frame of sight chooses its weapon and range.
void _DroneVision_HaveOpponentSight(Drone_tag *drone) {
    if (drone->opponent == NULL)
        return;
    uint now = GameState.NumFramesUnpaused;
    if (!MPSettings.maybeDroneAIEnabled && drone->sightFrames < DroneFunc_ReactionTime(drone))
        return;
    DCVars_tag dcv;
    Drone_DCVfromOBJ(drone->gameObj, &dcv);
    drone->sightFlags |= DRONE_SIGHT_SEES_OPPONENT;
    DroneVision_LogSeenOpponent(drone);
    if (MPSettings.maybeDroneAIEnabled && drone->sightFrames == 2) {
        BOTSTATE_combatWeaponChangeChoice(drone, drone->opponent, 1, 0, 0);
        if (drone->opponent != NULL && drone->botVars->weapon == Weap_Satchel &&
            BOTSTATE_opponentIsMissile(drone, (obj_tag *)-1))
            BOTSTATE_reallyCloseCombatRange(drone);
        else
            BOTSTATE_defaultCombatRange(drone);
    }
    if (drone->opponent == NULL)
        return;

    drone->opponentSeenFrame = now;
    Vec_Copy(&drone->opponent->position, &drone->opponentSeenPos);
    Vec_Copy(&drone->opponent->rotation, &drone->opponentSeenRot);
    if ((drone->sightFlags & DRONE_SIGHT_REACTED) || !behaviour_util_getProperty(DRONE_PROP_3C, drone->currentBehaviour))
        return;
    drone->sightFlags |= DRONE_SIGHT_REACTED;
    drone->firstSeenFrame = now;
    Drone_SM_SendMsgSelf(DRONE_MSG_OpponentSighted, NULL, 0, 0, &dcv);
    if (drone->dtype == DTYPE_HostageKiller) {
        Drone_SM_SendMsgSelf(DRONE_MSG_AttackNow, NULL, FRAME_RATE_INT * 8, 0, &dcv);
        Music_Event(0xf, 1);
    }
    if (behaviour_util_getProperty(DRONE_PROP_55, drone->currentBehaviour))
        DroneFunc_SetMissionFailReason(drone, 10);
}

#ifndef NIGHTFIRE_STANDALONE
// The original takes the drone in ESI.
// AUTOLTCG
void __declspec(naked) DroneVision_HaveOpponentSight(void) {
    _asm {
        push esi
        call _DroneVision_HaveOpponentSight
        add esp, 4
        ret
    }
}
#endif

// ---- drones alerting each other

// The drone with this state machine id, looking from `from` (or the first) on; the first if none has it. Our name.
Drone_tag *_DroneVision_FindDrone(Drone_tag *from, uint id) {
    if (NPCGlobals.NDrone2Count == 0)
        return NULL;
    Drone_tag *first = NPCGlobals.NDrone2List;
    if (from == NULL)
        from = first;
    for (Drone_tag *drone = from; drone != NULL; drone = drone->next)
        if (drone->sm.id == id)
            return drone;
    return first;
}

#ifndef NIGHTFIRE_STANDALONE
// The original takes `from` in EAX and the id in EDX.
// AUTOLTCG
void __declspec(naked) DroneVision_FindDrone(void) {
    _asm {
        push edx
        push eax
        call _DroneVision_FindDrone
        add esp, 8
        ret
    }
}
#endif

// The drone after the one with this id (looking from `from`, or the first, on); the first when that is the last or
// none has it. Our name.
// AUTOLTCG
Drone_tag *__fastcall DroneVision_DroneAfter(Drone_tag *from, uint id) {
    Drone_tag *first = NPCGlobals.NDrone2List;
    if (NPCGlobals.NDrone2Count == 0)
        return first;
    Drone_tag *drone = from != NULL ? from : first;
    while (drone != NULL && drone->sm.id != id)
        drone = drone->next;
    if (drone == NULL)
        drone = first;
    if (drone != NULL && drone->next != NULL)
        return drone->next;
    return first;
}

// Whether the drone sees the other: within 50 m, in view (DroneVision_ObjectVisibility; inView is then set) and a
// line of sight to its head
// AUTOINJECT
bool DroneVision_DroneCanSeeDrone(Drone_tag *drone, Drone_tag *other, char *inView) {
    obj_tag *otherObj = other->gameObj;
    if (Vec_SqDist3D(&drone->gameObj->position, &otherObj->position) > 2500.0f)
        return false;
    if (DroneVision_ObjectVisibility(drone, otherObj) < 0.7f)
        return false;
    if (inView != NULL)
        *inView = 1;
    return NDrone2_CanSeeObject(drone, other->gameObj, HEAD_BONE, 0);
}

// Whether what the alerter knows (its flags2) is something the drone's behaviour lets it be alerted by
static bool AlertedByWhatItKnows(Drone_tag *drone, uint known) {
    uint *behaviour = drone->currentBehaviour;
    if (known & 8) {
        if ((behaviour_util_getProperty(DRONE_PROP_26, behaviour) || behaviour_util_getProperty(DRONE_PROP_29, behaviour)) &&
            (known & 2) && behaviour_util_getProperty(DRONE_PROP_27, behaviour))
            return true;
        if (behaviour_util_getProperty(DRONE_PROP_29, behaviour))
            return true;
    }
    if (known & 0x10)
        return true;
    if ((known & 2) && behaviour_util_getProperty(DRONE_PROP_27, behaviour))
        return true;
    if ((known & 4) && behaviour_util_getProperty(DRONE_PROP_4F, behaviour))
        return true;
    if ((known & 1) && behaviour_util_getProperty(DRONE_PROP_28, behaviour) && (known & 0x20))
        return true;
    if ((known & 0x10000) && behaviour_util_getProperty(DRONE_PROP_28, behaviour) && (known & 0x200000))
        return true;
    return (known & 1) && behaviour_util_getProperty(DRONE_PROP_28, behaviour);
}

// Whether the alerter alerts the drone. The drone must be open to it (flag 0x2000, not an ally or a hostage, alive,
// not alerted yet, not in some states) and the alerter attacking for 60 x FRAME_RATE_DIV frames. In the
// Powerstation levels skin classes 5 and 6 are alerted outright (with line 0xd0); otherwise the drone must be open to
// what the alerter knows and see it. Alerted: flag 0x20000, the alerter's flags2 moved up 16 bits into its own
// (in CastleIndoors1 its high half too), alertedBy and DRONE_MSG_ConsideredAlerted. Returns whether the alerter was
// in view.
bool _DroneVision_ConsiderAlerted(Drone_tag *drone, Drone_tag *alerter) {
    uint flags = drone->flags;
    char inView = 0;
    if (!(flags & 0x2000) || drone->side == 2 || drone->dtype == DTYPE_Hostage ||
        MPDrone_MaybeIsDyingOrDead(drone->gameObj))
        return false;
    HASHCODE level = GameState.CurrentLevelHashcode;
    switch (drone->sm.curState) {
    case DSTATE_PlayScript:
        if (drone->alertness >= 1.0f)
            return false;
        break;
    case DSTATE_EnemyMission:
        if (level == HT_Level_EvilBase)
            return false;
        break;
    case DSTATE_InterogateAssist:
    case DSTATE_InterogateAssistWait:
    case DSTATE_GoToGoalPosition:
    case DSTATE_HostageGoToGoalPosition:
    case DSTATE_EnemyRunToPoint:
        return false;
    }
    if (flags & 0x20000)
        return false;
    // On the x87: the frames are converted unsigned
    if ((double)GameState.NumFramesUnpaused < (double)alerter->alertedFrame + (double)FRAME_RATE_DIV * 60.0f)
        return false;

    if (level >= HT_Level_PowerStationA1 && level <= HT_Level_PowerStationA2 &&
        (drone->skinClass == 6 || drone->skinClass == 5)) {
        if (!Drone_IsTalking(drone))
            NDrone2_SpeechSFXd(drone, 0xd0, 0, 0, 0);
    } else {
        if (level == HT_Level_CastleIndoors1 && NPCGlobals.unknown1392) {
            drone->viewRange = 20.0f;
            drone->viewAngle = kPi;
        }
        if (!AlertedByWhatItKnows(drone, alerter->flags2))
            return false;
        if (!DroneVision_DroneCanSeeDrone(drone, alerter, &inView))
            return inView;
    }

    drone->flags |= 0x20000;
    uint known = alerter->flags2 << 16;
    if (GameState.CurrentLevelHashcode == HT_Level_CastleIndoors1)
        known |= alerter->flags2 & 0xffff0000;
    drone->flags2 |= known;
    drone->alertedBy = alerter;
    Drone_Message(drone->gameObj, DRONE_MSG_ConsideredAlerted, NULL, 0);
    return inView;
}

#ifndef NIGHTFIRE_STANDALONE
// The original takes the drone in ESI and the alerter in EDI.
// AUTOLTCG
void __declspec(naked) DroneVision_ConsiderAlerted(void) {
    _asm {
        push edi
        push esi
        call _DroneVision_ConsiderAlerted
        add esp, 8
        ret
    }
}
#endif

// A drone knows something it can pass on: running, not an ally, not alerted by another and some flags2
static bool KnowsSomething(Drone_tag *drone) {
    if (drone->flags & 0x20000)
        return false;
    if (!(drone->flags & 0x100))
        return false;
    if (drone->side == 2)
        return false;
    return drone->flags2 != 0;
}

// Of the two, the one that knows something may alert the other (not when both do)
bool _DroneVision_DetermineAlertCheck(Drone_tag *a, Drone_tag *b) {
    bool aKnows = KnowsSomething(a);
    bool bKnows = KnowsSomething(b);
    if (aKnows) {
        if (bKnows)
            return false;
        return _DroneVision_ConsiderAlerted(b, a);
    }
    if (!bKnows)
        return false;
    return _DroneVision_ConsiderAlerted(a, b);
}

#ifndef NIGHTFIRE_STANDALONE
// The original takes a in EDX and b in EAX.
// AUTOLTCG
void __declspec(naked) DroneVision_DetermineAlertCheck(void) {
    _asm {
        push eax
        push edx
        call _DroneVision_DetermineAlertCheck
        add esp, 8
        ret
    }
}
#endif

// Pairs of drones (each with those after it in the list) until five have been in each other's view; the next frame
// goes on from the pair it stopped at (NPCGlobals.alertCursor), counting a row for each pair of that one, then
// starts the rows again from the first drone. Not in HendersonD or SpaceStationD.
// AUTOINJECT
void DroneVision_FindAlertedDrones(void) {
    ushort count = NPCGlobals.NDrone2Count;
    if (count == 0)
        return;
    Drone_tag *a = NPCGlobals.NDrone2List;
    bool stop = false;
    ushort rows = 0;
    ushort found = 0;
    if (a == NULL)
        return;
    if (GameState.CurrentLevelHashcode == HT_Level_HendersonD || GameState.CurrentLevelHashcode == HT_Level_SpaceStationD)
        return;

    Drone_tag *b;
    if (NPCGlobals.alertCursor[0] == 0 && NPCGlobals.alertCursor[1] == 0) {
        b = a->next;
    } else {
        a = _DroneVision_FindDrone(a, NPCGlobals.alertCursor[0]);
        for (b = _DroneVision_FindDrone(a->next, NPCGlobals.alertCursor[1]); b != NULL && !stop; b = b->next) {
            if (_DroneVision_DetermineAlertCheck(a, b))
                found++;
            if (found > 4)
                stop = true;
            rows++;
        }
        a = NPCGlobals.NDrone2List;
    }

    for (; a != NULL; a = a->next) {
        if (rows >= count)
            return;
        if (stop)
            break;
        for (b = a->next; b != NULL && !stop; b = b->next) {
            if (_DroneVision_DetermineAlertCheck(a, b))
                found++;
            if (found > 4)
                stop = true;
        }
        if (stop)
            break;
        rows++;
    }
    if (rows >= count)
        return;
    NPCGlobals.alertCursor[0] = a != NULL ? a->sm.id : 0;
    NPCGlobals.alertCursor[1] = b != NULL ? b->sm.id : 0;
}

// ---- each frame

// Once the drones have been loaded and enabled 15 frames: the alerts between drones; each drone's visibility where
// it stands, its eyes and how well it sees its opponent; lines of sight in turn from the drone after the last one
// tested, until the first frame's test that costs one (NPCGlobals.visionTests); and each drone's frames with and
// without sight of its opponent, with the "seen" and "lost" lines.
// AUTOINJECT
void DroneVision_ProcessDroneSight(void) {
    ushort left = NPCGlobals.NDrone2Count;
    if (left == 0)
        return;
    uint now = GameState.NumFramesUnpaused;
    if (NPCGlobals.postLoadFrame + 15 > now || NPCGlobals.enabledFrame + 15 > now)
        return;

    DroneVision_FindAlertedDrones();
    ushort tests = NPCGlobals.visionTests;
    for (Drone_tag *drone = NPCGlobals.NDrone2List; drone != NULL; drone = drone->next) {
        obj_tag *obj = drone->gameObj;
        if (MPDrone_MaybeIsDyingOrDead(obj))
            continue;
        drone->positionVisibility = Drone_VisibilityForPosition(&obj->position, 0);
        if (drone->gameObj != NULL)
            NDrone2_GetHeadPosOld(drone->gameObj, &drone->headPos, &drone->headYaw);
        if ((drone->flags & 0x10000) && drone->opponent != NULL)
            _DroneVision_OpponentInView(drone, 0.0f);
    }

    Drone_tag *tested = DroneVision_DroneAfter(NPCGlobals.NDrone2List, NPCGlobals.sightCursor);
    for (;;) {
        if (tested == NULL)
            tested = NPCGlobals.NDrone2List;
        NPCGlobals.sightCursor = tested->sm.id;
        uint frame = GameState.NumFramesUnpaused;
        if (!MPDrone_MaybeIsDyingOrDead(tested->gameObj) && (tested->flags & 0x10000) && tested->opponent != NULL) {
            _DroneVision_SeekOpponentLOS(tested);
            tested->sightTestFrame = frame;
        }
        left--;
        if (left == 0 || NPCGlobals.visionTests > tests)
            break;
        tested = tested->next;
    }

    for (Drone_tag *drone = NPCGlobals.NDrone2List; drone != NULL; drone = drone->next) {
        if (!MPDrone_MaybeIsDyingOrDead(drone->gameObj) && (drone->flags & 0x10000) && drone->opponent != NULL &&
            (drone->sightFlags & DRONE_SIGHT_LOS)) {
            drone->unseenFrames = 0;
            drone->sightFrames++;
            _DroneVision_HaveOpponentSight(drone);
            NPCGlobals.seenOpponentCount++;
            if (drone->sightFrames > FRAME_RATE_INT && !NPCGlobals.newSightSaid) {
                DCVars_tag dcv;
                Drone_DCVfromOBJ(drone->gameObj, &dcv);
                DroneFunc_NewSightTalk(&dcv);
                NPCGlobals.newSightSaid = 1;
                NPCGlobals.lostSightSaid = 0;
            }
        } else {
            uint flags = drone->sightFlags & ~DRONE_SIGHT_SEES_OPPONENT;
            drone->sightFlags = flags;
            drone->unseenFrames++;
            drone->sightFrames = 0;
            drone->unknown41 = 1;
            if ((flags & DRONE_SIGHT_REACTED) && drone->unseenFrames > FRAME_RATE_INT * 2) {
                NPCGlobals.lostOpponentCount++;
                if (!NPCGlobals.lostSightSaid && NPCGlobals.unknown174 == 0) {
                    DCVars_tag dcv;
                    Drone_DCVfromOBJ(drone->gameObj, &dcv);
                    DroneFunc_LostSightTalk(&dcv);
                    NPCGlobals.lostSightSaid = 1;
                    NPCGlobals.newSightSaid = 0;
                }
            }
        }
    }
}
