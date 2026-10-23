// A multiplayer bot's weapons. A bot holds weapons by id in BOT_vars_t.weapons, each with the rounds in its gun; the
// rounds of an alt-fire variant that shares its base weapon's ammo are kept in the base's slot (Player_AmmoIndex).
// The ammo carried beside the guns is per ammo type, in BOT_vars_t.reserveAmmo, up to ammo_data's maximum. Only ids
// 0-82 are walked: the ones above are gadgets and vehicle weapons.

#include "BOTWEAP.h"
#include "BOTSTATE.h"
#include "Drone.h"
#include "DroneWeapon.h"
#include "../mp/multiplayer.h"
#include "../obj/Pickup.h"      // PickupMatrix
#include "../obj/Player.h"      // Player_AmmoIndex
#include "../weapon_stats.h"
#include "../../game.h"
#include "../../util/hashtable.h"

#include <math.h>
#include <string.h>



// Fists: a bot always holds them
#define WEAPON_FISTS 1

// The character whose bot throws Weap_OddjobHat (an mp_characters id)
#define SKIN_ODDJOB_HAT 0x1a

// XBE_GLOBAL(0x001639c8, 2)
short BOT_startWeapon = 6;

// Clears a bot's guns and ammo.
// Not named on PS2; the name is ours.
// AUTOINJECT
void BOTWEAP_resetWeapons(obj_tag *obj) {
    BOT_vars_t *vars = ((Drone_tag *)obj->extraObjectData)->botVars;
    memset(vars->reserveAmmo, 0, sizeof(vars->reserveAmmo));
    for (int i = 0; i < BOT_WEAPON_IDS; i++) {
        BOT_weapon_t *weapon = &vars->weapons[i];
        weapon->held = 0;
        weapon->rounds = 0;
        weapon->unknown7[0] = 0;
        weapon->unknown7[1] = 0;
        weapon->zoomRoot = (float)sqrt((double)weapon_data[i].maxZoom);
    }
}

// Fills the gun from the ammo carried, one round at a time for a weapon with flag 0x100. With the reserve empty only
// the start weapon reloads, to a full clip. Returns whether there was anything to load; with checkOnly set nothing
// is changed. drone->clipSize gets the clip size or the reserve left, whichever is smaller.
// Not named on PS2; the name is ours.
// AUTOINJECT
bool BOTWEAP_reload(obj_tag *obj, char checkOnly) {
    Drone_tag *drone = (Drone_tag *)obj->extraObjectData;
    BOT_vars_t *vars = drone->botVars;
    uint weapon = vars->weapon;
    const weapon_definition_tag *wpn = &weapon_data[weapon];
    uint ammoType = wpn->ammoType;
    short slot = Player_AmmoIndex((short)weapon);
    short reserve = vars->reserveAmmo[ammoType];

    if (reserve <= 0) {
        if (weapon == (uint)(int)BOT_startWeapon) {
            drone->clip = wpn->clipSize;
            drone->clipSize = wpn->clipSize;
            return true;
        }
        return false;
    }

    BOT_weapon_t *gun = &vars->weapons[slot];
    int load = wpn->clipSize - gun->rounds;
    if (load > 0 && (wpn->weaponFlags & 0x100))
        load = 1;
    if (load > reserve)
        load = reserve;
    if (load == 0)
        return false;
    if (!checkOnly) {
        vars->reserveAmmo[ammoType] = reserve - load;
        gun->rounds += load;
        drone->clip = gun->rounds;
        drone->clipSize = wpn->clipSize < vars->reserveAmmo[ammoType] ? wpn->clipSize : vars->reserveAmmo[ammoType];
    }
    return true;
}

// The rounds in the gun; the start weapon always has one.
// AUTOINJECT
short BOTWEAP_AmmoInGun(Drone_tag *drone, short weapon) {
    if (weapon == BOT_startWeapon)
        return 1;
    return drone->botVars->weapons[Player_AmmoIndex(weapon)].rounds;
}

