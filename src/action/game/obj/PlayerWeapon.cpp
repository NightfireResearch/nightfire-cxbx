#include "PlayerWeapon.h"
#include "../../engine/CRuntime.h"
#include "Player.h"
#include "GT.h"
#include "bullet.h"
#include "Gas.h"
#include "Light.h"
#include "control.h"
#include "../Upgrade.h"
#include "../view.h"
#include "../mp/multiplayer.h"
#include "../sp/PlayerStats.h"
#include "../sp/SSys.h"
#include "../sp/SwitchChannels.h"
#include "../drone/Drone.h"
#include "../drone/NDrone2.h"
#include "../../engine/Anim.h"
#include "../../engine/viewer.h"
#include "../../engine/Collide.h"
#include "../../math/X87Ops.h"
#include "../../input.h"
#include "../../game.h"
#include "../../sound/Sound.h"
#include <math.h>
#include <string.h>
#include "../../engine/Text.h"
#include "../Autoaim.h"
#include "../../math/math.h"

#pragma fp_contract(off)

// The player's weapons: what he carries and carries over between the parts of a mission, choosing and cycling weapons,
// the gun in the first-person view (its states, anims, sway and position), firing, the zoom, the auto-aim, the laser
// sight, the muzzle flash and the watch on the wrist.

extern uint8_t IsWarmReset;
// XBE_GLOBAL(0x0029aacc, 0x28)
SSYSITEM SSysItems[SSYS_ITEMS];
extern uint8_t surfaced;
extern uint8_t GlobalBlur;
extern float FlashMod;  // Check_Target's flashing, 0.5 to 1
extern uint8_t CamHint;
extern uint8_t StickyHint;

// Health given back on continuing a mission, per difficulty
extern float ContinueHealthBoostEasy;
extern float ContinueHealthBoostMedium;
extern float ContinueHealthBoostHard;

// Autoaim (ReadTuningVars sets them)
extern float Autoaim_Angle_H;
extern float Autoaim_Angle_V;
extern float Autoaim_Range;
extern float Autoaim_LockOnMul;
extern float Autoaim_EasyMul;
extern float Autoaim_NormalMul;
extern float Autoaim_HardMul;

// The watch's hands (WatchOrHands_Create clears them too)
// XBE_GLOBAL(0x00279148, 0x4)
obj_tag *SecondHand;
// XBE_GLOBAL(0x0027914c, 0x4)
obj_tag *MinuteHand;
// XBE_GLOBAL(0x00279150, 0x4)
obj_tag *HourHand;

// The digits shown on a weapon's datums: the satchel charge's timer (Bullet_DoTrails too) and the decryptor's display
// XBE_GLOBAL(0x00181a88, 0x28) readonly relocate
HASHCODE SatchelDigits[10] = {
    HT_Entity_OBJECT_LED_0,
    HT_Entity_OBJECT_LED_1,
    HT_Entity_OBJECT_LED_2,
    HT_Entity_OBJECT_LED_3,
    HT_Entity_OBJECT_LED_4,
    HT_Entity_OBJECT_LED_5,
    HT_Entity_OBJECT_LED_6,
    HT_Entity_OBJECT_LED_7,
    HT_Entity_OBJECT_LED_8,
    HT_Entity_OBJECT_LED_9,
};
// XBE_GLOBAL(0x00181ab0, 0x28) readonly relocate
HASHCODE DecryptorDigits[10] = {
    HT_Entity_OBJECT_LED_0_Decryptor,
    HT_Entity_OBJECT_LED_1_Decryptor,
    HT_Entity_OBJECT_LED_2_Decryptor,
    HT_Entity_OBJECT_LED_3_Decryptor,
    HT_Entity_OBJECT_LED_4_Decryptor,
    HT_Entity_OBJECT_LED_5_Decryptor,
    HT_Entity_OBJECT_LED_6_Decryptor,
    HT_Entity_OBJECT_LED_7_Decryptor,
    HT_Entity_OBJECT_LED_8_Decryptor,
    HT_Entity_OBJECT_LED_9_Decryptor,
};

// The view cone's length (0x0017c110; Camera.cpp)
#define MAX_VIEW_CONE 2000.0f

// What a player carries over from one part of a mission to the next (Player_RamSave, Player_RamLoad)
#pragma pack(push, 1)
typedef struct {
    PlayerMissionStats stats;           // 0x000 - player 0's
    short ammo[33];                     // 0x038
    char _pad7a[2];
    WeaponStatus weaponStats[114];      // 0x07c
    SSYSITEM ssysItems[SSYS_ITEMS];     // 0x5d4
    float health;                       // 0x5fc
    float armor;                        // 0x600
    uint32_t switchChannelTimes[256];   // 0x604
    short bondMoments[10];              // 0xa04 - BondMoments[0x65] on
    short weapon;                       // 0xa18 - the weapon being switched to
    char weaponTail[0x33];              // 0xa1a - BLData::keys
    char switchChannels[256];           // 0xa4d
    char switchChannelsHold[256];       // 0xb4d
    char switchChannelsPrev[256];       // 0xc4d
} RamSaveData_t;
#pragma pack(pop)

static_assert(sizeof(RamSaveData_t) == 0xd4d, "RamSaveData_t size");
static_assert(offsetof(RamSaveData_t, ssysItems) == 0x5d4, "RamSaveData_t ssysItems");
static_assert(offsetof(RamSaveData_t, weaponTail) == 0xa1a, "RamSaveData_t weaponTail");

// XBE_GLOBAL(0x00276768, 0xd4d)
RamSaveData_t RamSaveData;

// The weapons by preference, best first (Player_IsBetterWeapon, Player_GetBestWeapon)
// XBE_GLOBAL(0x00181ad8, 0x24)
static const ushort BestWeapons[18] = {0x42, 0x32, 0x1a, 0x18, 0x1c, 0x16, 0x15, 0x08, 0x10,
                                       0x0e, 0x0d, 0x0c, 0x12, 0x06, 0x04, 0x02, 0x0a, 0x01};

// Player_WeaponChange's third argument (and Player.cpp's)
#define CHANGE_WEAPONS 0
#define CHANGE_GADGETS 1

// Gadgets are weapons 0x4a to 0x5e
static bool IsGadget(int weapon) {
    return weapon > 0x49 && weapon < 0x5f;
}

// Turns the gun towards the crosshairs: by their offset over the view's zoom, pi/6 at an offset of 1, across scaled
// by the aspect ratio and less the recoil's.
void Player_TiltWeaponTowardsCrosshairs(obj_tag *player) {
    BLData *blData = (BLData *)player->extraObjectData;
    viewer_tag *viewer = glb_viewer[blData->playerNum];
    Extended zoom = viewer->zoom;
    blData->weaponObject->rotation.x = (float)(Extended(blData->crosshairOffsetY) / zoom * Extended(-0.5235988f));
    blData->weaponObject->rotation.y = (float)(Extended(blData->recoilY)
        - Extended(blData->crosshairOffsetX) / zoom * Extended(0.5235988f) * Extended(viewer->AspectRatio));
    blData->weaponObject->rotation.z = 0.0f;
}

// The C runtime's x87 asin (__CIasin_default, Crt::CIasin): the argument in ST0, the result in ST0
#if NIGHTFIRE_X87
static double __declspec(naked) CIAsin(double x) {
    _asm {
        fld qword ptr [esp + 4]
        call Crt::CIasin
        ret
    }
}
#else
static double CIAsin(double x) {
    return Crt::CIasin(x);
}
#endif

// ---------------------------------------------------------------------------------------------------------------
// What the player carries

// Empties the player's pockets, or with the all-weapons cheat fills them: every ammo type, and every base weapon with a
// full clip. Each weapon's zoom starts at the square root of its maximum.
// AUTOINJECT
void Player_InitAmmoWeapons(obj_tag *player) {
    BLData *blData = (BLData *)player->extraObjectData;

    for (int i = 0; i < 33; i++)
        blData->ammo[i] = CheatInfo.AllWeapons ? ammo_data[i].maxNumPerPlayer : 0;

    for (int i = 0; i < NUM_WEAPONS; i++) {
        WeaponStatus *status = &blData->weaponStats[i];
        if (CheatInfo.AllWeapons) {
            status->enabled = 1;
            status->clipOrCooldown = weapon_data[i].clipSize;
        }
        else {
            status->enabled = 0;
            status->clipOrCooldown = 0;
        }
        if (!weapon_data[i].isBaseWeapon)
            status->enabled = 0;
        status->fireModeIndex = 0;
        status->variant = 0;
        status->zoom = (float)sqrt((double)weapon_data[i].maxZoom);
    }

    blData->lastGadget = 0;
    blData->lastWeapon = 0;
}

// A key taken: its byte, unbounded
// AUTOINJECT
void Player_EquipKey(BLData *blData, ushort key) {
    blData->keys[key] = 1;
}

// A key lost (Lock_ReLock)
// AUTOINJECT
void Player_UnEquipKey(BLData *blData, ushort key) {
    blData->keys[key] = 0;
}

// Gives the player a weapon (upgraded as his upgrades say, in single player) and ammo for it. A weapon he did not have
// comes with a full clip and the rest of the ammo goes to his stock; one he had only adds ammo, and nothing when the
// stock is full or there is none to add. Returns whether he took it. With the auto-switch option, a better weapon is
// chosen at once.
// AUTOINJECT
bool Player_EquipWeapon(BLData *blData, short weapon, short amount) {
    obj_tag *player = glb_players[blData->playerNum];
    weapon_definition_tag *def = &weapon_data[weapon];

    if (def->weaponModelHashcode == 0) {
        NF_WARN("FYI: Weapon %d has no skin\n", weapon); // GC check (0x800f6760)
        return false;
    }
    if (hashtable_getitem(def->weaponModelHashcode) == NULL) {
        NF_WARN("FYI: Weapon %d not loaded\n", weapon); // GC check (0x800f6760)
        return false;
    }

    if (!MPSettings.isMultiplayer) {
        weapon = (short)Upgrade_Weapon(weapon);
        def = &weapon_data[weapon];
        if (def->weaponBaseNum == 0x33)
            blData->weaponStats[0x11].enabled = 0;
    }

    WeaponStatus *base = &blData->weaponStats[(ushort)def->weaponBaseNum];
    if (!base->enabled) {
        base->enabled = 1;
        short extra = amount + blData->weaponStats[Player_AmmoIndex(weapon)].clipOrCooldown;
        blData->weaponStats[Player_AmmoIndex(weapon)].clipOrCooldown = def->clipSize;
        extra -= def->clipSize;
        if (extra > 0)
            blData->ammo[def->ammoType] += extra;
    }
    else {
        if (blData->ammo[def->ammoType] >= ammo_data[def->ammoType].maxNumPerPlayer)
            return false;
        if (amount == 0)
            return false;
        blData->ammo[def->ammoType] += amount;
    }

    short most = ammo_data[def->ammoType].maxNumPerPlayer;
    if (blData->ammo[def->ammoType] >= most)
        blData->ammo[def->ammoType] = most;

    if (PlayerInputs[blData->playerNum].autoSwitchBetterWeapon && Player_IsBetterWeapon(player, weapon))
        player->animState->switchingToWeaponId = (char)weapon;

    if (weapon == Weap_Ronin) {
        blData->weaponStats[0x53].enabled = 0;
        if (player->animState->currentWeaponId == 0x53)
            player->animState->switchingToWeaponId = Weap_Ronin;
    }
    return true;
}

