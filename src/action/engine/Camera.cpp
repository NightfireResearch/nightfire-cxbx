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
#include "../memory.h"
#include "../game/view.h"
#include "Vision.h"
#include "../input.h"
#include "../gfx/Sprite.h"

#include <bit>
#include <math.h>
#include <string.h>

// XBE_GLOBAL(0x001dc740, 0x4)
static uint32_t ScreenBlankerState;
extern uint32_t IsWidescreen;

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

    // Calculate the projection scale factors: on the x87, the half angle's tangent by FPTAN and its reciprocal, and the
    // depth scale, stay unrounded
    double reciprocal = (double)(Extended(1.0f) / FPtan(param_2 * 0.5));
    double scale = ((double)MAX_VIEW_CONE + 0.2f) / (0.2f - (double)MAX_VIEW_CONE);
    vwr->projectionScaleX = (float)-(scale * (vwr->width * 0.5) * reciprocal);
    vwr->zoom = 1.0f;
    vwr->projectionScaleY = (float)-(scale * (vwr->height * 0.5) * reciprocal);
    vwr->projectionScaleX = vwr->projectionScaleX / vwr->AspectRatio;
}

// A new view: at the origin, disabled, with nothing to draw and no fade
// AUTOINJECT
void Viewer_Init(viewer_tag *viewer) {
    viewer->world = NULL;
    View_RotTransMatrix(&CONST_ZERO_VECTOR, &CONST_ZERO_VECTOR, &viewer->viewMatrix);
    Vec_Copy(&CONST_ZERO_VECTOR, &viewer->someVec);
    Vec_Copy(&CONST_ZERO_VECTOR, &viewer->pos);
    viewer->cameraUpdated = 0;
    memset(&viewer->draw, 0, sizeof(viewer->draw));
    Mat_IdentityT(&viewer->mtx);
    Vec_Zero(&viewer->prevPos);
    viewer->zoom = 1.0f;
    viewer->someCel = NULL;
    viewer->camMode = 0;
    viewer->enabled = 0;
    viewer->spritesShown = 0;
    viewer->trackDistanceUsed = 4.0f;
    viewer->trackDistance = 4.0f;
    viewer->trackPitch = 0.0f;
    viewer->trackYaw = 0.0f;
    viewer->apocalypseEffect = 0.0f;
    viewer->swingPhase = 0.0f;
    viewer->player = NULL;
    viewer->field247_0x1e4 = 0;
    viewer->field248_0x1e8 = 0;
    viewer->field249_0x1ec = 0;
    viewer->field250_0x1f0 = 0;
    viewer->shearAngle = 0.0f;
    viewer->nightVisionRelated = 0;
    viewer->fade = 0.0f;
    viewer->fadeRate = 0.0f;
    viewer->fadeColour[3] = 0;
    viewer->fadeColour[2] = 0;
    viewer->fadeColour[1] = 0;
    viewer->fadeColour[0] = 0;
    viewer->fadeHold = 0;
    viewer->fadeState = VIEWFADE_NONE;
    Stack_Init(&viewer->stateStack, 12, viewer->stateStackMem);
}

// The view at a position and angles; the view's own copies of both are what Camera_ApplySwing and the trackers
// start from
// AUTOINJECT
void Camera_Set(viewer_tag *viewer, _VECTOR *transform, _VECTOR *rotation) {
    if (viewer == NULL || transform == NULL || rotation == NULL)
        return;

    View_RotTransMatrix(rotation, transform, &viewer->viewMatrix);
    Vec_Copy(rotation, &viewer->someVec);
    Vec_Copy(transform, &viewer->pos);
    viewer->cameraUpdated = 0;
}

// Each view's draw lists hold this many entries, half in multiplayer
static const ushort DrawListSizes[3] = {0x200, 0x200, 0x80};