// Gives the bot a weapon with `count` rounds: a new weapon gets a gun load and the rest goes to the reserve; one it
// already holds (by its base weapon) adds all of them to the reserve, unless that is full or count is 0. False also
// when the weapon's model is not in the level (fists need none).
// AUTOINJECT
bool BOTWEAP_EquipWeapon(Drone_tag *drone, short weapon, short count) {
    BOT_vars_t *vars = drone->botVars;
    const weapon_definition_tag *wpn = &weapon_data[weapon];

    if (weapon != WEAPON_FISTS) {
        if (wpn->weaponModelHashcode == 0 || hashtable_getitem(wpn->weaponModelHashcode) == NULL)
            return false;
    }

    BOT_weapon_t *base = &vars->weapons[(ushort)wpn->weaponBaseNum];
    if (!base->held) {
        base->held = 1;
        vars->weapons[weapon].held = 1;
        short loaded = count < wpn->clipSize ? count : wpn->clipSize;
        vars->weapons[Player_AmmoIndex(weapon)].rounds = loaded;
        short extra = count - wpn->clipSize;
        if (extra > 0)
            vars->reserveAmmo[wpn->ammoType] += extra;
    } else {
        if (vars->reserveAmmo[wpn->ammoType] >= ammo_data[wpn->ammoType].maxNumPerPlayer || count == 0)
            return false;
        vars->reserveAmmo[wpn->ammoType] += count;
    }

    short *reserve = &vars->reserveAmmo[wpn->ammoType];
    short most = ammo_data[wpn->ammoType].maxNumPerPlayer;
    *reserve = *reserve < most ? *reserve : most;
    return true;
}

// Ammo picked up for a weapon: an empty gun of a weapon with flag 0x10 is loaded first, and the rest goes to the
// reserve up to its maximum. False when the reserve is already full or count is 0.
// AUTOINJECT
bool BOTWEAP_EquipAmmo(Drone_tag *drone, short weapon, short count) {
    const weapon_definition_tag *wpn = &weapon_data[weapon];
    BOT_vars_t *vars = drone->botVars;

    if (vars->reserveAmmo[wpn->ammoType] >= ammo_data[wpn->ammoType].maxNumPerPlayer)
        return false;
    if (count == 0)
        return false;

    if (wpn->weaponFlags & 0x10) {
        if (vars->weapons[Player_AmmoIndex(weapon)].rounds == 0) {
            short loaded = count < wpn->clipSize ? count : wpn->clipSize;
            vars->weapons[Player_AmmoIndex(weapon)].rounds = loaded;
        }
        count -= wpn->clipSize;
        if (count <= 0)
            return true;
    }

    short *reserve = &vars->reserveAmmo[wpn->ammoType];
    *reserve += count;
    short most = ammo_data[wpn->ammoType].maxNumPerPlayer;
    *reserve = *reserve < most ? *reserve : most;
    return true;
}

// Drops the base weapons whose model is not in the level (fists need none).
// AUTOINJECT
void BOTWEAP_CheckWeaponsLoaded(Drone_tag *drone) {
    BOT_vars_t *vars = drone->botVars;
    for (short i = 0; i < BOT_WEAPON_IDS; i++) {
        if (!weapon_data[i].isBaseWeapon)
            continue;
        HASHCODE model = weapon_data[i].weaponModelHashcode;
        if (model == 0 || (i != WEAPON_FISTS && hashtable_getitem(model) == NULL))
            vars->weapons[i].held = 0;
    }
}

// AUTOINJECT
bool BOTWEAP_hasWeapon(Drone_tag *drone, short weapon) {
    return drone->botVars->weapons[weapon].held != 0;
}

// Takes rounds from the gun in the bot's hand, not below 0. An empty gun returns whether the bot holds the start
// weapon. Firing weapon 0x45 restarts the bot's ammo regeneration timer.
// AUTOINJECT
bool BOTWEAP_decrRounds(Drone_tag *drone, short rounds) {
    BOT_vars_t *vars = drone->botVars;
    BOT_weapon_t *gun = &vars->weapons[Player_AmmoIndex(drone->gameObj->animState->currentWeaponId)];

    short left = gun->rounds;
    if (left == 0)
        return vars->weapon == BOT_startWeapon;
    gun->rounds = left - rounds;
    if (gun->rounds < 0)
        gun->rounds = 0;
    if (drone->gameObj->animState->currentWeaponId == Weap_OddjobHat) {
        vars->ammoRegenTimer = GameState.NumFramesUnpaused;
        if (vars->ammoRegenTimer == 0)
            vars->ammoRegenTimer = 1;
    }
    return true;
}

