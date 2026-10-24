// A multiplayer bot's goals - a pickup, an opponent or the game mode's objective - and the opponent its personality
// prefers. See docs/drone/bots-and-navigation/README.md 5.4 and 5.6.

#include "BOTSTATE.h"
#include "Drone.h"
#include "NDrone2.h"            // Drone_SM_SetState, the DSTATEs
#include "../mp/multiplayer.h"
#include "../../game.h"         // MPGame
#include "../obj/Pickup.h"      // PICKUPINFO
#include <math.h>
#include <bit>
#include "NDrone2_Combat.h"
#include "BOTWEAP.h"
#include "DroneAnim.h"
#include "../obj/control.h"
#include "../weapon_stats.h"
#include "../../engine/AINetwork.h"
#include "../../engine/Anim.h"
#include "../../math/math.h"
#include "../../math/X87Ops.h"
#include "../../util/Random.h"
#include "../../util/Sort.h"
#include "NDrone2_Alert.h"
#include "NDrone2_Route.h"

// A recovery stat in frames before a recovering bot sees again
constexpr float RecoveryScale = -0.6666667f;
static_assert(std::bit_cast<uint32_t>(RecoveryScale) == 0xbf2aaaab, "not the original's -2/3");

// Whether a bot other than playerIndex has obj as its objective goal. Every bot slot is asked, whatever its team;
// a player on no team gets false.
// AUTOINJECT
bool BOTSTATE_isObjAlreadyAnotherTeamObjective(obj_tag *obj, int playerIndex) {
    if (MPSettings.Player[playerIndex].TeamId == NO_TEAM)
        return false;

    for (int i = NUM_PLAYERS; i < NUM_AGENTS; i++) {
        if (i == playerIndex)
            continue;
        obj_tag *other = MPGame.players[i].playerObj;
        if (other == NULL)
            continue;
        BOT_goal_t *objective = &((Drone_tag *)other->extraObjectData)->botVars->goals[1];
        if (objective->kind == BOT_GOAL_OBJECTIVE && objective->target != NULL
            && ((MP_OBJ_EXT *)objective->target)->gameObj == obj)
            return true;
    }
    return false;
}

// The player index of the opponent the bot's personality prefers, or 0xff for none. The assassin in the Assassin
// game mode always prefers its target. A Guardian returns the nearest live team-mate that is not a bot Guardian and
// takes it as the friend to guard; when that one is within 4.5 m it goes to BotGuardFriendIdle and returns 0xff.
// AUTOINJECT
uchar BOTSTATE_getPreferredTraitOpponentObjIndex(Drone_tag *drone) {
    obj_tag *me = drone->gameObj;
    BOT_vars_t *bot = drone->botVars;
    uchar preferred = 0xff;

    if (Control_Plr2Ind(me) < 0)
        return 0xff;
    if (MPSettings.GameMode == GM_ASSASSIN && MP_IsAssasin(drone->gameObj))
        return (uchar)Control_Plr2Ind(MP_getAssassinTarget());

    switch (drone->botVars->stats.personality) {
        case BOT_PERSONALITY_GUARDIAN: {
            float nearest = 640000.0f;
            for (int i = 0; i < NUM_AGENTS; i++) {
                obj_tag *other = MPGame.players[i].playerObj;
                if (other == NULL || !(bot->players[i].flags & BOT_PLAYER_TEAMMATE) || MP_playerIsDead(other))
                    continue;
                if (i >= NUM_PLAYERS
                    && ((Drone_tag *)other->extraObjectData)->botVars->stats.personality == BOT_PERSONALITY_GUARDIAN)
                    continue;
                float distanceSq = Vec_SqDist3D(&me->position, &other->position);
                if (distanceSq < nearest) {
                    nearest = distanceSq;
                    preferred = (uchar)i;
                }
            }
            bot->guardFriend = preferred == 0xff ? NULL : MPGame.players[preferred].playerObj;

            // Kept as the original has it: a team-mate in slot 0 never starts the guard state
            if (nearest < 20.25f && preferred != 0) {
                DCVars_tag dc;
                if (Drone_DCVfromOBJ(me, &dc))
                    BOTSTATE_setStateChange(drone, DSTATE_BotGuardFriendIdle);
                return 0xff;
            }
            return preferred;
        }

        case BOT_PERSONALITY_JUDGE: {
            // the living opponent with the most points
            float most = 0.0f;
            for (int i = 0; i < NUM_AGENTS; i++) {
                obj_tag *other = MPGame.players[i].playerObj;
                if (other == NULL || other == me || (bot->players[i].flags & BOT_PLAYER_TEAMMATE)
                    || MP_playerIsDead(other))
                    continue;
                if (MPGame.players[i].points > most) {
                    most = MPGame.players[i].points;
                    preferred = (uchar)i;
                }
            }
            return preferred;
        }

        case BOT_PERSONALITY_BERSERKER: {
            // the current opponent, or else the nearest opponent present
            if (drone->opponent != NULL)
                return (uchar)Control_Plr2Ind(drone->opponent);
            float nearest = 640000.0f;
            for (int i = 0; i < NUM_AGENTS; i++) {
                BOT_playerInfo_t *info = &bot->players[i];
                if ((info->flags & BOT_PLAYER_PRESENT) && !(info->flags & BOT_PLAYER_TEAMMATE)
                    && info->distanceSq < nearest) {
                    nearest = info->distanceSq;
                    preferred = (uchar)i;
                }
            }
            return preferred;
        }

        case BOT_PERSONALITY_VENGEFUL: {
            // whoever killed the bot last
            short killer = MPGame.players[drone->botVars->playerIndex].idxOfMyKiller;
            if (killer != -1) {
                obj_tag *other = MPGame.players[killer].playerObj;
                if (other != NULL && other != me && !MP_areObjectsOnSameTeam(other, me) && !MP_playerIsDead(other))
                    preferred = (uchar)killer;
            }
            return preferred;
        }

        case BOT_PERSONALITY_ASSASSIN: {
            // the living opponent with the least health, if less than the bot's own
            float least = drone->health;
            for (int i = 0; i < NUM_AGENTS; i++) {
                obj_tag *other = MPGame.players[i].playerObj;
                if (other == NULL || other == me || (bot->players[i].flags & BOT_PLAYER_TEAMMATE)
                    || MP_playerIsDead(other))
                    continue;
                float health;
                if (other->objectType == OBJECTTYPE_DRONE || other->objectType == OBJECTTYPE_DEAD_DRONE)
                    health = ((Drone_tag *)other->extraObjectData)->health;
                else
                    health = ((BLData *)other->extraObjectData)->health;
                if (health < least) {
                    least = health;
                    preferred = (uchar)i;
                }
            }
            return preferred;
        }
    }
    return preferred;
}

