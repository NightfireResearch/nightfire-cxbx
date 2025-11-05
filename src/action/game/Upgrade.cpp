#include "../actionhelpers.h"

uint UpgradedHandguns[4] = {
    2,
    4,
    6,
    8
};

uint UpgradedSnipers[4] = {
    0x1e,
    0x20,
    0x22,
    0x22
};

uint UpgradedSilencedSnipers[4] = {
    0x24,
    0x26,
    0x28,
    0x28
};

uint UpgradedDartGuns[4] = {
    0x43,
    0x44,
    0x44,
    0x44
};

uint UpgradedTasers[4] = {
    0x4a,
    0x4c,
    0x4c,
    0x4c
};

uint UpgradedLasers[4] = {
    0x4e,
    0x4f,
    0x4f,
    0x4f
};

uint UpgradedPDA[4] = {
    Weap_Decryptor,
    Weap_Decryptor_Upgraded,
    Weap_Decryptor_Upgraded,
    Weap_Decryptor_Upgraded
};

typedef enum {
    WUG_Handgun = 0,
    WUG_UNKNOWN1,
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

// Singleplayer weapon upgrades
// AUTOINJECT
uint Upgrade_Weapon(uint id_base) {

    const uint playerId = 0;

    switch(id_base) {

        default:
            return id_base;

        case 0x06:
            return UpgradedHandguns[Upgrades[playerId][WUG_Handgun]];

        case 0x1e:
            return UpgradedSnipers[Upgrades[playerId][WUG_Sniper]];

        case 0x24:
            return UpgradedSilencedSnipers[Upgrades[playerId][WUG_Sniper]];
            
        case 0x43:
            return UpgradedDartGuns[Upgrades[playerId][WUG_DartGun]];

        case 0x4a:
            return UpgradedTasers[Upgrades[playerId][WUG_Taser]];
            
        case 0x4e:
            return UpgradedLasers[Upgrades[playerId][WUG_Laser]];

        case Weap_Camera: {
            float maxZoom = (Upgrades[playerId][WUG_Camera] > 0 ? 16.0f : 8.0f);
            weapon_data[Weap_Camera].maxZoom = maxZoom;
            weapon_data[Weap_Camera2].maxZoom = maxZoom;
            return id_base;
        }

        case Weap_Decryptor:
            return UpgradedPDA[Upgrades[playerId][WUG_PDA]];
    
        
        case 0x0a: // 10: Kowloon 40 (Semi/Burst)

        if(GameState.WeaponUpgradeRelated == 0)
            return id_base;

        return 0x0c; // Kowloon 40 (Auto)

    
    }
}