// The weapon's reserve and the rounds in its gun
// AUTOINJECT
int BOTWEAP_getWeaponAmmoAmount(Drone_tag *drone, short weapon) {
    BOT_vars_t *vars = drone->botVars;
    return vars->reserveAmmo[weapon_data[weapon].ammoType] + vars->weapons[Player_AmmoIndex(weapon)].rounds;
}

// AUTOINJECT
int BOTWEAP_getWeaponClipSize(Drone_tag *drone, short weapon) {
    return weapon_data[weapon].clipSize;
}

// The weapons a bot may choose to use
// AUTOINJECT
bool BOTWEAP_isValidWeapon(short weapon) {
    switch (weapon) {
        case 1: case 2: case 3: case 4: case 5: case 6: case 7: case 8: case 9: case 0xa: case 0xb: case 0xc:
        case 0xd: case 0xe: case 0xf: case 0x10: case 0x11: case 0x12: case 0x13: case 0x14: case 0x15: case 0x16:
        case 0x18: case 0x19: case 0x1a: case 0x1c: case 0x1d: case 0x1e: case 0x1f: case 0x20: case 0x21:
        case 0x22: case 0x23: case 0x24: case 0x25: case 0x26: case 0x27: case 0x28: case 0x29: case 0x2a:
        case 0x2b: case 0x2c: case 0x2d: case 0x2e: case 0x2f: case 0x33: case 0x34: case 0x35: case 0x36:
        case 0x37: case 0x3a: case 0x3b: case 0x3c: case 0x3d: case 0x3e: case 0x3f: case 0x40: case 0x41:
        case 0x42: case 0x45: case 0x52:
            return true;
        default:
            return false;
    }
}

// How much a bot wants a weapon: 100 for the first in this list, one less for each place after, 40 for a weapon not
// in it and 39 for fists.
// Not named on PS2; the name is ours.
// AUTOINJECT
int BOTWEAP_getWeaponPriority(short weapon) {
    static const short ranking[] = {
        0x45, 0x42, 0x2e, 0x2f, 0x2d, 0x2c, 0x2a, 0x2b, 0x33, 0x41, 0x1a, 0x19, 0x16, 0x18, 0x13,
        0x12, 0x15, 0x14, 0x1d, 0x1c, 0x23, 0x21, 0x1f, 0x22, 0x20, 0x1e, 0x29, 0x27, 0x25, 0x28,
        0x26, 0x24, 0x11, 0x08, 0x09, 0x04, 0x05, 0x0e, 0x0f, 0x10, 0x0d, 0x0b, 0x0a, 0x0c, 0x06,
        0x07, 0x03, 0x02, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x40, 0x34, 0x52, 0x3a, 0x35, 0x36, 0x37,
    };
    uint rank = 0;
    while (rank < sizeof(ranking) / sizeof(ranking[0]) && ranking[rank] != weapon)
        rank++;
    if (weapon == WEAPON_FISTS)
        rank++;
    return 100 - rank;
}

// The drone animation set (Drone_tag.animSet) a bot uses with a weapon
// Not named on PS2; the name is ours.
// AUTOINJECT
short BOTWEAP_getAnimSet(short weapon) {
    switch (weapon) {
        case 1: case 0x34: case 0x35: case 0x36: case 0x37: case 0x3a: case 0x3b: case 0x3c: case 0x3d: case 0x3e:
        case 0x3f: case 0x40: case 0x45: case 0x52: case 0x53:
            return 0x18;
        case 2: case 3: case 4: case 5: case 6: case 7: case 8: case 9: case 0xa: case 0xb: case 0xc: case 0x41:
        case 0x42:
            return 0x16;
        case 0xd:
            return 0x1a;
        case 0xe: case 0xf: case 0x10: case 0x11:
            return 0x17;
        case 0x12: case 0x13: case 0x14: case 0x15: case 0x2a: case 0x2b: case 0x33:
            return 0x15;
        case 0x2c: case 0x2d: case 0x2e: case 0x2f:
            return 0x19;
        default:
            return 0x14;
    }
}

