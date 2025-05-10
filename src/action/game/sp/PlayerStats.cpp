#include "PlayerStats.h"
#include "../mp/multiplayer.h"

#pragma pack(push, 1)
typedef struct {
    uint timesDetected;
    uint shotsFired;
    uint unknown1;
    uint enemiesDispatched;
    uint enemiesDisabled;
    uint enemiesSurrendered;
    uint enemiesSpawned;
    uint health;
    uint unknown2;
    uint bondBonuses;
    uint unknown3[3];
    char timerPaused;
    char unknown4[3];
} PlayerMissionStats;

static_assert(sizeof(PlayerMissionStats) == 0x38, "PlayerMissionStats size mismatch");

#pragma pack(pop)

#define PlrMissionStats (*(PlayerMissionStats(*)[10])0x00278e70)

// AUTOGEN
void PlrStat_ResetForMission(void);

// Likely a helper function which was inlined into the below funcs
// AUTOINJECT
bool PlrStat_OkToUpdate(uint playerNum) {
  if(playerNum >= 10)
    return false;

  if(!MPSettings.isMultiplayer) {
    int missionStatus = Mission_Status();
    if (missionStatus > 1 || missionStatus < 0)
      return false;
  }

  return true;
}

// AUTOINJECT
void PlrStat_LogEnemySurrender(uint playerNum) {

    if(!PlrStat_OkToUpdate(playerNum))
        return;

    PlrMissionStats[playerNum].enemiesSurrendered++;
}

// AUTOINJECT
void PlrStat_LogEnemyDispatched(uint playerNum) {

    if(!PlrStat_OkToUpdate(playerNum))
        return;

    PlrMissionStats[playerNum].enemiesDispatched++;
}

// AUTOINJECT
void PlrStat_LogEnemyDisabled(uint playerNum) {

    if(!PlrStat_OkToUpdate(playerNum))
        return;

    PlrMissionStats[playerNum].enemiesDisabled++;
}


// AUTOINJECT
void PlrStat_LogHealth(uint health, uint playerNum) {

    if(!PlrStat_OkToUpdate(playerNum))
        return;
    
    PlrMissionStats[playerNum].health = health;
}


// AUTOINJECT
void PlrStat_LogEnemySpawned(void) {

    // Original game code does not check PlrStat_OkToUpdate, we replicate that behaviour here
    // Also does not apply to a playerNum, it's a global count

    PlrMissionStats[0].enemiesSpawned++;
}