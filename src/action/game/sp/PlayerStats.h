#ifndef PLAYERSTATS_H
#define PLAYERSTATS_H

#include "../../actionhelpers.h"

void PlrStat_ResetForMission(void);
void PlrStat_LogEnemySurrender(uint playerNum);
void PlrStat_LogEnemySpawned(void);
void PlrStat_LogEnemyDispatched(uint playerNum);
void PlrStat_LogEnemyDisabled(uint playerNum);

#endif // PLAYERSTATS_H