// The weapons NDrone2_DSTATE_BotGlobal changes away from when it can
// Not named on PS2; the name is ours.
// AUTOINJECT
bool BOTWEAP_shouldChangeWeapon(short weapon) {
    switch (weapon) {
        case 0x34: case 0x35: case 0x36: case 0x37: case 0x3a: case 0x3b: case 0x3c: case 0x3d: case 0x3e: case 0x3f:
        case 0x40: case 0x52: case 0x53:
            return true;
        default:
            return false;
    }
}

// Whether NDrone2_DSTATE_BotAttackReload plays a reload animation for the weapon
// Not named on PS2; the name is ours.
// AUTOINJECT
bool BOTWEAP_hasReloadAnim(short weapon) {
    switch (weapon) {
        case 1: case 0x34: case 0x35: case 0x36: case 0x37: case 0x3a: case 0x3b: case 0x3c: case 0x3d: case 0x3e:
        case 0x3f: case 0x40: case 0x41: case 0x45: case 0x52: case 0x53:
            return false;
        default:
            return true;
    }
}

// Whether a weapon has ammo: in the reserve or the gun, or one that takes none (ammo type 0), or the start weapon.
// Not named on PS2; the name is ours.
// AUTOINJECT
bool BOTWEAP_hasAmmo(Drone_tag *drone, short weapon) {
    uchar ammoType = weapon_data[weapon].ammoType;
    if (drone->botVars->reserveAmmo[ammoType] != 0)
        return true;
    if (BOTWEAP_AmmoInGun(drone, weapon) != 0)
        return true;
    if (ammoType == 0)
        return true;
    return weapon == BOT_startWeapon;
}

// A bot's weapons at the start of a match or after respawning: fists, the start weapon with two clips, a satchel
// for the side that plants it in Protection and Demolition, and Weap_OddjobHat in hand for the character that throws
// it.
// AUTOINJECT
void BOTWEAP_InitWeapon(Drone_tag *drone) {
    BOT_vars_t *vars = drone->botVars;
    MPTeam team = MPSettings.Player[vars->playerIndex].TeamId;

    BOTWEAP_resetWeapons(drone->gameObj);
    BOT_startWeapon = weapon_data[PickupMatrix[MPSettings.weaponSet][0]].weaponBaseNum;
    short loaded = weapon_data[BOT_startWeapon].clipSize * 2;
    BOTWEAP_EquipWeapon(drone, WEAPON_FISTS, 999);
    BOTWEAP_EquipWeapon(drone, BOT_startWeapon, loaded);
    vars->weapon = (uchar)BOT_startWeapon;

    if ((MPSettings.GameMode == GM_PROTECTION && team == PHOENIX) ||
        (MPSettings.GameMode == GM_DEMOLITION && team == MI6))
        BOTWEAP_EquipWeapon(drone, Weap_Satchel, 1);

    if (vars->skin == SKIN_ODDJOB_HAT) {
        BOTWEAP_EquipWeapon(drone, Weap_OddjobHat, 1);
        vars->weapon = Weap_OddjobHat;
    }
    BOTWEAP_CheckWeaponsLoaded(drone);
}

// Puts a held weapon in the bot's hand: its drone weapon, animation set and clip. False if the bot does not hold it.
// AUTOINJECT
bool BOTWEAP_changeWeapon(Drone_tag *drone, short weapon) {
    BOT_vars_t *vars = drone->botVars;
    if (!vars->weapons[weapon].held)
        return false;

    vars->weapon = (uchar)weapon;
    DCVars_tag dcv;
    Drone_DCVfromOBJ(drone->gameObj, &dcv);
    DroneWeap_ChangeWeapon(&dcv, weapon);
    drone->animSet = BOTWEAP_getAnimSet(weapon);
    drone->gameObj->animState->currentWeaponId = (char)weapon;

    const weapon_definition_tag *wpn = &weapon_data[weapon];
    short rounds = vars->weapons[Player_AmmoIndex(weapon)].rounds;
    drone->clip = rounds == 0 ? 1 : rounds;
    short reserve = vars->reserveAmmo[wpn->ammoType];
    drone->clipSize = wpn->clipSize < reserve ? wpn->clipSize : reserve;
    return true;
}

