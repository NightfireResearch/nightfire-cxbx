#ifndef NDRONE2_HOSTAGE_H
#define NDRONE2_HOSTAGE_H

#include "NDrone2.h"

// Hostage killers and their hostages (NDrone2_Hostage.cpp). The pair point at each other through
// Drone_tag.hostagePartner.
void NDrone2_SetupHostageExecute(DCVars_tag *dcv);
bool NDrone2_SetupHostageKiller(DCVars_tag *dcv);
bool NDrone2_MonitorTalkToMissionObj(DCVars_tag *dcv);
bool NDrone2_TalkToMissionObj(DCVars_tag *dcv);
bool NDrone2_DSTATE_HostageKiller(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_HostageKillerAttack(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_Hostage(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_HostageDie(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_HostageSaved(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_HostageIdle(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_HostageHide(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_HostageDead(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);

#endif // NDRONE2_HOSTAGE_H
