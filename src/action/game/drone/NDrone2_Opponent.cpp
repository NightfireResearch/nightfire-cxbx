// A drone's opponent. NDrone2_FindOpponent picks it each frame (NDrone2_ControlSTANDARD): for enemies and civilians
// the player, for allies a helicopter in view or the nearest enemy drone they see, for bots the cheapest player by
// what the bot knows of each (BOT_vars_t.players). NDrone2_SetOpponent sets it, with the point aimed at
// (NDrone2_SetOpponentAimPos) and the opponent's entry in the targets the drones share (NPCGlobals.targets). Also the
// alerts a drone broadcasts to the others (NPCGlobals.alert).

#include "NDrone2_Opponent.h"
#include "NDrone2.h"
#include "BOT.h"
#include "Behaviour.h"             // behaviour_util_getProperty
#include "DroneTables.h"           // DRONE_PROP
#include "../../game.h"            // GameState, MPGame
#include "../obj/object.h"
#include "../obj/Player.h"         // BLData
#include "../obj/control.h"        // control_first_object
#include "../mp/multiplayer.h"     // MPSettings, MP_getObjectTeam
#include "../sp/PlayerStats.h"     // PlrStat_OkToUpdate
#include "../../engine/Anim.h"     // AnimGetBoneWorldTrans
#include "../../math/math.h"
#include "../../util/Random.h"
#include "DroneVision.h"

#include <bit>
#include <math.h>
#include <stdint.h>
#include "BOTSTATE.h"

#pragma fp_contract(off)

// The bones a drone's line of sight tests in turn (Drone_tag.viewBone)

// A bone's world position rather than its parent-relative one (AnimGetBoneWorldTrans)
#define WORLD_BONE(bone) (0x80000000 | (bone))
#define AIM_BONE 2

// MP_sendBotMessage's messages
#define BOT_MSG_OBJECTIVE_SEEN 0x3b
#define BOT_MSG_NEW_OPPONENT 0x45

// The cost NDrone2_FindOpponent's bots give a player they do not consider
#define NO_COST 640000.0f

// The frames before the drones receive an alert
#define ALERT_DELAY 30

constexpr float kPi = 3.1415927f;
constexpr float kRadToDeg = 57.295776f;
constexpr float kObjectiveAimHeight = 1.75f;   // above a multiplayer objective's position
constexpr float kAimTowardsHead = 0.2857143f;  // 2/7 of the way from bone 2 to the head
constexpr float kCopterAbove = 0.17453294f;    // the pitches an ally takes a helicopter at
constexpr float kCopterBelow = -0.5235988f;
static_assert(std::bit_cast<uint32_t>(kPi) == 0x40490fdb, "not the original's constant");
static_assert(std::bit_cast<uint32_t>(kRadToDeg) == 0x42652ee0, "not the original's constant");
static_assert(std::bit_cast<uint32_t>(kAimTowardsHead) == 0x3e924925, "not the original's constant");
static_assert(std::bit_cast<uint32_t>(kCopterAbove) == 0x3e32b8c3, "not the original's constant");
static_assert(std::bit_cast<uint32_t>(kCopterBelow) == 0xbf060a92, "not the original's constant");
static_assert(std::bit_cast<uint32_t>(0.04f) == 0x3d23d70a, "not the original's constant");

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

// How far, in degrees, obj lies off the way `from` faces. The original leaves the product unrounded.
static double FacingDegrees(obj_tag *from, obj_tag *obj) {
    _VECTOR away;
    Vec_Subtract(&from->position, &obj->position, &away);
    return Vec_AngleDifference(from->rotation.y, maybeAtan2(away.x, away.z) + kPi) * (double)kRadToDeg;
}

// ---- the targets the drones share

// The target entry for obj: the one it has, else the first free one, cleared. Only enemies get one.
// AUTOINJECT
uchar DroneFunc_AllocateTargetID(Drone_tag *drone, obj_tag *obj) {
    if (drone->side != 1)
        return NO_TARGET_SLOT;
    for (uchar i = 0; i < MAX_DRONE_TARGETS; i++) {
        if ((NPCGlobals.targets[i].flags & DRONE_TARGET_USED) && NPCGlobals.targets[i].obj == obj)
            return i;
    }
    for (uchar i = 0; i < MAX_DRONE_TARGETS; i++) {
        DroneTarget_tag *target = &NPCGlobals.targets[i];
        if (!(target->flags & DRONE_TARGET_USED)) {
            target->seenFrame = 0;
            target->pos.x = 0.0f;
            target->pos.y = 0.0f;
            target->pos.z = 0.0f;
            target->flags = DRONE_TARGET_USED;
            target->obj = obj;
            return i;
        }
    }
    return NO_TARGET_SLOT;
}

