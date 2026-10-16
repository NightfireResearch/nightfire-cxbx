#ifndef NDRONE2_OPPONENT_H_
#define NDRONE2_OPPONENT_H_

#include "Drone.h"

// A drone's opponent (NDrone2_Opponent.cpp): choosing it, setting it and the point aimed at on it, the targets the
// drones share (NPCGlobals.targets), and the alerts a drone broadcasts (NPCGlobals.alert).

// Drone_tag.targetSlot with no NPCGlobals.targets entry
#define NO_TARGET_SLOT 0xff

obj_tag* NDrone2_FindOpponent(Drone_tag *drone);
void NDrone2_SetOpponent(Drone_tag *drone, obj_tag *opponent);
void NDrone2_SetOpponentAimPos(Drone_tag *drone);
bool NDrone2_OpponentBonePos(obj_tag *gameObj, uint bone, _VECTOR *pos);
bool NDrone2_AnglesToObj(Drone_tag *drone, obj_tag *obj, _VECTOR *rot);

uchar DroneFunc_AllocateTargetID(Drone_tag *drone, obj_tag *obj);
void DroneFunc_TargetSeen(Drone_tag *drone, _VECTOR *pos);
void DroneFunc_UpdateTargets(void);
void DroneFunc_ClearTargets(void);

void NDrone2_SetupAlertTarget(void);
void NDrone2_DroneAlertToObject(DCVars_tag *dcv, uint msgType, obj_tag *obj, float param0, float param1,
                                float param2);
void NDrone2_DroneAlertToPosition(obj_tag *sender, uint msgType, _VECTOR *pos, cel_tag *cel, float param0,
                                  float param1, float param2);

// Drone.cpp
void Drone_BuildDynamicAwarePoints(void);
void Drone_ProcessOpponents(void);
void Drone_UpdateDummyPlayer(char active);

#endif // NDRONE2_OPPONENT_H_