// Enables or disables a view (its draw lists come and go with it), sets whether its sprites show, and with a position
// puts it there, facing along z
// AUTOINJECT
void Camera_Enable(ushort cameraNum, char enable, undefined1 sprites, _VECTOR *position) {
    viewer_tag *viewer = glb_viewer[cameraNum];
    if (viewer == NULL)
        return;

    viewer->enabled = enable;
    glb_viewer[cameraNum]->spritesShown = sprites;
    if (enable) {
        if (viewer->draw.lists[0] == NULL) {
            memset(&viewer->draw, 0, sizeof(viewer->draw));
            uint total = 0;
            for (int i = 0; i < 3; i++) {
                ushort size = DrawListSizes[i];
                if (MPSettings.isMultiplayer)
                    size >>= 1;
                viewer->draw.sizes[i] = size;
                total += size;
            }
            DrawEntry *block = (DrawEntry *)Mem_MallocXbox(total * sizeof(DrawEntry), total * XBOX_SIZEOF(DrawEntry),
                                                           0x504, 0);
            uint at = 0;
            for (int i = 0; i < 3; i++) {
                viewer->draw.lists[i] = block + at;
                at += viewer->draw.sizes[i];
            }
        }
    } else {
        if (viewer->draw.lists[0] != NULL)
            Mem_Free((void **)&viewer->draw.lists[0]);
        memset(&viewer->draw, 0, sizeof(viewer->draw));
    }

    if (position != NULL) {
        glb_viewer[cameraNum]->someCel = NULL;
        Camera_Set(glb_viewer[cameraNum], position, &CONST_ZERO_VECTOR);
    }
}

// Each camera keeps a stack of its states (bit 0 enabled, bit 8 sprites shown) so that a scripted
// camera can take over and give them back.
#define CAMERA_STATE_SPRITES_SHIFT 8