// The drone's target was seen at pos, now
// AUTOINJECT
void DroneFunc_TargetSeen(Drone_tag *drone, _VECTOR *pos) {
    if (drone->targetSlot == NO_TARGET_SLOT)
        return;
    DroneTarget_tag *target = &NPCGlobals.targets[drone->targetSlot];
    target->flags |= DRONE_TARGET_SEEN;
    target->seenFrame = GameState.NumFramesUnpaused;
    Vec_Copy(pos, &target->pos);
}

// Each frame before the drones look (Drone_InitComms): every target in use follows its object, and one whose object
// is gone is freed. Our name; nightfire-port has it inline in Drone_InitComms.
// FUNC_AT(0x00031190)
void DroneFunc_UpdateTargets(void) {
    for (int i = 0; i < MAX_DRONE_TARGETS; i++) {
        DroneTarget_tag *target = &NPCGlobals.targets[i];
        if (!(target->flags & DRONE_TARGET_USED))
            continue;
        if (target->obj == NULL) {
            target->flags = 0;
        } else {
            Vec_Copy(&target->obj->position, &target->pos);
            target->seenFrame = GameState.NumFramesUnpaused;
        }
    }
}

// After the opponents are worked out (Drone_InitComms): every target is freed, its position left. Our name;
// nightfire-port has it inline in Drone_InitComms.
// FUNC_AT(0x00031110)
void DroneFunc_ClearTargets(void) {
    for (int i = 0; i < MAX_DRONE_TARGETS; i++) {
        NPCGlobals.targets[i].flags = 0;
        NPCGlobals.targets[i].obj = NULL;
        NPCGlobals.targets[i].seenFrame = 0;
    }
}

// ---- the opponent

// A bone of an animated drone (flag 0x100000) or player (true); else the object's position, when pos is given.
// Only NDrone2_SetOpponentAimPos calls it (our name). A NULL object with pos given reads its position from address
// 0x24, as in the original; neither call can pass one.
// FUNC_AT(0x00037870)
bool NDrone2_OpponentBonePos(obj_tag *gameObj, uint bone, _VECTOR *pos) {
    if (gameObj != NULL && gameObj->animState != NULL
        && (gameObj->objectType == OBJECTTYPE_DRONE || gameObj->objectType == OBJECTTYPE_PLAYER)
        && (gameObj->objectType != OBJECTTYPE_DRONE || (((Drone_tag *)gameObj->extraObjectData)->flags & 0x100000))
        && (gameObj->animState->animObj.flags & ANIMOBJ_POSE_VALID)) {
        AnimGetBoneWorldTrans(gameObj, WORLD_BONE(bone), 0, pos, NULL);
        return true;
    }
    if (pos != NULL)
        Vec_Copy(&gameObj->position, pos);
    return false;
}

// Where on the opponent the drone aims (aimOffset, from the opponent's position): the bone its line of sight tests
// (bone 2 until it has one). A bot aims at bone 2 raised 2/7 of the way to the head, and at the head itself one time
// in four (with trait 1) or in four times its accuracy.
// AUTOINJECT
void NDrone2_SetOpponentAimPos(Drone_tag *drone) {
    obj_tag *opponent = drone->opponent;
    if (opponent == NULL)
        return;
    if (MPSettings.maybeDroneAIEnabled) {
        NDrone2_GetHeadPos(opponent, &drone->aimOffset, NULL);
        uint odds = (drone->botVars->stats.traitFlags & 1) ? 4 : drone->accuracy * 4;
        _VECTOR head;
        if (Rand_Rand(odds) != odds - 1 && NDrone2_OpponentBonePos(drone->opponent, AIM_BONE, &head)) {
            Vec_Swap(&head, &drone->aimOffset);
            double towardsHead = ((double)drone->aimOffset.y - head.y) * kAimTowardsHead;
            drone->aimOffset.y = (float)(drone->aimOffset.y - towardsHead);
        }
    } else {
        uint bone = (drone->sightFlags & 2) ? Drone_View_Bones[drone->viewBone] : AIM_BONE;
        NDrone2_OpponentBonePos(opponent, bone, &drone->aimOffset);
    }
    Vec_Subtract(&drone->aimOffset, &drone->opponent->position, &drone->aimOffset);
}

