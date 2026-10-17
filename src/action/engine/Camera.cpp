#include "Camera.h"

#include "../game/mp/multiplayer.h" // For MPSettings
#include "../game/obj/build.h"
#include "viewer.h"
#include "viewFov.h"
#include "../util/Stack.h"
#include "../game/obj/Player.h"
#include "../game/obj/ThirdCam.h"
#include "../game/obj/Wire.h"
#include "../game/obj/CreepWall.h"
#include "../math/X87Ops.h"

#include <math.h>

// XBE_GLOBAL(0x001dc740, 0x4)
static uint32_t ScreenBlankerState;
#define IsWidescreen U32_AT(0x001f6610)

// This doesn't seem to be written to anywhere - a constant that was incorrectly not marked as such?
#define MAX_VIEW_CONE (2000.0f)
// #define MAX_VIEW_CONE FLOAT_AT(0x0017c110)

// AUTOINJECT
void Camera_CalcViewAngles(ushort playerNum,float param_2) {

    viewer_tag *vwr = glb_viewer[playerNum];

    if (vwr == NULL) 
        return;

    if(vwr->width == 0.0f || vwr->height == 0.0f)
        return;

    vwr->fovRadians = param_2;

    int initialisedCameras = 0;
    for(int i = 0; i < 4; i++) {
        if(glb_viewer[i] != NULL) {
            initialisedCameras++;
        }
    }

    vwr->AspectRatio = (IsWidescreen ? 1.7777778f : 1.3333334f);

    // Special case: When 2 players, we need to modify the aspect ratio according to whether the layout is left/right or top/bottom
    if(initialisedCameras == 2) {
        if(MultiplayerLayout_LeftRightOrTopBtm == 1) {
            vwr->AspectRatio *= 0.5f;
        } else {
            vwr->AspectRatio *= 2.0f;
        }
    }

    // Calculate the projection scale factors
    float tanHalfFov = tanf(vwr->fovRadians * 0.5f);
    float scale = -(MAX_VIEW_CONE + 0.2f) / (0.2f - MAX_VIEW_CONE);
    vwr->zoom = 1.0f;
    vwr->projectionScaleY = scale * vwr->height * 0.5f * (1.0f / tanHalfFov);
    vwr->projectionScaleX = (scale * vwr->width * 0.5f * (1.0f / tanHalfFov)) / vwr->AspectRatio;



}

// AUTOGEN
void Camera_Enable(ushort cameraNum,char param_2,undefined1 param_3,_VECTOR *param_4);

// Each camera keeps a stack of its states (bit 0 enabled, bit 8 sprites shown) so that a scripted
// camera can take over and give them back.
#define CAMERA_STATE_SPRITES_SHIFT 8

// Saves every camera's enabled and sprites-shown flags on its own stack.
// AUTOINJECT
void __stdcall Camera_PushStates(void) {
    for (int i = 0; i < 11; i++) {
        viewer_tag *viewer = glb_viewer[i];
        if (viewer != NULL) {
            uintptr_t state = viewer->field26_0x2a << CAMERA_STATE_SPRITES_SHIFT | viewer->field25_0x29;
            Stack_Push(&viewer->stateStack, (void *)state);
        }
    }
}

// Gives every camera back the state Camera_PushStates saved last, where its stack has one.
// AUTOINJECT
void __stdcall Camera_PopStates(void) {
    for (ushort i = 0; i < 11; i++) {
        viewer_tag *viewer = glb_viewer[i];
        if (viewer == NULL || Stack_IsEmpty(&viewer->stateStack))
            continue;

        uintptr_t state = (uintptr_t)Stack_Pop(&viewer->stateStack);
        Camera_Enable(i, state & 1, state >> CAMERA_STATE_SPRITES_SHIFT & 1, NULL);
    }
}

