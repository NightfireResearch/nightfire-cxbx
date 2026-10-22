#include "weapon_stats.h"

#include <string.h>

// XBE_GLOBAL(0x0018ce08, 0x198) readonly relocate
AmmoDataEntry ammo_data[NUM_AMMO_TYPES] = {
    {0, 0, HASHCODE_NONE, (Action_TranslatedText)0x00000000},
    {20, 70, (HASHCODE)0x0200043f, (Action_TranslatedText)0x05000025},
    {25, 90, (HASHCODE)0x0200043f, (Action_TranslatedText)0x05000026},
    {24, 36, (HASHCODE)0x02000445, (Action_TranslatedText)0x0500001f},
    {15, 50, HASHCODE_NONE, (Action_TranslatedText)0x05000016},
    {10, 90, (HASHCODE)0x02000447, (Action_TranslatedText)0x05000022},
    {10, 100, (HASHCODE)0x02000446, (Action_TranslatedText)0x05000023},
    {16, 30, (HASHCODE)0x02000444, (Action_TranslatedText)0x0500001d},
    {16, 30, (HASHCODE)0x02000444, (Action_TranslatedText)0x0500001e},
    {7, 24, (HASHCODE)0x02000548, (Action_TranslatedText)0x05000017},
    {7, 32, (HASHCODE)0x02000443, (Action_TranslatedText)0x05000018},
    {14, 35, (HASHCODE)0x02000445, (Action_TranslatedText)0x05000021},
    {6, 6, HASHCODE_NONE, (Action_TranslatedText)0x0500001c},
    {6, 12, HASHCODE_NONE, (Action_TranslatedText)0x05000020},
    {4, 4, HASHCODE_NONE, (Action_TranslatedText)0x05000027},
    {0, 21, HASHCODE_NONE, (Action_TranslatedText)0x05000019},
    {0, 20, HASHCODE_NONE, (Action_TranslatedText)0x0500001a},
    {0, 20, HASHCODE_NONE, (Action_TranslatedText)0x0500001b},
    {1, 10, HASHCODE_NONE, (Action_TranslatedText)0x05000024},
    {1, 9, HASHCODE_NONE, (Action_TranslatedText)0x05000075},
    {1, 0, HASHCODE_NONE, (Action_TranslatedText)0x00000000},
    {1, 9, HASHCODE_NONE, (Action_TranslatedText)0x05000024},
    {1, 4, HASHCODE_NONE, (Action_TranslatedText)0x0500003e},
    {1, 9, HASHCODE_NONE, (Action_TranslatedText)0x0500003f},
    {1, 9, HASHCODE_NONE, (Action_TranslatedText)0x0500003c},
    {1, 9, HASHCODE_NONE, (Action_TranslatedText)0x05000031},
    {1, 0, HASHCODE_NONE, (Action_TranslatedText)0x00000000},
    {1, 0, HASHCODE_NONE, (Action_TranslatedText)0x05000040},
    {1, 4, HASHCODE_NONE, (Action_TranslatedText)0x05000049},
    {1, 4, HASHCODE_NONE, (Action_TranslatedText)0x0500004c},
    {1, 0, HASHCODE_NONE, (Action_TranslatedText)0x0500004b},
    {1, 0, HASHCODE_NONE, (Action_TranslatedText)0x00000000},
    {1, 0, HASHCODE_NONE, (Action_TranslatedText)0x00000000},
    {0, 0, HASHCODE_NONE, (Action_TranslatedText)0x00000000},
};

// The table, generated from the XBE by tools/weapon_table.py: weapon_data as the image holds it, WeaponTableData as
// the constructor leaves it, and WeaponTable_ApplyFrameRate for the fields the original computes from the frame rate
#include "WeaponTable.inc"

static_assert(sizeof(WeaponTableData) == sizeof(weapon_data), "WeaponTable.inc holds the whole table");

// The weapon table's static constructor, run with the C runtime's other initialisers before Game_Main. The original
// writes entries 0x35 onwards one store at a time (44 KB of code; 0-0x34 are initialised data in the image); this
// writes the whole table from WeaponTable.inc, then the fields that depend on the frame rate, as the original did.
// AUTOINJECT
void __stdcall WeaponDataTableInit(void) {
    memcpy(&weapon_data, WeaponTableData, sizeof(WeaponTableData));
    WeaponTable_ApplyFrameRate(weapon_data);
}
