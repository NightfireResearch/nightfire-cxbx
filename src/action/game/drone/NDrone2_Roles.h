#ifndef NDRONE2_ROLES_H
#define NDRONE2_ROLES_H

#include "NDrone2.h"

// The scripted roles' states (NDrone2_Roles.cpp): truck drivers, abseilers, running from scary objects, the elevator
// jumper, interrogators and civilian challengers, Kiko, the allies that lead or follow the player, the castle chat
// guard, ambushers and the test states
bool NDrone2_DSTATE_TruckDriverInit(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_TruckDriverInitAlert(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_TruckDriverIdle(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_TruckDriverMission(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_NearArmedBond(DCVars_tag *dcv, float radius);
bool NDrone2_DSTATE_AbseilInit(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_AbseilSlide(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_AbseilHang(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_AbseilStepOff(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_AbseilDeath(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_RunAwayFromObject(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_HideFromScaryObject(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_RecoverFromScaryObject(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_ElevatorJumper(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_HoldItRightThere(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_FailMission(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_StandBlind(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_HangUp(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_InterogateAssist(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_InterogateAssistWait(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_Interogator(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_Interogate(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_InterogateWalk(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_CivilianChallenge(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_KikoMission(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_KikoMissionRun(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_AllyLead(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_AllyLeadPlayerInWay(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_AllyLeadHide(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_AllyLeadWait(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_AllyLeadMissionWait(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_AllyLeadBondCombat(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_AllyFollowInit(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_AllyFollow(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_AllyFollowWait(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_AllyFollowDone(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_ReFindMissionPath(DCVars_tag *dcv);
bool NDrone2_DSTATE_CastleChatGuard1(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_FindAmbushCover(DCVars_tag *dcv);
bool NDrone2_DSTATE_AmbushInit(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_AmbushWait(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_Tester1(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_Tester2(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_Tester3(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_Tester4(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);

#endif // NDRONE2_ROLES_H