// AUTOGEN
bool Camera_SetFade(ushort viewerNum, float durationFrames, uint colourAndAlpha, undefined1 fadeMode);

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
    Camera_CalcViewAngles(viewerNum, ViewFov_ForViewer(viewerNum));

}

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
    build_link_world_to_viewer(viewer, world);
    glb_viewer[idx] = viewer;
  }
  if (param_3 != 0) {
                    /* Linked list insert at front? */
    pcVar1 = build_alloc_cel(glb_viewer[idx]->world->firstCel);
    glb_viewer[idx]->world->firstCel = pcVar1;
    iVar1 = glb_viewer[idx]->world->firstCel;
    iVar1->bitsFromPlacementTag |= 0x10040000;
    (iVar1->someMin).z = -1000.0;
    (iVar1->someMin).y = -1000.0;
    (iVar1->someMin).x = -1000.0;
    (iVar1->someMax).z = 1000.0;
    (iVar1->someMax).y = 1000.0;
    (iVar1->someMax).x = 1000.0;
  }
  glb_viewer[idx]->world = param_2;
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


// AUTOINJECT
void Camera_CreateCameras(void) {

    ScreenBlankerState = 0;

    Camera_Create(0, glb_world, 0, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);

    Camera_Create(5, build_alloc_world(), 1, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    Camera_Create(7, build_alloc_world(), 1, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);

    Camera_Create(4, glb_world, 0, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);

    Camera_Create(6, NULL, 0, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    Camera_Create(8, NULL, 0, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    Camera_Create(10, NULL, 0, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);

    // Inlined and optimised for the fact that param_2==0
    Camera_Enable(8, 0, 0, NULL);

    Camera_Create(9, glb_world, 0, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT); // Specific to Xbox?

    int numPlayers = (MPSettings.isMultiplayer) ? MPSettings.numPlayers : 1;

    for (int i = 0; i < numPlayers; i++) {
        if(glb_viewer[i] == NULL)
            Camera_Create(i, glb_world, 0, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    }

    switch(numPlayers) {
        case 1:
            Camera_ScreenCoords(0, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
            break;
        case 2:
            if(MultiplayerLayout_LeftRightOrTopBtm == 1) {
                Camera_ScreenCoords(0, 0, 0, SCREEN_WIDTH/2, SCREEN_HEIGHT);
                Camera_ScreenCoords(1, SCREEN_WIDTH/2, 0, SCREEN_WIDTH/2, SCREEN_HEIGHT);
            } else {
                Camera_ScreenCoords(0, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT/2);
                Camera_ScreenCoords(1, 0, SCREEN_HEIGHT/2, SCREEN_WIDTH, SCREEN_HEIGHT/2);
            }
            break;
        case 3:
        case 4:
            Camera_ScreenCoords(0, 0, 0, SCREEN_WIDTH/2, SCREEN_HEIGHT/2);
            Camera_ScreenCoords(1, SCREEN_WIDTH/2, 0, SCREEN_WIDTH/2, SCREEN_HEIGHT/2);
            Camera_ScreenCoords(2, 0, SCREEN_HEIGHT/2, SCREEN_WIDTH/2, SCREEN_HEIGHT/2);
            Camera_ScreenCoords(3, SCREEN_WIDTH/2, SCREEN_HEIGHT/2, SCREEN_WIDTH/2, SCREEN_HEIGHT/2);
            break;
    }
}


// AUTOGEN
void Camera_CheckLocation(ushort idx);
// AUTOGEN
void Camera_UpdateGlbVars(ushort idx);
// AUTOGEN
void Camera_SetToPlayer(obj_tag *player, viewer_tag *viewer);
// AUTOGEN
void Camera_Set(viewer_tag *viewer, _VECTOR *transform, _VECTOR *rotation);

void Concat(float *param_1, float *param_2);

#define normaliseToPlusMinusPi ((float (__cdecl *)(float angle))0x00024050)
#define GlobalBlur U8_AT(0x001f667c)

// Camera_Shear's working: the view's position kept while it is changed, the wobble's two scales, and the matrix
// XBE_GLOBAL(0x001dc744, 0x3c)
static _MATRIX ShearMatrix;
// XBE_GLOBAL(0x001dc780, 0x4)
static float ShearY;
// XBE_GLOBAL(0x001dc784, 0x4)
static float ShearX;
// XBE_GLOBAL(0x001dc788, 0xc)
static _VECTOR ShearPosition;

// The view under water: its axes stretched and skewed by up to a fortieth, cycling once every 2 pi seconds, and blurred.
// AUTOINJECT
void Camera_Shear(viewer_tag *vwr) {
    if (vwr == NULL)
        return;

    Vec_Copy(Mat_Position(vwr->viewMatrix), &ShearPosition);
    ShearX = (float)(FCos(vwr->shearAngle) * Extended(0.025f));
    ShearY = (float)(FSin(vwr->shearAngle) * Extended(0.025f));
    vwr->shearAngle = normaliseToPlusMinusPi(REC_FRAME_RATE + vwr->shearAngle);
    Mat_IdentityT(&ShearMatrix);
    ShearMatrix.m[4] = ShearX;
    ShearMatrix.m[1] = ShearY;
    Mat_Scale3f(&ShearMatrix, &ShearMatrix, ShearX + 1.0f, ShearY + 1.0f, ShearX + 1.0f);
    Concat(vwr->viewMatrix.m, ShearMatrix.m);
    Mat_CopyRot(&ShearMatrix, &vwr->viewMatrix);
    Vec_Copy(&ShearPosition, Mat_Position(vwr->viewMatrix));
    GlobalBlur = 0x91;
}

// The originals, called by address: unnamed placements of the view (also Player_PositionCamera's camera mode 2)
#define Camera_PlaceAround ((void (__cdecl *)(_VECTOR *position, viewer_tag *viewer, float scale))0x000245a0)
#define Camera_PlaceAbove ((void (__cdecl *)(_VECTOR *position, viewer_tag *viewer, float scale))0x00024610)
#define Vec_Dist2D ((double (__cdecl *)(_VECTOR *a, _VECTOR *b))0x000d4e20)

#define HALF_PI 1.5707964f
#define PI 3.1415927f
#define QUARTER_PI 0.7853982f

// The view behind a player on a wire or creep wall (camera mode 0xb): at its distance off the nearest point of the
// line, its pitch by how far along it that point is (fixed for a wall), turned by an eighth either way while he moves
// along it; or on the line's third-person camera's path, at the same fraction along, when the line has one. A camera
// whose show flag is clear leaves the view where it was.
// AUTOINJECT
void camera_tracking(obj_tag *obj, viewer_tag *viewer) {
    BLData *blData = glb_blokes[(uchar)viewer->idx];
    obj_tag *line = blData->attachedToSpecialMovementItem;
    if (line == NULL) {
        viewer->camMode = 0;
        return;
    }

    _VECTOR endA = line->position;
    _VECTOR endB = blData->attachedToSpecialMovementItem->position;
    line = blData->attachedToSpecialMovementItem;
    _VECTOR at;
    Vec_Spherical_2_Cartesian(&at.x, line->radius, line->rotation.y + HALF_PI, line->rotation.z);
    endA.x = endA.x + at.x;
    endA.y = endA.y + at.y;
    endA.z = endA.z + at.z;
    endB.x = endB.x - at.x;
    endB.y = endB.y - at.y;
    endB.z = endB.z - at.z;
    line = blData->attachedToSpecialMovementItem;
    bool wall = line->objectType == OBJECTTYPE_CREEPWALL;
    if (wall || ((WIRE *)line->extraObjectData)->type == WIRE_1) {
        endA.y = endA.y + 1.0f;
        endB.y = endB.y + 1.0f;
    }
    else {
        endA.y = endA.y - 1.0f;
        endB.y = endB.y - 1.0f;
    }

    vecutil_calculate_closest_point_on_line(&at, &endA, &endB, &obj->position);
    float length = (float)Vec_Dist2D(&endA, &endB);
    float along = (float)(Vec_Dist2D(&endA, &at) / length);
    line = blData->attachedToSpecialMovementItem;
    if (line->objectType == OBJECTTYPE_CREEPWALL)
        viewer->trackPitch = 2.7488937f;
    else
        viewer->trackPitch = (float)(PI - (double)along * 0.5235988f);

    line = blData->attachedToSpecialMovementItem;
    wall = line->objectType == OBJECTTYPE_CREEPWALL;
    double yaw = line->rotation.y;
    if (wall)
        yaw = yaw + PI;
    float yawStored = (float)yaw;
    char step = blData->traverseStep;
    if (wall) {
        if (step == 7) {
            yaw = yaw - QUARTER_PI;
            yawStored = (float)yaw;
        }
        if (step == 8) {
            yaw = yaw + QUARTER_PI;
            yawStored = (float)yaw;
        }
    }
    else {
        if (step == 7) {
            yaw = yaw + QUARTER_PI;
            yawStored = (float)yaw;
        }
        if (step == 8) {
            yaw = yaw - QUARTER_PI;
            yawStored = (float)yaw;
        }
    }
    viewer->trackYaw = (float)yaw;

    float distance = viewer->trackDistance;
    viewer->trackDistanceUsed = distance;
    _VECTOR offset;
    Vec_Spherical_2_Cartesian(&offset.x, distance, yawStored, -viewer->trackPitch);
    viewer->pos.x = at.x - offset.x;
    viewer->pos.z = at.z - offset.z;
    viewer->pos.y = at.y - offset.y;

    uchar show = 1;
    line = blData->attachedToSpecialMovementItem;
    if (line->objectType == OBJECTTYPE_CREEPWALL && ((CREEPWALL *)line->extraObjectData)->thirdCam > 0) {
        obj_tag *camera = ThirdPersonCameras[((CREEPWALL *)line->extraObjectData)->thirdCam];
        show = ((THIRDCAM *)camera->extraObjectData)->show;
        ThirdCam_GetPos(camera, along, &viewer->viewMatrix);
        Vec_Copy(Mat_Position(viewer->viewMatrix), &viewer->pos);
    }
    line = blData->attachedToSpecialMovementItem;
    if (line->objectType == OBJECTTYPE_THIRDCAM_OR_WIRE && ((WIRE *)line->extraObjectData)->thirdCam > 0) {
        obj_tag *camera = ThirdPersonCameras[((WIRE *)line->extraObjectData)->thirdCam];
        show = ((THIRDCAM *)camera->extraObjectData)->show;
        ThirdCam_GetPos(camera, along, &viewer->viewMatrix);
        Vec_Copy(Mat_Position(viewer->viewMatrix), &viewer->pos);
    }
    if (show) {
        Camera_PlaceAround(&at, viewer, 1.0f);
        Camera_PlaceAbove(&at, viewer, 1.0f);
        Camera_Set(viewer, &viewer->pos, &viewer->someVec);
    }
}

// AUTOINJECT
void Camera_Update(uint idx) { 
    viewer_tag * viewer = glb_viewer[idx];
    if(viewer == NULL)
        return;

    if(viewer->cameraUpdateCallback != NULL && viewer->cameraUpdateObj != NULL) {
        viewer->cameraUpdateCallback(viewer->cameraUpdateObj, viewer->cameraUpdateObj->extraObjectData);
    }

    if(viewer->someCel != NULL && (viewer->pos).y < (viewer->someCel)->shearHeight) {
        // Under the cel's water level (its shearHeight) the view wobbles
        Camera_Shear(viewer);
    }

    if(viewer->apocalypseEffect > 0.0f) {
        // Apocalypse camera shake effect
        float a = REC_FRAME_RATE * viewer->apocalypseEffect;
        Rand_FRand_MVar2_Vec(Mat_Position(viewer->viewMatrix), a, a);
        double shake = viewer->apocalypseEffect - Float_FRand(viewer->apocalypseEffect * 0.25f);
        viewer->apocalypseEffect = shake;
        if(shake < 0.0)
            viewer->apocalypseEffect = 0.0f;
    }

    Mat_World2ViewMat(&viewer->viewMatrix, &viewer->worldMatrix);
    Vec_Copy(Mat_Position(viewer->viewMatrix), &viewer->pos);
    Mat_Copy(&viewer->viewMatrix, &viewer->mtx);
    Mat_Scale3f(&viewer->mtx, &viewer->mtx, -1.0f, 1.0f, 1.0f);
    viewer->cameraUpdated = true;

}

// AUTOINJECT
void Camera_UpdateAll(void) {
    for(int i = 0; i < 11; i++) {
        if(glb_viewer[i] != NULL) {
            Camera_Update(i);
            Camera_CheckLocation(i);
            Camera_UpdateGlbVars(i);
        }
    }

}

// AUTOINJECT
void Camera_SetUpdator(ushort num, obj_tag *gameObj, cameraUpdateFunc func) {

    if(glb_viewer[num] == NULL)
        return;
    
    glb_viewer[num]->cameraUpdateCallback = func;
    glb_viewer[num]->cameraUpdateObj = gameObj;
}