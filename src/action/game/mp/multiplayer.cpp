#include "multiplayer.h"
#include "../../game.h"
#include "../gamestate.h"

#define CurrentAssassinObjId (((obj_tag *)0x0026178c))
#define AssassinTarget (((obj_tag *)0x00261788))
// AUTOGEN
unsigned int Control_Plr2Ind(obj_tag* a);


// AUTOINJECT
bool MP_areObjectsOnSameTeam(obj_tag* a, obj_tag* b) {
   
    if ((MPSettings.relatedToTeamIdentitySomehow == 0) && (MPSettings.GameMode != GM_ASSASSIN))
        return false;
    
    short idx_a = Control_Plr2Ind(a);
    short idx_b = Control_Plr2Ind(b);

    if ((idx_a > -1) && (idx_b > -1)) {
        
        MPTeam team_a = MPSettings.Player[idx_a].TeamId;
        MPTeam team_b = MPSettings.Player[idx_b].TeamId;
        
        if ((team_a != NO_TEAM) && (team_b != NO_TEAM))
            return team_a == team_b;
        
    }

    return false;
}

// AUTOINJECT
MPTeam MP_getObjectTeam(obj_tag* param_1) {
  
  if ((MPSettings.relatedToTeamIdentitySomehow == 0) && (MPSettings.GameMode != GM_ASSASSIN)) {
    return NO_TEAM;
  }

  short idx = Control_Plr2Ind(param_1);
  return MPSettings.Player[idx].TeamId;
}

// AUTOINJECT
bool MP_IsAssasin(obj_tag *param_1) {
  return ((CurrentAssassinObjId != NULL) && (CurrentAssassinObjId == param_1));
}

// AUTOINJECT
bool MP_IsTarget(obj_tag *param_1) {
  return ((AssassinTarget != NULL) && (AssassinTarget == param_1));
}