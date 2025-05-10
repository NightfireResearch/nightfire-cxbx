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


// AUTOINJECT
void PlrStat_LogEnemySurrender(uint playerNum) {
  bool bVar1;
  long lVar2;

  if(playerNum >= 10)
    return;

  if (!MPSettings.isMultiplayer) {
    int missionStatus = Mission_Status();
    if (missionStatus > 1 || missionStatus < 0)
      return;
  }

  PlrMissionStats[playerNum].enemiesSurrendered++;
}