// After a warm reset (the next part of the same mission), puts back what Player_RamSave kept: ammo, weapons, the
// security system, the mission's statistics, Bond moments, the switch channels, health and armour, and the weapon
// being switched to. Continuing after a death, the health is raised to at least the difficulty's minimum.
// AUTOINJECT
void Player_RamLoad(BLData *blData, char boostHealth) {
    if (!IsWarmReset)
        return;

    memcpy(blData->ammo, RamSaveData.ammo, sizeof(RamSaveData.ammo));
    memcpy(blData->weaponStats, RamSaveData.weaponStats, sizeof(RamSaveData.weaponStats));
    memcpy(blData->keys, RamSaveData.weaponTail, sizeof(RamSaveData.weaponTail));
    SSys_RamLoad(RamSaveData.ssysItems);
    float health = RamSaveData.health;
    memcpy(&PlrMissionStats[0], &RamSaveData.stats, sizeof(RamSaveData.stats));
    memcpy(&BondMoments[0x65], RamSaveData.bondMoments, sizeof(RamSaveData.bondMoments));
    memcpy(switch_channels_time, RamSaveData.switchChannelTimes, sizeof(RamSaveData.switchChannelTimes));
    memcpy(switch_channels, RamSaveData.switchChannels, sizeof(RamSaveData.switchChannels));
    memcpy(switch_channels_hold, RamSaveData.switchChannelsHold, sizeof(RamSaveData.switchChannelsHold));
    memcpy(switch_channels_prev, RamSaveData.switchChannelsPrev, sizeof(RamSaveData.switchChannelsPrev));

    if (boostHealth) {
        float least;
        switch (GameState.difficultyModifier) {
            case 1:
                least = ContinueHealthBoostEasy;
                break;
            case 2:
                least = ContinueHealthBoostMedium;
                break;
            default:
                least = ContinueHealthBoostHard;
                break;
        }
        if (least > health)
            health = least;
        RamSaveData.health = health;
    }

    Player_SetHealth(blData, health);
    blData->armor = RamSaveData.armor;
    blData->ramLoadedWeaponId = RamSaveData.weapon;
}

// Keeps what the player carries into the next part of the mission (Player_RamLoad)
// AUTOINJECT
void Player_RamSave(BLData *blData) {
    memcpy(RamSaveData.ammo, blData->ammo, sizeof(RamSaveData.ammo));
    memcpy(RamSaveData.weaponStats, blData->weaponStats, sizeof(RamSaveData.weaponStats));
    memcpy(RamSaveData.weaponTail, blData->keys, sizeof(RamSaveData.weaponTail));
    memcpy(RamSaveData.ssysItems, SSysItems, sizeof(RamSaveData.ssysItems));
    memcpy(&RamSaveData.stats, &PlrMissionStats[0], sizeof(RamSaveData.stats));
    memcpy(RamSaveData.bondMoments, &BondMoments[0x65], sizeof(RamSaveData.bondMoments));
    memcpy(RamSaveData.switchChannelTimes, switch_channels_time, sizeof(RamSaveData.switchChannelTimes));
    memcpy(RamSaveData.switchChannels, switch_channels, sizeof(RamSaveData.switchChannels));
    memcpy(RamSaveData.switchChannelsHold, switch_channels_hold, sizeof(RamSaveData.switchChannelsHold));
    memcpy(RamSaveData.switchChannelsPrev, switch_channels_prev, sizeof(RamSaveData.switchChannelsPrev));
    RamSaveData.health = blData->health;
    RamSaveData.armor = blData->armor;
    RamSaveData.weapon = glb_players[blData->playerNum]->animState->switchingToWeaponId;
}

// ---------------------------------------------------------------------------------------------------------------
// Choosing weapons

// Whether the player has ammo for a weapon: carried, loaded, a weapon that needs none, or for a detonator a live
// projectile of its own that he fired. Oddjob's hat needs its full charge.
// AUTOINJECT
bool Player_WeaponHasAmmo(BLData *blData, short weapon) {
    if (weapon == Weap_OddjobHat)
        return blData->weaponStats[Weap_OddjobHat].clipOrCooldown >= weapon_data[Weap_OddjobHat].clipSize;

    weapon_definition_tag *def = &weapon_data[weapon];
    if (blData->ammo[def->ammoType] != 0)
        return true;
    if (Player_AmmoInGun(blData, weapon) != 0)
        return true;
    if (def->ammoType == 0)
        return true;
    if (def->weaponFlags & WPNFLAG_DETONATOR) {
        for (obj_tag *obj = control_first_object(); obj != NULL; obj = obj->nextObject) {
            if (obj->objectType != OBJECTTYPE_BULLET)
                continue;
            BU_tag *bullet = (BU_tag *)obj->extraObjectData;
            if (bullet->wpnDef->weaponBaseNum == def->weaponBaseNum && bullet->firedByObj == glb_players[blData->playerNum])
                return true;
        }
    }
    return false;
}

// Whether the player can go back to a weapon: he has its base weapon, and ammo for it. The name is ours.
// FUNC_AT(0x000b9f70)
bool Player_WeaponUsable(BLData *blData, short weapon) {
    short base = weapon_data[weapon].weaponBaseNum;
    return weapon_data[base].isBaseWeapon && blData->weaponStats[base].enabled && Player_WeaponHasAmmo(blData, base);
}

// Whether a weapon is better than the one in the player's hand, by the BestWeapons list. Never while he holds a gadget.
// AUTOINJECT
bool Player_IsBetterWeapon(obj_tag *player, short weapon) {
    short base = weapon_data[weapon].weaponBaseNum;
    short heldBase = weapon_data[player->animState->currentWeaponId].weaponBaseNum;
    if (IsGadget(heldBase))
        return false;

    short heldRank = -1;
    short rank = -1;
    for (short i = 0; i < 18; i++) {
        short listed = weapon_data[BestWeapons[i]].weaponBaseNum;
        if (listed == heldBase)
            heldRank = i;
        if (listed == base)
            rank = i;
    }
    if (heldRank == -1 || rank == -1)
        return false;
    return rank < heldRank;
}

// Switches to the best weapon the player has ammo for, by the BestWeapons list (each in the variant last chosen;
// weapon 0x1a always as itself)
// AUTOINJECT
void Player_GetBestWeapon(obj_tag *player) {
    BLData *blData = (BLData *)player->extraObjectData;
    for (short i = 0; i < 18; i++) {
        ushort base = weapon_data[BestWeapons[i]].weaponBaseNum;
        short weapon;
        if (base == 0x1a)
            weapon = 0x1a;
        else
            weapon = (short)(base + blData->weaponStats[(short)base].variant);
        if (blData->weaponStats[(short)base].enabled && Player_WeaponHasAmmo(blData, weapon)) {
            player->animState->switchingToWeaponId = (char)weapon;
            return;
        }
    }
}

// The weapon in the hand has run out: the Ronin is set down (and weapon 0x53 takes its place), a weapon flagged for it
// goes to its alt-fire variant if that has ammo, anything else to the best weapon there is. The fists, the lasers and
// the gadgets 0x4a-0x51 stay.
// AUTOINJECT
void Player_HandleHasNoAmmo(obj_tag *player) {
    AnimState *anim = player->animState;
    BLData *blData = (BLData *)player->extraObjectData;
    char current = anim->currentWeaponId;

    switch (current) {
        case 1:
        case 50:
        case 51:
        case 74:
        case 75:
        case 76:
        case 77:
        case 78:
        case 79:
        case 80:
        case 81:
            return;

        case Weap_Ronin:
            if (blData->weaponStats[Weap_Ronin].clipOrCooldown > 0) {
                anim->switchingToWeaponId = Weap_Ronin;
                return;
            }
            blData->weaponStats[Weap_Ronin].enabled = 0;
            Player_EquipWeapon(blData, 0x53, 999);
            player->animState->switchingToWeaponId = 0x53;
            return;

        default:
            if (weapon_data[current].weaponFlags & WPNFLAG_SWITCH_TO_ALT) {
                short alt = (short)(current + weapon_data[current].offsetToNextAltFireVariant);
                if (Player_WeaponHasAmmo(blData, alt)) {
                    player->animState->switchingToWeaponId = (char)alt;
                    return;
                }
            }
            Player_GetBestWeapon(player);
            return;
    }
}

// Cycles to the next weapon (direction 1) or the one before (-1): weapons 1-0x47, gadgets 0x4a-0x5e, or only weapon 1
// (mode 2, see Player_Weapon). From a weapon the gadget button first goes back to the last gadget, and the other way
// round. Each base weapon comes in the variant last chosen, or as itself when that has no ammo; those the player does
// not have or has no ammo for are passed over.
// AUTOINJECT
void Player_WeaponChange(obj_tag *player, char direction, char mode) {
    BLData *blData = (BLData *)player->extraObjectData;

    switch ((short)player->subState) {
        case MovementType_Grapple:
        case MovementType_RemoteControl:
        case MovementType_Emplacement:
        case MovementType_Dead:
        case MovementType_Ronin:
            return;
    }
    switch ((short)blData->weaponObject->curState) {
        case GUN_LOWER:
        case GUN_RAISE:
        case GUN_RELOAD_START:
        case GUN_RELOADING:
        case GUN_RELOAD_END:
        case GUN_SWITCH_VARIANT:
        case GUN_FIRING:
        case GUN_THROW_WINDUP:
        case GUN_LOWER_USED_UP:
        case GUN_LOWERING_USED_UP:
        case GUN_SCOPE_RAISE:
        case GUN_SCOPE_LOWER:
            return;
    }

    AnimState *anim = player->animState;
    char current = anim->currentWeaponId;
    if (current == Weap_Hands)
        return;
    if (current > 0x5c && current <= 0x5e)
        return;
    if (anim->animFlags & 1)
        return;

    int lowest, highest;
    if (mode == CHANGE_GADGETS) {
        lowest = 0x49;
        highest = 0x5f;
        if (current > 0 && current < 0x48 && blData->lastGadget != 0 && Player_WeaponUsable(blData, blData->lastGadget)) {
            player->animState->switchingToWeaponId = (char)blData->lastGadget;
            blData->lastWeapon = player->animState->currentWeaponId;
            return;
        }
    }
    else {
        lowest = 0;
        if (mode == CHANGE_WEAPONS) {
            highest = 0x48;
            if (current > 0x49 && current < 0x5f && blData->lastWeapon != 0 &&
                Player_WeaponUsable(blData, blData->lastWeapon)) {
                player->animState->switchingToWeaponId = (char)blData->lastWeapon;
                blData->lastGadget = player->animState->currentWeaponId;
                if (blData->lastGadget == Weap_Camera_Upgraded)
                    blData->lastGadget = Weap_Camera;
                return;
            }
        }
        else {
            highest = 2;
            if (current == 1)
                Sound_PlayExt(SFX_FRONT_END_MENU_ITEM_ERROR, 100.0f, 0, 0);
        }
    }

    current = player->animState->currentWeaponId;
    if (current > 0 && current < 0x48)
        blData->lastWeapon = current;
    current = player->animState->currentWeaponId;
    if (current > 0x49 && current < 0x5f)
        blData->lastGadget = current;

    anim = player->animState;
    anim->prevHeldWeaponId = anim->currentWeaponId;
    if (anim->switchingToWeaponId < lowest || anim->switchingToWeaponId > highest)
        anim->switchingToWeaponId = (char)lowest;
    anim->switchingToWeaponId = (char)weapon_data[anim->switchingToWeaponId].weaponBaseNum;

    for (short tries = 1;; tries++) {
        anim = player->animState;
        anim->switchingToWeaponId += direction;
        if (anim->switchingToWeaponId >= highest)
            anim->switchingToWeaponId = (char)(lowest + 1);
        if (anim->switchingToWeaponId <= lowest)
            anim->switchingToWeaponId = (char)(highest - 1);
        if (tries > 0x72)
            break;

        char base = anim->switchingToWeaponId;
        if (weapon_data[base].isBaseWeapon != 1)
            continue;
        if (base != Weap_Camera)
            anim->switchingToWeaponId = base + blData->weaponStats[base].variant;
        WeaponStatus *status = &blData->weaponStats[base];
        if (status->enabled) {
            if (Player_WeaponHasAmmo(blData, player->animState->switchingToWeaponId))
                return;
            if (status->variant > 0 && Player_WeaponHasAmmo(blData, base)) {
                status->variant = 0;
                player->animState->switchingToWeaponId = base;
                return;
            }
        }
        player->animState->switchingToWeaponId = base;
    }
    player->animState->switchingToWeaponId = player->animState->currentWeaponId;
}

