#ifndef CONTROL_H_
#define CONTROL_H_

#include "../../actionhelpers.h"

#include "object.h"

obj_tag* Control_ReturnNextObjectOfType(ObjectType type, obj_tag* from);
void Control_DeleteAllObjectsOfType(ObjectType type);
obj_tag* control_first_object(void);
void control_delete_object(obj_tag* obj);
void control_init_object(obj_tag* obj);
obj_tag* control_create_object(int sizeBytes,_VECTOR *pos,_VECTOR *rot,quaternion_tag *quat);
void control_movement_object_handler(char);
obj_tag * Control_CreateObjEx(unsigned short, _VECTOR *, _VECTOR *, _MATRIX *, celglist_tag *, obj_tag *,char,unsigned short,float,unsigned short,unsigned char,unsigned char,unsigned char);
void Control_SetGList(obj_tag *obj, celglist_tag *celgl);;
void control_unlink_object(obj_tag* obj);
bool Control_NextLOD(obj_tag *);
bool Controls_StraddleTest(obj_tag *param_1);

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

#define control_funcs ((ControlFunction*)0x00163b98)


#endif // CONTROL_H_