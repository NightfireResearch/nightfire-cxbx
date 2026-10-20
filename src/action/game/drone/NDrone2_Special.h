#ifndef NDRONE2_SPECIAL_H
#define NDRONE2_SPECIAL_H

#include "NDrone2.h"

// The alarm states, the enemy mission, the ninja, the space drake and the astronaut (NDrone2_Special.cpp)
bool NDrone2_DSTATE_RunToAlarm(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_PressAlarm(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_DonePressAlarm(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_EnemyMission(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
void NDrone2_CreateNinjaEyes(obj_tag *gameObj);
bool NDrone2_DSTATE_NinjaStand(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_NinjaAttack(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_NinjaAttackLongRange(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_NinjaAttackMidRange(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_NinjaAttackShortRange(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_NinjaGetCloseToPlayer(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_NinjaSword(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_NinjaSomersault(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_NinjaBackflip(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_NinjaSideflip(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_NinjaSideflipLeft(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_NinjaSideflipRight(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_NinjaStandFire(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_NinjaNoRoute(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_SpaceDrake(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_AstronautLaunch(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_AstronautHit(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_AstronautDeath(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_AstronautCombat(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_AstronautCombatMove(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);

#endif // NDRONE2_SPECIAL_H