// Picks the goal for a slot (0 a pickup or the preferred opponent, 1 the game mode's objective, falling back to
// slot 0 when there is none), writes it and sets off for it with BOTSTATE_gotoGoal, whose result it returns. With
// pick flag 8 it plans the route at once, and picks again if the route passes near the opponent. 0 when there is
// nothing to go for, and the slot is cleared.
// AUTOINJECT
uchar BOTSTATE_pickGoal(DCVars_tag *dc, int slot) {
    BOT_vars_t *bot = dc->drone->botVars;
    obj_tag *me = dc->gameObj;
    ushort nearestNode = bot->nearestNavNode;
    BOT_goal_t *goal = &bot->goals[slot];
    float bestDistance = 0.0f;
    float bestScore = -1.0f;
    int best = -1;                  // a pickup index, -2 the objective or the opponent, -3 nothing to do
    int kind = BOT_GOAL_PICKUP;
    void *target = NULL;
    CelPos_tag *targetPos = NULL;

    short playerIndex = Control_Plr2Ind(me);
    if (playerIndex < 0)
        return 0;

    int botIndex = bot->botIndex;
    MPTeam team = MPSettings.Player[playerIndex].TeamId;
    ushort playerFlags = MPGame.players[playerIndex].flags;
    ushort teamFlags = MPGame.teamObjectiveFlags[team];
    AIPath_tag *path = dc->drone->route1.path;
    int lastPickup = bot->lastPickup;
    uchar subtype = 0;

    BOTSTATE_uninitGoal(bot, slot);

    CelPos_tag here;
    here.pos = me->position;
    here.cel = dc->cel;

    if (slot == 1) {
        if (goal->weightObjective != 0.0f) {
            static const ushort goldenEyeKeyBits[2] = { 1, 2 };
            int attempt = 0;        // the espionage base's approach quadrant, or the uplink or key being tried
            int bestQuadrant = 0;
            MP_OBJ_EXT *fallback = NULL;    // an uplink or key another bot is already going for

            for (;;) {
                MP_OBJ_EXT *candidate = NULL;
                bool again = false;
                subtype = 0;

                switch (MPSettings.GameMode) {
                    case GM_CTF:
                        if (team == NO_TEAM)
                            break;
                        if (playerFlags & MPPLAYER_FLAG_1) {
                            candidate = MP_getBaseObj(team);
                            subtype = BOT_OBJECTIVE_OWN_BASE;
                        } else if (!(teamFlags & 1)) {
                            candidate = MP_getFlagObj(team == PHOENIX);
                            subtype = BOT_OBJECTIVE_ENEMY_FLAG;
                        }
                        break;

                    case GM_DEMOLITION:
                        if (team == NO_TEAM)
                            break;
                        subtype = BOT_OBJECTIVE_DEFEND_OR_DESTROY;
                        MP_getDemolitionObj_NotExt();
                        candidate = MP_getDemolitionObj();
                        break;

                    case GM_PROTECTION:
                        if (team == NO_TEAM)
                            break;
                        subtype = BOT_OBJECTIVE_DEFEND_OR_DESTROY;
                        MP_getProtectionObj_NotExt();
                        candidate = MP_getProtectionObj();
                        break;

                    case GM_BLUEPRINT:
                        if (team == NO_TEAM)
                            break;
                        if (playerFlags & MPPLAYER_FLAG_2) {
                            candidate = MP_getEsponageBaseObj(team, attempt);
                            subtype = BOT_OBJECTIVE_ESPIONAGE_BASE;
                        } else if (!(teamFlags & 1)) {
                            candidate = MP_getBlueprintObj();
                            subtype = BOT_OBJECTIVE_BLUEPRINT;
                        }
                        break;

                    case GM_GOLDENEYE:
                        subtype = BOT_OBJECTIVE_GOLDENEYE;
                        if (attempt == 2) {
                            if (best != -2)
                                candidate = fallback;
                        } else {
                            if (!(goldenEyeKeyBits[attempt] & teamFlags)) {
                                MP_OBJ_EXT *key = MP_getGoldenEyeObj(attempt);
                                if (key != NULL) {
                                    if (BOTSTATE_isObjAlreadyAnotherTeamObjective(key->gameObj, bot->playerIndex)) {
                                        if (fallback == NULL || (Rand_Rand(100) & 0x20))
                                            fallback = key;
                                    } else {
                                        fallback = key;
                                        candidate = key;
                                    }
                                }
                            }
                            attempt++;
                            if (attempt < 3)
                                again = true;
                        }
                        break;

                    case GM_UPLINK:
                        if (team == NO_TEAM)
                            break;
                        subtype = BOT_OBJECTIVE_UPLINK;
                        candidate = MP_getUplinkObj(-1 - attempt, team);
                        attempt++;
                        if (candidate == NULL) {
                            if (best != -2)
                                candidate = fallback;
                        } else {
                            if (BOTSTATE_isObjAlreadyAnotherTeamObjective(candidate->gameObj, playerIndex)) {
                                if (fallback == NULL || Rand_Rand(MPSettings.numBots) == (uint)attempt)
                                    fallback = candidate;
                                candidate = NULL;
                            }
                            again = true;
                        }
                        break;

                    case GM_TEAMKOTH:
                        if (team == NO_TEAM)
                            break;
                        // fall through
                    case GM_KOTH:
                        if (playerFlags & MPPLAYER_IN_HILL) {
                            best = -3;
                        } else {
                            candidate = MP_getHillObj();
                            subtype = BOT_OBJECTIVE_HILL;
                        }
                        break;

                    default:
                        break;
                }

                // Scored like a pickup: the nearer by the objective's emitter, the better
                float distance;
                if (candidate != NULL && candidate->gameObj != NULL && candidate->gameObj->objectType != OBJECTTYPE_GFX
                    && NDrone2_DistanceToEmitter(&here, path, &nearestNode, &candidate->aiEmitter, me, &distance)) {
                    int maxDistance = goal->maxEmitterDistance;
                    if (distance <= maxDistance) {
                        double score = ((double)maxDistance + 1.0f - distance) * goal->weightObjective;
                        if (score >= bestScore && (score != bestScore || distance < bestDistance)) {
                            bestScore = (float)score;
                            bestDistance = distance;
                            best = -2;
                            kind = BOT_GOAL_OBJECTIVE;
                            targetPos = &candidate->celPos;
                            target = candidate;
                            bestQuadrant = attempt;
                        }
                    }
                }

                // The espionage base is tried from each of its four quadrants, and left at the best one
                if (MPSettings.GameMode == GM_BLUEPRINT && candidate == MP_getEsponageBaseObj(team, -1)) {
                    attempt++;
                    if (attempt < 4)
                        continue;
                    if (bestScore != -1.0f)
                        MP_getEsponageBaseObj(team, bestQuadrant);
                }
                if (!again)
                    break;
            }
        }

        if (bestScore == -1.0f) {
            // no objective: a pickup instead
            slot = 0;
            goal = &bot->goals[0];
            BOTSTATE_uninitGoal(bot, 0);
            BOTSTATE_setGoalPickPrefs(bot, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0);
        }
    } else if (slot == 0 && goal->pickFlags == 0 && bot->preferredOpponent != 0xff) {
        obj_tag *opponent = MPGame.players[bot->preferredOpponent].playerObj;
        if ((opponent->objectType == OBJECTTYPE_PLAYER || opponent->objectType == OBJECTTYPE_DRONE)
            && !MP_playerIsDead(opponent)) {
            best = -2;
            kind = BOT_GOAL_OBJECT;
            targetPos = NULL;
            target = opponent;
        } else {
            bot->preferredOpponent = 0xff;
        }
    }

    bool ignoreVisits = (goal->pickFlags & BOT_PICK_IGNORE_VISITS) != 0;
    // the team defending the object: explosives count double for it
    bool defending = MPSettings.GameMode == GM_PROTECTION ? team == PHOENIX
                                                          : MPSettings.GameMode == GM_DEMOLITION && team == MI6;

    // Each pickup's distance by its emitter, worked out once. The original clears this only when it searches
    // pickups from the start; a search after a dropped objective or opponent (below) sees uninitialised stack.
    float pickupDistance[ARRAY_SIZE(MPpickups)] = {};

    bool searchPickups = best != -2 && best != -3;
    if (searchPickups) {
        if ((double)goal->weightAmmo + goal->weightWeapon + goal->weightHealth == 0.0) {
            goal->weightWeapon = 1.0f;
            goal->weightAmmo = 1.0f;
            goal->weightHealth = 1.0f;
        }
        if (goal->maxEmitterDistance > 0xfe)
            goal->maxEmitterDistance = 0xfe;
    }

    for (;;) {
        if (searchPickups) {
            // Pass 0 takes only what the bot needs, 1 any weapon or ammo, 2 with all weights 1, 3 also the pickups
            // visited lately, further away the more recent the visit
            int passes = (goal->pickFlags & BOT_PICK_SINGLE_PASS) ? 1 : 4;
            subtype = 0;
            for (int pass = 0; pass < passes; pass++) {
                if (pass == 2) {
                    goal->weightHealth = 1.0f;
                    goal->weightAmmo = 1.0f;
                    goal->weightWeapon = 1.0f;
                }

                for (int i = 0, counted = 0; counted < MPSettings.numActivePickups; i++) {
                    MP_PICKUP *pickup = &MPpickups[i];
                    if (pickup->gameObj == NULL)
                        continue;
                    counted++;
                    if (pickup->celPos.cel == NULL)
                        continue;

                    float distance;
                    if (pass == 0 && pickupDistance[i] == 0.0f) {
                        NDrone2_DistanceToEmitter(&here, path, &nearestNode, &pickup->aiEmitter, me, &distance);
                        pickupDistance[i] = distance;
                    } else {
                        distance = pickupDistance[i];
                    }
                    if (i == lastPickup)
                        continue;

                    int maxDistance = goal->maxEmitterDistance;
                    if (distance > maxDistance)
                        continue;
                    float closeness = (float)((double)maxDistance + 1.0f - distance);

                    float visited = pickup->botPickupVisitTimes[botIndex];
                    if (ignoreVisits)
                        visited = 0.0f;
                    if (pass == 3) {
                        if (visited != 0.0f) {
                            double factor = 4500.0f;
                            if (MPGame.TimeIncPaused > visited) {
                                factor = fabs((double)MPGame.TimeIncPaused - visited) * 50.0f;
                                if (factor < 1.0f)
                                    factor = 1.0f;
                            }
                            distance = (float)(factor * distance);
                        }
                    } else if (visited != 0.0f) {
                        continue;
                    }

                    PICKUPINFO *info = (PICKUPINFO *)pickup->gameObj->extraObjectData;
                    if (info->state == PICKUP_RESPAWNING && ((goal->pickFlags & BOT_PICK_SKIP_RESPAWNING) || distance < 5.0f))
                        continue;

                    double score;
                    switch (info->kind) {
                        case 0:     // a weapon
                            if (goal->weightWeapon == 0.0f)
                                continue;
                            if (pass == 0 && (!BOTSTATE_isPreferredWeapon(bot, info->item)
                                              || BOTSTATE_hasWeapon(bot, info->item))) {
                                if (!defending || weapon_data[info->item].botWeaponClass != 4)
                                    continue;
                                closeness = closeness + closeness;
                            }
                            score = (double)closeness * goal->weightWeapon;
                            break;
                        case 1:     // ammo
                            if (goal->weightAmmo == 0.0f)
                                continue;
                            if (pass == 0 && !BOTSTATE_hasWeapon(bot, info->item))
                                continue;
                            score = (double)closeness * goal->weightAmmo;
                            break;
                        case 3:     // health
                            if (goal->weightHealth == 0.0f)
                                continue;
                            score = (double)closeness * goal->weightHealth;
                            break;
                        default:
                            continue;
                    }

                    if (score >= bestScore && (score != bestScore || distance < bestDistance)) {
                        bestScore = (float)score;
                        bestDistance = distance;
                        best = i;
                        kind = BOT_GOAL_PICKUP;
                    }
                }
                if (best != -1)
                    break;
            }
        }

        if (best == -3 || best == -1)
            break;

        if (kind == BOT_GOAL_PICKUP) {
            targetPos = &MPpickups[best].celPos;
            target = &MPpickups[best];
            bot->lastPickup = (uchar)best;
        } else if (kind != BOT_GOAL_OBJECTIVE && kind != BOT_GOAL_OBJECT) {
            return 0;
        }

        if (slot >= 0 && slot < 2) {
            BOT_goal_t *chosen = &bot->goals[slot];
            chosen->kind = (uchar)kind;
            chosen->flags = 0;
            chosen->lastRouteStatus = 0;
            if (targetPos != NULL)
                chosen->pos = *targetPos;
            else
                chosen->pos.cel = NULL;
            chosen->target = target;
            chosen->subtype = subtype;
        }

        uchar going = BOTSTATE_gotoGoal(dc, slot, DSTATE_BotIdle);
        if (!going || !(goal->pickFlags & BOT_PICK_AVOID_OPPONENT))
            return going;

        Drone_tag *drone = dc->drone;
        NDrone2_DistanceToAIPoint(dc, &drone->aiPoint);
        uint status = NDrone2_MoveToGoalPosition(dc, (CelPos_tag *)&drone->aiPoint.pos, drone->aiPoint.radius, 0);
        if (!BOTSTATE_validateRoute(dc, status))
            return going;
        bool nearOpponent = BOTSTATE_isPathWithinObjectRange(dc->drone, dc->drone->opponent, 4.0f, 4);
        NDrone2_InvalidateAttackRoute(dc);
        if (!nearOpponent)
            return going;

        // The route passes the opponent: rule this one out and search the pickups again. For the objective or
        // the opponent (-2) the original writes pickupDistance[-2], the start position's z.
        BOTSTATE_uninitGoal(bot, slot);
        if (best >= 0)
            pickupDistance[best] = 255.0f;
        else
            here.pos.z = 255.0f;
        best = -1;
        searchPickups = true;
    }

    // Nothing to go for
    if (slot != 0xff) {
        BOT_vars_t *owner = bot->drone->botVars;
        if (bot->goals[slot].subtype == BOT_OBJECTIVE_ENEMY_FLAG)
            owner->flags |= BOT_FLAG_ENEMY_FLAG;
        else
            owner->flags &= ~BOT_FLAG_ENEMY_FLAG;
        if (bot->activeGoal == slot)
            bot->activeGoal = 0xff;
        BOT_goal_t *dropped = &bot->goals[slot];
        dropped->kind = BOT_GOAL_NONE;
        dropped->flags = 0;
        dropped->lastRouteStatus = 0;
        dropped->timeBudget = 0.0f;
        dropped->subtype = 0;
    }
    return 0;
}

