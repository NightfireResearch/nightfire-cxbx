#ifndef CONTROL_H_
#define CONTROL_H_

#include "../../actionhelpers.h"
#include "../../../common/xbeClass.h"

#include "object.h"

obj_tag* Control_ReturnNextObjectOfType(ObjectType type, obj_tag* from);
void Control_DeleteAllObjectsOfType(ObjectType type);
obj_tag* control_first_object(void);
void control_delete_object(obj_tag* obj);
void control_init_object(obj_tag* obj);
obj_tag* control_create_object(int sizeBytes,_VECTOR *pos,_VECTOR *rot,quaternion_tag *quat);
// control_create_object of extra data that is xboxSizeBytes on the Xbox (XboxHeap.h; XBOX_SIZEOF)
#if XBOX_SIZES_DIFFER
obj_tag* control_create_object_xbox(int sizeBytes, int xboxSizeBytes, _VECTOR *pos, _VECTOR *rot,
                                    quaternion_tag *quat);
#else
#define control_create_object_xbox(sizeBytes, xboxSizeBytes, pos, rot, quat) control_create_object(sizeBytes, pos, rot, quat)
#endif
void Control_InheritVelocity(obj_tag *obj, obj_tag *from, float unused);
void control_movement_object_handler(char);
obj_tag* Control_CreateObjEx(ushort size, _VECTOR *pos, _VECTOR *rot, _MATRIX *mtx, celglist_tag *glist, obj_tag *carrier,
                             char linkToRoom, ushort transformFlags, float scale, ushort effectFlags, uchar tweakR,
                             uchar tweakG, uchar tweakB);
void Control_SetGList(obj_tag *obj, celglist_tag *celgl);;
float control_ClosestPlayerPnt(_VECTOR *pos, _VECTOR *out);
void control_unlink_object(obj_tag* obj);
bool Control_NextLOD(obj_tag *);
bool Controls_StraddleTest(obj_tag *obj);
void Control_SetEffectType(obj_tag *obj, uint effectType);
void control_link_object_to_cel(obj_tag *obj, cel_tag *cel);
void Control_GetObjMatrix(obj_tag *obj, _MATRIX *mtx);
void Control_BuildWorldSph(obj_tag *obj);
void control_handle_cel_change(obj_tag *obj);
void maybeControl_Switches_Update(void);
void Control_ObjectReferenced(obj_tag *obj, char referenced);
short Control_Plr2Ind(obj_tag *obj);

// Each object type has 3 optional functions which are called by the control system
// These are Update, Collide and Delete.
// They each take a pointer to the object as their only parameter, and return nothing
typedef void (*ObjectUpdateFunc)(obj_tag*);
typedef void (*ObjectCollideFunc)(obj_tag*);
typedef void (*ObjectDeleteFunc)(obj_tag*);

// These are all stored in a table "control_funcs"
// This table is indedexed by ObjectType.
typedef struct {
    ObjectUpdateFunc updateFunc;
    ObjectCollideFunc collideFunc;
    ObjectDeleteFunc deleteFunc;
} ControlFunction;

#define OBJECTTYPE_FUNCS 85
extern ControlFunction control_funcs[OBJECTTYPE_FUNCS];   // ControlFuncs.cpp


void control_init_object_lists(void);

#endif // CONTROL_H_