// Starts the change to the weapon being switched to: between variants of one model, the mode switch anim plays and the
// variant is remembered; otherwise the gun is lowered (if it has a lower anim) or the new one comes straight up.
// AUTOINJECT
void Player_WeaponSelect(obj_tag *player) {
    AnimState *anim = player->animState;
    BLData *blData = (BLData *)player->extraObjectData;
    char next = anim->switchingToWeaponId;
    short base = weapon_data[next].weaponBaseNum;

    if (weapon_data[anim->currentWeaponId].weaponBaseNum == base) {
        blData->weaponStats[(ushort)base].variant = next - (char)base;
        anim = player->animState;
        next = anim->switchingToWeaponId;
        if (weapon_data[anim->currentWeaponId].weaponModelHashcode == weapon_data[next].weaponModelHashcode) {
            anim->currentWeaponId = next;
            AnimScriptAddSpeed(blData->weaponObject, weapon_data[player->animState->currentWeaponId].animModeSwitch, 1.3f);
            blData->weaponObject->curState = GUN_SWITCH_VARIANT;
            char current = player->animState->currentWeaponId;
            short currentBase = weapon_data[current].weaponBaseNum;
            blData->weaponStats[(ushort)currentBase].variant = current - (char)currentBase;
            return;
        }
    }

    if (weapon_data[anim->prevHeldWeaponId].animLower != 0) {
        blData->weaponObject->curState = GUN_LOWER;
        return;
    }
    anim->currentWeaponId = next;
    blData->weaponObject->curState = GUN_RAISE;
}

// ---------------------------------------------------------------------------------------------------------------
// The gun in the view

// Gives the weapon object the model of the weapon in the hand, with the player's sleeves, and sets up the anim set
// again
// AUTOINJECT
void Player_SetWeaponAnim(obj_tag *player) {
    weapon_definition_tag *weapon = &weapon_data[player->animState->currentWeaponId];
    BLData *blData = (BLData *)player->extraObjectData;

    if (weapon->weaponModelHashcode != 0) {
        AnimObjectDelete(blData->weaponObject);
        AnimObjectNew(blData->weaponObject, weapon->weaponModelHashcode);
        AnimObjectSetSleeveType(blData->weaponObject, (uchar)blData->punchSkin);
        View_SetDrawInNoViews(blData->weaponObject);
        blData->laserSight = (weapon->weaponFlags & WPNFLAG_LASER_SIGHT) != 0;
    }
    else {
        View_SetDrawInNoViews(blData->weaponObject);
    }
    PlayerAnimSetInit(player);
    blData->weaponObject->effectFlags |= EFFECT_NO_COLLISION | EFFECT_PLACED_LIGHTS | EFFECT_DRAW_LAST | EFFECT_STRADDLES_CELS | EFFECT_SUNLIT;
}

// Places the gun: its own rotation on the view's (the player's turned by the pitch), at the eyes plus its position in
// that rotation, then back 0.2 down and 0.5 forward in the view's
// AUTOINJECT
void Player_PositionGun(obj_tag *player, BLData *blData) {
    obj_tag *gun = blData->weaponObject;
    gun->transformFlags |= TRANSFORM_MOVED;

    _MATRIX view, look;
    RotMatrixX(-(blData->pitchFromHorizontal * HALF_PI), &view);
    Concat(&player->transformMatrix, &view);
    Mat_Copy(&view, &look);
    RotMatrixZYX(&gun->rotation, &gun->transformMatrix);
    Concat(&view, &gun->transformMatrix);

    _VECTOR at, head;
    ApplyMatrixLV(&gun->transformMatrix, &gun->position, &at);
    Player_GetHeadPos(&head, player, NULL);
    _VECTOR *place = Mat_Position(gun->transformMatrix);
    Vec_Add2(&head, &at, place);

    _VECTOR back = {0.0f, -0.2f, 0.5f};
    ApplyMatrixLV(&look, &back, &at);
    Vec_Add2(place, &at, place);
}

// The gun's sway: the walk's (by the forward movement), a slow idle one, and the firing kick dying away; then the gun is
// tilted towards the crosshair and its state moved on (Player_SetWeaponAnimObj)
// AUTOINJECT
void Player_WeaponRecoil(obj_tag *player) {
    BLData *blData = (BLData *)player->extraObjectData;
    _VECTOR sway;

    float phase = blData->lastMovement.z + blData->swayPhase;
    blData->swayPhase = phase;
    sway.x = (float)(FSin(phase) * Extended(0.01f));
    Extended walkLift = FCos(phase);
    if (!((double)walkLift >= 0.0))   // FCOM: a NaN counts as below
        walkLift = -walkLift;
    walkLift = walkLift * Extended(0.01f);
    sway.z = (float)(FSin((double)phase * 0.84329f) * Extended(0.02f));

    blData->recoilX = blData->recoilX * 0.75f;
    blData->recoilY = blData->recoilY * 0.75f;

    double idle = (double)HALF_PI / _FRAME_RATE + blData->idleSwayPhase;
    blData->idleSwayPhase = (float)idle;
    sway.x = (float)(FSin(idle) * Extended(0.001f) + Extended(sway.x));
    sway.y = (float)(FCos(idle * 1.7f) * Extended(0.001f) + walkLift);

    Player_TiltWeaponTowardsCrosshairs(player);
    Player_SetWeaponAnimObj(player, &sway);
}

// Whether a drone is alive and not in state 1 (by the second test of Player_SetWeaponAnimObj). The name is ours.
// FUNC_AT(0x000b7bd0)
bool Player_DroneAwake(Drone_tag *drone) {
    if (drone->health < 0.0f || drone->health == 0.0f)
        return false;
    return drone->gameObj->curState != 1;
}

// Whether any drone that is alive and not in state 1 is more than half alert
static bool AnyDroneAlert(void) {
    for (Drone_tag *drone = NPCGlobals.NDrone2List; drone != NULL; drone = drone->next) {
        if (Player_DroneAwake(drone) && drone->alertness > 0.5f)
            return true;
    }
    return false;
}

// Spawns the shot when the fire (or throw) anim reaches the weapon's frame for it, once
static void SpawnAtFrame(obj_tag *player, BLData *blData, weapon_definition_tag *weapon) {
    float frame = 0.0f;
    sAnimScript_tag *script = AnimListBuild(blData->weaponObject, 4, 0, NULL, 1);
    if (script != NULL)
        frame = script->curFrame;
    if (!(frame < (float)weapon->bulletSpawnFrame) && !blData->bulletSpawned) {
        blData->bulletSpawned = 1;
        Player_WeaponInitBullet(player, weapon);
    }
}

// Plays the weapon's idle anim, if it has one, and settles the gun
static void ToIdle(obj_tag *gun, weapon_definition_tag *weapon) {
    if (weapon->animIdle != 0)
        AnimScriptAdd(gun, weapon->animIdle);
    gun->curState = GUN_IDLE;
}