// Each frame from BotGlobal: ends the goals that ran out of time or whose target is gone, re-picks the preferred
// opponent, and acts on the active goal's route - on a failure or on arrival the goal ends and the bot changes
// state. A Guardian that reaches a friend it is to guard goes to BotGuardFriendIdle instead.
// AUTOINJECT
void BOTSTATE_processGoals(DCVars_tag *dc) {
    Drone_tag *drone = dc->drone;
    BOT_vars_t *bot = drone->botVars;
    short playerIndex = Control_Plr2Ind(dc->gameObj);
    if (playerIndex < 0)
        return;
    MPTeam team = MPSettings.Player[playerIndex].TeamId;

    for (int slot = 0; slot < 2; slot++) {
        BOT_goal_t *goal = &bot->goals[slot];
        MP_PICKUP *pickup = NULL;
        MP_OBJ_EXT *objective = NULL;
        obj_tag *object = NULL;
        switch (goal->kind) {
            case BOT_GOAL_PICKUP:
                pickup = (MP_PICKUP *)goal->target;
                break;
            case BOT_GOAL_OBJECTIVE:
                objective = (MP_OBJ_EXT *)goal->target;
                break;
            case BOT_GOAL_OBJECT:
                object = (obj_tag *)goal->target;
                break;
        }

        if (goal->kind == BOT_GOAL_NONE) {
            if (slot == bot->activeGoal)
                bot->activeGoal = 0xff;
            continue;
        }

        bool ended = false;
        if ((double)goal->startTime + goal->period < MPGame.TimeIncPaused) {
            ended = true;
        } else if (goal->kind == BOT_GOAL_OBJECTIVE) {
            if (objective == NULL) {
                ended = true;
            } else if (team != NO_TEAM) {
                ushort bit = 0;
                if (objective->flags & 1)
                    bit = 1;
                else if (objective->flags & 2)
                    bit = 2;
                if (bit & MPGame.teamObjectiveFlags[team])
                    ended = true;
            }
        } else if (goal->kind == BOT_GOAL_OBJECT) {
            if (object == NULL || object->objectType == OBJECTTYPE_GFX || MP_playerIsDead(object)) {
                ended = true;
            } else if (bot->stats.personality == BOT_PERSONALITY_GUARDIAN) {
                short other = Control_Plr2Ind(object);
                if (other == -1) {
                    if (!BOTSTATE_opponentIsMissile(drone, object)) {
                        goal->arriveOrNextState = DSTATE_BotIdle;
                        ended = true;
                    }
                } else {
                    // a team-mate within 4.5 m and 2 m of height: guard it
                    BOT_playerInfo_t *info = &bot->players[other];
                    if ((info->flags & BOT_PLAYER_PRESENT) && (info->flags & BOT_PLAYER_TEAMMATE)
                        && info->distanceSq < 20.25f
                        && fabs((double)dc->gameObj->position.y - object->position.y) < 2.0f) {
                        goal->arriveOrNextState = DSTATE_BotGuardFriendIdle;
                        bot->guardFriend = object;
                        ended = true;
                    }
                }
            }
        }

        uint state = dc->aiStateMachine->curState;
        bool going = state == DSTATE_BotGotoGoalPosition || state == DSTATE_BotGuardFriendFollow;

        if (ended) {
            if (slot == bot->activeGoal && going) {
                NDrone2_InvalidateAttackRoute(dc);
                uint next = goal->arriveOrNextState;
                Drone_SM_SetState(&drone->sm, (DSTATE)next, 0);
                drone->botVars->goalReturnState = next;
            }
            BOTSTATE_uninitGoal(bot, slot);
            continue;
        }

        if (slot != bot->activeGoal || !going)
            continue;

        if (goal->kind == BOT_GOAL_OBJECT && bot->preferredOpponent != 0xff
            && object == MPGame.players[bot->preferredOpponent].playerObj) {
            uchar preferred = BOTSTATE_getPreferredTraitOpponentObjIndex(drone);
            if (preferred != bot->preferredOpponent) {
                bot->preferredOpponent = preferred;
                NDrone2_InvalidateAttackRoute(dc);
                BOTSTATE_setStateChange(drone, DSTATE_BotIdle);
                BOTSTATE_uninitGoal(bot, slot);
                continue;
            }
        }

        if (!BOTSTATE_validateRoute(dc, goal->lastRouteStatus)) {
            NDrone2_InvalidateAttackRoute(dc);
            if (pickup != NULL)
                BOTSTATE_setPickupVisitTime(drone, pickup);
            BOTSTATE_setStateChange(drone, goal->arriveOrNextState);
            BOTSTATE_uninitGoal(bot, slot);
            continue;
        }

        if (!(goal->flags & BOT_GOAL_COMPLETE) && goal->lastRouteStatus != AIROUTE_ARRIVED)
            continue;

        // Arrived
        if (goal->kind == BOT_GOAL_OBJECT && object == bot->guardFriend) {
            short other = Control_Plr2Ind(object);
            if (other == -1) {
                goal->arriveOrNextState = DSTATE_BotIdle;
            } else {
                // the friend moved off again: follow
                BOT_playerInfo_t *info = &bot->players[other];
                if ((info->flags & BOT_PLAYER_PRESENT) && (info->flags & BOT_PLAYER_TEAMMATE)
                    && info->distanceSq > 20.25f) {
                    BOTSTATE_gotoGoal(dc, slot, goal->arriveOrNextState);
                    continue;
                }
            }
        } else if (goal->kind == BOT_GOAL_OBJECTIVE && MPSettings.GameMode == GM_BLUEPRINT
                   && objective == MP_getEsponageBaseObj(team, -1)
                   && (MPGame.players[bot->playerIndex].flags & MPPLAYER_FLAG_2)
                   && MP_getBlueprintObj() != NULL && MP_getBlueprintObj()->gameObj != NULL) {
            MP_BluePrintReachedBase(MP_getBlueprintObj()->gameObj, (MPOBJECT *)MP_getBlueprintObj()->gameObj->extraObjectData);
        }

        NDrone2_InvalidateAttackRoute(dc);
        if (goal->kind == BOT_GOAL_PICKUP)
            BOTSTATE_setPickupVisitTime(drone, pickup);
        BOTSTATE_setStateChange(drone, goal->arriveOrNextState);
        BOTSTATE_uninitGoal(bot, slot);
    }
}

