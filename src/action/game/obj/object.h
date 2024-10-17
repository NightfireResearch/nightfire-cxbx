#ifndef OBJECT_H_
#define OBJECT_H_

#include "../../math/math.h"

typedef enum {
    // TODO: Complete this
    CAR = 0x28,

} ObjectType;

// A base object consists of the minimum functionality required to be updated, rendered, collided with, destroyed, etc.
// All specific object types will consist of this, plus additional data specific to that object type (pointed to by extraObjectData)
typedef struct {
    // TODO: Complete this
    int bla;
    _MATRIX transformMatrix;
    ObjectType objectType;
    void* extraObjectData;
} obj_tag;


#endif // OBJECT_H_