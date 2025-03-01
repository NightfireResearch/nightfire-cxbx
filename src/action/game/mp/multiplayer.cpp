#include "multiplayer.h"
#include "../../game.h"
#include "../gamestate.h"

// AUTOGEN
unsigned int Control_Plr2Ind(obj_tag* a);


// AUTOINJECT
bool MP_areObjectsOnSameTeam(obj_tag* a, obj_tag* b) {
   
    if ((MPSettings.relatedToTeamIdentitySomehow == 0) && (MPSettings.GameMode != GM_ASSASSIN))
        return false;
    
    short idx_a = Control_Plr2Ind(a);
    short idx_b = Control_Plr2Ind(b);

    if ((idx_a > -1) && (idx_b > -1)) {
        
        int team_a = MPSettings.Player[idx_a].TeamId;
        int team_b = MPSettings.Player[idx_b].TeamId;
        
        if ((team_a != 2) && (team_b != 2))
            return team_a == team_b;
        
    }

    return false;
}
