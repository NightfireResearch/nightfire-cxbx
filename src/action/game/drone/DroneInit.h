#ifndef DRONEINIT_H_
#define DRONEINIT_H_

#include "Drone.h"

// A drone's creation and initialisation (DroneInit.cpp)
obj_tag* NDrone2_CreateObj(DIVars_tag *diVars);
void NDrone2_PostLoad_Init(obj_tag *gameObj);
obj_tag* NDrone2_DefaultInit(DIVars_tag *diVars);
void DroneInit_Collision(Drone_tag *drone);
void DroneFunc_SetAsAttacking(Drone_tag *drone);
bool NDrone2_SetupGroups(void);
void NDrone2_Enable(char enable, Drone_tag *drone);

#endif // DRONEINIT_H_
