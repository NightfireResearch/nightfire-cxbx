#ifndef WEAPON_STATS_H
#define WEAPON_STATS_H

#include "../actionhelpers.h"
#include "../game.h"

// The table is at 0x0018cfa0 and consists of 115 entries
#define weapon_data (*(weapon_definition_tag(*)[115])0x0018cfa0)

// One per ammo type (weapon_definition_tag::ammoType): 34 entries at 0x0018ce08
typedef struct {
    short maybeClipSize;
    short maybeMaxNumPerPlayer; // the most of this ammo a player can carry, not counting what is in the gun
    uint casingHashcode;
    Action_TranslatedText ammoName;
} AmmoDataEntry;
static_assert(sizeof(AmmoDataEntry) == 12, "Bad size for AmmoDataEntry");

#define NUM_AMMO_TYPES 34
#define ammo_data (*(AmmoDataEntry(*)[NUM_AMMO_TYPES])0x0018ce08)

// The table's static constructor (0x000f5530): fills it from WeaponTable.inc (weapon_stats.cpp)
void __stdcall WeaponDataTableInit(void);

#endif // WEAPON_STATS_H