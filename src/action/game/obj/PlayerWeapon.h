#ifndef PLAYERWEAPON_H_
#define PLAYERWEAPON_H_

#include "../../actionhelpers.h"

// The player's weapons (PlayerWeapon.cpp): what he carries, choosing and changing weapons, the gun in the view and its
// anims, firing, the zoom, the auto-aim, the laser sight, the muzzle flash and the watch.

// BLData::idleState: the gun's idle anims (Player_SetWeaponAnimObj)
enum IdleState {
    IDLE_NONE = 0,              // idle, counting towards a fidget or lowering the gun
    IDLE_PLAYING = 1,           // a fidget, or the gun coming back up, is playing
    IDLE_LOWERED = 3,           // lowered (weapon_definition_tag::animRelax) until a drone is alert or the player aims
};

// The weapon object's states (BLData::weaponObject->curState)
enum GunState {
    GUN_IDLE = 0,
    GUN_LOWER = 1,              // put away, to change weapons
    GUN_LOWERING = 2,
    GUN_RAISE = 3,              // the new weapon's model, brought up
    GUN_RAISING = 4,
    GUN_RELOAD_START = 5,
    GUN_RELOADING = 6,
    GUN_RELOAD_END = 7,
    GUN_SWITCH_VARIANT = 8,     // between variants of one model; also the end of continuous fire
    GUN_FIRING = 9,
    GUN_PUT_BACK = 10,          // a raise turned round
    GUN_THROW_WINDUP = 11,
    GUN_THROWING = 12,
    GUN_LOWER_USED_UP = 13,     // put away for good (the weapon was used up)
    GUN_LOWERING_USED_UP = 14,
    GUN_SCOPE_RAISE = 15,       // the bring-to-eye anim of a weapon that has one
    GUN_SCOPE_LOWER = 16,
};

// Player_WeaponChange's third argument: cycle the weapons, the gadgets, or the party guest's restricted cycle
// (CastleIndoors1 before SWITCH_CASTLEINDOORS1_ARMED is on)
enum WeaponChangeMode {
    WEAPONCHANGE_WEAPONS = 0,
    WEAPONCHANGE_GADGETS = 1,
    WEAPONCHANGE_UNARMED = 2,
};

void Player_InitAmmoWeapons(obj_tag *player);
bool Player_EquipWeapon(BLData *blData, short weapon, short amount);
void Player_EquipKey(BLData *blData, ushort key);
void Player_UnEquipKey(BLData *blData, ushort key);
void Player_RamLoad(BLData *blData, char boostHealth);
void Player_RamSave(BLData *blData);
void Player_SetWeaponAnim(obj_tag *player);
void Player_SetWeaponAnimObj(obj_tag *player, _VECTOR *sway);
void Player_PositionGun(obj_tag *player, BLData *blData);
void Player_ResetWalkAnim(obj_tag *player);
obj_tag *WatchOrHands_Create(obj_tag *owner);
void Player_CreateBeams(obj_tag *player, char playerNum);
void Player_WeaponRecoil(obj_tag *player);
void Player_WeaponSelect(obj_tag *player);
bool Player_WeaponUsable(BLData *blData, short weapon);
void Player_WeaponChange(obj_tag *player, char direction, char mode);
bool Player_WeaponHasAmmo(BLData *blData, short weapon);
void Player_LaserPointer(obj_tag *player);
void Player_SetWatch(obj_tag *player);
void Player_GetBestWeapon(obj_tag *player);
bool Player_IsBetterWeapon(obj_tag *player, short weapon);
bool Player_ReloadAmmoType(obj_tag *player, bool test);
void Player_HandleHasNoAmmo(obj_tag *player);
bool Player_RoundToFire(obj_tag *player, uchar needed, uchar taken);
void Player_GetAimingPoint(obj_tag *player, _VECTOR *point);
void Player_ResetWalkAnim(obj_tag *player);
void Player_Zoom(obj_tag *player);
bool Player_DroneAwake(Drone_tag *drone);
void Check_AutoAim(obj_tag *player, obj_tag *target, float *best);
void Player_AutoAim(obj_tag *player);
void Player_WeaponFiring(obj_tag *player, char fire);
void Player_MuzzleFlash(obj_tag *player, _VECTOR *target);
void Draw_MuzzleFlash(obj_tag *player);
sAnimScript_tag *Player_SetFiringAnim(BLData *blData, obj_tag *weaponObj, weapon_definition_tag *weapon);
void Player_WeaponInitBullet(obj_tag *player, weapon_definition_tag *weapon);

extern HASHCODE SatchelDigits[10], DecryptorDigits[10];

#endif // PLAYERWEAPON_H_
