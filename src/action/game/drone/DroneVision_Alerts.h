#ifndef DRONEVISION_ALERTS_H_
#define DRONEVISION_ALERTS_H_

#include "../../actionhelpers.h"
#include "Drone.h"

// What a drone does about alerts (DroneVision_Alerts.cpp): the messages its states pass on, what it hears, whether
// it sees its opponent and should attack, the armed enemies near it, and the interrogation flags.

// Drone_tag.flags
#define DRONE_FLAG_ATTACK_MODE 0x4000000        // attacking (DroneFunc_CalmOnImpact)
// Set with DRONE_FLAG_ATTACK_MODE by the function that makes it attack (our names)
#define DRONE_FLAG_ATTACK_SIGHT 0x8000000       // DroneVision_EnemyLookForOpponent
#define DRONE_FLAG_ATTACK_SOUND 0x10000000      // DroneVision_AlertSound
#define DRONE_FLAG_ATTACK_ALARM 0x40000000      // DroneVision_GlobalAlarm
#define DRONE_FLAG_DEAF 0x400000                // hears nothing (our name)
#define DRONE_FLAG_NO_ALERTS 0x800000           // DroneVision_EnemyAlerts ignores every alert (our name)

// Drone_tag.flags2
#define DRONE_FLAG2_INTEROGATING 0x20           // DroneVision_BeginInterogate, _SetInterogateAssist
#define DRONE_FLAG2_INTEROGATED 0x10            // DroneVision_EndInterogate, _EndInterogateAssist

void DroneVision_BeginInterogate(Drone_tag *drone);
void DroneVision_EndInterogate(Drone_tag *drone);
void DroneVision_SetInterogateAssist(Drone_tag *drone);
void DroneVision_EndInterogateAssist(Drone_tag *drone);
bool DroneVision_TestEndInterogationAssist(Drone_tag *drone);

bool NDrone2_NearArmedDrone(DCVars_tag *dcv, float radius, char check);
bool NDrone2_NotNearArmedDrone(DCVars_tag *dcv, float radius);

ushort DroneVision_EnemyAlerts(DCVars_tag *dcv, MsgObject *msg);
ushort DroneVision_EnemyLookForOpponent(DCVars_tag *dcv);
void DroneFunc_HandleSoundAlerts(DCVars_tag *dcv);

// The bots' alerts and reactions (FUN_00038ff0 is NDrone2_HandleDroneAlert, our name)
bool NDrone2_InShoutingRange(DCVars_tag *dcv, obj_tag *obj);
bool NDrone2_HandleDroneAlert(DCVars_tag *dcv, MsgObject *msg, short state);
short NDrone2_ReactToOpponentSighted(DCVars_tag *dcv, short state);

// The originals take the DCVars in a register: the game reaches the adaptors, C++ calls the cores
ushort _DroneVision_AlertSound(DCVars_tag *dcv);
void DroneVision_AlertSound(void);
ushort _DroneVision_GlobalAlarm(DCVars_tag *dcv);
void DroneVision_GlobalAlarm(void);
ushort _DroneVision_ShoutMsg11(DCVars_tag *dcv);
void DroneVision_ShoutMsg11(void);
ushort _DroneVision_ShoutMsg12(DCVars_tag *dcv);
void DroneVision_ShoutMsg12(void);
ushort _DroneVision_ShoutMsg13(DCVars_tag *dcv);
void DroneVision_ShoutMsg13(void);
ushort _DroneVision_ShoutDroneAlert(DCVars_tag *dcv);
void DroneVision_ShoutDroneAlert(void);

#endif // DRONEVISION_ALERTS_H_
