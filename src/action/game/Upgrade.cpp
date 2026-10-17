#include "../actionhelpers.h"
#include "Upgrade.h"

// XBE_GLOBAL(0x00181b14, 0x10)
uint UpgradedHandguns[4] = {
    2,
    4,
    6,
    8
};

// XBE_GLOBAL(0x00181b34, 0x10)
uint UpgradedSnipers[4] = {
    0x1e,
    0x20,
    0x22,
    0x22
};

// XBE_GLOBAL(0x00181b44, 0x10)
uint UpgradedSilencedSnipers[4] = {
    0x24,
    0x26,
    0x28,
    0x28
};

// XBE_GLOBAL(0x00181b74, 0x10)
uint UpgradedDartGuns[4] = {
    0x43,
    0x44,
    0x44,
    0x44
};

// XBE_GLOBAL(0x00181b54, 0x10)
uint UpgradedTasers[4] = {
    0x4a,
    0x4c,
    0x4c,
    0x4c
};

// XBE_GLOBAL(0x00181b24, 0x10)
uint UpgradedLasers[4] = {
    0x4e,
    0x4f,
    0x4f,
    0x4f
};

// XBE_GLOBAL(0x00181b64, 0x10)
uint UpgradedPDA[4] = {
    Weap_Decryptor,
    Weap_Decryptor_Upgraded,
    Weap_Decryptor_Upgraded,
    Weap_Decryptor_Upgraded
};

// The multiplayer versions: the handgun upgrades differ by which pistol the scenario hands out, and the grapple
// is upgradable only in multiplayer.
// XBE_GLOBAL(0x00181b84, 0x10)
uint UpgradedPPK_MP[4] = {
    2,
    4,
    4,
    4
};

// XBE_GLOBAL(0x00181b94, 0x10)
uint UpgradedP99_MP[4] = {
    6,
    6,
    6,
    8
};

// XBE_GLOBAL(0x00181ba4, 0x10)
uint UpgradedGrapple_MP[4] = {
    80,
    81,
    81,
    81
};

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
            weapon_data[Weap_Camera_Upgraded].maxZoom = maxZoom;
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

// Multiplayer weapon upgrades: every variant of an upgradable weapon maps to the one this player's upgrade level
// gives. A jump table in the original; anything else comes back unchanged.
// AUTOINJECT
undefined4 Upgrade_MPWeapon(short playerNum, undefined4 weapon) {
    switch (weapon) {
        case 2:
        case 4:
            return UpgradedPPK_MP[Upgrades[playerNum][WUG_Handgun]];
        case 6:
        case 8:
            return UpgradedP99_MP[Upgrades[playerNum][WUG_Handgun]];
        case 0x1e:
        case 0x20:
        case 0x22:
            return UpgradedSnipers[Upgrades[playerNum][WUG_Sniper]];
        case 0x24:
        case 0x26:
        case 0x28:
            return UpgradedSilencedSnipers[Upgrades[playerNum][WUG_Sniper]];
        case 0x50:
            return UpgradedGrapple_MP[Upgrades[playerNum][WUG_Grapple]];
        default:
            return weapon;
    }
}
