#ifndef PLAYERWEAPON_H_
#define PLAYERWEAPON_H_

#include "../../actionhelpers.h"

// The player's weapons (PlayerWeapon.cpp): what he carries, choosing and changing weapons, the gun in the view and its
// anims, firing, the zoom, the auto-aim, the laser sight, the muzzle flash and the watch.

// BLData::idleState: the gun's idle anims (Player_SetWeaponAnimObj)
#define IDLE_NONE    0  // idle, counting towards a fidget or lowering the gun
#define IDLE_PLAYING 1  // a fidget, or the gun coming back up, is playing
#define IDLE_LOWERED 3  // lowered (weapon_definition_tag::animRelax) until a drone is alert or the player aims

// weapon_definition_tag::weaponFlags bits read here (docs/weapons.md)
#define WPNFLAG_UNDERWATER      0x00000002  // fires under water
#define WPNFLAG_LASER_SIGHT     0x00000008
#define WPNFLAG_SWITCH_TO_ALT   0x00000010  // out of ammo, the alt-fire variant is tried first
#define WPNFLAG_REAL_SCOPE      0x00000040
#define WPNFLAG_SHELL_RELOAD    0x00000100  // reloads a shell at a time
#define WPNFLAG_ALT_RELOADS     0x00000200  // the reload anims take turns
#define WPNFLAG_CONTINUOUS      0x00000400  // fires while the button is held, then plays animFireAlt
#define WPNFLAG_SILENCER        0x00000800
#define WPNFLAG_SLIDE_LOCK      0x00001000
#define WPNFLAG_NO_SMOKE        0x00002000
#define WPNFLAG_SINGLE_ACTION   0x00008000
#define WPNFLAG_FROM_MUZZLE     0x00010000  // the shot starts half way between the eye and the muzzle
#define WPNFLAG_DETONATOR       0x00020000
#define WPNFLAG_TWIN_GUNS       0x00040000
#define WPNFLAG_RANDOM_FIRE     0x00080000
#define WPNFLAG_REFIRE_HELD     0x00100000  // the fire anim starts again while the button is held

// weapon_definition_tag::projectileFlags bits read here
#define PROJFLAG_CASING         0x00000040
#define PROJFLAG_TASER          0x00000080
#define PROJFLAG_LASER          0x00000100
#define PROJFLAG_THROWN         0x00000400
#define PROJFLAG_MATERIAL_MASK  0x00000800

// The weapon object's states (BLData::weaponObject->curState)
#define GUN_IDLE            0
#define GUN_LOWER           1   // put away, to change weapons
#define GUN_LOWERING        2
#define GUN_RAISE           3   // the new weapon's model, brought up
#define GUN_RAISING         4
#define GUN_RELOAD_START    5
#define GUN_RELOADING       6
#define GUN_RELOAD_END      7
#define GUN_SWITCH_VARIANT  8   // between variants of one model; also the end of continuous fire
#define GUN_FIRING          9
#define GUN_PUT_BACK        10  // a raise turned round
#define GUN_THROW_WINDUP    11
#define GUN_THROWING        12
#define GUN_LOWER_USED_UP   13  // put away for good (the weapon was used up)
#define GUN_LOWERING_USED_UP 14
#define GUN_SCOPE_RAISE     15
#define GUN_SCOPE_LOWER     16

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
