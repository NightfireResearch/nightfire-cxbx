#ifndef DRONEVISION_ALERTS_H_
#define DRONEVISION_ALERTS_H_

#include "../../actionhelpers.h"
#include "Drone.h"

// What a drone does about alerts (DroneVision_Alerts.cpp): the messages its states pass on, what it hears, whether
// it sees its opponent and should attack, the armed enemies near it, and the interrogation flags.

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

// The alert states' sounds
ushort DroneVision_AlertSound(DCVars_tag *dcv);
ushort DroneVision_GlobalAlarm(DCVars_tag *dcv);
ushort DroneVision_ShoutMsg11(DCVars_tag *dcv);
ushort DroneVision_ShoutMsg12(DCVars_tag *dcv);
ushort DroneVision_ShoutMsg13(DCVars_tag *dcv);
ushort DroneVision_ShoutDroneAlert(DCVars_tag *dcv);

#endif // DRONEVISION_ALERTS_H_