// Saves every camera's enabled and sprites-shown flags on its own stack.
// AUTOINJECT
void __stdcall Camera_PushStates(void) {
    for (int i = 0; i < 11; i++) {
        viewer_tag *viewer = glb_viewer[i];
        if (viewer != NULL) {
            uintptr_t state = viewer->spritesShown << CAMERA_STATE_SPRITES_SHIFT | viewer->enabled;
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

// MSVC's __ftol2 as the original uses it: the low word of a truncation to 64 bits. NaN, values out of the 64-bit range
// and values that round to a multiple of 2^32 give 0 (as src/driving/platform/X87.h has it).
static int32_t Ftol(double x) {
    if (!(fabs(x) < 9223372036854775808.0))
        return 0;
    long long nearest = llrint(x);
    if (uint32_t(nearest) == 0)
        return 0;
    return int32_t(int64_t(x));
}

// Fades the view to a colour (0xRRGGBBAA, the alpha the fade reaches) over a number of frames at 60 a second; a
// negative count fades back from it. Refused while a fade that holds is running.
// AUTOINJECT
bool Camera_SetFade(ushort viewerNum, float durationFrames, uint colourAndAlpha, undefined1 fadeMode) {
    viewer_tag *viewer = glb_viewer[viewerNum];
    if (viewer == NULL || (viewer->fadeHold && viewer->fadeState))
        return false;

    constexpr float rateScale = 6.535948e-05f;   // about 1 / (60 x 255)
    static_assert(std::bit_cast<uint32_t>(rateScale) == 0x3889119b, "not the original's constant");
    double alpha = (int)(colourAndAlpha & 0xff);
    viewer->fadeHold = fadeMode;
    viewer->fadeState = VIEWFADE_FADING;
    double rate = 60.0 / durationFrames * alpha * rateScale;
    viewer->fadeRate = (float)rate;
    double start = rate < 0.0 ? 1.0f : 0.0f;
    viewer->fadeColour[0] = colourAndAlpha >> 24;
    viewer->fadeColour[1] = colourAndAlpha >> 16;
    double fade = alpha * start * (1.0f / 255.0f);
    viewer->fadeColour[2] = colourAndAlpha >> 8;
    viewer->fade = (float)fade;
    viewer->fadeColour[3] = Ftol(fade * 255.0f);
    viewer->spritesShown = 1;
    return true;
}

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

// AUTOINJECT
void Camera_Create(int idx, world_tag *param_2, char param_3, ushort posX, ushort posY, ushort width, ushort height) {
  viewer_tag *viewer;
  cel_tag *pcVar1;
  world_tag *world;
  cel_tag *iVar1;
  
  if (glb_viewer[idx] == NULL) {
    world = param_2;
    viewer = build_alloc_viewer(idx);
    glb_viewer[idx] = build_link_world_to_viewer(viewer, world);
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
  glb_viewer[idx]->enabled = param_3;
  glb_viewer[idx]->spritesShown = 1;
  glb_viewer[idx]->cameraUpdateObj = NULL;
  glb_viewer[idx]->cameraUpdateCallback = NULL;
  Camera_Enable(idx, param_3, 1, NULL);
  Camera_ScreenCoords(idx, (float)posX, (float)posY, (float)width, (float)height);
  return;
}

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

// Eases value towards target by rate of the gap, stopping there rather than overshooting it. On the x87 the gap
// stays unrounded, and a NaN gap counts as a fall.
float _moveTowardsValue(float value, float target, float rate) {
    Extended gap = Extended(target) - Extended(value);
    float moved = (float)(Extended(rate) * gap + Extended(value));
    double rising = (double)gap;
    if (!(rising < 0.0) && rising == rising)
        return moved > target ? target : moved;
    return moved < target ? target : moved;
}

#ifndef NIGHTFIRE_STANDALONE
// The original's arguments on the stack and the result in ST0, the caller cleaning up
// AUTOLTCG
float __declspec(naked) moveTowardsValue(float value, float target, float rate) {
    _asm {
        push dword ptr [esp + 0xc]
        push dword ptr [esp + 0xc]
        push dword ptr [esp + 0xc]
        call _moveTowardsValue
        add esp, 0xc
        ret
    }
}
#endif

// The debug cameras' placements. Each eases one part of the view rate of the way to where it should be, then sets
// the view to its own position and angles (Camera_Set).

// Camera_PlaceBehind's offsets: a turn from the facing, and a distance
// XBE_GLOBAL(0x0017c168, 0x4)
static float CameraBehindYaw = std::bit_cast<float>(0xbfbba5e3u);   // -1.466
// XBE_GLOBAL(0x0017c16c, 0x4)
static float CameraBehindDistance = -7.0f;

// XBE_GLOBAL(0x001fec50, 0x4)
float debug_cam_posy;

// The yaw, to face the target
// AUTOINJECT
void Camera_PlaceAround(_VECTOR *target, viewer_tag *viewer, float rate) {
    Extended yaw = FPatan((double)target->x - viewer->pos.x, (double)target->z - viewer->pos.z);
    viewer->someVec.y = (float)((yaw - Extended(viewer->someVec.y)) * Extended(rate) + Extended(viewer->someVec.y));
    Camera_Set(viewer, &viewer->pos, &viewer->someVec);
}

// The pitch, to look at the target
// AUTOINJECT
void Camera_PlaceAbove(_VECTOR *target, viewer_tag *viewer, float rate) {
    double dx = (double)target->x - viewer->pos.x;
    double dz = (double)target->z - viewer->pos.z;
    double dy = (double)target->y - viewer->pos.y;
    Extended pitch = -FPatan(dy, sqrt(dz * dz + dx * dx));
    viewer->someVec.x = (float)((pitch - Extended(viewer->someVec.x)) * Extended(rate) + Extended(viewer->someVec.x));
    Camera_Set(viewer, &viewer->pos, &viewer->someVec);
}

// The height, to height above the object (and debug_cam_posy). Not named on PS2; the name is ours.
// FUNC_AT(0x000246a0)
void Camera_PlaceOver(obj_tag *obj, viewer_tag *viewer, float rate, float height) {
    Extended y = Extended(debug_cam_posy) + Extended(obj->position.y) + Extended(height);
    viewer->pos.y = (float)((y - Extended(viewer->pos.y)) * Extended(rate) + Extended(viewer->pos.y));
    Camera_Set(viewer, &viewer->pos, &viewer->someVec);
}

// Across the ground, to distance (plus CameraBehindDistance) along the facing turned by CameraBehindYaw. The z's
// step is rounded to a float before it is added. Not named on PS2; the name is ours.
// FUNC_AT(0x00024710)
void Camera_PlaceBehind(_VECTOR *position, _VECTOR *rotation, viewer_tag *viewer, float rate, float distance) {
    Extended away = Extended(CameraBehindDistance) + Extended(distance);
    Extended yaw = Extended(CameraBehindYaw) + Extended(rotation->y);
    float stepZ = (float)((Extended(position->z) - FCos(yaw) * away - Extended(viewer->pos.z)) * Extended(rate));
    viewer->pos.x = (float)((Extended(position->x) - FSin(yaw) * away - Extended(viewer->pos.x)) * Extended(rate)
                            + Extended(viewer->pos.x));
    viewer->pos.z = (float)(Extended(stepZ) + Extended(viewer->pos.z));
    Camera_Set(viewer, &viewer->pos, &viewer->someVec);
}

// How far the follow camera can be from the head at the given angles: its trackDistance, or less where a ray from
// the head that far hits something; 0.1 short of that, unrounded. Not named on PS2; the name is ours.
// FUNC_AT(0x000247b0)
double Camera_FollowRoom(obj_tag *obj, viewer_tag *viewer, float yaw, float pitch) {
    HITDATA_tag *hits = NULL;
    float room = viewer->trackDistance;
    _VECTOR end, head;
    Vec_Spherical_2_Cartesian(&end.x, room, yaw, -pitch);
    Player_GetHeadPos(&head, obj, NULL);
    end.x = head.x - end.x;
    end.y = head.y - end.y;
    end.z = head.z - end.z;
    if (Collide_RayIntersect(&head, &end, obj->inCel, obj, NULL, &hits, 0, 8, 0)) {
        room = Vec_Dist3D(&hits->hitPosition, &head);
        Collide_FreeHitList(&hits);
    }
    return (double)room - 0.1f;
}

// Behind the player's head at the view's track angles, as far as the walls allow: the nearest of five rays (the
// angles and 0.1 either way of both) is eased into an eighth of a frame, from the straight ray's room or the
// distance last used, whichever is less. Then facing the head, rolled as the player is, the pitch taken from his
// aim and kept within 1.5. Camera modes 6 and 10. Not named on PS2; the name is ours.
// FUNC_AT(0x00024880)
void Camera_Follow(obj_tag *obj, viewer_tag *viewer) {
    BLData *blData = (BLData *)obj->extraObjectData;
    float nearest = viewer->trackDistance;
    float pitch = (float)((double)obj->rotation.x - (double)blData->pitchFromHorizontal * 1.5707964f);
    if (pitch < -1.5f)
        pitch = -1.5f;
    else if (pitch > 1.5f)
        pitch = 1.5f;

    static const float sides[4][2] = {{0.1f, 0.1f}, {0.1f, -0.1f}, {-0.1f, 0.1f}, {-0.1f, -0.1f}};
    for (int i = 0; i < 4; i++) {
        double room = Camera_FollowRoom(obj, viewer, (float)((double)viewer->trackYaw + sides[i][0]),
                                        (float)((double)viewer->trackPitch + sides[i][1]));
        if (room < nearest)
            nearest = (float)room;
    }
    double room = Camera_FollowRoom(obj, viewer, viewer->trackYaw, viewer->trackPitch);
    if (room < nearest)
        nearest = (float)room;
    if (!(room < viewer->trackDistanceUsed))
        room = viewer->trackDistanceUsed;
    float distance = (float)(((double)nearest - room) * 0.125f + room);
    viewer->trackDistanceUsed = distance;

    _VECTOR offset, head;
    Vec_Spherical_2_Cartesian(&offset.x, distance, viewer->trackYaw, -viewer->trackPitch);
    Player_GetHeadPos(&head, obj, NULL);
    viewer->pos.x = head.x - offset.x;
    viewer->pos.y = head.y - offset.y;
    viewer->pos.z = head.z - offset.z;
    Camera_PlaceAround(&head, viewer, 1.0f);
    Camera_PlaceAbove(&head, viewer, 1.0f);
    viewer->someVec.z = obj->rotation.z;
    viewer->trackYaw = obj->rotation.y;
    viewer->trackPitch = pitch;
    Camera_Set(viewer, &viewer->pos, &viewer->someVec);
}

// Facing the object from where the view is. Camera mode 4. Not named on PS2; the name is ours.
// FUNC_AT(0x00024d40)
void Camera_LookAtObject(obj_tag *obj, viewer_tag *viewer) {
    Camera_PlaceAround(&obj->position, viewer, 1.0f);
    Camera_PlaceAbove(&obj->position, viewer, 1.0f);
}

// A three-player match leaves the screen's fourth quarter to a picture: a 320 by 240 sprite in view 5
#define TEX_FOURTH_QUARTER ((HASHCODE)0x0300013c)

// AUTOINJECT
void Cameras_PostLoad(void) {
    if (MPSettings.numPlayers != 3 || !MPSettings.isMultiplayer)
        return;
    sprite *quarter = Sprite_CreateAndLink2Viewer(5);
    if (quarter == NULL)
        return;
    Sprite_SetParams(quarter, 5, 320, 240, TEX_FOURTH_QUARTER, 0x7f7f7fff);
    quarter->onscreenWidth = 320;
    quarter->onscreenHeight = 240;
}

// Around the player's head at the view's track angles and distance, facing it. With a trackDistance, the right stick
// turns it (the pitch kept within 1.5) and the distance eases a 32nd of the way there; without one it eases round
// behind the player and in, and once close in (0.1) a camera mode is ended, back to the first person. Camera mode 3.
// Not named on PS2; the name is ours.
// FUNC_AT(0x00024b10)
void Camera_Orbit(obj_tag *obj, viewer_tag *viewer) {
    BLData *blData = glb_blokes[(uchar)viewer->idx];
    _VECTOR offset, head;
    Vec_Spherical_2_Cartesian(&offset.x, viewer->trackDistanceUsed, viewer->trackYaw, -viewer->trackPitch);
    Player_GetHeadPos(&head, obj, NULL);
    viewer->pos.x = head.x - offset.x;
    viewer->pos.y = head.y - offset.y;
    viewer->pos.z = head.z - offset.z;
    Camera_PlaceAround(&head, viewer, 1.0f);
    Camera_PlaceAbove(&head, viewer, 1.0f);
    viewer->someVec.z = 0.0f;

    if (viewer->trackDistance != 0.0f) {
        float look = Input_Actionf((uchar)viewer->idx, ACTION_AIM_U_D, 1);
        double pitch = viewer->trackPitch - (double)look * ((double)REC_FRAME_RATE * 1.5707964f);
        viewer->trackPitch = (float)pitch;
        if (pitch < -1.5f)
            viewer->trackPitch = -1.5f;
        else if (pitch > 1.5f)
            viewer->trackPitch = 1.5f;
        float turn = Input_Actionf((uchar)viewer->idx, ACTION_AIM_L_R, 1);
        viewer->trackYaw = (float)(viewer->trackYaw - (double)REC_FRAME_RATE * 1.5707964f * turn);
        viewer->trackDistanceUsed = (float)(((double)viewer->trackDistance - viewer->trackDistanceUsed) * 0.03125f
                                            + viewer->trackDistanceUsed);
    }
    else {
        viewer->trackPitch = (float)(Vec_AngleDifference(viewer->trackPitch, obj->rotation.x) * 0.03125f
                                     + viewer->trackPitch);
        viewer->trackYaw = (float)(Vec_AngleDifference(viewer->trackYaw, obj->rotation.y) * 0.03125f
                                   + viewer->trackYaw);
        double distance = (double)viewer->trackDistanceUsed - 0.13333334f;
        viewer->trackDistanceUsed = (float)distance;
        if (blData->camMode != 0 && distance <= 0.1f) {
            blData->camMode = 0;
            blData->pitchFromHorizontal = 0.0f;
            viewer->trackDistanceUsed = 0.0f;
        }
    }
    Camera_Set(viewer, &viewer->pos, &viewer->someVec);
}

// Behind the player's head at the track angles and distance used, pulled in to where a ray from his position meets
// a wall and then a quarter in from that, facing the head; the view then moves a twelfth of the way from where it
// was, and the distance used a 32nd of the way to trackDistance. Camera mode 5. Not named on PS2; the name is ours.
// FUNC_AT(0x00024d70)
void Camera_Trail(obj_tag *obj, viewer_tag *viewer) {
    HITDATA_tag *hits = NULL;
    _VECTOR oldPos, oldRot, head, offset, back;
    Vec_Copy(&viewer->pos, &oldPos);
    Vec_Copy(&viewer->someVec, &oldRot);
    Player_GetHeadPos(&head, obj, NULL);
    Vec_Spherical_2_Cartesian(&offset.x, viewer->trackDistanceUsed, viewer->trackYaw, -viewer->trackPitch);
    viewer->pos.x = head.x - offset.x;
    viewer->pos.z = head.z - offset.z;
    viewer->pos.y = head.y - offset.y;
    if (Collide_RayIntersect(&obj->position, &viewer->pos, obj->inCel, obj, NULL, &hits, 0, 8, 0)) {
        Vec_Copy(&hits->hitPosition, &viewer->pos);
        Collide_FreeHitList(&hits);
    }
    Vec_Subtract(&viewer->pos, &head, &back);
    Vec_Normalise(&back, &back);
    back.x = back.x * 0.25f;
    back.y = back.y * 0.25f;
    back.z = back.z * 0.25f;
    Vec_Subtract(&viewer->pos, &back, &viewer->pos);
    viewer->trackDistanceUsed = (float)((double)Vec_Dist3D(&viewer->pos, &head) + 0.25f);
    Camera_PlaceAround(&head, viewer, 1.0f);
    Camera_PlaceAbove(&head, viewer, 1.0f);

    float pitch = viewer->someVec.x;
    viewer->trackDistanceUsed = (float)(((double)viewer->trackDistance - viewer->trackDistanceUsed) * 0.03125f
                                        + viewer->trackDistanceUsed);
    viewer->pos.x = (float)(((double)viewer->pos.x - oldPos.x) * 0.083333336f + oldPos.x);
    viewer->pos.y = (float)(((double)viewer->pos.y - oldPos.y) * 0.083333336f + oldPos.y);
    viewer->pos.z = (float)(((double)viewer->pos.z - oldPos.z) * 0.083333336f + oldPos.z);
    viewer->someVec.x = (float)(Vec_AngleDifference(oldRot.x, pitch) * 0.083333336f + oldRot.x);
    viewer->someVec.y = (float)(Vec_AngleDifference(oldRot.y, viewer->someVec.y) * 0.083333336f + oldRot.y);
    viewer->someVec.z = (float)(Vec_AngleDifference(oldRot.z, viewer->someVec.z) * 0.083333336f + oldRot.z);
    Camera_Set(viewer, &viewer->pos, &viewer->someVec);
}

// The multiplayer end-of-match camera: circling the target 0.7 a second, distance out from it and up (at least 0.75
// above it, at most range), looking at it. A 1.5 sphere at the view that meets something brings distanceWanted in
// to the hit (0.1 at least); the distance moves five times its gap a second towards distanceWanted, never past it.
// Not named on PS2; the name is ours.
// FUNC_AT(0x00025450)
void Camera_LookAtFrom(viewer_tag *viewer, EndCamera *cam) {
    if (cam == NULL)
        return;
    float c = (float)FCos(cam->angle);
    float s = (float)FSin(cam->angle);
    cam->angle = (float)((double)REC_FRAME_RATE * 0.7f + cam->angle);
    _VECTOR eye;
    Vec_Copy(&cam->target, &eye);
    eye.y = (float)((double)eye.y + cam->distance);
    double low = (double)cam->target.y + 0.75f;
    if (eye.y < low) {
        eye.y = (float)low;
    }
    else {
        double high = (double)cam->range + cam->target.y;
        if (eye.y > high)
            eye.y = (float)high;
    }
    eye.x = (float)((double)s * cam->distance + eye.x);
    eye.z = (float)((double)c * cam->distance + eye.z);

    _MATRIX look;
    Mat_LookAt(&look, &eye, &cam->target, &CONST_UP_VECTOR, &MAYBE_CONST_FORWARD_VECTOR);
    Mat_Copy(&look, &viewer->viewMatrix);
    _VECTOR *at = Mat_Position(viewer->viewMatrix);
    cel_tag *cel = build_FindCel(at, glb_world);
    HITDATA_tag *hits;
    Collide_SphereIntersect(at, 1.5f, cel, cam->subject, NULL, &hits, 0, 0, 3, 4);
    if (hits == NULL) {
        cam->distanceWanted = cam->range;
    }
    else {
        double wanted = (double)cam->range - ((double)cam->range - hits->dist);
        if (!(wanted > 0.1f))
            wanted = 0.1f;
        cam->distanceWanted = (float)wanted;
        Collide_FreeHitList(&hits);
    }

    float wanted = cam->distanceWanted;
    float gap = (float)((double)wanted - cam->distance);
    double moved = (double)REC_FRAME_RATE * gap * 5.0f + cam->distance;
    if (!(gap < 0.0f) && gap == gap)
        cam->distance = moved > wanted ? wanted : (float)moved;
    else
        cam->distance = moved < wanted ? wanted : (float)moved;
}

// An angle brought within plus or minus pi: one at pi or past it, as a remainder of 2pi, and then a turn
// towards zero if that is still outside. Left unrounded, as the original leaves it on the x87.
// AUTOINJECT
double normaliseToPlusMinusPi(float angle) {
    if (AbsFchs((double)angle) < 3.1415927f)
        return angle;
    double rest = fmod((double)angle, std::bit_cast<double>(0x401921fb60000000ull));
    if (AbsFchs(rest) < 3.1415927f)
        return rest;
    if (angle < 0.0f || angle != angle)
        return rest + 6.2831855f;
    return rest - 6.2831855f;
}
// XBE_GLOBAL(0x001f667c, 0x1)
uint8_t GlobalBlur;

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
    Concat(&vwr->viewMatrix, &ShearMatrix);
    Mat_CopyRot(&ShearMatrix, &vwr->viewMatrix);
    Vec_Copy(&ShearPosition, Mat_Position(vwr->viewMatrix));
    GlobalBlur = 0x91;
}

#define HALF_PI 1.5707964f
#define PI 3.1415927f
#define QUARTER_PI 0.7853982f
#define TWO_PI 6.2831855f

// Keeps the view's cel: the cel through a portal it moved through since last time, or, out of its cel's box, the
// cel it is now in. In none, it stays in the old one (or the world's first) and prevPos is not moved on.
// AUTOINJECT
void Camera_CheckLocation(ushort idx) {
    viewer_tag *viewer = glb_viewer[idx];
    if (viewer == NULL)
        return;

    Vec_Subtract(&viewer->pos, &viewer->prevPos, &viewer->move);
    if (!viewer->enabled)
        return;

    cel_tag *through = Cel_ObjectLeftCel(viewer->someCel, &viewer->prevPos, &viewer->pos);
    if (through != NULL) {
        viewer->someCel = through;
    } else if (!build_point_in_bounding_box(&viewer->pos, viewer->someCel)) {
        cel_tag *old = viewer->someCel;
        viewer->someCel = build_FindCel(&viewer->pos, viewer->world);
        if (viewer->someCel == NULL) {
            if (old != NULL) {
                viewer->someCel = old;
            } else {
                NF_WARN("Error : Camera cant be linked to a cel!\n"); // GC check (0x8010b688)
                viewer->someCel = glb_world->firstCel;
            }
            return;
        }
        viewer->someCel->addedToDraw = 1;
    }
    Vec_Copy(&viewer->pos, &viewer->prevPos);
}

// Runs the view's fade: the alpha follows it, and when it leaves 0 to 1 the fade is done (and the frame after, over)
// AUTOINJECT
void Camera_UpdateGlbVars(ushort idx) {
    viewer_tag *viewer = glb_viewer[idx];
    if (viewer == NULL)
        return;

    switch (viewer->fadeState) {
    case VIEWFADE_NONE:
        viewer->fadeColour[3] = 0;
        break;
    case VIEWFADE_FADING: {
        double fade = (double)FRAME_RATE_MUL * viewer->fadeRate + viewer->fade;
        viewer->fade = (float)fade;
        double alpha = fade * 255.0f;
        if (alpha < 0.0)
            alpha = 0.0f;
        else if (alpha > 255.0f)
            alpha = 255.0f;
        viewer->fadeColour[3] = Ftol(alpha);
        if (viewer->fade < 0.0f || viewer->fade > 1.0f)
            viewer->fadeState = VIEWFADE_DONE;
        break;
    }
    case VIEWFADE_DONE:
        viewer->fadeState = VIEWFADE_NONE;
        break;
    }
}

// A player's view: his matrix pitched by his look, from his eyes
// AUTOINJECT
void Camera_SetToPlayer(obj_tag *player, viewer_tag *viewer) {
    if (player == NULL || viewer == NULL)
        return;

    _MATRIX pitch;
    RotMatrixX((float)-(((BLData *)player->extraObjectData)->pitchFromHorizontal * (double)HALF_PI), &pitch);
    MulMatrix0(&player->transformMatrix, &pitch, &viewer->viewMatrix);
    Player_GetHeadPos(Mat_Position(viewer->viewMatrix), player, NULL);
}

// The scope's sway: the view's angles wobble at one, two and three times a turn every four seconds, by sway and how
// far the view is zoomed in. On the x87 the phase doubled for x is the unrounded one.
// AUTOINJECT
void Camera_ApplySwing(viewer_tag *viewer, float sway) {
    if (viewer == NULL)
        return;

    double phase = TWO_PI / ((double)_FRAME_RATE * 4.0f) + viewer->swingPhase;
    viewer->swingPhase = (float)phase;
    Extended zoomed = (double)viewer->zoom - 1.0f;
    viewer->someVec.x = (float)(FSin(phase + phase) * zoomed * Extended(sway) + Extended(viewer->someVec.x));
    viewer->someVec.y = (float)(FSin(viewer->swingPhase) * zoomed * Extended(sway) + Extended(viewer->someVec.y));
    viewer->someVec.z = (float)(FSin(viewer->swingPhase * 3.0) * zoomed * Extended(sway) + Extended(viewer->someVec.z));
    Camera_Set(viewer, &viewer->pos, &viewer->someVec);
}

// Shakes the players' views near pos: up to twice amount away, by amount x (1 - (distance / 2 amount)^2), at most 10
// AUTOINJECT
void Camera_Shake(_VECTOR *pos, float amount) {
    float range = amount + amount;
    for (int i = 0; i < 4; i++) {
        viewer_tag *viewer = glb_viewer[i];
        if (viewer == NULL)
            continue;

        double distance = Vec_Dist3D(pos, Mat_Position(viewer->viewMatrix));
        if (distance > range)
            continue;
        double near = distance / range;
        double shake = (1.0f - near * near) * range;
        if (shake > 0.0) {
            if (!(shake < 10.0f))
                shake = 10.0f;
            viewer->apocalypseEffect = (float)shake;
        }
    }
}

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
