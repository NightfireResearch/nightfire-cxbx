#ifndef NDRONE2_STATES_H_
#define NDRONE2_STATES_H_

#include "NDrone2.h"

// The starting, idle and patrol states, and a few small ones (NDrone2_States.cpp). Each returns whether it handled
// the message; the dispatcher (Drone_SM_RouteMsgDCV) reads only AL.
bool NDrone2_DSTATE_Global(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_WaitSwitch(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_Disabled(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_Idle(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_Alert(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_InitPatrol(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_Patrol(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_EnemyRunToPoint(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_DeleteMe(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_JustStand(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_WaitForever(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);

#endif // NDRONE2_STATES_H_
