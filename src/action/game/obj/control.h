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