#ifndef CAMERA_H
#define CAMERA_H

#include "../actionhelpers.h"

void Camera_CalcViewAngles(ushort playerNum,float param_2);
void Camera_ScreenCoords(ushort viewerNum,float PosX,float PosY,float Width,float Height);

#endif // CAMERA_H