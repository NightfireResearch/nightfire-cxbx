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
float _moveTowardsValue(float value, float target, float rate);
float moveTowardsValue(float value, float target, float rate);   // the original's entry (AUTOLTCG)
float normaliseToPlusMinusPi(float angle);
void Camera_PlaceAround(_VECTOR *target, viewer_tag *viewer, float rate);
void Camera_PlaceAbove(_VECTOR *target, viewer_tag *viewer, float rate);
void Camera_PlaceOver(obj_tag *obj, viewer_tag *viewer, float rate, float height);
double Camera_FollowRoom(obj_tag *obj, viewer_tag *viewer, float yaw, float pitch);
void Camera_Follow(obj_tag *obj, viewer_tag *viewer);
void Camera_Orbit(obj_tag *obj, viewer_tag *viewer);
void Camera_Trail(obj_tag *obj, viewer_tag *viewer);
typedef struct EndCamera EndCamera;
void Camera_LookAtFrom(viewer_tag *viewer, EndCamera *cam);
void Camera_LookAtObject(obj_tag *obj, viewer_tag *viewer);
void Cameras_PostLoad(void);
void Camera_PlaceBehind(_VECTOR *position, _VECTOR *rotation, viewer_tag *viewer, float rate, float distance);
void Camera_Enable(ushort cameraNum, char enable, undefined1 sprites, _VECTOR *position);

#endif // CAMERA_H