// The gun's state machine (BLData::weaponObject->curState: GUN_*), run every frame: the idle fidgets and lowering,
// putting a weapon away and bringing the next one up, reloading, firing and throwing, the scope's anims. Then the gun's
// place in the view: the weapon's offset (blended towards the crouched one as the player crouches, in single player)
// plus the sway.
// AUTOINJECT
void Player_SetWeaponAnimObj(obj_tag *player, _VECTOR *sway) {
    AnimState *anim = player->animState;
    weapon_definition_tag *previous = &weapon_data[anim->prevHeldWeaponId];
    BLData *blData = (BLData *)player->extraObjectData;
    short playerNum = blData->playerNum;
    weapon_definition_tag *current = &weapon_data[anim->currentWeaponId];
    bool multiplayer = MPSettings.isMultiplayer != 0;
    obj_tag *gun = blData->weaponObject;
    short idleBefore = blData->idleState;

    if (gun->curState == GUN_IDLE) {
        if (PlayerInputs[playerNum].anyInput != 0)
            blData->idleTicks = 0;
        else
            blData->idleTicks++;
        if (blData->idleWait > 0)
            blData->idleWait--;
    }
    else {
        blData->idleState = IDLE_NONE;
        blData->idleTicks = 0;
        blData->idleWait = (short)(FRAME_RATE_INT * 20);
    }

    weapon_definition_tag *shown = current;
    switch ((short)gun->curState) {
        case GUN_THROW_WINDUP:
            ((BLData *)player->extraObjectData)->zoomTarget = 1.0f;
            if (AnimScriptIsStopped(blData->weaponObject)) {
                blData->weaponObject->curState = GUN_THROWING;
                AnimScriptAdd(blData->weaponObject, current->animFireAlt);
            }
            break;

        case GUN_THROWING:
            ((BLData *)player->extraObjectData)->zoomTarget = 1.0f;
            SpawnAtFrame(player, blData, current);
            if (AnimScriptIsStopped(blData->weaponObject)) {
                if (!Player_ReloadAmmoType(player, true) && !CheatInfo.UnlimitedAmmo) {
                    blData->weaponObject->curState = GUN_IDLE;
                    Player_HandleHasNoAmmo(player);
                    if (player->animState->switchingToWeaponId != player->animState->currentWeaponId)
                        Player_WeaponSelect(player);
                    blData->weaponObject->curState = GUN_RAISE;
                    if (player->animState->switchingToWeaponId == 0x39) {
                        player->animState->currentWeaponId = 0x39;
                        blData->weaponObject->curState = GUN_IDLE;
                        blData->refireTimer = 0.0f;
                    }
                }
                else {
                    blData->weaponObject->curState = GUN_RAISE;
                }
            }
            break;

        case GUN_FIRING:
            if (current->bulletSpawnFrame != 0)
                SpawnAtFrame(player, blData, current);
            if (current->weaponFlags & WPNFLAG_CONTINUOUS) {
                // Held: the grapples while they reel in, anything else while there is ammo
                if (current->weaponBaseNum == 0x50 || current->weaponBaseNum == 0x51) {
                    if (player->subState == MovementType_Grapple && blData->grappleState == 1)
                        break;
                    if (blData->movementDisabled)
                        break;
                }
                else if (Input_Action(playerNum, ACTION_FIRE, 1) &&
                         Player_WeaponHasAmmo(blData, (ushort)current->weaponVariantNum)) {
                    break;
                }
                if (AnimScriptIsStopped(blData->weaponObject)) {
                    blData->weaponObject->curState = GUN_SWITCH_VARIANT;
                    AnimScriptAdd(blData->weaponObject, current->animFireAlt);
                    blData->idleState = IDLE_PLAYING;
                }
                break;
            }
            if ((current->weaponFlags & WPNFLAG_REFIRE_HELD) && Input_Action(playerNum, ACTION_FIRE, 1) &&
                AnimScriptIsStopped(blData->weaponObject)) {
                Player_SetFiringAnim(blData, blData->weaponObject, current);
            }
            if (current->weaponModelHashcode == 0) {
                blData->weaponObject->curState = GUN_IDLE;
            }
            else if (AnimScriptIsStopped(blData->weaponObject)) {
                blData->weaponObject->curState = GUN_IDLE;
                // A slide-locked gun keeps its empty pose; the idle anim is not checked for here
                if (!(weapon_data[(ushort)current->weaponBaseNum].weaponFlags & WPNFLAG_SLIDE_LOCK))
                    AnimScriptAdd(blData->weaponObject, current->animIdle);
            }
            break;

        case GUN_SWITCH_VARIANT:
            if (current->weaponModelHashcode == 0) {
                gun->curState = GUN_IDLE;
            }
            else if (AnimScriptIsStopped(gun)) {
                blData->weaponObject->curState = GUN_IDLE;
                if (current->animIdle != 0)
                    AnimScriptAdd(blData->weaponObject, current->animIdle);
            }
            break;

        case GUN_SCOPE_RAISE:
            if (AnimScriptIsStopped(gun)) {
                player->animState->animFlags |= 1;
                blData->weaponObject->curState = GUN_IDLE;
                blData->idleState = IDLE_PLAYING;
            }
            break;

        case GUN_SCOPE_LOWER:
            if (AnimScriptIsStopped(gun)) {
                blData->weaponObject->curState = GUN_IDLE;
                blData->idleState = IDLE_PLAYING;
            }
            break;

        case GUN_RELOAD_START:
            if (AnimScriptIsStopped(gun)) {
                AnimScriptAdd(blData->weaponObject, current->animReload);
                blData->weaponObject->curState = GUN_RELOADING;
            }
            break;

        case GUN_RELOADING:
            if (current->animReload == 0) {
                Player_ReloadAmmoType(player, false);
                blData->weaponObject->curState = GUN_IDLE;
                break;
            }
            if (!AnimScriptIsStopped(gun))
                break;
            Player_ReloadAmmoType(player, false);
            if (current->weaponFlags & WPNFLAG_SHELL_RELOAD) {
                // Another shell while there are any and fire is not held
                if (Player_ReloadAmmoType(player, true) && !Input_Action(playerNum, ACTION_FIRE, 1)) {
                    AnimScriptAdd(blData->weaponObject, current->animReload);
                    break;
                }
                AnimScriptAdd(blData->weaponObject, current->animReloadEnd);
                blData->weaponObject->curState = GUN_RELOAD_END;
                blData->twinGunHand = 0;
                break;
            }
            ToIdle(blData->weaponObject, current);
            Input_ClearAction(playerNum, ACTION_FIRE);
            break;

        case GUN_RELOAD_END:
            if (AnimScriptIsStopped(gun)) {
                ToIdle(blData->weaponObject, current);
                Input_ClearAction(playerNum, ACTION_FIRE);
            }
            break;

        case GUN_LOWER:
        case GUN_LOWER_USED_UP:
            ((BLData *)player->extraObjectData)->zoomTarget = 1.0f;
            shown = previous;
            if (previous->animLower != 0) {
                if (idleBefore == IDLE_LOWERED && previous->animLowerRelaxed != 0)
                    AnimScriptAdd(blData->weaponObject, previous->animLowerRelaxed);
                else
                    AnimScriptAddSpeed(blData->weaponObject, previous->animLower, 1.25f);
                Sound_PlayExt(IsGadget((ushort)previous->weaponVariantNum) ? SFX_GADGET_LOWER_GADGET
                                                                           : SFX_WEAPON_GENERIC_LOWER_WEAPON,
                              100.0f, 0, 0);
            }
            if (blData->weaponObject->curState == GUN_LOWER_USED_UP)
                blData->weaponObject->curState = GUN_LOWERING_USED_UP;
            if (blData->weaponObject->curState == GUN_LOWER)
                blData->weaponObject->curState = GUN_LOWERING;
            break;

        case GUN_LOWERING:
        case GUN_LOWERING_USED_UP:
            shown = previous;
            if (AnimScriptIsStopped(gun)) {
                // A weapon used up is gone (in single player)
                if (blData->weaponObject->curState == GUN_LOWERING_USED_UP && !MPSettings.isMultiplayer)
                    blData->weaponStats[player->animState->prevHeldWeaponId].enabled = 0;
                anim = player->animState;
                if (anim->switchingToWeaponId != anim->currentWeaponId) {
                    anim->currentWeaponId = anim->switchingToWeaponId;
                    if (MPSettings.dronesAreBots)
                        MP_sendTeamBotMessage(NO_TEAM, DRONE_MSG_BotWeaponChanged,
                                              (void *)(uintptr_t)player->animState->switchingToWeaponId, 0,
                                              (uint)(uintptr_t)player);
                }
                blData->weaponObject->curState = GUN_RAISE;
            }
            break;

        case GUN_RAISE:
            Player_SetWeaponAnim(player);
            if (current->animRaise != 0) {
                AnimScriptAddSpeed(blData->weaponObject, current->animRaise, 1.25f);
                AnimObjectUpdate(blData->weaponObject);
                blData->weaponObject->curState = GUN_RAISING;
                Sound_PlayExt(IsGadget((ushort)current->weaponVariantNum) ? SFX_GADGET_RAISE_GADGET
                                                                          : SFX_WEAPON_GENERIC_RAISE_WEAPON,
                              100.0f, 0, 0);
            }
            else {
                ToIdle(blData->weaponObject, current);
            }
            break;

        case GUN_RAISING:
            if (AnimScriptIsStopped(gun)) {
                if (current->animIdle != 0)
                    AnimScriptAdd(blData->weaponObject, current->animIdle);
                blData->refireTimer = 0.0f;
                blData->weaponObject->curState = GUN_IDLE;
                Player_ResetZoom(player);
            }
            else {
                anim = player->animState;
                if (anim->switchingToWeaponId != anim->currentWeaponId) {
                    // Changed again while coming up: back down from where it is
                    anim->prevHeldWeaponId = anim->currentWeaponId;
                    AnimScriptSetPlayDirection(blData->weaponObject, 2);
                    blData->weaponObject->curState = GUN_LOWERING;
                }
            }
            break;

        case GUN_PUT_BACK:
            if (current->animRaise != 0) {
                AnimScriptSetPlayDirection(gun, 2);
                blData->weaponObject->curState = GUN_LOWERING;
            }
            else {
                if (current->animIdle != 0)
                    AnimScriptAdd(gun, current->animIdle);
                blData->weaponObject->curState = GUN_IDLE;
            }
            break;

        default:
            switch (blData->idleState) {
                case IDLE_NONE:
                    // A fidget after 20 seconds without input; the gun lowered when nothing is going on
                    if (current->animFidget != 0 && blData->idleTicks >= (short)FRAME_RATE_INT * 20) {
                        AnimScriptAdd(gun, current->animFidget);
                        blData->idleState = IDLE_PLAYING;
                    }
                    else if (current->animRelax != 0 && blData->idleWait == 0 && !(player->animState->animFlags & 1) &&
                             blData->crosshairOffsetX == 0.0f && blData->crosshairOffsetY == 0.0f && !AnyDroneAlert()) {
                        AnimScriptAdd(gun, current->animRelax);
                        blData->idleState = IDLE_LOWERED;
                    }
                    break;

                case IDLE_PLAYING:
                    if (AnimScriptIsStopped(gun)) {
                        if (current->animIdle != 0)
                            AnimScriptAdd(blData->weaponObject, current->animIdle);
                        blData->idleTicks = 0;
                        blData->idleState = IDLE_NONE;
                    }
                    break;

                case IDLE_LOWERED:
                    if (AnimScriptIsStopped(gun)) {
                        bool alert = AnyDroneAlert();
                        if ((player->animState->animFlags & 1) || alert || blData->crosshairOffsetX != 0.0f ||
                            blData->crosshairOffsetY != 0.0f) {
                            if (current->animRelaxEnd != 0)
                                AnimScriptAddSpeed(blData->weaponObject, current->animRelaxEnd, 1.25f);
                            blData->idleTicks = 0;
                            blData->idleState = IDLE_PLAYING;
                            blData->idleWait = (short)(FRAME_RATE_INT * 20);
                        }
                    }
                    break;
            }
            anim = player->animState;
            if (anim->switchingToWeaponId != anim->currentWeaponId) {
                anim->prevHeldWeaponId = anim->currentWeaponId;
                Player_WeaponSelect(player);
            }
            break;
    }

    // Where the gun sits in the view
    const float *offset = multiplayer ? shown->viewOffsetAlt : shown->viewOffset;
    float x = offset[0];
    float y = offset[1];
    float z = offset[2];
    if (!multiplayer && blData->headCrouchOffset > 0.0f) {
        double crouched = (double)blData->headCrouchOffset * 2.2222223f;
        double standing = 1.0 - crouched;
        x = (float)(crouched * shown->viewOffsetAlt[0] + standing * x);
        y = (float)(crouched * shown->viewOffsetAlt[1] + standing * y);
        z = (float)(crouched * shown->viewOffsetAlt[2] + standing * z);
    }
    gun = blData->weaponObject;
    gun->position.x = x;
    gun->position.y = y;
    gun->position.z = z;
    blData->weaponObject->position.x = sway->x + blData->weaponObject->position.x;
    blData->weaponObject->position.y = sway->y + blData->weaponObject->position.y;
    blData->weaponObject->position.z = sway->z + blData->weaponObject->position.z;
    blData->weaponObject->position.y = blData->weaponObject->position.y - -0.2f;
    blData->weaponObject->position.z = blData->weaponObject->position.z - 0.5f;
}

// ---------------------------------------------------------------------------------------------------------------
// Firing

// Plays a fire anim on the gun and puts it in the firing state: the alternative one for the last round of a slide-locked
// gun (or weapon base 0x11 empty), half the time for weapons that pick at random, and each other shot for twin guns.
// Returns the anim's script, or NULL when there is no model or no anim.
sAnimScript_tag *Player_SetFiringAnim(BLData *blData, obj_tag *weaponObj, weapon_definition_tag *weapon) {
    short loaded = Player_AmmoIndex(glb_players[blData->playerNum]->animState->currentWeaponId);
    HASHCODE fire = (HASHCODE)0;

    if (((weapon->weaponFlags & WPNFLAG_SLIDE_LOCK) || weapon->weaponBaseNum == 0x11) &&
        blData->weaponStats[loaded].clipOrCooldown <= 0)
        fire = weapon->animFireAlt;
    if ((weapon->weaponFlags & WPNFLAG_RANDOM_FIRE) && Rand_Rand(100) > 50)
        fire = weapon->animFireAlt;
    if (fire == 0)
        fire = weapon->animFire;
    if (weapon->weaponFlags & WPNFLAG_TWIN_GUNS) {
        blData->twinGunHand = 1 - blData->twinGunHand;
        fire = blData->twinGunHand ? weapon->animFireAlt : weapon->animFire;
    }

    if (weapon->weaponModelHashcode == 0 || fire == 0)
        return NULL;
    blData->weaponObject->curState = GUN_FIRING;
    return AnimScriptAdd(weaponObj, fire);
}

// Moves ammo from the stock into the gun: as much as the clip takes, or one shell for a weapon that loads shells.
// Returns whether there was any to move; with test set, only says so.
// AUTOINJECT
bool Player_ReloadAmmoType(obj_tag *player, bool test) {
    BLData *blData = (BLData *)player->extraObjectData;
    char current = player->animState->currentWeaponId;
    weapon_definition_tag *weapon = &weapon_data[current];
    uchar type = weapon->ammoType;
    short loaded = Player_AmmoIndex(current);

    short stock = blData->ammo[type];
    if (stock <= 0)
        return false;
    int needed = weapon->clipSize - blData->weaponStats[loaded].clipOrCooldown;
    if (needed > 0 && (weapon->weaponFlags & WPNFLAG_SHELL_RELOAD))
        needed = 1;
    if (needed > stock)
        needed = stock;
    if (needed == 0)
        return false;
    if (!test) {
        blData->ammo[type] = (short)(stock - needed);
        blData->weaponStats[loaded].clipOrCooldown += (short)needed;
    }
    return true;
}

// Whether the gun holds a shot's rounds (needed), taking taken of them unless the unlimited ammo cheat is on; a weapon
// without ammo always fires. Player_WeaponFiring asks with nothing taken, and takes them when it fires.
// AUTOINJECT
bool Player_RoundToFire(obj_tag *player, uchar needed, uchar taken) {
    BLData *blData = (BLData *)player->extraObjectData;
    char current = player->animState->currentWeaponId;
    if (weapon_data[current].ammoType == 0)
        return true;

    WeaponStatus *loaded = &blData->weaponStats[Player_AmmoIndex(current)];
    short clip = loaded->clipOrCooldown;
    if (clip < needed)
        return false;
    if (!CheatInfo.UnlimitedAmmo)
        loaded->clipOrCooldown = (short)(clip - taken);
    return true;
}

// Where the player is aiming: 256 along the view from his eyes, through the crosshair when he is in the first-person
// view
// AUTOINJECT
void Player_GetAimingPoint(obj_tag *player, _VECTOR *point) {
    BLData *blData = (BLData *)player->extraObjectData;
    viewer_tag *viewer = glb_viewer[blData->playerNum];

    _VECTOR head, through, ahead;
    Player_GetHeadPos(&head, player, NULL);
    if (blData->camMode == 0) {
        // x87 chains; a NaN over a NaN gives the x87's choice
        double across = (double)blData->crosshairOffsetX / viewer->zoom * viewer->width;
        double wide = (double)viewer->projectionScaleX + viewer->projectionScaleX;
        through.x = (float)X87NaNs(across / wide, across, wide);
        double down = (double)blData->crosshairOffsetY / viewer->zoom * viewer->height;
        double high = (double)viewer->projectionScaleY + viewer->projectionScaleY;
        through.y = (float)Negate(X87NaNs(down / high, down, high));
    }
    else {
        through.x = 0.0f;
        through.y = 0.0f;
    }
    through.z = -1.0f;

    _MATRIX view;
    RotMatrixX(-(blData->pitchFromHorizontal * HALF_PI), &view);
    Concat(&player->transformMatrix, &view);
    ApplyMatrixLV(&view, &through, &ahead);
    Vec_MulR32(&ahead, &ahead, -256.0f);
    Vec_Add2(&head, &ahead, point);
}

