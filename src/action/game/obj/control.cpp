#include "../../engine/Camera.h"
#include "control.h"

#include "../../actionhelpers.h"

#include "../../util/LList.h"
#include "ScriptPlayer.h"
#include "../../memory.h"
#include "../mp/multiplayer.h"
#include "../../math/math.h"
#include "../view.h"
#include "../../game.h"
#include "../../devtools/Demo.h"
#include "../../engine/Anim.h"
#include "../../engine/Collide.h"
#include "../../engine/Vision.h"
#include "../../engine/celglist.h"
#include "../../engine/viewer.h"
#include "../../math/X87Ops.h"
#include "../drone/BOT.h"
#include "../drone/Drone.h"
#include "../drone/NDrone2.h"
#include "../sp/SwitchChannels.h"
#include "Door.h"
#include "Effect.h"
#include "Explode.h"
#include "GunImp.h"
#include "Light.h"
#include "Player.h"
#include "Trigger.h"
#include "build.h"
#include "bullet.h"
#include "car.h"

#include <stdio.h>
#include <string.h>

// The game objects' list: its head is the start of an object, as far as the list's links (control_add_object_to_list
// is given its address), and its nextObject the first object
typedef struct OBJLIST_HEAD {
    LLNODE_tag *llPrev;
    LLNODE_tag *llNext;
    obj_tag *nextInCel;
    obj_tag *prevInCel;
    struct ATTACH_tag *attachments;
    obj_tag *nextObject;    // 0x14
    obj_tag *prevObject;    // 0x18
} OBJLIST_HEAD;
static_assert(offsetof(OBJLIST_HEAD, nextObject) == offsetof(obj_tag, nextObject), "the head's links are an object's");
static_assert(offsetof(OBJLIST_HEAD, prevObject) == offsetof(obj_tag, prevObject), "the head's links are an object's");
// XBE_GLOBAL(0x001df338, 0x1c)
static OBJLIST_HEAD DynamicObjList;

// Number of game objects
// XBE_GLOBAL(0x001df828, 0x4)
static uint32_t DynamicObjCount;

// Game objects which span multiple cels
// XBE_GLOBAL(0x001df41c, 0xc)

// Empty on the Xbox, where the linker shares one empty function among several (nightfire-port's name)
static void MP_DebugDraw(void) {}

// x kept within lo + radius and hi - radius, each bound compared before it is rounded to a float
static void ClampInBox(float *x, float lo, float hi, float radius) {
    double low = (double)radius + lo;
    if (low > *x) {
        *x = (float)low;
        return;
    }
    double high = (double)hi - radius;
    if (high < *x)
        *x = (float)high;
}

// AUTOINJECT
obj_tag* Control_ReturnNextObjectOfType(ObjectType type, obj_tag* from) {

    obj_tag* at = ((from == NULL) ? DynamicObjList.nextObject : from);

    while(at != NULL) {

        if(at->objectType == type)
            return at;

        at = at->nextObject;
    }

    return NULL;

}

// AUTOINJECT
void Control_DeleteAllObjectsOfType(ObjectType type) {

    obj_tag *at = DynamicObjList.nextObject;

    while (at != NULL) {

        if (at->objectType == type)
            at->flags |= OBJFLAG_DELETE;
        
        at = at->nextObject;
    }
    
}

// AUTOINJECT
obj_tag* control_first_object(void) {

    return DynamicObjList.nextObject;

}

// AUTOINJECT
void control_delete_object(obj_tag* obj) {

  //printf("Deleting object of type 0x%02x (%s)\n", obj->objectType, Object_GetName((ObjectType)obj->objectType));

  // First, call the object's delete function if it exists
  if(control_funcs[obj->objectType].deleteFunc != NULL)
    control_funcs[obj->objectType].deleteFunc(obj);

  // Clean up the object's hit list
  Collide_FreeHitList(&obj->hitList);

  // Remove from multiplayer game
  if (MPSettings.dronesAreBots) {
    MP_objectBeingDeleted(obj);
  }

  obj->objectType = OBJECTTYPE_DELETED;

  // Remove from forced list (objects straddling cels)
  if (obj->effectFlags & EFFECT_IN_FORCED_LIST) {
    LList_Remove(&ForcedList, (LLNODE_tag*)obj);
  }

  // Remove animation object and script player
  AnimObjectDelete(obj);
  SP_RemoveObj(obj, obj->scriptPlayer);

  DynamicObjCount--;

  // Remove from cel (doubly-linked)
  control_unlink_object(obj);

  // Remove from object list (doubly-linked)
  obj_tag* prev = obj->prevObject;
  obj_tag* next = obj->nextObject;
  if (next != NULL) {
    next->prevObject = prev;
  }
  if (prev != NULL) {
    prev->nextObject = next;
  }
  obj->prevObject = NULL;
  obj->nextObject = NULL;

  // Finally, free the memory
  Mem_Free((void**)&obj);
  return;

}

