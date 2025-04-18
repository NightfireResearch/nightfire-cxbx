#include "Camera.h"

#include "../game/mp/multiplayer.h" // For MPSettings

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

// AUTOGEN
void build_link_world_to_viewer(viewer_tag *viewer, cel_tag **world);

// AUTOGEN
viewer_tag* build_alloc_viewer(unsigned char idx);

// AUTOGEN
cel_tag* build_alloc_cel(cel_tag *cel);

// AUTOINJECT
void Camera_Create(int idx, world_tag *param_2, char param_3, ushort posX, ushort posY, ushort width, ushort height) {
  viewer_tag *viewer;
  cel_tag *pcVar1;
  world_tag *world;
  cel_tag *iVar1;
  
  if (glb_viewer[idx] == NULL) {
    world = param_2;
    viewer = build_alloc_viewer(idx);
    build_link_world_to_viewer(viewer,(cel_tag **)world);
    glb_viewer[idx] = viewer;
  }
  if (param_3 != 0) {
                    /* Linked list insert at front? */
    pcVar1 = build_alloc_cel(*(cel_tag **)glb_viewer[idx]->world);
    *(cel_tag **)glb_viewer[idx]->world = pcVar1;
    iVar1 = *(cel_tag **)glb_viewer[idx]->world;
    iVar1->bitsFromPlacementTag |= 0x10040000;
    (iVar1->someMin).z = -1000.0;
    (iVar1->someMin).y = -1000.0;
    (iVar1->someMin).x = -1000.0;
    (iVar1->someMax).z = 1000.0;
    (iVar1->someMax).y = 1000.0;
    (iVar1->someMax).x = 1000.0;
  }
  glb_viewer[idx]->world = (cel_tag *)param_2;
  glb_viewer[idx]->field25_0x29 = param_3;
  glb_viewer[idx]->field26_0x2a = 1;
  glb_viewer[idx]->cameraUpdateObj = NULL;
  glb_viewer[idx]->cameraUpdateCallback = NULL;
  Camera_Enable(idx, param_3, 1, NULL);
  Camera_ScreenCoords(idx, (float)posX, (float)posY, (float)width, (float)height);
  return;
}


// AUTOGEN
world_tag* build_alloc_world(void);

#define ScreenBlankerState U32_AT(0x001dc740)
#define glb_world (*(world_tag**)0x001f6674)

// AUTOINJECT
void Camera_CreateCameras(void) {

    ScreenBlankerState = 0;

    Camera_Create(0, glb_world, 0, 0, 0, 640, 480);

    Camera_Create(5, build_alloc_world(), 1, 0, 0, 640, 480);
    Camera_Create(7, build_alloc_world(), 1, 0, 0, 640, 480);

    Camera_Create(4, glb_world, 0, 0, 0, 640, 480);

    Camera_Create(6, NULL, 0, 0, 0, 640, 480);
    Camera_Create(8, NULL, 0, 0, 0, 640, 480);
    Camera_Create(10, NULL, 0, 0, 0, 640, 480);

    // Inlined and optimised for the fact that param_2==0
    Camera_Enable(8, 0, 0, NULL);

    Camera_Create(9, glb_world, 0, 0, 0, 640, 480); // Specific to Xbox?

    int numPlayers = (MPSettings.isMultiplayer) ? MPSettings.numPlayers : 1;

    for (int i = 0; i < numPlayers; i++) {
        if(glb_viewer[i] == NULL)
            Camera_Create(i, glb_world, 0, 0, 0, 640, 480);
    }

    switch(numPlayers) {
        case 1:
            Camera_ScreenCoords(0, 0, 0, 640, 480);
            break;
        case 2:
            if(MultiplayerLayout_LeftRightOrTopBtm == 1) {
                Camera_ScreenCoords(0, 0, 0, 320, 480);
                Camera_ScreenCoords(1, 320, 0, 320, 480);
            } else {
                Camera_ScreenCoords(0, 0, 0, 640, 240);
                Camera_ScreenCoords(1, 0, 240, 640, 240);
            }
            break;
        case 3:
        case 4:
            Camera_ScreenCoords(0, 0, 0, 320, 240);
            Camera_ScreenCoords(1, 320, 0, 320, 240);
            Camera_ScreenCoords(2, 0, 240, 320, 240);
            Camera_ScreenCoords(3, 320, 240, 320, 240);
            break;
    }
}