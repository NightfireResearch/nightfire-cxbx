#ifndef PLAYERSTATS_H
#define PLAYERSTATS_H

#include "../../actionhelpers.h"

#pragma pack(push, 1)

typedef struct SCORETABLE {
    HASHCODE levelHashcode;
    uint thresholdScoreBronze;
    uint thresholdScoreSilver;
    uint thresholdScoreGold;
    uint thresholdScorePlatinum;
    float unknown1;
    void* statsTable;
    uint unknown2[6];
    uint score;
    char isAction;
    char unknown3[3];
} SCORETABLE;

static_assert(sizeof(SCORETABLE) == 0x3c, "SCORETABLE size mismatch");

#pragma pack(pop)

bool PlrStat_OkToUpdate(void);
void PlrStat_ResetForMission(void);
void PlrStat_LogEnemySurrender(uint playerNum);
void PlrStat_LogEnemySpawned(void);
void PlrStat_LogEnemyDispatched(uint playerNum);
void PlrStat_LogEnemyDisabled(uint playerNum);
void PlrStat_LogEnemyDetectedPlayer(uint playerNum);
void PlrStat_LogHealth(uint health, uint playerNum);
SCORETABLE * PlrStats_GetLevelTotals(HASHCODE hashcode);
void PlarStat_LogTimerPause(uint playerNum);
void PlarStat_LogTimerUnpause(uint playerNum);

#endif // PLAYERSTATS_H