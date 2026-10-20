#ifndef BOTWEAP_H
#define BOTWEAP_H

// A multiplayer bot's weapons: what it holds, the rounds in each gun and its reserve of each ammo type
// (BOT_vars_t.weapons, reserveAmmo), and its choice among them.

#include "../../actionhelpers.h"
#include "BOT.h"

// The weapon every bot starts with: the base of the first weapon in the match's weapon set (BOTWEAP_InitWeapon). It
// never runs out of ammo.
extern short BOT_startWeapon;

void BOTWEAP_resetWeapons(obj_tag *obj);
bool BOTWEAP_reload(obj_tag *obj, char checkOnly);
short BOTWEAP_AmmoInGun(Drone_tag *drone, short weapon);
bool BOTWEAP_EquipWeapon(Drone_tag *drone, short weapon, short count);
bool BOTWEAP_EquipAmmo(Drone_tag *drone, short weapon, short count);
void BOTWEAP_CheckWeaponsLoaded(Drone_tag *drone);
bool BOTWEAP_hasWeapon(Drone_tag *drone, short weapon);
bool BOTWEAP_decrRounds(Drone_tag *drone, short rounds);
int BOTWEAP_getWeaponAmmoAmount(Drone_tag *drone, short weapon);
int BOTWEAP_getWeaponClipSize(Drone_tag *drone, short weapon);
bool BOTWEAP_isValidWeapon(short weapon);
int BOTWEAP_getWeaponPriority(short weapon);
short BOTWEAP_getAnimSet(short weapon);
bool BOTWEAP_shouldChangeWeapon(short weapon);
bool BOTWEAP_hasReloadAnim(short weapon);
bool BOTWEAP_hasAmmo(Drone_tag *drone, short weapon);
void BOTWEAP_InitWeapon(Drone_tag *drone);
bool BOTWEAP_changeWeapon(Drone_tag *drone, short weapon);
bool BOTWEAP_hasLoadedWeapon(Drone_tag *drone, short weapon);
short BOTWEAP_getExplosiveWeapon(Drone_tag *drone, float distance);
// Fills list[weapon] for weapons 0-82 (see BOTWEAP.cpp) and sets *count to 83; returns how many are usable
int BOTWEAP_listHeldLoadedWeapons(Drone_tag *drone, ushort *list, int *count, char preferredOnly);
bool BOTWEAP_tooCloseForWeapon(Drone_tag *drone, short weapon, float distance);

#endif // BOTWEAP_H