// ---- the moves a bot makes in combat

// An unarmed attack: one of four variants at random, played if the drone can make it, going back to BotAttack.
// Returns the anim state played (0x3d), or 0.
// AUTOINJECT
short BOTSTATE_chooseUnarmedAttackAnim(DCVars_tag *dc) {
    uint variant = Rand_Rand(4);
    if (NDrone2_CanAltAttack(dc, (short)variant) &&
        DroneAnim_CallAnim(DANIM_ALT_ATTACK, variant, DSTATE_BotAttack, 0, 0.0f, 0, dc))
        return DANIM_ALT_ATTACK;
    return 0;
}

// The combat move state asked for, or 0 to refuse it: in King of the Hill a bot inside the hill does not make a
// move that would take it out. The move is not refused when there is no room for it.
// AUTOINJECT
short BOTSTATE_checkAttackMove(DCVars_tag *dc, short state) {
    bool canMove;
    switch (state) {
        case DSTATE_BotAttackStrafeAimLeft: canMove = NDrone2_CanStrafeLeft(dc); break;
        case DSTATE_BotAttackStrafeAimRight: canMove = NDrone2_CanStrafeRight(dc); break;
        case DSTATE_BotAttackStepAimLeft: canMove = NDrone2_CanStepLeft(dc); break;
        case DSTATE_BotAttackStepAimRight: canMove = NDrone2_CanStepRight(dc); break;
        case DSTATE_BotAttackRollLeftCrouch: canMove = NDrone2_CanRollLeft(dc); break;
        case DSTATE_BotAttackRollRightCrouch: canMove = NDrone2_CanRollRight(dc); break;
        case DSTATE_BotAttackBackoff: canMove = NDrone2_CanBackoff(dc); break;
        default: return state;
    }
    if (!canMove)
        return state;

    if (MPSettings.GameMode == GM_KOTH || MPSettings.GameMode == GM_TEAMKOTH) {
        short playerIndex = Control_Plr2Ind(dc->gameObj);
        if (playerIndex < 0)
            return 0;
        // where the move's walk test (NDrone2_CanWalkStraightTo) ended
        if ((MPGame.players[playerIndex].flags & MPPLAYER_IN_HILL) && !MP_isPosOnHill(&LastWalkTarget))
            state = 0;
    }
    return state;
}

