#include "weapon_stats.h"

// The original constructor is located at 000f5530
void callOriginal(void) {
    reinterpret_cast<void (*)(void)>(0x000f5530)();
}

// On Xbox, weapon stats are set up partially in the compiled code, but partially (from 0x35 onwards) in a constructor, called before Game_Main()
// We have a special case for injecting all the data...
void ctor_WeaponDefinitionTable(void) {

    // Call the original constructor
    callOriginal();

    // Apply our patches to test that it does the right thing

    // Make the PP7 an instant kill
    // weapon_data[2].damage = 2000; // PP7, regular
    // weapon_data[3].damage = 2000; // PP7, regular, silenced
    // weapon_data[4].damage = 2000; // PP7, gold
    // weapon_data[5].damage = 2000; // PP7, gold, silenced

}