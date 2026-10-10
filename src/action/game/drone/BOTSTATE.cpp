// A multiplayer bot's goals - a pickup, an opponent or the game mode's objective - and the opponent its personality
// prefers. See docs/drone/bots-and-navigation/README.md 5.4 and 5.6.

#include "BOTSTATE.h"
#include "Drone.h"
#include "NDrone2.h"            // Drone_SM_SetState, the DSTATEs
#include "../mp/multiplayer.h"
#include "../../game.h"         // MPGame
#include "../obj/Pickup.h"      // PICKUPINFO
#include <math.h>

// Generated from its tag in multiplayer.cpp
short Control_Plr2Ind(obj_tag* a);

// AUTOGEN
void BOTSTATE_uninitGoal(BOT_vars_t *bot, uint slot);
// AUTOGEN
void BOTSTATE_setGoalPickPrefs(BOT_vars_t *bot, uint slot, float health, float ammo, float weapon, float objective, uchar param7, uint param8);
// AUTOGEN
bool BOTSTATE_opponentIsMissile(Drone_tag *drone, obj_tag *obj);
// AUTOGEN
bool BOTSTATE_isPathWithinObjectRange(Drone_tag *drone, obj_tag *obj, float range, int segments);
// AUTOGEN
void NDrone2_InvalidateAttackRoute(DCVars_tag *dc);
obj_tag* __stdcall MP_getDemolitionObj_NotExt(void);
void* __stdcall MP_getProtectionObj_NotExt(void);

// Originals whose Ghidra prototypes do not fit the calls: pointers typed as ints, a state number passed as a float,
// a missing argument, a result left in ST0
#define BOTSTATE_gotoGoal ((uchar (__cdecl *)(DCVars_tag *dc, int slot, uint nextState))0x0001bc30)
#define BOTSTATE_setStateChange ((void (__cdecl *)(Drone_tag *drone, uint state))0x0001c7f0)
#define BOTSTATE_validateRoute ((uchar (__cdecl *)(DCVars_tag *dc, uint routeStatus))0x0001c240)
#define BOTSTATE_setPickupVisitTime ((void (__cdecl *)(Drone_tag *drone, MP_PICKUP *pickup))0x0001c550)
#define BOTSTATE_isPreferredWeapon ((uchar (__cdecl *)(BOT_vars_t *bot, uint weapon))0x0001c0d0)
#define BOTSTATE_hasWeapon ((uchar (__cdecl *)(BOT_vars_t *bot, uint weapon))0x0001c180)
#define NDrone2_DistanceToAIPoint ((float (__cdecl *)(DCVars_tag *dc, AIPoint_tag *point))0x00044e70)
#define NDrone2_DistanceToEmitter ((uchar (__cdecl *)(CelPos_tag *from, AIPath_tag *path, ushort *nearestNode, AIEmitter_tag *emitter, obj_tag *mover, float *distance))0x00046260)
#define NDrone2_MoveToGoalPosition ((uint (__cdecl *)(DCVars_tag *dc, _VECTOR *pos, float radius, ushort param4))0x00046f30)
#define MP_getUplinkObj ((MP_OBJ_EXT *(__cdecl *)(int idx, int filterState))0x0009e230)
#define MP_getBlueprintObj ((MP_OBJ_EXT *(__cdecl *)(void))0x0009e2e0)
#define MP_getEsponageBaseObj ((MP_OBJ_EXT *(__cdecl *)(uint team, int approachQuadrant))0x0009e2f0)
#define MP_getGoldenEyeObj ((MP_OBJ_EXT *(__cdecl *)(uint key))0x0009e3a0)
#define MP_getAssassinTarget ((obj_tag *(__cdecl *)(void))0x0009e7b0)
#define MP_BluePrintReachedBase ((void (__cdecl *)(obj_tag *blueprint, void *blueprintData, int param3))0x000a0ec0)

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
                if (other == NULL || !(bot->players[i].flags & BOTPLAYER_TEAMMATE) || MP_playerIsDead(other))
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
                if (other == NULL || other == me || (bot->players[i].flags & BOTPLAYER_TEAMMATE)
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
                if ((info->flags & BOTPLAYER_PRESENT) && !(info->flags & BOTPLAYER_TEAMMATE)
                    && info->distanceSq < nearest) {
                    nearest = info->distanceSq;
                    preferred = (uchar)i;
                }
            }
            return preferred;
        }

        case BOT_PERSONALITY_VENGEFUL: {
            // whoever killed the bot last
            short killer = MPGame.players[drone->botVars->playerIndex].maybeIdxOfMyAssassin;
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
                if (other == NULL || other == me || (bot->players[i].flags & BOTPLAYER_TEAMMATE)
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

                    float visited = pickup->maybeBotPickupVisitTimes[botIndex];
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
                            if (pass == 0 && (!BOTSTATE_isPreferredWeapon(bot, info->weaponId)
                                              || BOTSTATE_hasWeapon(bot, info->weaponId))) {
                                if (!defending || weapon_data[info->weaponId].botWeaponClass != 4)
                                    continue;
                                closeness = closeness + closeness;
                            }
                            score = (double)closeness * goal->weightWeapon;
                            break;
                        case 1:     // ammo
                            if (goal->weightAmmo == 0.0f)
                                continue;
                            if (pass == 0 && !BOTSTATE_hasWeapon(bot, info->weaponId))
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
        uint status = NDrone2_MoveToGoalPosition(dc, &drone->aiPoint.pos, drone->aiPoint.radius, 0);
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
            owner->flags |= 4;
        else
            owner->flags &= ~4;
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
                    if ((info->flags & BOTPLAYER_PRESENT) && (info->flags & BOTPLAYER_TEAMMATE)
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

        if (!(goal->flags & 1) && goal->lastRouteStatus != 3)
            continue;

        // Arrived
        if (goal->kind == BOT_GOAL_OBJECT && object == bot->guardFriend) {
            short other = Control_Plr2Ind(object);
            if (other == -1) {
                goal->arriveOrNextState = DSTATE_BotIdle;
            } else {
                // the friend moved off again: follow
                BOT_playerInfo_t *info = &bot->players[other];
                if ((info->flags & BOTPLAYER_PRESENT) && (info->flags & BOTPLAYER_TEAMMATE)
                    && info->distanceSq > 20.25f) {
                    BOTSTATE_gotoGoal(dc, slot, goal->arriveOrNextState);
                    continue;
                }
            }
        } else if (goal->kind == BOT_GOAL_OBJECTIVE && MPSettings.GameMode == GM_BLUEPRINT
                   && objective == MP_getEsponageBaseObj(team, -1)
                   && (MPGame.players[bot->playerIndex].flags & MPPLAYER_FLAG_2)
                   && MP_getBlueprintObj() != NULL && MP_getBlueprintObj()->gameObj != NULL) {
            MP_BluePrintReachedBase(MP_getBlueprintObj()->gameObj, MP_getBlueprintObj()->gameObj->extraObjectData, 0);
        }

        NDrone2_InvalidateAttackRoute(dc);
        if (goal->kind == BOT_GOAL_PICKUP)
            BOTSTATE_setPickupVisitTime(drone, pickup);
        BOTSTATE_setStateChange(drone, goal->arriveOrNextState);
        BOTSTATE_uninitGoal(bot, slot);
    }
}