// The bot version of an evasive move (NDrone2_EvasiveMove): a strafe to either side, or now and then a roll. 0
// for none.
// AUTOINJECT
short BOTSTATE_EvasiveMove(DCVars_tag *dc) {
    Drone_tag *drone = dc->drone;
    switch (NDrone2_EvasiveMove(dc)) {
        case DSTATE_StrafeAimLeft:
            return DSTATE_BotAttackStrafeAimLeft;
        case DSTATE_StrafeAimRight:
            return DSTATE_BotAttackStrafeAimRight;
        case DSTATE_RollLeftCrouch:
            if (BOT_getMovePossibility(drone, 3))
                return DSTATE_BotAttackRollLeftCrouch;
            return 0;
        case DSTATE_RollRightCrouch:
            if (BOT_getMovePossibility(drone, 3))
                return DSTATE_BotAttackRollRightCrouch;
            return 0;
        default:
            return 0;
    }
}

// The nearest nav node is looked up again when next wanted
// AUTOINJECT
bool BOTSTATE_setNearestNavNode(DCVars_tag *dc) {
    dc->drone->botVars->nearestNavNode = -1;
    return true;
}

// ---- goals

// Clears a goal slot (0xff for none); it stops being the active goal. The flag 4 the bot's drone keeps says
// whether the goal was for the enemy flag.
// AUTOINJECT
void BOTSTATE_uninitGoal(BOT_vars_t *bot, uint slot) {
    if (slot == 0xff)
        return;
    BOT_goal_t *goal = &bot->goals[slot];
    BOT_vars_t *droneBot = bot->drone->botVars;
    if (goal->subtype == BOT_OBJECTIVE_ENEMY_FLAG)
        droneBot->flags |= 4;
    else
        droneBot->flags &= ~4u;
    if (bot->activeGoal == slot)
        bot->activeGoal = 0xff;
    goal->kind = BOT_GOAL_NONE;
    goal->flags = 0;
    goal->lastRouteStatus = 0;
    goal->timeBudget = 0.0f;
    goal->subtype = 0;
}

// Sets a goal slot's target: where (no cel when pos is NULL), what and which objective
// AUTOINJECT
void BOTSTATE_initGoal(BOT_vars_t *bot, int slot, uchar kind, CelPos_tag *pos, void *target, uchar subtype) {
    if (slot < 0 || slot >= 2)
        return;
    BOT_goal_t *goal = &bot->goals[slot];
    goal->kind = kind;
    goal->flags = 0;
    goal->lastRouteStatus = 0;
    if (pos != NULL)
        goal->pos = *pos;
    else
        goal->pos.cel = NULL;
    goal->target = target;
    goal->subtype = subtype;
}

// Sets off for a goal: it becomes the active one (the old one is cleared), the route goes to its object or place,
// and it gets its time: for an objective by the objective's kind - Team Players in a team game 1500 longer,
// Berserkers none - and none for an opponent or a pickup. The drone goes to nextState when it arrives. False for a
// bad slot.
// AUTOINJECT
uchar BOTSTATE_gotoGoal(DCVars_tag *dc, int slot, uint nextState) {
    if (slot < 0 || slot >= 2)
        return false;

    Drone_tag *drone = dc->drone;
    BOT_vars_t *bot = drone->botVars;
    BOT_goal_t *goal = &bot->goals[slot];
    if (bot->activeGoal != 0xff)
        BOTSTATE_uninitGoal(bot, bot->activeGoal);

    goal->startTime = MPGame.TimeIncPaused;
    goal->period = (float)(VIDEO_FRAME_RATE * 5);
    goal->arriveOrNextState = nextState;
    if (goal->kind == BOT_GOAL_OBJECT) {
        NDrone2_InvalidateAttackRoute(dc);
        Drone_tag *mover = dc->drone;
        AINetwork_SetupGoalPositionToObj(&mover->goalTarget, &mover->aiPoint, (obj_tag *)goal->target,
                                         BOT_getMovementSpeedMul(mover));
        dc->drone->botVars->goalFromState = dc->aiStateMachine->curState;
        dc->drone->initialState = (short)nextState;
        dc->drone->returnState = (short)nextState;
        goal->subtype = BOT_OBJECTIVE_OPPONENT;
    } else {
        NDrone2_InvalidateAttackRoute(dc);
        Drone_tag *mover = dc->drone;
        AINetwork_SetupGoalPosition(&mover->goalTarget, &mover->aiPoint, &goal->pos, BOT_getMovementSpeedMul(mover));
        dc->drone->botVars->goalFromState = dc->aiStateMachine->curState;
        dc->drone->initialState = (short)nextState;
        dc->drone->returnState = (short)nextState;
    }

    float budget = 0.0f;
    if (slot == 1) {
        switch (goal->subtype) {
            case BOT_OBJECTIVE_ENEMY_FLAG:
            case BOT_OBJECTIVE_GOLDENEYE:
            case BOT_OBJECTIVE_BLUEPRINT:
            case BOT_OBJECTIVE_UPLINK:
            case BOT_OBJECTIVE_HILL:
                budget = (float)Rand_Rand(600) + 500.0f;
                break;
            case BOT_OBJECTIVE_OWN_BASE:
            case BOT_OBJECTIVE_ESPIONAGE_BASE:
                budget = 1500.0f;
                break;
            case BOT_OBJECTIVE_DEFEND_OR_DESTROY:
                budget = (float)Rand_Rand(600) + 250.0f;
                break;
            case BOT_OBJECTIVE_OPPONENT:
                budget = (float)Rand_Rand(500) + 250.0f;
                break;
        }
        if (MPSettings.isTeamGame && dc->drone->botVars->stats.personality == BOT_PERSONALITY_TEAM_PLAYER)
            budget += 1500.0f;
        else if (dc->drone->botVars->stats.personality == BOT_PERSONALITY_BERSERKER)
            budget = 0.0f;
    }
    goal->timeBudget = budget;
    drone->botVars->flags &= ~BOT_FLAG_ENEMY_FLAG;
    bot->activeGoal = (uchar)slot;
    return true;
}

// AUTOINJECT
void BOTSTATE_setGoalComplete(DCVars_tag *dc, uint slot) {
    if (slot < 2)
        dc->drone->botVars->goals[slot].flags |= BOT_GOAL_COMPLETE;
}