// The rotation that points the drone at obj; false (and nothing written) without rot. Only an ally's
// NDrone2_FindOpponent calls it (our name).
// FUNC_AT(0x00037bc0)
bool NDrone2_AnglesToObj(Drone_tag *drone, obj_tag *obj, _VECTOR *rot) {
    if (rot == NULL)
        return false;
    Vec_Zero(rot);
    _VECTOR dir;
    Vec_Subtract(&obj->position, &drone->gameObj->position, &dir);
    Vec_DirToRot(&dir, rot);
    return true;
}

// A new opponent, or none: the point aimed at (1.75 m above a multiplayer objective), targetPos on it (on where it
// was last seen, with property 0x17, when it stays the same and has been out of sight over 5 s since a first
// sighting), and its target entry, seen now.
// AUTOINJECT
void NDrone2_SetOpponent(Drone_tag *drone, obj_tag *opponent) {
    bool same = drone->opponent == opponent;
    drone->opponent = opponent;
    if (MPSettings.maybeDroneAIEnabled)
        MP_sendBotMessage(drone->gameObj, BOT_MSG_NEW_OPPONENT, (uintptr_t)opponent, 0);
    if (opponent == NULL) {
        Vec_Zero(&drone->aimOffset);
        drone->targetSlot = NO_TARGET_SLOT;
        return;
    }

    switch (opponent->objectType) {
    case OBJECTTYPE_GFX:
        break;
    case OBJECTTYPE_DRONE:
    case OBJECTTYPE_PLAYER:
        NDrone2_SetOpponentAimPos(drone);
        break;
    case OBJECTTYPE_COPTER:
        Vec_Zero(&drone->aimOffset);
        same = false;
        break;
    default:
        Vec_Zero(&drone->aimOffset);
        if (MPSettings.maybeDroneAIEnabled
            && (MPSettings.GameMode == GM_PROTECTION || MPSettings.GameMode == GM_DEMOLITION))
            drone->aimOffset.y = kObjectiveAimHeight;
        break;
    }

    _VECTOR *aimAt = &opponent->position;
    if (same && drone->firstSeenFrame != 0 && (double)FRAME_RATE_DIV * 5.0f < (double)drone->unseenFrames
        && behaviour_util_getProperty(DRONE_PROP_17, drone->currentBehaviour) != 0)
        aimAt = &drone->opponentSeenPos;
    Vec_Add2(aimAt, &drone->aimOffset, &drone->targetPos);

    drone->targetSlot = DroneFunc_AllocateTargetID(drone, opponent);
    if (drone->targetSlot != NO_TARGET_SLOT)
        DroneFunc_TargetSeen(drone, &opponent->position);
}

