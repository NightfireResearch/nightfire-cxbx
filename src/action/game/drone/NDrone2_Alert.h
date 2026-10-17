#ifndef NDRONE2_ALERT_H
#define NDRONE2_ALERT_H

#include "NDrone2.h"

// Going to a goal position, alert routes and door guard partners, searching, investigating, and the seen and heard
// states (NDrone2_Alert.cpp)

double NDrone2_DistanceToAIPoint(DCVars_tag *dcv, AIPoint_tag *point);
uchar DroneFunc_Using2ndBehaviour(Drone_tag *drone);
bool DroneFunc_Set1stBehaviour(Drone_tag *drone);
bool DroneFunc_Set2ndBehaviour(Drone_tag *drone);
bool NDrone2_IsCivilian(DCVars_tag *dcv);
void NDrone2_CivilianScaredTalk(DCVars_tag *dcv);
uint NDrone2_MoveToAlertPosition(DCVars_tag *dcv);
bool NDrone2_SetDroneAlertRoute(DCVars_tag *dcv);
bool NDrone2_SetSoundAlertRoute(DCVars_tag *dcv);
void NDrone2_SetAngleToAlert(DCVars_tag *dcv, float offset);
float NDrone2_GetAngleDifferenceToAlert(DCVars_tag *dcv);
void DroneFunc_HeardNoiseTalk(DCVars_tag *dcv);
void plane_from_two_vectors_and_point(plane_equ_tag *plane, _VECTOR *a, _VECTOR *b, _VECTOR *point);
void NDrone2_SetDoorGuardPlanes(_VECTOR *partnerPos, _VECTOR *pos, _VECTOR *dir, Drone_tag *drone);
bool NDrone2_SetDronePartner(uchar dtype, DCVars_tag *dcv);
uint NDrone2_DoorGuardHandler(DCVars_tag *dcv);

bool NDrone2_DSTATE_GoToGoalPosition(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_HostageGoToGoalPosition(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_AllyGoToGoalPosition(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_AlertToPosition(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_SearchArea(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_Investigate(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_SeenDeadBody(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_SeenSurrenderedDrone(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_SeenDroneShot(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_SeenExplosive(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_HeardNoise(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_HeardNoiseAware(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_HeardNoiseSuspect(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_HeardNoiseAlert(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);

#endif // NDRONE2_ALERT_H