// AUTOINJECT
void control_unlink_object(obj_tag* obj) {
  obj_tag* prev = obj->prevInCel;
  obj_tag* next = obj->nextInCel;
  if (prev != NULL) {
    prev->nextInCel = next;
  }
  if (next != NULL) {
    next->prevInCel = prev;
  }
  obj->inCel = NULL;
  obj->prevInCel = NULL;
  obj->nextInCel = NULL;
}

// AUTOINJECT
void control_init_object(obj_tag* obj) {

    if(obj == NULL)
        return;
    
    obj->displayMask = 0x001f;
    obj->flags |= OBJFLAG_NEW;
    obj->drawnCountdown = 2;
    obj->creationTimeFrames = GameState.NumFramesUnpaused;
    obj->scale = 1.0;
    obj->tweakR = 0xff;
    obj->tweakG = 0xff;
    obj->tweakB = 0xff;
    obj->tweakA = 0xff;
    obj->ambientR = 0xff;
    obj->ambientG = 0xff;
    obj->ambientB = 0xff;

}

void control_add_object_to_list(obj_tag *list,obj_tag *obj) {
  obj_tag *tmp = list->nextObject;
  obj->prevObject = list;
  obj->nextObject = tmp;
  list->nextObject = obj;
  if (tmp != NULL)
    tmp->prevObject = obj;
}

// AUTOINJECT
obj_tag* control_create_object(int sizeBytes,_VECTOR *pos,_VECTOR *rot,quaternion_tag *quat) {

    int totalSize = sizeBytes + sizeof(obj_tag);
    obj_tag* obj = (obj_tag*)Mem_Malloc(totalSize, MALLOC_OBJS, 0);
    
    if(obj == NULL) {
        printf("Failed to allocate object of size %d bytes\n", sizeBytes);
        return NULL;
    }

    memset(obj, 0, totalSize);

    control_init_object(obj);

    if(pos != NULL) {
        Vec_Copy2(pos, &obj->position, &obj->lastPosition);
    }

    if(rot != NULL) {
        Vec_Copy2(rot, &obj->rotation, &obj->lastRotation);
    }

    if(quat == NULL) {
      View_RotTransMatrix(&obj->rotation, &obj->position, &obj->transformMatrix);
    }
    else {
      Quat_QuatToMat(quat, &obj->transformMatrix);
      Matrix_SetTrans(&obj->position, &obj->transformMatrix);
    }

    obj->extraObjectData = (void*)(obj + 1);
    
    control_add_object_to_list((obj_tag*)&DynamicObjList, obj);
    DynamicObjCount++;

    obj->llPrev = NULL;
    obj->llNext = NULL;
    
    return obj;
}

// Where the nearest player is (out unchanged without one nearer than 1000 units), and the square of his distance
// AUTOINJECT
float control_ClosestPlayerPnt(_VECTOR *pos, _VECTOR *out) {
    float nearest = 1000000.0f;
    for (int i = 0; i < 4; i++) {
        obj_tag *player = glb_players[i];
        if (player == NULL)
            continue;
        double distSq = Vec_SqDist3D(pos, &player->position);
        if (distSq < nearest) {
            Vec_Copy(&player->position, out);
            nearest = (float)distSq;
        }
    }
    return nearest;
}

// AUTOINJECT
void Control_SetEffectType(obj_tag *obj, uint effectType) {
  obj->effectFlags = (obj->effectFlags & (EFFECT_STRADDLES_CELS | EFFECT_FORCED_LISTED | EFFECT_ALWAYS_FORCED)) | effectType;
}