// The player's next step will be with whichever foot the walk anim chooses (BLData::field_0x8e4 = 0xff)
// AUTOINJECT
void Player_ResetWalkAnim(obj_tag *player) {
    ((BLData *)player->extraObjectData)->field_0x8e4 = 0xff;
}

// Starts the player's shots of a weapon. A detonator sets off his live projectiles of its base weapon and goes back to
// it; weapon 0x53 takes control of the turret he set down. Otherwise each projectile flies from the eyes (half way
// between them and the muzzle, for weapons that say so) at what is under the crosshair; the muzzle flash goes there
// too. A taser that reaches no drone or player within its range gives its charge back instead.
void Player_WeaponInitBullet(obj_tag *player, weapon_definition_tag *weapon) {
    BLData *blData = (BLData *)player->extraObjectData;
    int playerNum = blData->playerNum;

    if (weapon->weaponFlags & WPNFLAG_DETONATOR) {
        for (obj_tag *obj = control_first_object(); obj != NULL; obj = obj->nextObject) {
            if (obj->objectType != OBJECTTYPE_BULLET)
                continue;
            BU_tag *bullet = (BU_tag *)obj->extraObjectData;
            if (bullet->wpnDef->weaponBaseNum == weapon->weaponBaseNum && bullet->firedByObj == player)
                bullet->fuseTimer = -10.0f;
        }
        if (Player_WeaponHasAmmo(blData, (ushort)weapon->weaponBaseNum)) {
            player->animState->switchingToWeaponId = (char)weapon->weaponBaseNum;
            return;
        }
        blData->weaponStats[(ushort)weapon->weaponBaseNum].enabled = 0;
        Player_GetBestWeapon(player);
        return;
    }

    if (weapon->weaponVariantNum == 0x53) {
        for (obj_tag *obj = control_first_object(); obj != NULL; obj = obj->nextObject) {
            if (obj->objectType == OBJECTTYPE_GUNTURRET && ((GUNTURRET *)obj->extraObjectData)->owner == player) {
                GT_TakeControl(obj, player);
                return;
            }
        }
    }

    if (weapon->numBulletsPerShot == 0)
        return;

    _VECTOR from, aim, target, direction;
    HITDATA_tag *hitList = NULL;
    Player_GetHeadPos(&from, player, NULL);
    Player_GetAimingPoint(player, &aim);
    Collide_RayIntersect(&from, &aim, player->inCel, player, NULL, &hitList, 0,
                         (weapon->projectileFlags & PROJFLAG_MATERIAL_MASK) ? 0x208 : 0x20a, 0);
    HITDATA_tag *hit = hitList;
    if (hit != NULL)
        Vec_Copy(&hit->hitPosition, &target);
    else
        Vec_Copy(&aim, &target);

    char current = player->animState->currentWeaponId;
    if (current == 0x4a || current == 0x4c) {
        blData->taserHit = 0;
        if (hit != NULL && hit->hitObj != NULL &&
            (hit->hitObj->objectType == OBJECTTYPE_DRONE || hit->hitObj->objectType == OBJECTTYPE_PLAYER) &&
            Vec_Dist3D(&from, &hit->hitPosition) < weapon_data[player->animState->currentWeaponId].range) {
            blData->taserHit = 1;
        }
        if (!blData->taserHit) {
            blData->shotFired = 1;
            Collide_FreeHitList(&hitList);
            char taser = player->animState->currentWeaponId;
            int charge = weapon_data[taser].ammoPerShot + blData->weaponStats[taser].clipOrCooldown;
            int full = weapon_data[taser].clipSize;
            blData->weaponStats[taser].clipOrCooldown = (short)(charge < full ? charge : full);
            return;
        }
    }

    Collide_FreeHitList(&hitList);
    Vec_Copy(&target, &blData->muzzle);
    blData->shotFired = 1;

    // From between the eyes and the muzzle: not for the fists, the Ronin, the cameras and gadgets 0x58, 0x59 and 0x5b,
    // nor through a real scope
    bool fromMuzzle = weapon->weaponModelHashcode != 0;
    weapon_definition_tag *held = &weapon_data[player->animState->currentWeaponId];
    switch ((ushort)held->weaponBaseNum) {
        case 1:
        case Weap_Ronin:
        case Weap_Camera:
        case Weap_QWorm:
        case 0x59:
        case 0x5b:
            fromMuzzle = false;
            break;
    }
    if ((player->animState->animFlags & 1) && (held->weaponFlags & WPNFLAG_REAL_SCOPE))
        fromMuzzle = false;
    if ((held->weaponFlags & WPNFLAG_FROM_MUZZLE) && fromMuzzle) {
        _VECTOR muzzle;
        AnimGetBoneWorldTrans(blData->weaponObject, 0, 0, &muzzle, NULL);
        from.x = (float)(((double)muzzle.x + from.x) * 0.5f);
        from.y = (float)(((double)muzzle.y + from.y) * 0.5f);
        from.z = (float)(((double)muzzle.z + from.z) * 0.5f);
    }
    Vec_Subtract(&target, &from, &direction);
    Vec_Normalise(&direction, &direction);

    ushort shot = 0;
    do {
        if (weapon->damageClass & 0xf8)
            PlrStat_LogShotFired(blData->playerNum);
        if (weapon->weaponBaseNum == Weap_Camera)
            Sound_PlayExt(SFX_GADGET_CAMERA_SHOOT, 100.0f, 0, 0);
        if (weapon->weaponBaseNum == Weap_Ronin) {
            // The Ronin is set down a unit in front of the player; if it cannot be, the round goes back
            Mat_GetDir(&direction, &player->transformMatrix);
            auxVec_AddMulR32(&player->position, &direction, 1.0f, &from);
            if (GT_DeployMiniGun(&from, &player->transformMatrix, player) == NULL)
                blData->weaponStats[Weap_Ronin].clipOrCooldown++;
        }
        else {
            Bullet_init((short)playerNum, player, player, weapon, &from, &direction, 0xffff);
        }
        shot++;
    } while (shot < weapon->numBulletsPerShot);

    if (weapon->weaponModelHashcode != 0 && (weapon->projectileFlags & PROJFLAG_CASING) && !MPSettings.isMultiplayer)
        Bullet_init_casing(player);
    if (weapon->ammoType == 0 && weapon->onFired != NULL)
        weapon->onFired(player);
}

// Shows what the weapon in the hand shows on its datums: the satchel charge's timer, the decryptor's display, weapon
// 0x33's charge (and its light), weapon 0x1a's lights, the rounds left in weapon 0x11; and the camera's and the remote
// mine's hints, once a level.
static void Player_WeaponDisplays(obj_tag *player, BLData *blData, weapon_definition_tag *weapon) {
    obj_tag *gun = blData->weaponObject;

    if (weapon->weaponFlags & WPNFLAG_SILENCER) {
        AnimState *anim = gun->animState;
        if (anim != NULL && anim->animObj.skin->numDatums > 0) {
            // Off while switching to the variant without it
            if (gun->curState == GUN_SWITCH_VARIANT)
                AnimDatumSetEntity(gun, 0, 0, 0xff);
            else if (weapon->suppressorGfx != 0)
                AnimDatumSetEntity(gun, 0, weapon->suppressorGfx, 0xff);
        }
    }
    char held = player->animState->currentWeaponId;
    if (weapon_data[held + weapon->offsetToNextAltFireVariant].weaponFlags & WPNFLAG_SILENCER)
        AnimDatumSetEntity(blData->weaponObject, 0, 0, 0xff);

    if (weapon->weaponBaseNum == Weap_Satchel) {
        // The timer counts up to five seconds a variant, once the throw anim is past frame 22
        sAnimScript_tag *script;
        if (blData->weaponObject->curState != GUN_SWITCH_VARIANT ||
            ((script = AnimListBuild(blData->weaponObject, 4, 0, NULL, 1)) != NULL && !(script->curFrame < 22.0f))) {
            short timer = (short)((weapon->weaponVariantNum - weapon->weaponBaseNum + 1) * 5);
            if ((short)blData->weaponObject->subState < timer)
                blData->weaponObject->subState++;
            if ((short)blData->weaponObject->subState > timer)
                blData->weaponObject->subState--;
            AnimDatumSetEntity(blData->weaponObject, 1, SatchelDigits[(short)blData->weaponObject->subState / 10], 0xff);
            AnimDatumSetEntity(blData->weaponObject, 2, SatchelDigits[(short)blData->weaponObject->subState % 10], 0xff);
        }
    }

    if (weapon->weaponBaseNum == Weap_Decryptor || weapon->weaponBaseNum == Weap_Decryptor_Upgraded) {
        if (GlobalVars.DecoderDisplayShown) {
            // The code shown, last digit on datum 0, with '*' blank; kept a second after
            for (int i = 0; i < 4; i++) {
                char digit = GlobalVars.DecoderDisplay[i];
                AnimDatumSetEntity(blData->weaponObject, 3 - i, digit == '*' ? 0 : DecryptorDigits[(uchar)digit - '0'],
                                   0xff);
            }
            blData->weaponObject->subState = (ushort)Ftol(_FRAME_RATE);
        }
        else {
            if ((short)blData->weaponObject->subState > 0)
                blData->weaponObject->subState--;
            if (blData->weaponObject->subState == 0) {
                AnimDatumSetEntity(blData->weaponObject, 0, 0, 0xff);
                AnimDatumSetEntity(blData->weaponObject, 1, 0, 0xff);
                AnimDatumSetEntity(blData->weaponObject, 2, 0, 0xff);
                AnimDatumSetEntity(blData->weaponObject, 3, 0, 0xff);
            }
        }
    }

    if (weapon->weaponBaseNum == Weap_LaserBeamFromSamurai) {
        sAnimScript_tag *script;
        if (blData->weaponObject->curState == GUN_FIRING &&
            (script = AnimListBuild(blData->weaponObject, 4, 0, NULL, 1)) != NULL) {
            // While the fire anim winds up to the shot: a light at the muzzle growing with it, and the charge falling
            double spawnFrame = (double)weapon->bulletSpawnFrame;
            if (spawnFrame > script->curFrame) {
                float wound = (float)(script->curFrame / spawnFrame * 0.75f);
                _VECTOR muzzle;
                AnimGetBoneWorldTrans(blData->weaponObject, 0, 0, &muzzle, NULL);
                Light_Create(&muzzle, 0, 0xff, 0xff, 5.0f, 0, 0, 1, (float)((double)wound + wound), 0, 0, 0, 0xffff);
                double clip = (double)weapon->clipSize;
                blData->weaponStats[Weap_LaserBeamFromSamurai].clipOrCooldown =
                    (short)Ftol(clip - clip * script->curFrame / (double)weapon->bulletSpawnFrame);
            }
        }
        for (int datum = 0x1e; datum < 0x26; datum++)
            AnimDatumSetEntity(blData->weaponObject, datum, HT_Entity_Dynamo_GLOW, 0xff);
        for (int datum = 5; datum < 0x19; datum++)
            AnimDatumSetEntity(blData->weaponObject, datum, HT_Entity_Pulse_GLOW, 0xff);
    }

    if (weapon->weaponVariantNum == Weap_Camera && blData->weaponObject->curState == GUN_IDLE && !CamHint) {
        CamHint = 1;
        Text_AddMsg(blData->playerNum, 0, 1, (char *)Txt_BindLabel((Action_TranslatedText)0x1000216, 0), 0, 0xb4);
    }

    if (weapon->weaponVariantNum == Weap_Camera_Upgraded && blData->weaponObject->curState == GUN_IDLE) {
        _VECTOR lens;
        AnimGetBoneWorldTrans(blData->weaponObject, 0, 0, &lens, NULL);
        Light_Create(&lens, 0xff, 0x7f, 0, 1.5f, 0, 0, 1, (float)(Float_FRand(0.5f) + 0.5f), 0, 0, 0, 0xffff);
    }

    if (weapon->weaponBaseNum == 0x1a) {
        for (int datum = 8; datum < 12; datum++)
            AnimDatumSetEntity(blData->weaponObject, datum, HT_Entity_gunsight3d, 0xff);
        // After 15 seconds idle datum 8 flashes; after 45 they all go out
        if ((double)_FRAME_RATE * 15.0f < (double)blData->idleTicks && (int)GameState.LoadTimeStart % 40 > 20)
            AnimDatumSetEntity(blData->weaponObject, 8, 0, 0xff);
        if (blData->idleWait == 0)
            AnimDatumSetEntity(blData->weaponObject, 9, 0, 0xff);
        if (blData->weaponStats[0x1b].clipOrCooldown == 0)
            AnimDatumSetEntity(blData->weaponObject, 10, 0, 0xff);
        if (weapon->weaponVariantNum == 0x1b && blData->weaponStats[0x1b].clipOrCooldown <= 2 && FlashMod > 0.75f)
            AnimDatumSetEntity(blData->weaponObject, 10, 0, 0xff);
        if (blData->weaponStats[0x1a].clipOrCooldown == 0)
            AnimDatumSetEntity(blData->weaponObject, 11, 0, 0xff);
        if (weapon->weaponVariantNum == 0x1a && blData->weaponStats[0x1a].clipOrCooldown <= 6 && FlashMod > 0.75f)
            AnimDatumSetEntity(blData->weaponObject, 11, 0, 0xff);
        if ((double)_FRAME_RATE * 45.0f < (double)blData->idleTicks) {
            for (int datum = 8; datum < 12; datum++)
                AnimDatumSetEntity(blData->weaponObject, datum, 0, 0xff);
        }
    }

    if (weapon->weaponBaseNum == 0x11) {
        // The rounds on show: those loaded (one more while firing), or during the reload what it will load
        short rounds = blData->weaponStats[Player_AmmoIndex(0x11)].clipOrCooldown;
        if (rounds > 0 && blData->weaponObject->curState == GUN_FIRING)
            rounds++;
        sAnimScript_tag *script;
        if (blData->weaponObject->curState == GUN_RELOADING &&
            (script = AnimListBuild(blData->weaponObject, 4, 0, NULL, 1)) != NULL && script->curFrame > 80.0f) {
            short stock = blData->ammo[weapon->ammoType];
            rounds = weapon->clipSize < stock ? weapon->clipSize : stock;
        }
        for (int datum = 4; datum < 7; datum++)
            AnimDatumSetEntity(blData->weaponObject, datum, 0, 0xff);
        if (rounds > 0 && blData->weaponObject->curState != GUN_FIRING)
            AnimDatumSetEntity(blData->weaponObject, 4, HT_Entity_Bolt, 0xff);
        if (rounds > 1)
            AnimDatumSetEntity(blData->weaponObject, 5, HT_Entity_Bolt, 0xff);
        if (rounds > 2)
            AnimDatumSetEntity(blData->weaponObject, 6, HT_Entity_Bolt, 0xff);
    }

    if (weapon->weaponVariantNum == Weap_RemoteMine && blData->weaponObject->curState == GUN_IDLE && !StickyHint) {
        StickyHint = 1;
        Text_AddMsg(blData->playerNum, 0, 1, (char *)Txt_BindLabel((Action_TranslatedText)0x10002dc, 0), 0, 0xb4);
    }
}

