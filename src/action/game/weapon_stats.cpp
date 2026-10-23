#include "weapon_stats.h"

#include <string.h>

// XBE_GLOBAL(0x0018ce08, 0x198) readonly relocate
AmmoDataEntry ammo_data[NUM_AMMO_TYPES] = {
    {0, 0, HASHCODE_NONE, (Action_TranslatedText)0x00000000},
    {20, 70, HT_Entity_OBJECT_SHELL_7_65mm9mm, (Action_TranslatedText)0x05000025},
    {25, 90, HT_Entity_OBJECT_SHELL_7_65mm9mm, (Action_TranslatedText)0x05000026},
    {24, 36, HT_Entity_OBJECT_SHELL_357Magnum, (Action_TranslatedText)0x0500001f},
    {15, 50, HASHCODE_NONE, (Action_TranslatedText)0x05000016},
    {10, 90, HT_Entity_OBJECT_SHELL_5_56NATO, (Action_TranslatedText)0x05000022},
    {10, 100, HT_Entity_OBJECT_SHELL_5_7mm, (Action_TranslatedText)0x05000023},
    {16, 30, HT_Entity_OBJECT_SHELL_300Magnum, (Action_TranslatedText)0x0500001d},
    {16, 30, HT_Entity_OBJECT_SHELL_300Magnum, (Action_TranslatedText)0x0500001e},
    {7, 24, HT_Entity_GaugeRound, (Action_TranslatedText)0x05000017},
    {7, 32, HT_Entity_OBJECT_SHELL_12gMagnum, (Action_TranslatedText)0x05000018},
    {14, 35, HT_Entity_OBJECT_SHELL_357Magnum, (Action_TranslatedText)0x05000021},
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