// AUTOINJECT
void Control_SetGList(obj_tag *obj, celglist_tag *celgl) {

  if((obj == NULL) || (celgl == NULL))
    return;

  obj->objGraphics = celgl;
  Control_SetEffectType(obj, celgl->applyFlagsToObject);

  if(celgl->applyFlagsToObject & 0x40000)
    obj->displayMask &= 0xffef; // Confirmed against PS2 code (Xbox code does this as a single-byte operation)
  
}

// Empties the object lists: no dynamic objects, and a fresh forced-object list (entries of 0xe4 bytes, none
// preallocated).
// AUTOINJECT
void control_init_object_lists(void) {
    DynamicObjList.nextObject = NULL;
    DynamicObjList.prevObject = NULL;
    DynamicObjCount = 0;
    LList_Init(&ForcedList, sizeof(obj_tag), 0);
    Demo_ObjectListsReset();
}

// The matrix of an object from its position and rotation, as its transform flags say, then scaled. The scaling is
// done to the object's own matrix whatever matrix is passed in: every caller passes that one.
// AUTOINJECT
void Control_GetObjMatrix(obj_tag *obj, _MATRIX *mtx) {
    if (!(obj->transformFlags & TRANSFORM_MATRIX_PLACED)) {
        if (obj->transformFlags & TRANSFORM_KEEP_ROTATION)
            *Mat_Position(*mtx) = obj->position;
        else
            View_RotTransMatrix(&obj->rotation, &obj->position, mtx);
    }
    if (obj->scale != 1.0f && !(obj->transformFlags & TRANSFORM_NO_SCALE)) {
        _MATRIX *own = &obj->transformMatrix;
        Mat_Normalize(own);
        if (obj->transformFlags & TRANSFORM_SCALE_LENGTH_ONLY)
            Mat_Scale3f(own, own, 1.0f, 1.0f, obj->scale);
        else
            Mat_Scale3f(own, own, obj->scale, obj->scale, obj->scale);
    }
}

// The object's matrix, and its bounding sphere in the world: the model's (or with TRANSFORM_ANIMATED the
// animation's) through the matrix, its radius grown by the scale. With no model, the matrix's translation and a
// radius of 0.1. The last centre is kept.
// AUTOINJECT
void Control_BuildWorldSph(obj_tag *obj) {
    Control_GetObjMatrix(obj, &obj->transformMatrix);
    Vec_Copy(&obj->centrePoint, &obj->lastCentrePoint);
    if (obj->objGraphics == NULL) {
        obj->centrePoint = *Mat_Position(obj->transformMatrix);
        if (!(obj->transformFlags & TRANSFORM_FIXED_RADIUS))
            obj->radius = 0.1f;
        return;
    }
    if (obj->transformFlags & TRANSFORM_ANIMATED) {
        float *sphere = obj->animState->animObj.boundSphere;
        MatrixMultiplyVector(&obj->transformMatrix, (_VECTOR *)sphere, &obj->centrePoint);
        obj->radius = (float)(AbsFchs(obj->scale) * sphere[3]);
        return;
    }
    sphere_equ_tag *sphere = &obj->objGraphics->boundSphere;
    MatrixMultiplyVector(&obj->transformMatrix, (_VECTOR *)sphere, &obj->centrePoint);
    if (obj->transformFlags & TRANSFORM_FIXED_RADIUS)
        return;
    obj->radius = (float)(AbsFchs(obj->scale) * sphere->r);
}

// A cel heads its objects' list, and its marks', as an object would: the first object's prevInCel is the cel
static_assert(offsetof(cel_tag, firstObject) == offsetof(obj_tag, nextInCel), "a cel's firstObject is an object's nextInCel");
static_assert(offsetof(cel_tag, headPrev) == offsetof(obj_tag, prevInCel), "a cel's headPrev is an object's prevInCel");
static_assert(offsetof(cel_tag, attachments) == offsetof(obj_tag, attachments), "a cel's marks are where an object's are");

// Links an object into a cel (at the head of its list), or with no cel into the room its position is in. An object
// whose ambient light is still the initial white takes its cel's.
// AUTOINJECT
void control_link_object_to_cel(obj_tag *obj, cel_tag *cel) {
    if (cel != NULL) {
        if (cel->firstObject != NULL)
            cel->firstObject->prevInCel = obj;
        obj->nextInCel = cel->firstObject;
        obj->prevInCel = (obj_tag *)cel;
        cel->firstObject = obj;
        cel->headPrev = NULL;
        obj->inCel = cel;
    } else {
        build_LinkToRoom(obj, 0, glb_world);
    }
    cel = obj->inCel;
    if (cel != NULL && obj->ambientR == 0xff && obj->ambientG == 0xff && obj->ambientB == 0xff) {
        uchar state = switch_channels[cel->ambientChannel];
        obj->ambientR = cel->ambientR[state];
        obj->ambientG = cel->ambientG[state];
        obj->ambientB = cel->ambientB[state];
    }
}