// The goal being followed, or NULL
// AUTOINJECT
BOT_goal_t* BOTSTATE_getActiveGoal(Drone_tag *drone) {
    BOT_vars_t *bot = drone->botVars;
    if (bot->activeGoal == 0xff)
        return NULL;
    return &bot->goals[bot->activeGoal];
}

// A pickup weight: 1 - 1 / (full / have), times 25
static float PickWeight(const Extended &full, const Extended &have) {
    Extended one = 1.0f;
    return (float)((one - one / (full / have)) * Extended(25.0f));
}

// The weights BOTSTATE_pickGoal scores a slot's candidates by. With no health, ammo or weapon weight given, the
// bot works them out: health from how hurt it is against its stats, ammo and weapon from how short its weapon is
// of a clip. Each of the three is at most 25. maxEmitterDistance 0 is 0xfe.
// AUTOINJECT
void BOTSTATE_setGoalPickPrefs(BOT_vars_t *bot, uint slot, float health, float ammo, float weapon, float objective,
                               uchar maxEmitterDistance, uchar pickFlags) {
    if (slot >= 2)
        return;
    BOT_goal_t *goal = &bot->goals[slot];
    Drone_tag *drone = bot->drone;

    if (health == 0.0f && ammo == 0.0f && weapon == 0.0f) {
        float have = drone->health < 1.0f ? 1.0f : drone->health;
        health = PickWeight((double)bot->stats.health, have);

        int rounds = BOTWEAP_getWeaponAmmoAmount(drone, bot->weapon);
        int clip = BOTWEAP_getWeaponClipSize(drone, bot->weapon);
        if ((uint)rounds < (uint)clip) {
            // as unsigned values; the clip is used once as it is and once rounded to a float
            Extended left = (double)(uint)rounds;
            Extended atLeastOne = left < Extended(1.0f) ? Extended(1.0f) : left;
            Extended full = (double)(uint)clip;
            float fullStored = (float)full;
            ammo = PickWeight(full, atLeastOne);
            weapon = PickWeight(fullStored, atLeastOne);
        }
    }

    if (health > 25.0f)
        health = 25.0f;
    if (ammo > 25.0f)
        ammo = 25.0f;
    if (weapon > 25.0f)
        weapon = 25.0f;
    goal->weightHealth = health;
    goal->weightObjective = objective;
    goal->weightAmmo = ammo;
    goal->weightWeapon = weapon;
    goal->pickFlags = pickFlags;
    goal->maxEmitterDistance = maxEmitterDistance != 0 ? maxEmitterDistance : 0xfe;
}

// Whether a weapon suits the bot's preferred weapon class (BOT_stats_t.preferredWeaponClass): 1, 2, 4 and 5 take
// the weapons of class 1, 2, 3 and 4, 3 a list of its own. Fists, weapon 6 and no preference suit any bot.
// AUTOINJECT
bool BOTSTATE_isPreferredWeapon(BOT_vars_t *bot, uint weapon) {
    if (weapon == 1 || weapon == 6)
        return true;
    uchar preferred = bot->stats.preferredWeaponClass;
    if (preferred == 0)
        return true;
    uchar weaponClass = weapon_data[weapon].botWeaponClass;
    switch (preferred) {
        case 1:
            return weaponClass == 1;
        case 2:
            return weaponClass == 2;
        case 3:
            switch (weapon) {
                case 3: case 7: case 0x14: case 0x24: case 0x25:
                    return true;
                default:
                    return false;
            }
        case 4:
            return weaponClass == 3;
        case 5:
            return weaponClass == 4;
        default:
            return false;
    }
}

// AUTOINJECT
bool BOTSTATE_hasWeapon(BOT_vars_t *bot, uint weapon) {
    return BOTWEAP_hasWeapon(bot->drone, (short)weapon);
}

// What a route status (AINetwork) means for a bot's goal: 0-3 it goes on, and the failure count starts again five
// frames after the last failure; 5 is a failure, counted; anything else gives up.
// AUTOINJECT
uchar BOTSTATE_validateRoute(DCVars_tag *dc, uint routeStatus) {
    BOT_vars_t *bot;
    switch (routeStatus) {
        case AIROUTE_FOLLOWING: case AIROUTE_PARTIAL: case AIROUTE_DIRECT: case AIROUTE_ARRIVED:
            bot = dc->drone->botVars;
            if (bot->routeFailCount == 0 || GameState.NumFramesUnpaused - bot->lastRouteFailFrame >= 5)
                bot->routeFailCount = 0;
            return true;
        case AIROUTE_OFF_NETWORK:
            dc->drone->botVars->lastRouteFailFrame = GameState.NumFramesUnpaused;
            dc->drone->botVars->routeFailCount++;
            return false;
        default:
            return false;
    }
}

// Whether the bot's route passes within `range` of the object, over its first `segments` legs: from its start
// through its nodes to its end.
// AUTOINJECT
bool BOTSTATE_isPathWithinObjectRange(Drone_tag *drone, obj_tag *obj, float range, int segments) {
    AIRoute_tag *route = drone->activeRoute;
    bool near = false;
    if (obj == NULL || route == NULL)
        return false;

    _VECTOR pos;
    if (obj->objectType == OBJECTTYPE_DRONE || obj->objectType == OBJECTTYPE_PLAYER)
        NDrone2_FeetPos(obj, &pos);
    else
        pos = obj->position;

    _VECTOR from, to, nearest;
    if (route->count == 0) {
        Vec_Copy(&route->start.pos, &from);
        Vec_Copy(&route->dest.pos, &to);
        return Vec_SegmentSphereIntersect(&from, &to, &pos, range, &nearest);
    }

    AINode_tag *nodes = route->path->runtimeNodes;
    Vec_Copy(&route->start.pos, &from);
    Vec_Copy(&nodes[route->nodes[0]].fileNode->pos, &to);
    near = Vec_SegmentSphereIntersect(&from, &to, &pos, range, &nearest);
    if (near)
        return near;

    segments--;
    for (int i = 0; i < segments; i++) {
        if (i < route->count - 1) {
            Vec_Copy(&nodes[route->nodes[i]].fileNode->pos, &from);
            Vec_Copy(&nodes[route->nodes[i + 1]].fileNode->pos, &to);
            if (Vec_SegmentSphereIntersect(&from, &to, &pos, range, &nearest))
                return true;
        } else {
            Vec_Copy(&nodes[route->nodes[i]].fileNode->pos, &from);
            Vec_Copy(&route->dest.pos, &to);
            return Vec_SegmentSphereIntersect(&from, &to, &pos, range, &nearest);
        }
    }
    return near;
}

// When the bot last went for a pickup: 45 seconds ahead of the match clock (never 0, which means not visited).
// Only for a drone.
// AUTOINJECT
void BOTSTATE_setPickupVisitTime(Drone_tag *drone, MP_PICKUP *pickup) {
    if (drone == NULL || drone->gameObj->objectType != OBJECTTYPE_DRONE || pickup == NULL)
        return;
    double time = (double)MPGame.TimeIncPaused + 45.0f;
    if (time == 0.0)
        time += 0.1f;
    pickup->botPickupVisitTimes[drone->botVars->botIndex] = (float)time;
}

