// Shadow test for the weapon upgrade readers (src/action/game/Upgrade.cpp, Player.cpp): each is called through
// the original and through ours from the same starting state, and the results compared.
//
//   - Upgrade_MPWeapon, for every player, every upgrade level of the handgun, grapple and sniper groups, and
//     every weapon number up to 0x60.
//   - Player_EquipAmmo, on a scratch BLData, for every weapon, several amounts (none, a few, a clip's worth, more
//     than the stock can take, negative) and starting states (stock empty, part full, one short, full; gun empty
//     or loaded), at each sniper upgrade level: the return value's low 16 bits and the whole scratch BLData.
//
// Run at start with MenuShadowTests=on (MenuProbe.cpp); the upgrade levels are put back afterwards.

#include "UpgradeShadow.h"

#include "../../common/xbeOriginal.h"
#include "../actionhelpers.h"
#include "../game/Upgrade.h"
#include "../game/weapon_stats.h"
#include "../game/obj/Player.h"

#include <stdio.h>
#include <string.h>

static const unsigned kUpgradeMPWeapon = 0x000b9d40;
static const unsigned kPlayerEquipAmmo = 0x000ba790;
static int unreachable;   // runs where the original could not be put back, so "theirs" was ours too

typedef undefined4(__cdecl *MPWeaponFn)(short, undefined4);
typedef uint(__cdecl *EquipAmmoFn)(BLData *, short, short);

static int TestMPWeapon(int *runs) {
    int mismatches = 0;
    for (short player = 0; player < 4; player++)
        for (uchar level = 0; level < 4; level++) {
            Upgrades[player][WUG_Handgun] = level;
            Upgrades[player][WUG_Grapple] = level;
            Upgrades[player][WUG_Sniper] = level;
            for (undefined4 weapon = 0; weapon <= 0x60; weapon++) {
                undefined4 theirs;
                {
                    XbeOriginalScope original(kUpgradeMPWeapon);
                    unreachable += !original.ok;
                    theirs = ((MPWeaponFn)kUpgradeMPWeapon)(player, weapon);
                }
                undefined4 ours = Upgrade_MPWeapon(player, weapon);
                (*runs)++;
                if (theirs != ours) {
                    mismatches++;
                    printf("[upgrades] MISMATCH Upgrade_MPWeapon(player %d, weapon 0x%x) at level %d: original 0x%x, "
                           "ours 0x%x\n", player, weapon, level, theirs, ours);
                }
            }
        }
    return mismatches;
}

static int TestEquipAmmo(int *runs) {
    static BLData theirsData, oursData, start;
    const short amounts[] = {0, 1, 3, 7, 30, 200, 1000, -3};
    int mismatches = 0;
    for (uchar level = 0; level < 4; level++) {
        Upgrades[0][WUG_Sniper] = level;
        for (short weapon = 0; weapon < NUM_WEAPONS; weapon++) {
            uchar type = weapon_data[weapon].ammoType;
            short max = type < NUM_AMMO_TYPES ? ammo_data[type].maybeMaxNumPerPlayer : 0;
            short stocks[] = {0, (short)(max / 2), (short)(max - 1), max};
            for (short stock : stocks)
                for (short clip = 0; clip < 2; clip++)
                    for (short amount : amounts) {
                        memset(&start, 0xcd, sizeof(start));   // everything it should leave alone is noticed
                        for (int t = 0; t < NUM_AMMO_TYPES; t++)
                            start.ammo[t] = stock;
                        for (int w = 0; w < NUM_WEAPONS; w++)
                            start.weaponStats[w].clipOrCooldown = clip ? 5 : 0;
                        theirsData = start;
                        oursData = start;
                        uint theirs;
                        {
                            XbeOriginalScope original(kPlayerEquipAmmo);
                            unreachable += !original.ok;
                            theirs = ((EquipAmmoFn)kPlayerEquipAmmo)(&theirsData, weapon, amount);
                        }
                        uint ours = Player_EquipAmmo(&oursData, weapon, amount);
                        (*runs)++;
                        if ((ushort)theirs != (ushort)ours || memcmp(&theirsData, &oursData, sizeof(BLData)) != 0) {
                            mismatches++;
                            if (mismatches <= 20)
                                printf("[upgrades] MISMATCH Player_EquipAmmo(weapon %d, amount %d), stock %d, clip %d, "
                                       "sniper level %d: original %d, ours %d%s\n", weapon, amount, stock, clip, level,
                                       (short)theirs, (short)ours,
                                       memcmp(&theirsData, &oursData, sizeof(BLData)) ? " (BLData differs)" : "");
                        }
                    }
        }
    }
    return mismatches;
}

void UpgradeShadow_Run(void) {
    uchar saved[4][9];
    memcpy(saved, &Upgrades, sizeof(saved));
    int runs = 0;
    int mp = TestMPWeapon(&runs);
    int mpRuns = runs;
    int ammo = TestEquipAmmo(&runs);
    memcpy(&Upgrades, saved, sizeof(saved));
    printf("[upgrades] Upgrade_MPWeapon: %d runs, %d mismatches; Player_EquipAmmo: %d runs, %d mismatches%s\n",
           mpRuns, mp, runs - mpRuns, ammo, unreachable ? " - BUT THE ORIGINALS COULD NOT BE REACHED" : "");
}
