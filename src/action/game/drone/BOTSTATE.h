#ifndef BOTSTATE_H
#define BOTSTATE_H

// A bot's goals and its choice of opponent (docs/drone/bots-and-navigation/README.md 5.4 and 5.6).

#include "../../actionhelpers.h"
#include "BOT.h"

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
#define BOT_PICK_SKIP_UNKNOWN18_2 4     // skip every pickup whose PICKUPINFO.unknown18 is 2, not only the near ones
#define BOT_PICK_AVOID_OPPONENT 8       // plan the route at once and pick again if it passes the opponent
#define BOT_PICK_IGNORE_VISITS 0x20

bool BOTSTATE_isObjAlreadyAnotherTeamObjective(obj_tag *obj, int playerIndex);
uchar BOTSTATE_getPreferredTraitOpponentObjIndex(Drone_tag *drone);    // a player index, 0xff none
uchar BOTSTATE_pickGoal(DCVars_tag *dc, int slot);
void BOTSTATE_processGoals(DCVars_tag *dc);

#endif // BOTSTATE_H
