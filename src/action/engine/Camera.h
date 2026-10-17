#ifndef CAMERA_H
#define CAMERA_H

#include "../actionhelpers.h"

void Camera_CalcViewAngles(ushort playerNum,float param_2);
void Camera_ScreenCoords(ushort viewerNum,float PosX,float PosY,float Width,float Height);
void Camera_Create(int idx, world_tag *param_2, char param_3, ushort posX, ushort posY, ushort width, ushort height);
void Camera_CreateCameras(void);
void Camera_Update(uint idx);
void Camera_UpdateAll(void);
typedef void (*cameraUpdateFunc)(obj_tag *gameObj, void *context);
void Camera_SetUpdator(ushort num, obj_tag *gameObj, cameraUpdateFunc func);
void __stdcall Camera_PushStates(void);
void __stdcall Camera_PopStates(void);
bool Camera_SetFade(ushort viewerNum, float durationFrames, uint colourAndAlpha, undefined1 fadeMode);
void Camera_SetToPlayer(obj_tag *player, viewer_tag *viewer);
void Camera_Set(viewer_tag *viewer, _VECTOR *transform, _VECTOR *rotation);
void camera_tracking(obj_tag *obj, viewer_tag *viewer);
void Camera_Shear(viewer_tag *vwr);
void Viewer_Init(viewer_tag *viewer);
void Camera_CheckLocation(ushort idx);
void Camera_UpdateGlbVars(ushort idx);
void Camera_ApplySwing(viewer_tag *viewer, float sway);
void Camera_Shake(_VECTOR *pos, float amount);
void Camera_Enable(ushort cameraNum, char enable, undefined1 sprites, _VECTOR *position);

#endif // CAMERA_H