// A bot's choice (NDrone2_FindOpponent with bots on): it drops an opponent out of sight for 20 s (times its
// aggression), then takes the cheapest player it knows to be there, in range and in sight: the distance squared,
// cheaper for its preferred opponent, for a player with any of MPGame's bits 0-3, and for one active nearly facing
// it; dearer for a Berserker's or Guardian's when another bot has that player already. One outside its view angle
// is passed over unless the bot is distracted by it or at its objective. Failing that, the protection or demolition
// objective of its team, within 4 m, in view and in sight. Part of NDrone2_FindOpponent in the original (our split
// and name).
static obj_tag* NDrone2_FindBotOpponent(Drone_tag *drone) {
    obj_tag *me = drone->gameObj;
    if (MPDrone_MaybeIsDyingOrDead(me)) {
        if (drone->opponent != NULL)
            NDrone2_SetOpponent(drone, NULL);
        return NULL;
    }

    BOT_vars_t *bot = drone->botVars;
    int bestIndex = -1;
    obj_tag *best = NULL;
    MPTeam team = MP_getObjectTeam(me);
    ushort botIndex = bot->botIndex;
    float viewDegrees = drone->viewAngle * kRadToDeg;
    bool keepAny = drone->botVars->atObjective;
    float rangeSq = drone->viewRange * drone->viewRange;
    // The slot is read signed
    obj_tag *preferred = bot->preferredOpponent == 0xff ? NULL : MPGame.players[(int8_t)bot->preferredOpponent].playerObj;
    uchar attacksOnSight = drone->botVars->stats.traitFlags & BOT_TRAIT_ATTACK_ON_SIGHT;
    if (preferred != NULL && preferred->objectType != OBJECTTYPE_PLAYER && preferred->objectType != OBJECTTYPE_DRONE) {
        preferred = NULL;
        bot->preferredOpponent = 0xff;
    }
    DCVars_tag dcv;
    Drone_DCVfromOBJ(me, &dcv);     // filled and not used
    drone->botVars->atObjective = 0;
    if (attacksOnSight) {
        keepAny = true;
        rangeSq = 90000.0f;
    }

    obj_tag *opponent = drone->opponent;
    if (opponent != NULL) {
        bool lost = opponent->objectType == OBJECTTYPE_GFX
                    || (opponent->objectType == OBJECTTYPE_DRONE && MPDrone_MaybeIsDyingOrDead(opponent));
        if (!lost) {
            double patience = (double)_FRAME_RATE * 20.0f * BOT_getAggressionMul(drone);
            lost = drone->unseenFrames > (uint)Ftol(patience);
        }
        if (lost && ((MPSettings.GameMode != GM_PROTECTION && MPSettings.GameMode != GM_DEMOLITION)
                     || drone->opponent->objectType == OBJECTTYPE_DRONE
                     || drone->opponent->objectType == OBJECTTYPE_PLAYER))
            NDrone2_SetOpponent(drone, NULL);
    }

    float costs[NUM_AGENTS];
    bool distracted[NUM_AGENTS];
    float bestCost = NO_COST;
    for (int i = 0; i < NUM_AGENTS; i++) {
        obj_tag *player = MPGame.players[i].playerObj;
        BOT_playerInfo_t *info = &bot->players[i];
        costs[i] = NO_COST;
        distracted[i] = false;
        if (player == NULL || player == me)
            continue;
        char type = player->objectType;
        if (type == OBJECTTYPE_DRONE && MPDrone_MaybeIsDyingOrDead(player))
            continue;
        if (type == OBJECTTYPE_DEAD_DRONE || type == OBJECTTYPE_DEAD_PLAYER || type == OBJECTTYPE_GFX)
            continue;
        if (!(info->flags & BOT_PLAYER_PRESENT) || !(info->distanceSq < rangeSq))
            continue;
        if (!(info->flags & BOT_PLAYER_IN_SIGHT) && !attacksOnSight)
            continue;
        ushort favoured = MPGame.players[i].flags & 0xf;
        if (info->flags & BOT_PLAYER_TEAMMATE) {
            if (!attacksOnSight)
                FacingDegrees(me, player);   // worked out and not used
            continue;
        }

        // Kept on the FPU stack: rounded to a float only across BOTSTATE_increaseDistraction
        double cost = info->distanceSq;
        if (player == preferred)
            cost *= 0.04f;
        if (favoured)
            cost *= 0.25f;
        if (info->facing > -10.0f && info->facing < 10.0f) {
            if (info->flags & BOT_PLAYER_ACTIVE) {
                if (type != OBJECTTYPE_DRONE || ((Drone_tag *)player->extraObjectData)->opponent == me) {
                    cost = (float)(cost * 0.04f);
                    distracted[i] = true;
                    BOTSTATE_increaseDistraction(drone, 4.0f);
                }
            } else if (favoured) {
                cost *= 0.25f;
            }
        }
        if (player != drone->opponent && player != preferred
            && (bot->stats.personality == BOT_PERSONALITY_BERSERKER
                || bot->stats.personality == BOT_PERSONALITY_GUARDIAN)) {
            bool taken = false;
            if (i < NUM_PLAYERS) {
                for (int k = NUM_PLAYERS; k < NUM_AGENTS; k++) {
                    obj_tag *other = MPGame.players[k].playerObj;
                    if (k != i && other != NULL && ((Drone_tag *)other->extraObjectData)->opponent == player) {
                        taken = true;
                        break;
                    }
                }
            } else {
                taken = ((Drone_tag *)player->extraObjectData)->botVars->targeted != 0;
            }
            if (taken)
                cost *= 16.0f;
        }
        costs[i] = (float)cost;
        if (cost < bestCost) {
            bestCost = (float)cost;
            bestIndex = i;
            best = player;
        }
    }

    if (bestIndex != -1) {
        if (best == drone->opponent)
            return BOT_handleOpponentHistory(drone, best);
        while (best != NULL) {
            if (keepAny || distracted[bestIndex])
                return BOT_handleOpponentHistory(drone, best);
            double facing;
            if (bestIndex < NUM_PLAYERS)
                facing = FacingDegrees(me, best);
            else    // this bot's entry is players[bot->playerIndex], not [botIndex], as the original
                facing = ((Drone_tag *)best->extraObjectData)->botVars->players[botIndex].facing;
            if (!(facing < -viewDegrees) && !(facing > viewDegrees))
                return BOT_handleOpponentHistory(drone, best);

            costs[bestIndex] = NO_COST;
            bestIndex = -1;
            float lowest = NO_COST;
            for (int i = 0; i < NUM_AGENTS; i++) {
                if (costs[i] < lowest) {
                    lowest = costs[i];
                    bestIndex = i;
                }
            }
            if (bestIndex == -1)
                break;
            best = MPGame.players[bestIndex].playerObj;
        }
    }

    obj_tag *objective;
    if (MPSettings.GameMode == GM_PROTECTION) {
        if (team != PHOENIX)
            return NULL;
        objective = MP_getProtectionObj_NotExt();
    } else if (MPSettings.GameMode == GM_DEMOLITION) {
        if (team != MI6)
            return NULL;
        objective = MP_getDemolitionObj_NotExt();
    } else {
        return NULL;
    }
    if (objective == NULL || objective->subState == 1 || objective == drone->opponent)
        return NULL;
    if (!(Vec_SqDist3D(&me->position, &objective->position) < 16.0f))
        return NULL;
    double facing = FacingDegrees(me, objective);
    if (!(facing > -viewDegrees) || !(facing < viewDegrees))
        return NULL;
    if (!NDrone2_CanSeeObject(drone, objective, 5, 0))
        return NULL;
    MP_sendBotMessage(drone->gameObj, BOT_MSG_OBJECTIVE_SEEN, 1, 0);
    return BOT_handleOpponentHistory(drone, objective);
}

