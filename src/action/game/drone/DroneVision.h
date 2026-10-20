#ifndef DRONEVISION_H_
#define DRONEVISION_H_

#include "../../actionhelpers.h"
#include "../../engine/AINetwork.h"
#include "Drone.h"

// What the drones see (DroneVision.cpp; docs/drone/architecture/README.md)

// Lines of sight
bool DroneWeap_GetWeaponInfo(Drone_tag *drone, int *bone, uint *flash, ushort *sound, _VECTOR *muzzle,
                             _VECTOR *barrel);
void DroneFunc_ClampPosToCollisionCylinder(obj_tag *obj, _VECTOR *pos);
bool DroneVision_LineOfSightToObject(CelPos_tag *from, CelPos_tag *to, obj_tag *objA, obj_tag *objB, char unused,
                                     uint mask);
bool DroneVision_CanSeeObjectFrom(Drone_tag *drone, obj_tag *obj, uint bone, _VECTOR *from, cel_tag *cel, uint mask);
bool NDrone2_CanSeeObject(Drone_tag *drone, obj_tag *obj, uint bone, uint mask);
bool DroneVision_CanSeePositionFrom(obj_tag *viewer, _VECTOR *pos, cel_tag *cel, obj_tag *ignore, _VECTOR *eye,
                                    cel_tag *eyeCel);
bool DroneVision_CanSeePosition(obj_tag *viewer, _VECTOR *pos, cel_tag *cel, obj_tag *ignore);
bool DroneVision_GunThroughWall(Drone_tag *drone);

// How well a drone sees
float DroneVision_ObjectVisibility(Drone_tag *drone, obj_tag *obj);
// The originals take the drone in ESI (and the range in EAX's case, see DroneVision.cpp): the game reaches the
// adaptors, C++ calls the cores
double _DroneVision_OpponentVisibility(Drone_tag *drone);
void DroneVision_OpponentVisibility(void);
bool _DroneVision_OpponentInView(Drone_tag *drone, float range);
void DroneVision_OpponentInView(float range);
bool DroneVision_OpponentLOS(Drone_tag *drone);
bool _DroneVision_SeekOpponentLOS(Drone_tag *drone);
void DroneVision_SeekOpponentLOS(void);
void DroneVision_LogSeenOpponent(Drone_tag *drone);
void _DroneVision_HaveOpponentSight(Drone_tag *drone);
void DroneVision_HaveOpponentSight(void);

// Drones alerting each other
Drone_tag *_DroneVision_FindDrone(Drone_tag *from, uint id);
void DroneVision_FindDrone(void);
Drone_tag *__fastcall DroneVision_DroneAfter(Drone_tag *from, uint id);
bool DroneVision_DroneCanSeeDrone(Drone_tag *drone, Drone_tag *other, char *inView);
bool _DroneVision_ConsiderAlerted(Drone_tag *drone, Drone_tag *alerter);
void DroneVision_ConsiderAlerted(void);
bool _DroneVision_DetermineAlertCheck(Drone_tag *a, Drone_tag *b);
void DroneVision_DetermineAlertCheck(void);
void DroneVision_FindAlertedDrones(void);

// Once a frame, from Drone_InitComms
void DroneVision_ProcessDroneSight(void);

extern uint Drone_View_Bones[8];

#endif // DRONEVISION_H_
