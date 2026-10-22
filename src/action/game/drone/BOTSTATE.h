#ifndef BOTSTATE_H
#define BOTSTATE_H

// A bot's goals and its choice of opponent (docs/drone/bots-and-navigation/README.md 5.4 and 5.6).

#include "../../actionhelpers.h"
#include "BOT.h"
#include "../mp/multiplayer.h"     // MP_PICKUP

// BOT_goal_t.kind: what the goal's target is
enum {
    BOT_GOAL_NONE = 0,
    BOT_GOAL_PICKUP = 1,        // an MP_PICKUP
    BOT_GOAL_OBJECTIVE = 2,     // the game mode's MP_OBJ_EXT
    BOT_GOAL_OBJECT = 3,        // an opponent or a friend (obj_tag)
};

// BOT_goal_t.subtype: the objective the goal is for
enum {
    BOT_OBJECTIVE_ENEMY_FLAG = 1,
    BOT_OBJECTIVE_OWN_BASE = 2,
    BOT_OBJECTIVE_GOLDENEYE = 3,
    BOT_OBJECTIVE_BLUEPRINT = 4,
    BOT_OBJECTIVE_ESPIONAGE_BASE = 5,
    BOT_OBJECTIVE_UPLINK = 6,
    BOT_OBJECTIVE_HILL = 7,
    BOT_OBJECTIVE_DEFEND_OR_DESTROY = 8,    // the demolition or protection object
    BOT_OBJECTIVE_OPPONENT = 9,
};

// BOT_goal_t.pickFlags
#define BOT_PICK_SINGLE_PASS 2
#define BOT_PICK_SKIP_RESPAWNING 4      // skip every respawning pickup, not only the near ones
#define BOT_PICK_AVOID_OPPONENT 8       // plan the route at once and pick again if it passes the opponent
#define BOT_PICK_IGNORE_VISITS 0x20

bool BOTSTATE_isObjAlreadyAnotherTeamObjective(obj_tag *obj, int playerIndex);
uchar BOTSTATE_getPreferredTraitOpponentObjIndex(Drone_tag *drone);    // a player index, 0xff none
uchar BOTSTATE_pickGoal(DCVars_tag *dc, int slot);
short BOTSTATE_chooseUnarmedAttackAnim(DCVars_tag *dc);
short BOTSTATE_checkAttackMove(DCVars_tag *dc, short state);   // the state, or 0 when refused
short BOTSTATE_EvasiveMove(DCVars_tag *dc);
bool BOTSTATE_setNearestNavNode(DCVars_tag *dc);
void BOTSTATE_uninitGoal(BOT_vars_t *bot, uint slot);
void BOTSTATE_initGoal(BOT_vars_t *bot, int slot, uchar kind, CelPos_tag *pos, void *target, uchar subtype);
uchar BOTSTATE_gotoGoal(DCVars_tag *dc, int slot, uint nextState);
void BOTSTATE_setGoalComplete(DCVars_tag *dc, uint slot);
BOT_goal_t* BOTSTATE_getActiveGoal(Drone_tag *drone);
void BOTSTATE_setGoalPickPrefs(BOT_vars_t *bot, uint slot, float health, float ammo, float weapon, float objective,
                               uchar maxEmitterDistance, uchar pickFlags);
bool BOTSTATE_isPreferredWeapon(BOT_vars_t *bot, uint weapon);
bool BOTSTATE_hasWeapon(BOT_vars_t *bot, uint weapon);
uchar BOTSTATE_validateRoute(DCVars_tag *dc, uint routeStatus);
void BOTSTATE_stopAnim(Drone_tag *drone);
bool BOTSTATE_isPathWithinObjectRange(Drone_tag *drone, obj_tag *obj, float range, int segments);
void BOTSTATE_setPickupVisitTime(Drone_tag *drone, MP_PICKUP *pickup);
void BOTSTATE_startRecovery(DCVars_tag *dc);
bool BOTSTATE_opponentIsMissile(Drone_tag *drone, obj_tag *obj);   // obj -1: the drone's opponent
void BOTSTATE_reallyCloseCombatRange(Drone_tag *drone);
void BOTSTATE_defaultCombatRange(Drone_tag *drone);
uchar BOTSTATE_increaseDistraction(Drone_tag *drone, float amount);
uchar BOTSTATE_isDistracted(Drone_tag *drone);
void BOTSTATE_setStateChange(Drone_tag *drone, uint state);
short BOTSTATE_chooseCombatMove(DCVars_tag *dc);
short BOTSTATE_chooseDodgeMove(DCVars_tag *dc, uint directions);
uchar BOTSTATE_removeGoalsForObject(DCVars_tag *dc, obj_tag *obj, uint state);
short BOTSTATE_combatWeaponChangeChoice(Drone_tag *drone, obj_tag *opponent, char keepCurrent, char noMessage,
                                        char unused);
uint BOTSTATE_changeWeapon(BOT_vars_t *bot, uint weapon, char keepCurrent, char noChange);
short BOTSTATE_pickupWeaponChangeChoice(Drone_tag *drone, short weapon, char checkHeld, char checkLoaded);
void BOTSTATE_processGoals(DCVars_tag *dc);

#endif // BOTSTATE_H