// Whether the object straddles cels: one that does goes into the forced list (unless effect flag 0x20 is set), one
// that no longer does comes out. One always forced is not tested, and the answer is true for it alone.
// AUTOINJECT
bool Controls_StraddleTest(obj_tag *obj) {
    if (obj->effectFlags & EFFECT_ALWAYS_FORCED) {
        obj->effectFlags |= EFFECT_STRADDLES_CELS;
        return true;
    }
    HITTEST_tag test = {};
    test.type = HITTEST_SPHERE;
    Vec_Copy(&obj->centrePoint, &test.pos);
    test.radius = obj->radius;
    test.cel = obj->inCel;
    ushort count;
    Collide_StraddleCels(&test, &count);
    if (count > 1) {
        if (!(obj->effectFlags & EFFECT_FORCED_LISTED) && !(obj->effectFlags & EFFECT_NO_COLLISION)) {
            LList_Add(&ForcedList, (LLNODE_tag *)obj);
            obj->effectFlags |= EFFECT_FORCED_LISTED;
        }
        obj->effectFlags |= EFFECT_STRADDLES_CELS;
        return false;
    }
    if (obj->effectFlags & EFFECT_FORCED_LISTED)
        LList_Remove(&ForcedList, (LLNODE_tag *)obj);
    obj->effectFlags &= ~(EFFECT_FORCED_LISTED | EFFECT_STRADDLES_CELS);
    obj->llPrev = NULL;
    obj->llNext = NULL;
    return false;
}

// After an object has moved: it follows the portals it crossed into another cel, or finds its room again. One left
// in no room is dealt with by its kind: most are put back in their old cel, inside its box by their radius.
// AUTOINJECT
void control_handle_cel_change(obj_tag *obj) {
    if (obj == NULL)
        return;
    obj->transformFlags &= ~TRANSFORM_MOVED;
    cel_tag *cel = obj->inCel;
    if (cel == NULL) {
        if (!build_LinkToRoom(obj, 0, glb_world) && obj->objectType == OBJECTTYPE_DRONE)
            Drone_FellOutMap(obj);
        return;
    }
    if (obj->parent != NULL)
        return;
    if (Vec_IsEqual(&obj->centrePoint, &obj->lastCentrePoint, 0.0f))
        return;
    if ((obj->effectFlags & EFFECT_IN_FORCED_LIST) || obj->radius > 0.1f) {
        if (Controls_StraddleTest(obj))
            return;
    }
    cel_tag *entered;
    if (obj->transformFlags & 0x80)
        entered = Cel_ObjectLeftCel(obj->inCel, &obj->lastCentrePoint, &obj->centrePoint);
    else
        entered = Cel_ObjectLeftCel(obj->inCel, &obj->lastPosition, &obj->position);
    if (entered != NULL) {
        control_unlink_object(obj);
        control_link_object_to_cel(obj, entered);
        return;
    }
    if (build_point_in_bounding_box(&obj->position, cel))
        return;
    control_unlink_object(obj);
    if (build_LinkToRoom(obj, 0, glb_world))
        return;
    switch (obj->objectType) {
    case OBJECTTYPE_CAR:
        Car_Deactivate(obj);
        return;
    case OBJECTTYPE_BULLET:
    case OBJECTTYPE_6:
    case OBJECTTYPE_EFFECT:
        obj->flags |= OBJFLAG_DELETE;
        return;
    case OBJECTTYPE_CASING:
        obj->curState = 2;
        View_SetDrawInNoViews(obj);
        return;
    case OBJECTTYPE_DRONE:
        Drone_FellOutMap(obj);
        if (MPSettings.dronesAreBots)
            return;
        // fall through
    default:
        if (!(obj->effectFlags & EFFECT_NO_COLLISION)) {
            // Back to the last position, then within the cel's box by the radius (each bound compared before it is
            // rounded to a float)
            float radius = obj->radius;
            Vec_Copy(&obj->lastPosition, &obj->position);
            ClampInBox(&obj->position.x, cel->boxMin.x, cel->boxMax.x, radius);
            ClampInBox(&obj->position.y, cel->boxMin.y, cel->boxMax.y, radius);
            ClampInBox(&obj->position.z, cel->boxMin.z, cel->boxMax.z, radius);
            Vec_Copy(&obj->position, Mat_Position(obj->transformMatrix));
            Vec_Copy(&obj->position, &obj->lastCentrePoint);
        }
        // fall through
    case OBJECTTYPE_ANIMDEBUGHANDLER:
    case OBJECTTYPE_DOOR:
    case OBJECTTYPE_SIMPLESCRIPT:
    case OBJECTTYPE_ANIMOBJECT:
        control_link_object_to_cel(obj, cel);
        return;
    }
}