// Whether a gadget can be used now: the grapples only on a grapple point (in single player), the Q-Worm on its target
// in range, the decryptors on a lock that takes them (or the object drawn with gfx 0x20002f8) in range
static bool GadgetCanFire(obj_tag *player, BLData *blData, weapon_definition_tag *weapon) {
    short base = weapon->weaponBaseNum;
    if (base == 0x50 || base == 0x51) {
        if (player->subState == MovementType_Grapple && blData->grappleState == 1)
            return false;
        if (blData->movementDisabled)
            return false;
        if (blData->thirdIcon != THIRDICON_GRAPPLE && !MPSettings.isMultiplayer)
            return false;
    }
    if (base == Weap_QWorm) {
        obj_tag *target = blData->gadgetTarget;
        if (target == NULL || target->objectType != OBJECTTYPE_QWORM || target->curState != 0)
            return false;
        if (Vec_Dist3D(&player->position, &target->position) > weapon->range)
            return false;
    }
    base = weapon->weaponBaseNum;
    if (base == Weap_Decryptor || base == Weap_Decryptor_Upgraded) {
        obj_tag *target = blData->gadgetTarget;
        if (target == NULL)
            return false;
        bool lockReady = false;
        if (target->objectType == OBJECTTYPE_LOCK) {
            short kind = *(short *)target->extraObjectData;
            if ((kind == 0 || kind == 2) && (target->curState == 0 || target->curState == 5))
                lockReady = true;
        }
        if (!(target->objectType == 0 &&
              blData->gadgetTarget->objGraphics == hashtable_hashcode_to_celglist(HT_Entity_terminal_keyboard)) &&
            !lockReady)
            return false;
        if (Vec_Dist3D(&player->position, &blData->gadgetTarget->position) > weapon->range)
            return false;
    }
    return true;
}

// Starts a reload, from the start anim if the weapon has one
static void StartReload(BLData *blData, weapon_definition_tag *weapon, bool slideLockStart) {
    blData->weaponObject->curState = GUN_RELOADING;
    if (weapon->animReloadStart != 0) {
        blData->weaponObject->curState = GUN_RELOAD_START;
        AnimScriptAdd(blData->weaponObject, weapon->animReloadStart);
        return;
    }
    if (weapon->animReload == 0)
        return;
    if (weapon->weaponFlags & WPNFLAG_ALT_RELOADS) {
        blData->twinGunHand = 1 - blData->twinGunHand;
        AnimScriptAdd(blData->weaponObject, blData->twinGunHand ? weapon->animReload : weapon->animReloadEnd);
        return;
    }
    AnimScriptAddSpeed(blData->weaponObject, weapon->animReload, 1.35f);
    if (slideLockStart && (weapon->weaponFlags & WPNFLAG_SLIDE_LOCK)) {
        // Emptied: the reload starts with the slide back
        sAnimScript_tag *script = AnimListBuild(blData->weaponObject, 4, 0, NULL, 1);
        if (script != NULL)
            script->curFrame = 6.0f;
    }
}

// The trigger, every frame: the weapon's displays, reloading (by hand, or when the gun runs dry), the gadgets' tests,
// and firing when the refire delay is over: rounds taken, a rumble, the fire anim, and the shot (now or at the anim's
// frame). Remote control: fire sets the guided missile off. fire set fires whether or not the button is pressed.
// AUTOINJECT
void Player_WeaponFiring(obj_tag *player, char fire) {
    BLData *blData = (BLData *)player->extraObjectData;
    weapon_definition_tag *weapon = &weapon_data[player->animState->currentWeaponId];

    blData->shotFired = 0;
    for (int i = 0; i < 32; i++)
        View_SetDrawInNoViews(blData->beamObjs[i]);
    View_SetDrawInNoViews(blData->sightObj);
    Player_WeaponDisplays(player, blData, weapon);

    Player_GetAimingPoint(player, &blData->muzzle);

    // Reload, or use whatever is in front of the player
    if (!Input_Action(blData->playerNum, ACTION_FIRE, 1) && Input_Action(blData->playerNum, ACTION_RELOAD, 4) &&
        player->subState != MovementType_RemoteControl) {
        uchar used = Player_Activate(blData, player);
        AnimState *anim = player->animState;
        if (!((anim->animFlags & 1) && (weapon_data[anim->currentWeaponId].weaponFlags & WPNFLAG_REAL_SCOPE)) && !used &&
            blData->weaponObject->curState == GUN_IDLE && Player_ReloadAmmoType(player, true)) {
            char held = player->animState->currentWeaponId;
            blData->burstShotsLeft = weapon->shotsPerTrigger[(uchar)blData->weaponStats[held].fireModeIndex];
            if (weapon->weaponFlags & WPNFLAG_ALT_RELOADS) {
                blData->weaponObject->curState = GUN_RELOADING;
                blData->twinGunHand = 1 - blData->twinGunHand;
                AnimScriptAdd(blData->weaponObject, blData->twinGunHand ? weapon->animReload : weapon->animReloadEnd);
                return;
            }
            StartReload(blData, weapon, false);
            return;
        }
    }

    if (Input_Action(blData->playerNum, ACTION_FIRE, 1)) {
        if (!surfaced && !(weapon->weaponFlags & WPNFLAG_UNDERWATER))
            return;
        if (weapon->weaponBaseNum == Weap_Camera && !(player->animState->animFlags & 1))
            return;
        if (!GadgetCanFire(player, blData, weapon))
            return;
        if (!Player_RoundToFire(player, weapon->ammoPerShot, 0) && player->subState != MovementType_RemoteControl)
            return;
    }
    else {
        blData->burstShotsLeft = 0;
        if (!Player_RoundToFire(player, weapon->ammoPerShot, 0)) {
            // Empty: reload, or change weapons when there is nothing to reload with
            if (blData->weaponObject->curState != GUN_IDLE || player->subState == MovementType_RemoteControl)
                return;
            if (!Player_ReloadAmmoType(player, true)) {
                Player_HandleHasNoAmmo(player);
                return;
            }
            char held = player->animState->currentWeaponId;
            blData->burstShotsLeft = weapon->shotsPerTrigger[(uchar)blData->weaponStats[held].fireModeIndex];
            blData->refireTimer = 0.0f;
            StartReload(blData, weapon, true);
            return;
        }
    }

    if (Input_Action(blData->playerNum, ACTION_FIRE, 4) || fire == 1) {
        if (player->subState == MovementType_RemoteControl) {
            if (blData->remoteControlDevice != NULL)
                Bullet_handle_object_destruction(blData->remoteControlDevice, NULL);   // unread: the original passes the weapon
            return;
        }
        if (blData->refireTimer <= 0.0f) {
            char held = player->animState->currentWeaponId;
            blData->burstShotsLeft = weapon->shotsPerTrigger[(uchar)blData->weaponStats[held].fireModeIndex];
            blData->refireTimer = 1.0f;
        }
    }

    if (!(blData->refireTimer > 0.0f))
        return;
    double left = blData->refireTimer - (double)FRAME_RATE_MUL;
    blData->refireTimer = (float)left;
    switch ((short)blData->weaponObject->curState) {
        case GUN_IDLE:
        case GUN_THROW_WINDUP:
            break;
        case GUN_FIRING:
            if (weapon->weaponFlags & WPNFLAG_SINGLE_ACTION)
                return;
            break;
        default:
            return;
    }
    if (!(left <= 0.0))
        return;
    if (--blData->burstShotsLeft < 0)
        return;
    if (!Player_RoundToFire(player, weapon->ammoPerShot, weapon->ammoPerShot))
        return;

    if (player->objectType == OBJECTTYPE_PLAYER && weapon->rumble != 0)
        Input_RumbleStart(blData->playerNum, 5, weapon->rumble);
    double delay = (double)(uint)weapon->refireDelay;
    if (!(1.0 <= delay))
        delay = 1.0;
    blData->refireTimer = (float)delay;

    sAnimScript_tag *script = NULL;
    if (!(blData->weaponObject->curState == GUN_FIRING && (weapon->weaponFlags & (WPNFLAG_CONTINUOUS | WPNFLAG_REFIRE_HELD))))
        script = Player_SetFiringAnim(blData, blData->weaponObject, weapon);
    if (weapon->projectileFlags & PROJFLAG_THROWN) {
        if (weapon->animFireAlt != 0) {
            blData->weaponObject->curState = GUN_THROW_WINDUP;
            if (script != NULL)
                script->speed = 1.5f;
        }
        else {
            blData->weaponObject->curState = GUN_THROWING;
        }
    }

    if (weapon->bulletSpawnFrame == 0) {
        blData->bulletSpawned = 1;
        Player_WeaponInitBullet(player, weapon);
        return;
    }
    blData->bulletSpawned = 0;
}

// ---------------------------------------------------------------------------------------------------------------
// The zoom and the auto-aim