// AUTOINJECT
bool BOTWEAP_hasLoadedWeapon(Drone_tag *drone, short weapon) {
    if (!drone->botVars->weapons[weapon].held)
        return false;
    return BOTWEAP_hasAmmo(drone, weapon);
}

// The first of the explosive weapons the bot holds with ammo whose blast does not reach `distance`, in this order;
// 0 for none.
// Not named on PS2; the name is ours.
// AUTOINJECT
short BOTWEAP_getExplosiveWeapon(Drone_tag *drone, float distance) {
    static const short explosives[] = {0x2d, 0x2c, 0x2a, 0x33, 0x2b, 0x2e, 0x2f};
    BOT_vars_t *vars = drone->botVars;
    for (int i = 0; i < (int)(sizeof(explosives) / sizeof(explosives[0])); i++) {
        short weapon = explosives[i];
        if (vars->weapons[weapon].held && BOTWEAP_hasAmmo(drone, weapon) &&
            distance > weapon_data[weapon].explodeRadius)
            return weapon;
    }
    return 0;
}

// list[weapon] for each of the 83 weapons: the weapon in the high byte, and in the low byte 0xfe less its priority
// (BOTWEAP_getWeaponPriority) for a usable weapon - held, valid, with ammo and, with preferredOnly, preferred (or
// fists) - and 0xff otherwise. Sorting the list puts the most wanted first. Either pointer may be NULL.
// AUTOINJECT
int BOTWEAP_listHeldLoadedWeapons(Drone_tag *drone, ushort *list, int *count, char preferredOnly) {
    BOT_vars_t *vars = drone->botVars;
    ushort ownList[BOT_WEAPON_IDS];
    int ownCount;
    int usable = 0;

    if (list == NULL)
        list = ownList;
    if (count == NULL)
        count = &ownCount;

    for (short weapon = 0; weapon < BOT_WEAPON_IDS; weapon++) {
        list[weapon] = (ushort)(weapon << 8) | 0xff;
        if (!vars->weapons[weapon].held || !BOTWEAP_isValidWeapon(weapon) || !BOTWEAP_hasAmmo(vars->drone, weapon))
            continue;
        if (preferredOnly && !BOTSTATE_isPreferredWeapon(vars, weapon) && weapon != WEAPON_FISTS)
            continue;
        list[weapon] = (ushort)(weapon << 8) | (ushort)(0xfe - (uchar)BOTWEAP_getWeaponPriority(weapon));
        usable++;
    }
    *count = BOT_WEAPON_IDS;
    return usable;
}

// Whether `distance` is inside the weapon's blast, for a bot that has some other usable weapon than fists and the
// explosives (weapon class 4) to change to. Never for a Berserker, or while the opponent is a missile.
// AUTOINJECT
bool BOTWEAP_tooCloseForWeapon(Drone_tag *drone, short weapon, float distance) {
    if (drone->botVars->stats.personality == BOT_PERSONALITY_BERSERKER)
        return false;
    if (drone->opponent != NULL && BOTSTATE_opponentIsMissile(drone, (obj_tag *)-1))
        return false;

    ushort list[BOT_WEAPON_IDS];
    int count = 0;
    BOTWEAP_listHeldLoadedWeapons(drone, list, &count, 0);
    uint i;
    for (i = 0; i < (uint)count; i++) {
        ushort entry = list[i];
        if ((entry & 0xff) == 0xff || (entry & 0xff00) == WEAPON_FISTS << 8)
            continue;
        if (weapon_data[entry >> 8].botWeaponClass != 4)
            break;
    }
    if (i == (uint)count)
        return false;
    return distance < weapon_data[weapon].explodeRadius;
}
