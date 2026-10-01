#include "weapon_stats.h"

#include <string.h>

// The table's contents, generated from the XBE by tools/weapon_table.py: WeaponTableData, and
// WeaponTable_ApplyFrameRate for the fields the original computes from the frame rate
#include "WeaponTable.inc"

static_assert(sizeof(WeaponTableData) == sizeof(weapon_data), "WeaponTable.inc holds the whole table");

// The weapon table's static constructor, run with the C runtime's other initialisers before Game_Main. The original
// writes entries 0x35 onwards one store at a time (44 KB of code; 0-0x34 are initialised data in the image); this
// writes the whole table from WeaponTable.inc, then the fields that depend on the frame rate, as the original did.
// The table itself stays at 0x0018cfa0 while original code still reads it there.
// AUTOINJECT
void __stdcall WeaponDataTableInit(void) {
    memcpy(&weapon_data, WeaponTableData, sizeof(WeaponTableData));
    WeaponTable_ApplyFrameRate(weapon_data);
}
