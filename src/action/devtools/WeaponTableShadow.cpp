// Shadow test for WeaponDataTableInit (src/action/game/weapon_stats.cpp): the original constructor and ours must
// leave the same weapon table behind, byte for byte.
//
// This runs during injection, before the C runtime calls the static constructors, so neither has run yet: ours is
// run, then the original over it (it writes entries 0x35 onwards; 0-0x34 are initialised data, which ours rewrites
// with the same values), and the two are compared. The C runtime runs ours again afterwards, as usual. Run at start
// with MenuShadowTests=on (MenuProbe.cpp).

#include "WeaponTableShadow.h"

#include "../../common/xbeOriginal.h"
#include "../game/weapon_stats.h"

#include <stdio.h>
#include <string.h>

static const unsigned kWeaponDataTableInit = 0x000f5530;

void WeaponTableShadow_Run(void) {
    static uint8_t ours[sizeof(weapon_data)];
    WeaponDataTableInit();
    memcpy(ours, &weapon_data, sizeof(ours));
    {
        XbeOriginalScope original(kWeaponDataTableInit);
        ((void(__stdcall *)(void))(uintptr_t)kWeaponDataTableInit)();
    }
    const uint8_t *theirs = (const uint8_t *)&weapon_data;
    int mismatches = 0;
    for (size_t i = 0; i < sizeof(ours); i++) {
        if (ours[i] != theirs[i]) {
            if (mismatches++ < 8)
                printf("[weapons] entry %u +0x%03x: ours %02x, the original's %02x\n", (unsigned)(i / sizeof(weapon_data[0])),
                       (unsigned)(i % sizeof(weapon_data[0])), ours[i], theirs[i]);
        }
    }
    memcpy(&weapon_data, ours, sizeof(ours));
    printf("[weapons] weapon table: %d of %u bytes differ from the original constructor's\n", mismatches,
           (unsigned)sizeof(ours));
}
