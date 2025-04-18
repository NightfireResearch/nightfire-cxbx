#ifndef CAMERA_H
#define CAMERA_H

#include "../actionhelpers.h"

void Camera_CalcViewAngles(ushort playerNum,float param_2);
void Camera_ScreenCoords(ushort viewerNum,float PosX,float PosY,float Width,float Height);
void Camera_Create(int idx, world_tag *param_2, char param_3, ushort posX, ushort posY, ushort width, ushort height);
void Camera_CreateCameras(void);
void Camera_UpdateAll(void);
typedef void (*cameraUpdateFunc)(obj_tag *gameObj, void *context);
void Camera_SetUpdator(ushort num, obj_tag *gameObj, cameraUpdateFunc func);

#endif // CAMERA_H