#ifndef DRONETUNING_H_
#define DRONETUNING_H_

#include "../../actionhelpers.h"

// Drones: damage they take and their armour (NDrone2_HitDamage, NDrone2_ExplosiveImpact), their firing (DroneWeap_*,
// Drone_ModBulletDamage) and their captains (NDrone2_DefaultInit). P_TWEAKS/P_TWEAKS2 edit most of them.
#define DroneDamage_Easy                         FLOAT_AT(0x00164068)
#define DroneDamage_Normal                       FLOAT_AT(0x0016406c)
#define DroneDamage_Hard                         FLOAT_AT(0x00164070)
#define DroneDamage_Head                         FLOAT_AT(0x00164074)
#define DroneDamage_Legs                         FLOAT_AT(0x00164078)
#define DroneDamage_Arms                         FLOAT_AT(0x0016407c)
#define DroneDamage_Torso                        FLOAT_AT(0x00164080)
#define DroneArmour_Helmet                       FLOAT_AT(0x00164084)
#define DroneArmour_Combat                       FLOAT_AT(0x00164088)
#define DroneArmour_Jacket                       FLOAT_AT(0x0016408c)
#define DroneArmour_Vest                         FLOAT_AT(0x00164090)
#define DroneFiring_BurstDelay_Min               FLOAT_AT(0x00164094)
#define DroneFiring_BurstDelay_Normal            FLOAT_AT(0x00164098)
#define DroneFiring_BurstDelay_Max               FLOAT_AT(0x0016409c)
#define DroneFiring_BurstDelay_MinDist           FLOAT_AT(0x001640a0)
#define DroneFiring_BurstDelay_MaxDist           FLOAT_AT(0x001640a4)
#define DroneFiring_Accuracy_Easy                FLOAT_AT(0x001640a8)
#define DroneFiring_Accuracy_Normal              FLOAT_AT(0x001640ac)
#define DroneFiring_Accuracy_Hard                FLOAT_AT(0x001640b0)
#define DroneFiring_NewSighting_TimeToHit        FLOAT_AT(0x001640b4)
#define DroneFiring_TargetFirstMoved_TimeToHit   FLOAT_AT(0x001640b8)
#define DroneFiring_TargetFirstMoved_Accuracy    FLOAT_AT(0x001640bc)
#define DroneFiring_TargetMoving_Accuracy        FLOAT_AT(0x001640c0)
#define DroneFiring_TargetFirstStopped_TimeToHit FLOAT_AT(0x001640c4)
#define DroneFiring_TargetFirstStopped_Accuracy  FLOAT_AT(0x001640c8)
#define DroneFiring_TooClose_Distance            FLOAT_AT(0x001640cc)
#define DroneFiring_TooClose_Accuracy            FLOAT_AT(0x001640d0)
#define DroneFiring_TooClose_Damage              FLOAT_AT(0x001640d4)
#define DroneFiring_PlayerBackShot_Damage        FLOAT_AT(0x001640d8)
#define DroneCaptain_Mod_BulletDamage            FLOAT_AT(0x001640dc)
#define DroneCaptain_Mod_BulletAccuracy          FLOAT_AT(0x001640e0)
#define DroneCaptain_Mod_Health                  FLOAT_AT(0x001640e4)

#endif // DRONETUNING_H_