// The scope's zoom: aiming, the zoom button moves the target by 2.5% a frame (and the weapon remembers where it was
// left); otherwise the zoom moves 15% a frame towards the target and the motor's sound stops. The target stays between
// 1 and the weapon's maximum (and 50). A change of zoom blurs the view, in single player.
// AUTOINJECT
void Player_Zoom(obj_tag *player) {
    BLData *blData = (BLData *)player->extraObjectData;
    float before = blData->zoom;
    float input = 0.0f;
    if (player->animState->animFlags & 1)
        input = Input_Actionf(blData->playerNum, ACTION_SCOPE_ZOOM, 1);

    float gap, step;
    if (input == 0.0f) {
        gap = blData->zoom - blData->zoomTarget;
        double rate = (double)FRAME_RATE_MUL * 0.15f + 1.0f;
        step = (float)rate;
        if (gap > 0.0f)
            step = (float)(1.0 / rate);
        if (blData->zoomSound != NULL) {
            Sound_Stop(blData->zoomSound);
            blData->zoomSound = NULL;
        }
    }
    else {
        double rate = (double)FRAME_RATE_MUL * 0.025f + 1.0f;
        step = (float)rate;
        if (input > 0.0f)
            step = (float)(1.0 / rate);
        blData->zoomTarget = step * blData->zoomTarget;
        gap = input;
    }

    if (blData->zoomTarget > blData->zoomMax)
        blData->zoomTarget = blData->zoomMax;
    if (blData->zoomTarget < 1.0f)
        blData->zoomTarget = 1.0f;
    else if (blData->zoomTarget > 50.0f)
        blData->zoomTarget = 50.0f;

    if (gap != 0.0f) {
        double zoom = (double)step * blData->zoom;
        blData->zoom = (float)zoom;
        if (gap < 0.0f) {
            if (zoom > blData->zoomTarget)
                blData->zoom = blData->zoomTarget;
        }
        else if (zoom < blData->zoomTarget) {
            blData->zoom = blData->zoomTarget;
        }
    }

    if (input != 0.0f) {
        blData->zoomTarget = blData->zoom;
        blData->weaponStats[(ushort)weapon_data[player->animState->currentWeaponId].weaponBaseNum].zoom = blData->zoom;
        if (blData->zoom != before &&
            (Input_Action(blData->playerNum, ACTION_TMP_ZOOMOUT, 4) || Input_Action(blData->playerNum, ACTION_TMP_ZOOMIN, 4)) &&
            blData->zoomSound != NULL) {
            Sound_Stop(blData->zoomSound);
        }
    }

    if (!MPSettings.isMultiplayer && blData->zoom != before)
        GlobalBlur = 0x7f;
}

// Whether to aim at a target, and if so where: a live enemy drone (by team with bots), another live player, or
// anything else that was drawn, closer than the best so far, inside the auto-aim cone (wider for the weapon's auto-aim
// strength and the difficulty, narrower further away, wider again for the target locked on to) and in sight. The
// crosshair goes to it on the screen; *best becomes its distance.
// AUTOINJECT
void Check_AutoAim(obj_tag *player, obj_tag *target, float *best) {
    BLData *blData = (BLData *)player->extraObjectData;
    viewer_tag *viewer = glb_viewer[blData->playerNum];

    if (target == NULL || !target->drawnCountdown)
        return;
    if (target->objectType == OBJECTTYPE_DRONE) {
        Drone_tag *drone = (Drone_tag *)target->extraObjectData;
        if (drone->health <= 0.0f)
            return;
        if (drone->skinClass == SKINCLASS_NINJA)
            return;
        if (target->curState == 1)
            return;
        if (MPSettings.dronesAreBots) {
            if (MP_areObjectsOnSameTeam(player, drone->gameObj))
                return;
        }
        else if (drone->side != DRONE_SIDE_ENEMY) {
            return;
        }
    }
    else if (target->objectType == OBJECTTYPE_PLAYER) {
        if (MPSettings.dronesAreBots && MP_areObjectsOnSameTeam(player, target))
            return;
        BLData *other = (BLData *)target->extraObjectData;
        if (other->playerNum == blData->playerNum)
            return;
        if (other->health <= 0.0f)
            return;
    }
    else if (target->objectType == OBJECTTYPE_DEAD_PLAYER) {
        return;
    }

    _VECTOR head, at;
    Player_GetHeadPos(&head, player, NULL);
    if (target->objectType == OBJECTTYPE_DRONE || target->objectType == OBJECTTYPE_PLAYER)
        AnimGetBoneWorldTrans(target, 0x80000002, 0, &at, NULL);
    else
        Vec_Copy(&target->position, &at);

    double range = Vec_Dist3D(&head, &at);
    if (range > *best)
        return;
    float distance = (float)range;

    float bearing = (float)FPatan((double)at.x - head.x, (double)at.z - head.z);
    double turn = Vec_AngleDifference(bearing, player->rotation.y);

    char held = player->animState->currentWeaponId;
    double strength = (1.0 - (double)distance / Autoaim_Range) * ((double)weapon_data[held].autoAimStrength * 0.01f);
    switch (GameState.difficultyModifier) {
        case 1:
            strength = strength * Autoaim_EasyMul;
            break;
        case 3:
        case 4:
            strength = strength * Autoaim_HardMul;
            break;
        default:
            strength = strength * Autoaim_NormalMul;
            break;
    }
    if (held == 0x50 || held == 0x51)
        strength = 1.0;
    float across = (float)(Autoaim_Angle_H * strength);
    float upDown = (float)(Autoaim_Angle_V * strength);
    if (target == blData->autoAimTarget) {
        across = Autoaim_LockOnMul * across;
        upDown = Autoaim_LockOnMul * upDown;
    }
    if (across == 0.0f && upDown == 0.0f)
        return;
    if (!(fabs(turn) < across))
        return;

    float elevation = (float)CIAsin(((double)at.y - head.y) / distance);
    double tilt = Vec_AngleDifference(elevation, blData->pitchFromHorizontal * HALF_PI);
    if (!(fabs(tilt) < upDown))
        return;

    if (!Collide_LineOfSight(&head, &at, player->inCel, player, target, 0xa))
        return;

    _VECTOR screen = {0.0f, 0.0f, 0.0f};
    if (View_3DPoint2Screen(&at, &screen, (uchar)viewer->idx) > 0) {
        blData->crosshairOffsetX = (float)(((double)screen.x - (double)viewer->width * 0.5f) / ((double)viewer->width * 0.25f));
        blData->autoAimTarget = target;
        blData->crosshairOffsetY = (float)(((double)screen.y - (double)viewer->height * 0.5f) / ((double)viewer->height * 0.25f));
    }
    *best = distance;
}

// Auto-aim, with the player's option on (always for the grapples): unscoped, the nearest target in the cone (grapple
// points for the grapples, within their range; else drones and players, within Autoaim_Range) draws the crosshair, and
// the crosshair and aim ease back to the centre; scoped, the crosshair eases to the aim. Then the gadgets' targets
// (Check_Target).
// AUTOINJECT
void Player_AutoAim(obj_tag *player) {
    BLData *blData = (BLData *)player->extraObjectData;
    char autoAim = MPSettings.isMultiplayer ? PlayerInputs[blData->playerNum].autoAimMultiplayer
                                            : PlayerInputs[blData->playerNum].autoAim;
    char held = player->animState->currentWeaponId;
    bool grapple = held == 0x50 || held == 0x51;

    if (player->animState->animFlags & 1) {
        blData->crosshairOffsetX = (float)(((double)blData->aimX - blData->crosshairOffsetX) * 0.5f + blData->crosshairOffsetX);
        blData->crosshairOffsetY = (float)(((double)blData->aimY - blData->crosshairOffsetY) * 0.5f + blData->crosshairOffsetY);
        Check_Target(player);
        return;
    }

    if (grapple || autoAim) {
        float best = Autoaim_Range;
        if (grapple) {
            best = weapon_data[held].range;
            for (obj_tag *obj = control_first_object(); obj != NULL; obj = obj->nextObject) {
                if (obj->objectType == OBJECTTYPE_GRAPPLE)
                    Check_AutoAim(player, obj, &best);
            }
        }
        else {
            for (Drone_tag *drone = NPCGlobals.NDrone2List; drone != NULL; drone = drone->next)
                Check_AutoAim(player, drone->gameObj, &best);
            for (int i = 0; i < 4; i++)
                Check_AutoAim(player, glb_players[i], &best);
        }
        if (best == Autoaim_Range)
            blData->autoAimTarget = NULL;
    }
    blData->crosshairOffsetX = blData->crosshairOffsetX * 0.5f;
    blData->crosshairOffsetY = blData->crosshairOffsetY * 0.5f;
    blData->aimX = blData->aimX * 0.5f;
    blData->aimY = blData->aimY * 0.5f;
    Check_Target(player);
}

// ---------------------------------------------------------------------------------------------------------------
// The laser sight, the muzzle flash and the watch

// The object the gun or the watch is drawn on: placed by its matrix, drawn in no view, its owner the player. The
// watch's hands go with the last one.
// AUTOINJECT
obj_tag* WatchOrHands_Create(obj_tag *owner) {
    obj_tag *obj = control_create_object(0, NULL, NULL, NULL);
    SecondHand = NULL;
    MinuteHand = NULL;
    HourHand = NULL;
    if (obj != NULL) {
        obj->transformFlags |= TRANSFORM_MATRIX_PLACED;
        Vec_Zero(&obj->position);
        Vec_Zero(&obj->rotation);
        View_SetDrawInNoViews(obj);
        obj->curState = 0;
        obj->objectType = 0;
        obj->parent = owner;
        obj->effectFlags |= EFFECT_NO_COLLISION | EFFECT_DRAW_LAST;
    }
    return obj;
}

// The player's 32 beams (the laser sight's, the taser's): graphics objects stretched along their length, drawn
// only in the player's own view. Not named on PS2; the name is ours.
// FUNC_AT(0x000b6dd0)
void Player_CreateBeams(obj_tag *player, char playerNum) {
    BLData *blData = (BLData *)player->extraObjectData;
    for (int i = 0; i < 32; i++) {
        obj_tag *beam = control_create_object(0, NULL, NULL, NULL);
        if (beam == NULL)
            continue;
        beam->transformFlags |= TRANSFORM_SCALE_LENGTH_ONLY;
        beam->objectType = OBJECTTYPE_GFX;
        beam->effectFlags |= EFFECT_NO_COLLISION;
        beam->parent = player;
        beam->scale = 1.0f;
        View_SetDrawInThisViewOnly(beam, playerNum);
        blData->beamObjs[i] = beam;
    }
}

