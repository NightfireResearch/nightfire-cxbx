#ifndef UPGRADE_H_
#define UPGRADE_H_

#include "../actionhelpers.h"

// Which upgrade level each player has reached in each group (0-3); Set_Upgrade and ResetUpgrade write it.
typedef enum {
    WUG_Handgun = 0,
    WUG_Grapple = 1,     // multiplayer only: Upgrade_MPWeapon picks the grapple from it
    WUG_Camera = 2,
    WUG_Sniper = 3,
    WUG_DartGun = 4,
    WUG_PDA = 5,
    WUG_Taser = 6,
    WUG_Laser = 7,
    WUG_UNKNOWN8,
} WeaponUpgradeGroup;

// Upgrades is an array of type uchar[4][9] located at 0x00279120. First index is player ID, second is weapon upgrade group
#define Upgrades (*(uchar(*)[4][9])0x00279120)

// The weapon each upgrade level turns a base weapon into (Upgrade.cpp)
extern uint UpgradedHandguns[4];
extern uint UpgradedSnipers[4];
extern uint UpgradedSilencedSnipers[4];
extern uint UpgradedDartGuns[4];
extern uint UpgradedTasers[4];
extern uint UpgradedLasers[4];
extern uint UpgradedPDA[4];
extern uint UpgradedPPK_MP[4];
extern uint UpgradedP99_MP[4];
extern uint UpgradedGrapple_MP[4];

uint Upgrade_Weapon(uint id_base);
undefined4 Upgrade_MPWeapon(short playerNum, undefined4 weapon);


#endif // UPGRADE_H_