// The rooms whose models change with a switch channel: a CELKIND_ROOM with a channel and a second model takes that
// model when its channel changes. The model is read before it is known there is one.
// FUNC_AT(0x0002d640)
void Control_Switches_Update(void) {
    for (cel_tag *cel = glb_world->firstCel; cel != NULL; cel = cel->nextCel) {
        uchar channel = cel->ambientChannel;
        if (channel == 0)
            continue;
        celglist_tag *next = cel->glist->lod;
        if (next != NULL && (cel->bitsFromPlacementTag & CELKIND_MASK) == CELKIND_ROOM &&
            switch_channels[channel] != switch_channels_hold[channel])
            cel->glist = next;
    }
}

// What a deleted object may still be pointed at by: bullets' shooters and targets, explosions' sources, and the hits
// in every object's hit list
static void control_ForgetObject(obj_tag *deleted) {
    for (obj_tag *obj = DynamicObjList.nextObject; obj != NULL; obj = obj->nextObject) {
        if (obj->objectType == OBJECTTYPE_BULLET) {
            BU_tag *bullet = (BU_tag *)obj->extraObjectData;
            if (bullet->firedByObj == deleted)
                bullet->firedByObj = NULL;
            if (bullet->heatseekTarget == deleted)
                bullet->heatseekTarget = NULL;
        } else if (obj->objectType == OBJECTTYPE_EXPLODE) {
            EXPLODE_tag *explode = (EXPLODE_tag *)obj->extraObjectData;
            if (explode->from == deleted)
                explode->from = NULL;
        }
        for (HITDATA_tag *hit = obj->hitList; hit != NULL; hit = hit->next) {
            if (hit->hitObj == deleted)
                hit->hitObj = NULL;
        }
    }
}

// An object moved this frame: its rotation within +-pi, its bounds, cel and light, and its last position
static void control_SettleMoved(obj_tag *obj) {
    if (obj->transformFlags & TRANSFORM_MOVED) {
        obj->rotation.x = normaliseToPlusMinusPi(obj->rotation.x);
        obj->rotation.y = normaliseToPlusMinusPi(obj->rotation.y);
        obj->rotation.z = normaliseToPlusMinusPi(obj->rotation.z);
        Control_BuildWorldSph(obj);
        control_handle_cel_change(obj);
        Lights_CalcAmbientLight(obj);
        Vec_Copy(&obj->position, &obj->lastPosition);
        if (!(obj->transformFlags & (TRANSFORM_MATRIX_PLACED | TRANSFORM_KEEP_ROTATION)))
            Vec_Copy(&obj->rotation, &obj->lastRotation);
    }
    if (obj->transformFlags & TRANSFORM_POSITION_FROM_MATRIX)
        Vec_Copy(Mat_Position(obj->transformMatrix), &obj->position);
}