// The drone's opponent, worked out again each frame; returns the one taken, when it is new or a bot's. Enemies and
// civilians: the player while alive and the stats may be updated. Allies: a helicopter in states 1 to 3 from 30
// degrees below to 10 above; else they keep an opponent they see that is still up, and look along NDrone2List,
// from where they stopped, for an enemy drone nearer in the plane that they see, stopping when a line of sight has
// been tested.
// AUTOINJECT
obj_tag* NDrone2_FindOpponent(Drone_tag *drone) {
    ushort visionTests = NPCGlobals.visionTests;
    if (MPSettings.maybeDroneAIEnabled)
        return NDrone2_FindBotOpponent(drone);

    if (drone->side != 2) {
        obj_tag *player = NDrone2_Player();
        if (!PlrStat_OkToUpdate()) {
            player = NULL;
        } else if (player != NULL) {
            if (player->objectType == OBJECTTYPE_PLAYER) {
                if (((BLData *)player->extraObjectData)->health <= 0.0f)
                    player = NULL;
            } else if (player->objectType == OBJECTTYPE_DEAD_PLAYER) {
                player = NULL;
            }
        }
        NDrone2_SetOpponent(drone, player);
        return NULL;
    }

    for (obj_tag *obj = control_first_object(); obj != NULL; obj = obj->nextObject) {
        if (obj->objectType != OBJECTTYPE_COPTER || (short)obj->curState <= 0 || (short)obj->curState >= 4)
            continue;
        _VECTOR rot;
        if (NDrone2_AnglesToObj(drone, obj, &rot) && rot.x > kCopterAbove)
            continue;
        if (rot.x < kCopterBelow)
            continue;
        NDrone2_SetOpponent(drone, obj);
        return NULL;
    }

    obj_tag *opponent = drone->opponent;
    if (opponent != NULL) {
        bool keep = NDrone2_CanSeeObject(drone, opponent, 5, 0);
        if (keep) {
            opponent = drone->opponent;
            if (opponent->objectType == OBJECTTYPE_DRONE)
                keep = !MPDrone_MaybeIsDyingOrDead(opponent);
            else if (opponent->objectType == OBJECTTYPE_COPTER)
                keep = (short)opponent->curState < 4;
        }
        if (!keep)
            NDrone2_SetOpponent(drone, NULL);
    }

    float nearestSq = NO_COST;
    if (drone->opponent != NULL)
        nearestSq = drone->distanceToTarget * drone->distanceToTarget;
    Drone_tag *other = drone->opponentCursor != NULL ? drone->opponentCursor->next : NPCGlobals.NDrone2List;
    Drone_tag *current = drone->opponent != NULL ? (Drone_tag *)drone->opponent->extraObjectData : NULL;
    for (; other != NULL && other != drone->opponentCursor; other = other->next) {
        if (other == drone || other == current)
            continue;
        obj_tag *obj = other->gameObj;
        if (obj != NULL) {
            Drone_tag *otherDrone = (Drone_tag *)obj->extraObjectData;
            if (!(otherDrone->flags & 0x600) && (otherDrone->flags & 0x100) && !(otherDrone->health <= 0.0f)
                && obj->objectType != OBJECTTYPE_DEAD_DRONE && !(obj->flags & 1) && other->side == 1
                && Vec_SqDist2D(&drone->gameObj->position, &obj->position) < nearestSq
                && NDrone2_CanSeeObject(drone, other->gameObj, 5, 0)) {
                NDrone2_SetOpponent(drone, other->gameObj);
                drone->opponentCursor = other;
                return other->gameObj;
            }
        }
        if (visionTests < NPCGlobals.visionTests)
            break;
    }
    drone->opponentCursor = other;
    return NULL;
}