// The laser sight (weapons with flag 8, or whose alt-fire variant has it): a beam from the gun's datum 4 along it, to
// where the line from the eyes 100 out along it first hits something (the point on that line nearest the hit), only in
// the player's own first-person view; brighter in night vision. Where it hits, the red dot, just off the surface and
// growing with distance.
// AUTOINJECT
void Player_LaserPointer(obj_tag *player) {
    HITDATA_tag *hitList = NULL;
    BLData *blData = (BLData *)player->extraObjectData;
    obj_tag *beam = blData->beamObjs[0];
    obj_tag *dot = blData->sightObj;
    if (!blData->laserSight)
        return;

    char held = player->animState->currentWeaponId;
    weapon_definition_tag *weapon = &weapon_data[held];
    int playerNum = blData->playerNum;
    if (!(weapon->weaponFlags & WPNFLAG_LASER_SIGHT) &&
        !(weapon_data[held + weapon->offsetToNextAltFireVariant].weaponFlags & WPNFLAG_LASER_SIGHT))
        return;
    if (beam == NULL || dot == NULL)
        return;

    _VECTOR head, from, out, end;
    _MATRIX datum;
    Player_GetHeadPos(&head, player, NULL);
    AnimGetBoneWorldTrans(blData->weaponObject, 4, 0, &from, &datum);
    _VECTOR ahead = {0.0f, 0.0f, 100.0f};
    MatrixMultiplyVector(&datum, &ahead, &out);

    HITDATA_tag *hit = NULL;
    if (Collide_RayIntersect(&head, &out, player->inCel, player, NULL, &hitList, 0, 8, 0))
        hit = hitList;
    if (hit != NULL)
        vecutil_calculate_closest_point_on_line(&end, &head, &out, &hit->hitPosition);
    else
        end = out;

    beam = blData->beamObjs[0];
    beam->position = from;
    beam->transformFlags &= ~TRANSFORM_MOVED;
    if (glb_viewer[playerNum]->nightVisionRelated == 1) {
        hashtable_set_object_to_entity_gfx(beam, HT_Entity_lazer);
        double flicker = Float_FRand(128.0f);
        beam->tweakA = (uchar)Ftol((flicker + (beam->tweakA + 0x3f)) * 0.5f);
    }
    else {
        hashtable_set_object_to_entity_gfx(beam, HT_Entity_watch_lazer);
        uchar alpha = beam->tweakA + (uchar)(Rand_Rand(9) - 4);
        beam->tweakA = alpha;
        if (alpha < 0x10)
            beam->tweakA = 0x10;
        else if (alpha > 0x30)
            beam->tweakA = 0x30;
    }
    if (blData->camMode == 0)
        View_SetDrawInThisViewOnly(beam, blData->playerNum);

    float length = Vec_Dist3D(&from, &end);
    beam->transformFlags |= TRANSFORM_SCALE_LENGTH_ONLY | TRANSFORM_MOVED;
    beam->scale = length;
    beam->effectFlags |= EFFECT_DRAW_LAST;
    vecutil_cartesian_to_spherical_acc(&beam->rotation, from.x - end.x, from.y - end.y, from.z - end.z);
    beam->rotation.x = -beam->rotation.x;
    beam->rotation.z = 0.0f;

    if (hit != NULL) {
        Mat_Align2Dir(&dot->transformMatrix, &hit->surfaceNormal.normal, &CONST_UP_VECTOR, &MAYBE_CONST_FORWARD_VECTOR);
        _VECTOR out2;
        Mat_GetDir(&out2, &dot->transformMatrix);
        dot->transformFlags |= TRANSFORM_MOVED;
        dot->tweakR = 0x40;
        dot->tweakG = 0;
        dot->tweakB = 0;
        dot->position.x = (float)((double)out2.x * 0.01f + end.x);
        dot->position.y = (float)((double)out2.y * 0.01f + end.y);
        dot->position.z = (float)((double)out2.z * 0.01f + end.z);
        double size = (double)length * length / MAX_VIEW_CONE + 1.0f;
        dot->scale = (float)size;
        if (size > 4.0f)
            dot->scale = 4.0f;
        View_SetDrawInAllViews(dot);
    }
    Collide_FreeHitList(&hitList);
}

// A shot's flash: a light at the muzzle (datum 0, or 4 for the twin guns' other hand) for 3 frames (7 for weapon base
// 0x33) and a puff of smoke, in single player. A beam weapon draws its beam to the target, or to what is in the way. In
// multiplayer, the flash on the character's own weapon datum.
// AUTOINJECT
void Player_MuzzleFlash(obj_tag *player, _VECTOR *target) {
    weapon_definition_tag *weapon = &weapon_data[player->animState->currentWeaponId];
    BLData *blData = (BLData *)player->extraObjectData;
    _VECTOR muzzle;
    _MATRIX datum;

    if (weapon->weaponModelHashcode != 0 && weapon->muzzleFlash1stPerson != 0) {
        uint bone = ((weapon->weaponFlags & WPNFLAG_TWIN_GUNS) && !blData->twinGunHand) ? 4 : 0;
        AnimGetBoneWorldTrans(blData->weaponObject, bone, 0, &muzzle, &datum);
        blData->muzzleFlashFrames = weapon->weaponBaseNum == 0x33 ? 7 : 3;
        Light_Create(&muzzle, weapon->muzzleFlash_r, weapon->muzzleFlash_g, weapon->muzzleFlash_b,
                     weapon->muzzleLightRadius, 0, 0, 1, 2.0f, 0, 0, 0, 0xffff);
        if (!MPSettings.isMultiplayer && !(weapon->weaponFlags & WPNFLAG_NO_SMOKE)) {
            _VECTOR spin = {0.0f, 0.0f, 0.0f};
            spin.z = (float)Float_FRand(TWO_PI);
            Gas_Init(player->inCel, &muzzle, &spin, NULL, 0.05f, 0.1f, HT_Entity_smoke_1_dust, 7, 0x48403080, 1, 0x80);
        }
    }

    if ((weapon->projectileFlags & (PROJFLAG_TASER | PROJFLAG_LASER)) && target != NULL) {
        HITDATA_tag *hitList = NULL;
        AnimGetBoneWorldTrans(blData->weaponObject, 0, 0, &muzzle, NULL);
        if (Collide_RayIntersect(&muzzle, target, player->inCel, player, NULL, &hitList, 0, 8, 0) && hitList != NULL)
            Vec_Copy(&hitList->hitPosition, target);
        if (weapon->projectileFlags & PROJFLAG_LASER)
            Player_SetupLaser(blData, target);
        if ((weapon->projectileFlags & PROJFLAG_TASER) && blData->taserHit)
            Player_SetupTaser(blData, target);
        Collide_FreeHitList(&hitList);
    }

    if (MPSettings.isMultiplayer) {
        HASHCODE flash = (HASHCODE)0;
        int datumId = AnimDatumGetWeaponInfo(player, NULL, &flash, NULL);
        if (datumId >= 0)
            AnimDatumSetEntity(player, datumId + 1, flash, 3);
    }
}

// The muzzle flash in the player's own view while it lasts: the weapon's flash at the muzzle, pointing along it, spun
// at random, scaled 0.9 to 1.1, at a random alpha in the weapon's range
// AUTOINJECT
void Draw_MuzzleFlash(obj_tag *player) {
    BLData *blData = (BLData *)player->extraObjectData;
    weapon_definition_tag *weapon = &weapon_data[player->animState->currentWeaponId];
    char playerNum = blData->playerNum;
    if (weapon->muzzleFlash1stPerson == 0)
        return;

    _VECTOR forward = {0.0f, 0.0f, 1.0f};
    _VECTOR muzzle, tip;
    _MATRIX datum;
    uint bone = ((weapon->weaponFlags & WPNFLAG_TWIN_GUNS) && !blData->twinGunHand) ? 4 : 0;
    AnimGetBoneWorldTrans(blData->weaponObject, bone, 0, &muzzle, &datum);
    MatrixMultiplyVector(&datum, &forward, &tip);

    blData->muzzleFlashObj->position = muzzle;
    tip.x = tip.x - muzzle.x;
    tip.y = tip.y - muzzle.y;
    tip.z = tip.z - muzzle.z;
    Vec_Cartesian_2_Spherical(&blData->muzzleFlashObj->rotation, tip.x, tip.y, tip.z);
    obj_tag *flash = blData->muzzleFlashObj;
    flash->rotation.x = -flash->rotation.x;
    blData->muzzleFlashObj->rotation.z = (float)Float_FRand(TWO_PI);
    View_SetDrawInThisViewOnly(blData->muzzleFlashObj, playerNum);
    hashtable_set_object_to_entity_gfx(blData->muzzleFlashObj, weapon->muzzleFlash1stPerson);
    blData->muzzleFlashObj->transformFlags |= 0x24;
    blData->muzzleFlashObj->effectFlags |= EFFECT_DRAW_LAST;
    blData->muzzleFlashObj->scale = (float)(Float_FRand(0.2f) + 0.9f);

    flash = blData->muzzleFlashObj;
    _VECTOR aim = {flash->rotation.x, flash->rotation.y, 0.0f};
    _MATRIX aimed;
    RotMatrixZYX(&aim, &aimed);
    _VECTOR roll = {0.0f, 0.0f, blData->muzzleFlashObj->rotation.z};
    RotMatrix(&roll, &datum);
    MulMatrix0(&aimed, &datum, &blData->muzzleFlashObj->transformMatrix);
    flash = blData->muzzleFlashObj;
    *Mat_Position(flash->transformMatrix) = flash->position;
    MatrixScale(&blData->muzzleFlashObj->transformMatrix, blData->muzzleFlashObj->scale);

    float spread = (float)(weapon->muzzleFlashAlphaMax - weapon->muzzleFlashAlphaMin);
    double alpha = Float_FRand(spread) + weapon->muzzleFlashAlphaMin;
    blData->muzzleFlashObj->tweakA = (uchar)Ftol(alpha);
}

// The watch on the wrist (the weapon object with skin 0x500004b, in single player): its face on datum 2 and the three
// hands, set to the mission's start time (by level) plus its elapsed time. With anything else in hand the hands go.
// AUTOINJECT
void Player_SetWatch(obj_tag *player) {
    float time = 0.0f;
    BLData *blData = (BLData *)player->extraObjectData;
    if (MPSettings.isMultiplayer)
        return;
    obj_tag *gun = blData->weaponObject;
    if (gun == NULL || gun->animState == NULL)
        return;

    // The hour each level starts at, in centiseconds
    switch (GameState.CurrentLevelHashcode) {
        case HT_Level_HendersonA:
        case HT_Level_HendersonB:
        case HT_Level_HendersonC:
        case HT_Level_HendersonD:
            time = 3240000.0f;
            break;
        case HT_Level_CastleExterior:
        case HT_Level_CastleCourtyard:
        case HT_Level_CastleIndoors1:
        case HT_Level_CastleIndoors2:
            time = 7020000.0f;
            break;
        case HT_Level_TowerA:
        case HT_Level_TowerB:
        case HT_Level_TowerC:
            time = 8100000.0f;
            break;
        case HT_Level_PowerStationA1:
        case HT_Level_PowerStationA2:
            time = 2880000.0f;
            break;
        case HT_Level_Tower2A:
        case HT_Level_Tower2B:
        case HT_Level_Tower2C:
        case HT_Level_Tower2Elevator:
            time = 2160000.0f;
            break;
        case HT_Level_EvilBase:
        case HT_Level_EvilSilo:
        case HT_Level_EvilBaseC:
            time = 1440000.0f;
            break;
        default:
            break;
    }

    if (gun->animState->animObj.skin->hashcode != HT_Skin_Watch) {
        if (HourHand != NULL) {
            HourHand->flags |= 1;
            HourHand = NULL;
        }
        if (MinuteHand != NULL) {
            MinuteHand->flags |= 1;
            MinuteHand = NULL;
        }
        if (SecondHand != NULL) {
            SecondHand->flags |= 1;
            SecondHand = NULL;
        }
        return;
    }

    AnimDatumSetEntity(gun, 2, HT_Entity_bits_Hands_GLOW, 0xff);
    time = (float)((double)PlrMissionStats[0].timeElapsed + time);
    _VECTOR face;
    _MATRIX faceMtx;
    AnimGetBoneWorldTrans(blData->weaponObject, 2, 0, &face, &faceMtx);

    if (HourHand == NULL)
        HourHand = Control_CreateObjEx(0, &face, NULL, &faceMtx, hashtable_hashcode_to_celglist(HT_Entity_Little_hand),
                                       player, 1, 0, 1.0f, 0, 0xff, 0xff, 0xff);
    if (MinuteHand == NULL)
        MinuteHand = Control_CreateObjEx(0, &face, NULL, &faceMtx, hashtable_hashcode_to_celglist(HT_Entity_Big_hand),
                                         player, 1, 0, 1.0f, 0, 0xff, 0xff, 0xff);
    if (SecondHand == NULL)
        SecondHand = Control_CreateObjEx(0, &face, NULL, &faceMtx,
                                         hashtable_hashcode_to_celglist(HT_Entity_Second_hand),
                                         player, 1, 0, 1.0f, 0, 0xff, 0xff, 0xff);

    // A turn every 12 hours, every hour and every minute
    obj_tag *hands[3] = {HourHand, MinuteHand, SecondHand};
    static const float rates[3] = {1.4544411e-06f, 1.7453292e-05f, 0.0010471976f};
    for (int i = 0; i < 3; i++) {
        if (hands[i] == NULL)
            continue;
        _MATRIX turned;
        RotMatrixX((float)(-fmod((double)time * rates[i], (double)TWO_PI) - HALF_PI), &turned);
        Concat(&faceMtx, &turned);
        Mat_CopyRot(&turned, &hands[i]->transformMatrix);
        Matrix_SetTrans(Mat_Position(faceMtx), &hands[i]->transformMatrix);
        hands[i]->effectFlags = (hands[i]->effectFlags & ~EFFECT_CEL_LIT) | EFFECT_DRAW_LAST;
        hands[i]->transformFlags |= 0x24;
    }
}