// A frame of the objects: doors, triggers and switched rooms; each object's update; the deletion of those marked
// (until a pass deletes none); the moved settled; collision, and the moved settled again; and objects carried by
// another follow it into its cel.
// AUTOINJECT
void control_movement_object_handler(char) {
    Drone_InitComms();
    MP_DebugDraw();
    Doors_Calc();
    Trigger_Calc();
    Control_Switches_Update();
    memcpy(switch_channels_prev, switch_channels_hold, sizeof(switch_channels_prev));
    memcpy(switch_channels_hold, switch_channels, sizeof(switch_channels_hold));

    for (obj_tag *obj = DynamicObjList.nextObject, *next; obj != NULL; obj = next) {
        next = obj->nextObject;
        if (obj->transformFlags & (TRANSFORM_MATRIX_PLACED | TRANSFORM_KEEP_ROTATION))
            Mat_GetDir(&obj->lastRotation, &obj->transformMatrix);
        ObjectUpdateFunc update = control_funcs[(uchar)obj->objectType].updateFunc;
        if (update != NULL)
            update(obj);
    }

    bool deleted;
    do {
        deleted = false;
        for (obj_tag *obj = DynamicObjList.nextObject, *next; obj != NULL; obj = next) {
            next = obj->nextObject;
            if (obj->flags & OBJFLAG_DELETE) {
                bool referenced = (obj->transformFlags & TRANSFORM_REFERENCED) != 0;
                Effect_RemoveSObjs(obj);
                control_delete_object(obj);
                if (referenced)
                    control_ForgetObject(obj);
                deleted = true;
            } else if (obj->flags & OBJFLAG_NEW) {
                Control_BuildWorldSph(obj);
                Lights_CalcAmbientLight(obj);
                obj->flags &= ~OBJFLAG_NEW;
            }
        }
    } while (deleted);

    for (obj_tag *obj = DynamicObjList.nextObject; obj != NULL; obj = obj->nextObject)
        control_SettleMoved(obj);
    Collide_Update(DynamicObjList.nextObject);
    for (obj_tag *obj = DynamicObjList.nextObject; obj != NULL; obj = obj->nextObject)
        control_SettleMoved(obj);

    for (obj_tag *obj = DynamicObjList.nextObject; obj != NULL; obj = obj->nextObject) {
        if (obj->drawnCountdown != 0)
            obj->drawnCountdown--;
        if (obj->animState != NULL && GameState.InhibitGameDraw == 0)
            memset(obj->animState->animObj.lod, 0xff, sizeof(obj->animState->animObj.lod));
        obj_tag *carrier = obj->parent;
        if (carrier != NULL && obj->inCel != carrier->inCel) {
            control_unlink_object(obj);
            control_link_object_to_cel(obj, carrier->inCel);
            Control_BuildWorldSph(obj);
        }
    }
}

// The next model down, if the object's model has one
// AUTOINJECT
bool Control_NextLOD(obj_tag *obj) {
    if (obj == NULL || obj->objGraphics == NULL || obj->objGraphics->lod == NULL)
        return false;
    obj->objGraphics = obj->objGraphics->lod;
    return true;
}

// Marks an object as one others may point at, so that deleting it clears their pointers
// AUTOINJECT
void Control_ObjectReferenced(obj_tag *obj, char referenced) {
    if (referenced)
        obj->transformFlags |= TRANSFORM_REFERENCED;
    else
        obj->transformFlags &= ~TRANSFORM_REFERENCED;
}

// An object made with its model, carrier, transform flags, scale, effect flags and tweak colour; a matrix given
// replaces the one made from the rotation, and gives the position.
// AUTOINJECT
obj_tag* Control_CreateObjEx(ushort size, _VECTOR *pos, _VECTOR *rot, _MATRIX *mtx, celglist_tag *glist,
                             obj_tag *carrier, char linkToRoom, ushort transformFlags, float scale, ushort effectFlags,
                             uchar tweakR, uchar tweakG, uchar tweakB) {
    obj_tag *obj = control_create_object(size, pos, rot, NULL);
    if (obj == NULL)
        return NULL;
    if (mtx != NULL) {
        Mat_Copy(mtx, &obj->transformMatrix);
        Vec_Copy(Mat_Position(obj->transformMatrix), &obj->position);
    }
    obj->transformFlags |= transformFlags;
    obj->scale = scale;
    obj->parent = carrier;
    obj->objGraphics = glist;
    obj->effectFlags = effectFlags | (glist != NULL ? glist->applyFlagsToObject : 0);
    obj->tweakR = tweakR;
    obj->tweakG = tweakG;
    obj->tweakB = tweakB;
    if (linkToRoom)
        build_LinkToRoom(obj, 0, glb_world);
    return obj;
}

