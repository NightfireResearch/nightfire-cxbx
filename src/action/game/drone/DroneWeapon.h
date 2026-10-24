#ifndef DRONEWEAPON_H_
#define DRONEWEAPON_H_

#include "Drone.h"

// The drones' weapons (DroneWeapon.cpp): bursts of fire at the opponent, grenades, close combat, changing and dropping
// the weapon in hand, and the damage their bullets do to the player.

// Firing, once a frame (NDrone2_ControlSTANDARD)
void DroneWeap_HandleFiring(DCVars_tag *dcv);
void DroneWeap_Fire(char restart, DCVars_tag *dcv);
void DroneWeap_FireWeapon(DCVars_tag *dcv);
bool DroneWeap_Ready2Fire(DCVars_tag *dcv);
void DroneWeap_DoBulletAccuracy(DCVars_tag *dcv);
void DroneWeap_DoOpponentTargetting(DCVars_tag *dcv);
void DroneWeap_AimAtOpponent(DCVars_tag *dcv);
short DroneWeap_FireDelay(DCVars_tag *dcv);
short DroneWeap_BurstLength(DCVars_tag *dcv);
void DroneWeap_NextBulletTimeNewBurst(DCVars_tag *dcv);

// Firing
bool DroneWeap_DoFiring(DCVars_tag *dcv);
void DroneWeap_NextBulletTime(DCVars_tag *dcv, char newBurst, char start);
bool DroneWeap_ManageBurstCount(DCVars_tag *dcv);
uint DroneWeap_BurstDelay(Drone_tag *drone);

// The opponent
bool DroneWeap_AimTarget_IsOpponent(char checkAngle, char keepTurning, DCVars_tag *dcv);
bool DroneWeap_OpponentIsAimingAtMe(DCVars_tag *dcv, float angle);
bool DroneWeap_OpponentHasWeapon(DCVars_tag *dcv);

// Grenades, close combat, the weapon in hand
bool DroneWeap_CanThrowGrenade(DCVars_tag *dcv);
void DroneWeap_ThrowGrenade(DCVars_tag *dcv);
void DroneWeap_PlaceTertiary(DCVars_tag *dcv);
void DroneWeap_CloseCombatImpact(DCVars_tag *dcv, int kind);
void DroneWeap_ChangeWeapon(DCVars_tag *dcv, int weapon);
void DroneWeap_DropWeapon(DCVars_tag *dcv);
bool DroneWeap_DropPathClear(Drone_tag *drone, _VECTOR *to);

// The damage drones' bullets do to the player (Player_DealWithObjHit)
bool Drone_ModPlayerHitDamage(obj_tag *player, HITDATA_tag *hit);
// Returns in ST0, unrounded: the caller rounds it when it stores it
double Drone_ModBulletDamage(Drone_tag *drone, obj_tag *player, HITDATA_tag *hit);
// The original takes the drone in ECX
bool __fastcall Drone_MayDealBackshotDamage(Drone_tag *drone);

#endif // DRONEWEAPON_H_
