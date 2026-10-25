#ifndef DRONETUNING_H_
#define DRONETUNING_H_

#include "../../actionhelpers.h"

// Drones: damage they take and their armour (NDrone2_HitDamage, NDrone2_ExplosiveImpact), their firing (DroneWeap_*,
// Drone_ModBulletDamage) and their captains (NDrone2_DefaultInit). P_TWEAKS/P_TWEAKS2 edit most of them.
extern float DroneDamage_Easy;
extern float DroneDamage_Normal;
extern float DroneDamage_Hard;
extern float DroneDamage_Head;
extern float DroneDamage_Legs;
extern float DroneDamage_Arms;
extern float DroneDamage_Torso;
extern float DroneArmour_Helmet;
extern float DroneArmour_Combat;
extern float DroneArmour_Jacket;
extern float DroneArmour_Vest;
extern float DroneFiring_BurstDelay_Min;
extern float DroneFiring_BurstDelay_Normal;
extern float DroneFiring_BurstDelay_Max;
extern float DroneFiring_BurstDelay_MinDist;
extern float DroneFiring_BurstDelay_MaxDist;
extern float DroneFiring_Accuracy_Easy;
extern float DroneFiring_Accuracy_Normal;
extern float DroneFiring_Accuracy_Hard;
extern float DroneFiring_NewSighting_TimeToHit;
extern float DroneFiring_TargetFirstMoved_TimeToHit;
extern float DroneFiring_TargetFirstMoved_Accuracy;
extern float DroneFiring_TargetMoving_Accuracy;
extern float DroneFiring_TargetFirstStopped_TimeToHit;
extern float DroneFiring_TargetFirstStopped_Accuracy;
extern float DroneFiring_TooClose_Distance;
extern float DroneFiring_TooClose_Accuracy;
extern float DroneFiring_TooClose_Damage;
extern float DroneFiring_PlayerBackShot_Damage;
extern float DroneCaptain_Mod_BulletDamage;
extern float DroneCaptain_Mod_BulletAccuracy;
extern float DroneCaptain_Mod_Health;

#endif // DRONETUNING_H_
