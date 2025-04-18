#include "Camera.h"

#include "viewer.h"

// Array of 10 pointers to viewer_tag objects, located in memory at 0x001f661c
#define glb_viewer (*(viewer_tag*(*)[10])0x001f661c)

// AUTOGEN
void __cdecl Camera_CalcViewAngles(ushort playerNum,float param_2);

// AUTOGEN
void Camera_Enable(ushort cameraNum,char param_2,undefined1 param_3,_VECTOR *param_4);

// AUTOINJECT
void Camera_ScreenCoords(ushort viewerNum,float PosX,float PosY,float Width,float Height) {
  
    viewer_tag *vwr = glb_viewer[viewerNum];
  
    if (vwr == NULL) 
        return;

    vwr->width = Width;
    vwr->height = Height;
    vwr->xMax = PosX + Width;
    vwr->xMin = PosX;
    vwr->yMax = PosY + Height;
    vwr->yMin = PosY;
    Camera_CalcViewAngles(viewerNum, DEG2RAD(60.0f)); // TODO: Hardcoded FOV

}

// NOAUTOINJECT
// void Camera_Create(int idx, world_tag *param_2, char param_3, ushort posX, ushort posY, ushort width, ushort height) {
//   viewer_tag *viewer;
//   cel_tag *pcVar1;
//   world_tag *world;
//   cel_tag *iVar1;
  
//   if (glb_viewer[idx] == NULL) {
//     world = param_2;
//     viewer = build_alloc_viewer((char)idx);
//     build_link_world_to_viewer(viewer,(cel_tag **)world);
//     glb_viewer[idx] = viewer;
//   }
//   if (param_3 != NULL) {
//                     /* Linked list insert at front? */
//     pcVar1 = build_alloc_cel(*(cel_tag **)glb_viewer[idx]->world);
//     *(cel_tag **)glb_viewer[idx]->world = pcVar1;
//     iVar1 = *(cel_tag **)glb_viewer[idx]->world;
//     iVar1->bitsFromPlacementTag = iVar1->bitsFromPlacementTag | 0x10040000;
//     (iVar1->someMin).z = -1000.0;
//     (iVar1->someMin).y = -1000.0;
//     (iVar1->someMin).x = -1000.0;
//     (iVar1->someMax).z = 1000.0;
//     (iVar1->someMax).y = 1000.0;
//     (iVar1->someMax).x = 1000.0;
//   }
//   glb_viewer[idx]->world = (cel_tag *)param_2;
//   glb_viewer[idx]->field_0x29 = param_3;
//   glb_viewer[idx]->field26_0x2a = 1;
//   glb_viewer[idx]->cameraUpdateObj = NULL;
//   glb_viewer[idx]->cameraUpdateCallback = NULL;
//   Camera_Enable((ushort)idx,param_3,1,NULL);
//   Camera_ScreenCoords((ushort)idx,(float)posX,(float)posY,(float)width,(float)height);
//   return;
// }