// The bot starts recovering from a hit: its recovery stat in frames, x2/3, back from now
// AUTOINJECT
void BOTSTATE_startRecovery(DCVars_tag *dc) {
    Drone_tag *drone = dc->drone;
    if (drone->botVars->flags & BOT_FLAG_RECOVERING)
        return;
    int frames = Ftol((double)drone->recover * RecoveryScale);
    drone->botVars->recoveryEndFrame = GameState.NumFramesUnpaused - frames;
    drone->botVars->flags |= BOT_FLAG_RECOVERING;
}

// Whether obj (-1 for the bot's opponent) is the object the bot's side is to destroy: the protection object for
// Phoenix, the demolition object for MI6. Ghidra's name.
// AUTOINJECT
bool BOTSTATE_opponentIsMissile(Drone_tag *drone, obj_tag *obj) {
    BOT_vars_t *bot = drone->botVars;
    if (obj == (obj_tag *)-1)
        obj = drone->opponent;

    obj_tag *target;
    if (MPSettings.GameMode == GM_PROTECTION) {
        if (MPSettings.Player[bot->playerIndex].TeamId != PHOENIX)
            return false;
        target = MP_getProtectionObj_NotExt();
    } else if (MPSettings.GameMode == GM_DEMOLITION) {
        if (MPSettings.Player[bot->playerIndex].TeamId != MI6)
            return false;
        target = MP_getDemolitionObj_NotExt();
    } else {
        return false;
    }
    if (target == NULL || target->subState == 1)
        return false;
    return target == obj;
}

// The combat ranges for fighting up close
// AUTOINJECT
void BOTSTATE_reallyCloseCombatRange(Drone_tag *drone) {
    drone->combatRanges[0] = 0.5f;
    drone->combatRanges[1] = 1.5f;
    drone->combatRanges[4] = 1.6f;
}

// The combat ranges back to the bot's own
// AUTOINJECT
void BOTSTATE_defaultCombatRange(Drone_tag *drone) {
    BOT_vars_t *bot = drone->botVars;
    drone->combatRanges[0] = bot->savedCombatRanges[0];
    drone->combatRanges[1] = bot->savedCombatRanges[1];
    drone->combatRanges[4] = bot->savedCombatRanges[2];
}

// Adds to how distracted a bot on its way to a goal is, up to the goal's time budget, where it stays. Returns false
// while it is below the budget (and at least 0); otherwise whether BOT_FLAG_ENEMY_FLAG is clear.
// AUTOINJECT
uchar BOTSTATE_increaseDistraction(Drone_tag *drone, float amount) {
    BOT_vars_t *bot = drone->botVars;
    bool result = !(bot->flags & BOT_FLAG_ENEMY_FLAG);
    if (bot->activeGoal == 0xff || drone->sm.curState != DSTATE_BotGotoGoalPosition)
        return result;

    BOT_goal_t *goal = &bot->goals[bot->activeGoal];
    bot->distraction = amount + bot->distraction;
    BOT_vars_t *botNow = drone->botVars;
    if (botNow->distraction < goal->timeBudget) {
        if (botNow->distraction < 0.0f)
            botNow->distraction = 0.0f;
        return false;
    }
    botNow->distraction = goal->timeBudget;
    return result;
}

// Whether a bot may be drawn from its goal: always unless it is on its way to one (with flag 4 clear), and then
// once its distraction, if any, has reached the goal's time budget.
// AUTOINJECT
uchar BOTSTATE_isDistracted(Drone_tag *drone) {
    BOT_vars_t *bot = drone->botVars;
    if (bot->flags & BOT_FLAG_ENEMY_FLAG)
        return false;
    if (bot->activeGoal == 0xff || drone->sm.curState != DSTATE_BotGotoGoalPosition)
        return true;
    if (bot->distraction != 0.0f)
        return true;
    BOT_goal_t *goal = BOTSTATE_getActiveGoal(drone);
    if (goal == NULL)
        return true;
    return !(bot->distraction < goal->timeBudget);
}

// AUTOINJECT
void BOTSTATE_setStateChange(Drone_tag *drone, uint state) {
    Drone_SM_SetState(&drone->sm, (DSTATE)state, 0);
    drone->botVars->goalReturnState = state;
}

// A move in combat: the evasive move when there is one, else now and then a strafe or a step to either side. 0
// for none.
// AUTOINJECT
short BOTSTATE_chooseCombatMove(DCVars_tag *dc) {
    short move = BOTSTATE_EvasiveMove(dc);
    if (move != 0)
        return BOTSTATE_checkAttackMove(dc, move);
    if (!BOT_getMovePossibility(dc->drone, 2))
        return 0;
    switch (Rand_Rand(4)) {
        case 0: return BOTSTATE_checkAttackMove(dc, DSTATE_BotAttackStrafeAimLeft);
        case 1: return BOTSTATE_checkAttackMove(dc, DSTATE_BotAttackStrafeAimRight);
        case 2: return BOTSTATE_checkAttackMove(dc, DSTATE_BotAttackStepAimLeft);
        case 3: return BOTSTATE_checkAttackMove(dc, DSTATE_BotAttackStepAimRight);
    }
    return 0;
}

// A move out of the way in the directions allowed (DODGE_*): a strafe or a step to a side, tried in one of four
// orders chosen at random, or (one time in five, or when none is allowed) a roll from NDrone2_EvasiveMove or a back
// off. 0 for none.
// Not named on PS2; the name is ours.
// AUTOINJECT
short BOTSTATE_chooseDodgeMove(DCVars_tag *dc, uint directions) {
    static const short orders[4][4] = {
        {DSTATE_BotAttackStrafeAimLeft, DSTATE_BotAttackStrafeAimRight, DSTATE_BotAttackStepAimLeft,
         DSTATE_BotAttackStepAimRight},
        {DSTATE_BotAttackStepAimLeft, DSTATE_BotAttackStrafeAimLeft, DSTATE_BotAttackStrafeAimRight,
         DSTATE_BotAttackStepAimRight},
        {DSTATE_BotAttackStrafeAimRight, DSTATE_BotAttackStepAimRight, DSTATE_BotAttackStepAimLeft,
         DSTATE_BotAttackStrafeAimLeft},
        {DSTATE_BotAttackStrafeAimRight, DSTATE_BotAttackStrafeAimLeft, DSTATE_BotAttackStepAimRight,
         DSTATE_BotAttackStepAimLeft},
    };
    uint order = Rand_Rand(5);
    if (order < 4) {
        for (int i = 0; i < 4; i++) {
            short move = orders[order][i];
            bool left = move == DSTATE_BotAttackStrafeAimLeft || move == DSTATE_BotAttackStepAimLeft;
            if (!(directions & (left ? DODGE_LEFT : DODGE_RIGHT)))
                continue;
            move = BOTSTATE_checkAttackMove(dc, move);
            if (move != 0)
                return move;
        }
    }

    switch (NDrone2_EvasiveMove(dc)) {
        case DSTATE_RollLeftCrouch:
            if (directions & DODGE_LEFT)
                return BOTSTATE_checkAttackMove(dc, DSTATE_BotAttackRollLeftCrouch);
            return 0;
        case DSTATE_RollRightCrouch:
            if (directions & DODGE_RIGHT)
                return BOTSTATE_checkAttackMove(dc, DSTATE_BotAttackRollRightCrouch);
            return 0;
        default:
            if (directions & DODGE_BACK)
                return BOTSTATE_checkAttackMove(dc, DSTATE_BotAttackBackoff);
            return 0;
    }
}