// ---- the alerts

// The alert target the routes plan to: where the last alert was
// AUTOINJECT
void NDrone2_SetupAlertTarget(void) {
    AITarget_tag *target = &NPCGlobals.alertTarget;
    target->targetBit = AITARGET_ALERT;
    target->obj = NULL;
    Vec_Copy(&NPCGlobals.alert.pos, &target->goal.pos);
    target->goal.cel = NPCGlobals.alert.cel;
    target->builtFrame = 0;
}

// Broadcasts an alert about obj, where it is and how it moves, to every drone in 30 frames. Nothing for no object.
// AUTOINJECT
void NDrone2_DroneAlertToObject(DCVars_tag *dcv, uint msgType, obj_tag *obj, float param0, float param1,
                                float param2) {
    if (obj == NULL)
        return;
    DroneAlert_tag *alert = &NPCGlobals.alert;
    alert->obj = obj;
    alert->sender = dcv->gameObj;
    alert->frame = GameState.NumFramesUnpaused;
    alert->msgType = msgType;
    Vec_Copy(&obj->position, &alert->pos);
    alert->cel = obj->inCel;
    Vec_Copy(&obj->rotation, &alert->rot);
    Vec_Subtract(&obj->lastPosition, &obj->position, &alert->velocity);
    alert->param[0] = param0;
    alert->param[1] = param1;
    alert->param[2] = param2;
    NDrone2_SetupAlertTarget();
    Drone_SM_BroadcastMsg(msgType, alert, ALERT_DELAY, dcv->drone->sm.id);
}

// Broadcasts an alert about a position to every drone in 30 frames, from a drone's id when the sender is one
// AUTOINJECT
void NDrone2_DroneAlertToPosition(obj_tag *sender, uint msgType, _VECTOR *pos, cel_tag *cel, float param0,
                                  float param1, float param2) {
    uint senderId = 0;
    if (sender != NULL && sender->objectType == OBJECTTYPE_DRONE)
        senderId = ((Drone_tag *)sender->extraObjectData)->sm.id;
    DroneAlert_tag *alert = &NPCGlobals.alert;
    alert->sender = sender;
    alert->obj = NULL;
    alert->frame = GameState.NumFramesUnpaused;
    alert->msgType = msgType;
    Vec_Copy(pos, &alert->pos);
    alert->cel = cel;
    alert->param[0] = param0;
    alert->param[1] = param1;
    alert->param[2] = param2;
    NDrone2_SetupAlertTarget();
    Drone_SM_BroadcastMsg(msgType, alert, ALERT_DELAY, senderId);
}
