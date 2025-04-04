#ifndef WEAPON_STATS_H
#define WEAPON_STATS_H

#include "../actionhelpers.h"
#include "../game.h"

// The table is at 0x0018cfa0 and consists of 115 entries
#define weapon_data (*(weapon_definition_tag(*)[115])0x0018cfa0)

void ctor_WeaponDefinitionTable(void);

#endif // WEAPON_STATS_H