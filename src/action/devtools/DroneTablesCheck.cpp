// Compares the drone tables (src/action/game/drone/DroneTables.cpp, from the initialisers tools/drone_tables.py
// generates) with the game's copies, byte for byte, at start (MenuCheckLists=on, MenuProbe.cpp): that the initialisers
// compile to exactly the XBE's bytes with our struct layouts.

#include "DroneTablesCheck.h"

#include "../game/drone/DroneTables.h"
#include "../../common/xbeOriginal.h"

#include <stdio.h>
#include <string.h>

// The game's copies
#define GameStateFuncs 0x00177ca0
#define GameModeSettings 0x001776f8
#define GameTypeSettings 0x001778a0
#define GameBehaviourLayouts 0x001634e0
#define GameBehaviourMasks 0x00163598
#define GameBotStats 0x00163628

// Word by word. A function the tables name by C++ name is ours once ported, while the game's copy holds its address,
// whose jump leads to ours: the same function, so a word that differs only that way matches.
static int Compare(const char *name, const void *ours, const void *theirs, size_t size, size_t entry) {
    if (memcmp(ours, theirs, size) == 0)
        return 0;
    for (size_t i = 0; i < size; i += 4) {
        uint32_t a = 0, b = 0;
        size_t n = size - i < 4 ? size - i : 4;
        memcpy(&a, (const uchar *)ours + i, n);
        memcpy(&b, (const uchar *)theirs + i, n);
        if (a != b && XbeOriginal_Target(b) != a) {
            printf("[drone tables] %s differs from entry %u (byte 0x%x): ours %08x, the game's %08x\n", name,
                   (uint)(i / entry), (uint)i, a, b);
            return 1;
        }
    }
    return 0;
}

void DroneTablesCheck_Run(void) {
    int differ = 0;
    differ += Compare("NDrone2_StateFuncs", NDrone2_StateFuncs, (const void *)GameStateFuncs,
                      sizeof(NDrone2_StateFuncs), sizeof(NDrone2_StateFuncs[0]));
    differ += Compare("DroneModeSettings", DroneModeSettings, (const void *)GameModeSettings, sizeof(DroneModeSettings),
                      sizeof(DroneModeSettings[0]));
    differ += Compare("DroneTypeSettings", DroneTypeSettings, (const void *)GameTypeSettings, sizeof(DroneTypeSettings),
                      sizeof(DroneTypeSettings[0]));
    differ += Compare("DroneBehaviourLayouts", DroneBehaviourLayouts, (const void *)GameBehaviourLayouts,
                      sizeof(DroneBehaviourLayouts), sizeof(DroneBehaviourLayouts[0]));
    differ += Compare("DroneBehaviourMasks", DroneBehaviourMasks, (const void *)GameBehaviourMasks,
                      sizeof(DroneBehaviourMasks), 4);
    differ += Compare("DefaultBotStats", DefaultBotStats, (const void *)GameBotStats, sizeof(DefaultBotStats),
                      sizeof(DefaultBotStats[0]));
    printf("[drone tables] 6 tables checked against the game's: %d differ\n", differ);
}