// Drops the bot's goals for an object (an objective whose object it is, or one with none; an opponent or friend).
// When the active goal goes, the bot changes to `state`. Returns whether it did.
// Not named on PS2; the name is ours.
// AUTOINJECT
uchar BOTSTATE_removeGoalsForObject(DCVars_tag *dc, obj_tag *obj, uint state) {
    BOT_vars_t *bot = dc->drone->botVars;
    uchar changed = false;
    for (uint slot = 0; slot < 2; slot++) {
        BOT_goal_t *goal = &bot->goals[slot];
        bool active = bot->activeGoal == slot;
        if (goal->kind == BOT_GOAL_OBJECTIVE) {
            if (goal->target != NULL && ((MP_OBJ_EXT *)goal->target)->gameObj != obj)
                continue;
        } else if (goal->kind == BOT_GOAL_OBJECT) {
            if (goal->target != obj)
                continue;
        } else {
            continue;
        }
        BOTSTATE_uninitGoal(bot, slot);
        if (active) {
            BOTSTATE_setStateChange(dc->drone, state);
            changed = true;
        }
    }
    return changed;
}

// The order the weapon choice sorts a listHeldLoadedWeapons list in: by the low byte, most wanted first
static int __cdecl CompareWeaponEntries(const void *a, const void *b) {
    return *(const uchar *)a - *(const uchar *)b;
}

// The weapon a bot should fight `opponent` with (0 for none): the most wanted of the usable ones (preferred ones
// first, when there are two), but not an explosive too close for its blast once it has something else; the side
// that destroys the objective prefers the satchel against it, and a Berserker the satchel's runner-up. Unless
// noMessage, the bot is told of a change (bot message 0x42) - with keepCurrent, only when it differs from the weapon
// in hand. Nothing without an opponent in sight.
// AUTOINJECT
short BOTSTATE_combatWeaponChangeChoice(Drone_tag *drone, obj_tag *opponent, char keepCurrent, char noMessage,
                                        char unused) {
    BOT_vars_t *bot = drone->botVars;
    ushort list[BOT_WEAPON_IDS];
    int count = 0;
    int pick = 0;
    short weapon;

    if (opponent == NULL || !(drone->sightFlags & DRONE_SIGHT_SEES_OPPONENT))
        return 0;

    uint usable = BOTWEAP_listHeldLoadedWeapons(drone, list, &count, 1);
    if (usable < 2) {
        usable = BOTWEAP_listHeldLoadedWeapons(drone, list, &count, 0);
        if (usable == 0)
            return 0;
    }
    if (BOTSTATE_opponentIsMissile(drone, opponent)) {
        // the explosives in the order the bot wants them against the objective
        static const short explosives[] = {0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x40, 0x34, 0x52, 0x3a, 0x35, 0x36, 0x37};
        for (short i = 0; i < (short)(sizeof(explosives) / sizeof(explosives[0])); i++) {
            ushort *entry = &list[explosives[i]];
            if ((*entry & 0xff) != 0xff)
                *entry = (*entry & 0xff00) | (ushort)(100 - i);
        }
    }
    QuickSort(list, count, sizeof(list[0]), Compare_CustomFunction, CompareWeaponEntries);

    if (usable < 2) {
        weapon = 1;
    } else {
        if (bot->stats.personality == BOT_PERSONALITY_BERSERKER) {
            if ((list[0] & 0xff00) == Weap_Satchel << 8)
                pick = 1;
        } else {
            bool planter = false;
            if (MPSettings.GameMode == GM_PROTECTION) {
                if (MPSettings.Player[bot->playerIndex].TeamId == PHOENIX)
                    planter = true;
            } else if (MPSettings.GameMode == GM_DEMOLITION) {
                if (MPSettings.Player[bot->playerIndex].TeamId == MI6)
                    planter = true;
            }

            _VECTOR toOpponent;
            Vec_Subtract(&opponent->position, &drone->gameObj->position, &toOpponent);
            float distance = (float)Vec_Magnitude(&toOpponent);

            pick = (short)(usable - 2) < 0 ? 0 : (short)(usable - 2);
            bool other = false;     // a usable weapon other than fists and the explosives, further down the list
            for (short i = (short)(usable - 1); i >= 0; i--) {
                ushort entry = list[i];
                const weapon_definition_tag *wpn = &weapon_data[entry >> 8];
                if (wpn->botWeaponClass == 4) {
                    if (planter && (entry & 0xff00) == Weap_Satchel << 8) {
                        obj_tag *target;
                        if (MPSettings.GameMode == GM_PROTECTION)
                            target = MP_getProtectionObj_NotExt();
                        else if (MPSettings.GameMode == GM_DEMOLITION)
                            target = MP_getDemolitionObj_NotExt();
                        else
                            continue;
                        if (target != NULL && target->subState != 1 && target == opponent)
                            pick = i;
                    } else if (!other || !(distance < wpn->explodeRadius || distance == wpn->explodeRadius)) {
                        pick = i;
                    }
                } else {
                    if ((entry & 0xff00) != 0x100)
                        other = true;
                    pick = i;
                }
            }
        }
        weapon = list[pick] >> 8;
    }

    if (!BOTWEAP_isValidWeapon(weapon))
        weapon = 1;
    else if (weapon == 0)
        return 0;
    if (!noMessage && !(keepCurrent && weapon == bot->weapon))
        MP_sendBotMessage(drone->gameObj, DRONE_MSG_BotChangeWeapon, weapon, 0);
    return weapon;
}

// Changes the bot to a weapon, or to its choice against its opponent for weapon 0. Returns the weapon (0 when it
// could not change); with keepCurrent the weapon in hand is kept, and with noChange nothing is changed.
// AUTOINJECT
uint BOTSTATE_changeWeapon(BOT_vars_t *bot, uint weapon, char keepCurrent, char noChange) {
    if (weapon == 0) {
        weapon = BOTSTATE_combatWeaponChangeChoice(bot->drone, bot->drone->opponent, keepCurrent, 1, 0);
        if (weapon == 0)
            return 0;
    }
    if (keepCurrent && weapon == bot->weapon)
        return weapon;
    if (noChange)
        return weapon;
    return BOTWEAP_changeWeapon(bot->drone, (short)weapon) ? weapon : 0;
}

// After a pickup: the weapon the bot should now fight with, when it holds the weapon picked up (with checkHeld) or
// holds it loaded (with checkLoaded). 0 otherwise.
// AUTOINJECT
short BOTSTATE_pickupWeaponChangeChoice(Drone_tag *drone, short weapon, char checkHeld, char checkLoaded) {
    if (checkLoaded) {
        if (!BOTWEAP_hasLoadedWeapon(drone, weapon))
            return 0;
    } else if (checkHeld && !BOTWEAP_hasWeapon(drone, weapon)) {
        return 0;
    }
    return BOTSTATE_combatWeaponChangeChoice(drone, drone->opponent, 1, 0, 0);
}

// Ends the drone's animation at the frame it has reached, and deletes its A3 script
// AUTOINJECT
void BOTSTATE_stopAnim(Drone_tag *drone) {
    sAnimScript_tag *script = drone->animScript;
    script->numFrames = (short)Ftol(script->curFrame);
    if (drone->blendScript != NULL) {
        drone->blendScript->ownerSlot = NULL;
        AnimScriptDelete(drone->gameObj, &drone->blendScript);
    }
}