// Kept from one call to the next, as in the original
// XBE_GLOBAL(0x001df8a4, 0xc)
static _VECTOR InheritOldR;
// XBE_GLOBAL(0x001df898, 0xc)
static _VECTOR InheritNewR;
// XBE_GLOBAL(0x001df88c, 0xc)
static _VECTOR InheritPt1;
// XBE_GLOBAL(0x001df880, 0xc)
static _VECTOR InheritDelta;
// XBE_GLOBAL(0x001df874, 0xc)
static _VECTOR InheritDeltaV;
// XBE_GLOBAL(0x001df868, 0xc)
static _VECTOR DeltaR;
// XBE_GLOBAL(0x001df82c, 0x3c)
static _MATRIX M;

// An object standing on another moves as it moved this frame, and turns about it as it turned (in pitch and yaw
// from its direction when its matrix is its own, else in yaw); the object's matrix turns with it when its own too.
// The last argument is not read.
// AUTOINJECT
void Control_InheritVelocity(obj_tag *obj, obj_tag *from, float) {
    Vec_Zero(&InheritOldR);
    Vec_Zero(&InheritNewR);
    Vec_Subtract(&obj->position, &obj->lastPosition, &InheritPt1);
    Vec_Subtract(&from->position, &from->lastPosition, &InheritDelta);
    Vec_Subtract(&InheritPt1, &InheritDelta, &InheritDeltaV);
    _VECTOR *at = (obj->transformFlags & TRANSFORM_MATRIX_PLACED) ? Mat_Position(obj->transformMatrix) : &obj->position;
    Vec_Add2(&InheritDelta, at, at);

    if (from->transformFlags & (TRANSFORM_MATRIX_PLACED | TRANSFORM_KEEP_ROTATION)) {
        _VECTOR dir;
        Mat_GetDir(&dir, &from->transformMatrix);
        _VECTOR *old = &from->lastRotation;
        InheritOldR.x = (float)FPatan(old->y, sqrt((double)old->x * old->x + (double)old->z * old->z));
        InheritOldR.y = (float)FPatan(old->x, old->z);
        InheritNewR.x = (float)FPatan(dir.y, sqrt((double)dir.z * dir.z + (double)dir.x * dir.x));
        InheritNewR.y = (float)FPatan(dir.x, dir.z);
    } else {
        InheritOldR.y = from->lastRotation.y;
        InheritNewR.y = from->rotation.y;
    }
    Vec_Subtract(&InheritOldR, &InheritNewR, &DeltaR);
    if (Vec_IsEqual(&DeltaR, &CONST_ZERO_VECTOR, 0.0002f))
        return;

    Vec_Subtract(Mat_Position(obj->transformMatrix), Mat_Position(from->transformMatrix), &InheritPt1);
    RotMatrix(&DeltaR, &M);
    ApplyMatrixLV(&M, &InheritPt1, &InheritDeltaV);
    Vec_Subtract(&InheritPt1, &InheritDeltaV, &InheritPt1);
    at = (obj->transformFlags & TRANSFORM_MATRIX_PLACED) ? Mat_Position(obj->transformMatrix) : &obj->position;
    Vec_Add2(at, &InheritPt1, at);
    obj->rotation.y = obj->rotation.y - DeltaR.y;
    if (obj->transformFlags & (TRANSFORM_MATRIX_PLACED | TRANSFORM_KEEP_ROTATION)) {
        Mat_InverseSelf(&M);
        Concat(&M, &obj->transformMatrix);
    }
}

// The player (or bot) an object is, 0 to 3, or -1. In multiplayer the index counts only if that player's object is
// this one. A manned gun emplacement (OBJECTTYPE_GUNTURRET2) answers its sub-state.
// AUTOINJECT
short Control_Plr2Ind(obj_tag *obj) {
    if (obj == NULL)
        return -1;
    short index;
    switch (obj->objectType) {
    case OBJECTTYPE_PLAYER:
    case OBJECTTYPE_DEAD_PLAYER:
        index = ((BLData *)obj->extraObjectData)->playerNum;
        break;
    case OBJECTTYPE_DRONE:
    case OBJECTTYPE_DEAD_DRONE:
        index = ((Drone_tag *)obj->extraObjectData)->botVars->playerIndex;
        break;
    case OBJECTTYPE_GUNTURRET2:
        if (obj->curState != GUNIMP_MANNED)
            return -1;
        return obj->subState;
    default:
        return -1;
    }
    if (MPSettings.dronesAreBots && MPGame.players[index].playerObj != obj)
        return -1;
    return index;
}
