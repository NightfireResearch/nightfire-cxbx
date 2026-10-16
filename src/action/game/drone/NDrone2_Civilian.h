#ifndef NDRONE2_CIVILIAN_H
#define NDRONE2_CIVILIAN_H

#include "NDrone2.h"

// The civilian states, the party girls and the start of an ally that leads (NDrone2_Civilian.cpp)
bool NDrone2_DSTATE_CivilianInit(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_Civilian(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_CivilianPatrol(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_ReturnToPatrolPath(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_CivilianMission(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_CivilianMissionWait(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_AllyLeadInit(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_PartyGirlInit(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_PartyGirl(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);

#endif // NDRONE2_CIVILIAN_H
