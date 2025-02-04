#ifndef OBJECT_H_
#define OBJECT_H_

#include "../../math/math.h"

#include <stddef.h>

typedef enum {
    // TODO: Complete this
    CAR = 0x28,

} ObjectType;

// A base object consists of the minimum functionality required to be updated, rendered, collided with, destroyed, etc.
// All specific object types will consist of this, plus additional data specific to that object type (pointed to by extraObjectData)
#pragma pack(push, 1)

// Forward declaration of obj_tag so that it can be used within the struct
struct obj_tag;

typedef struct obj_tag {
    // TODO: Complete this
    char _pad_1[0x14];
    obj_tag *nextObject;
    char _pad_11111[0xc];
    _VECTOR position;
    char _pad_2[0x40];
    _MATRIX transformMatrix; // 0x70 - 0xAC
    char _pad_9999[4];
    void* maybeCollision; // 0xB0
    char _pad_3[0x8];
    void* extraObjectData; // 0xBC
    char _pad_4[0xc];
    int someFlags_0xcc; // 0xCC, unclear what the meaning is but sometimes relevant for rendering or object state or straddle tests?
    unsigned short curState; // 0xD0
    unsigned short playerNum; // 0xD2
    char _pad_5[0x4];
    char _pad_6[0x3];
    ObjectType objectType;
} obj_tag;
#pragma pack(pop)

//char (*__kaboom)[offsetof(obj_tag,objectType)] = 1;
static_assert(offsetof(obj_tag, position) == 0x24, "Offset of position not correct");
static_assert(offsetof(obj_tag, transformMatrix) == 0x70, "Offset of transformMatrix not correct");
static_assert(offsetof(obj_tag, maybeCollision) == 0xb0, "Offset of maybeCollision not correct");
static_assert(offsetof(obj_tag, extraObjectData) == 0xbc, "Offset of extraObjectData not correct");
static_assert(offsetof(obj_tag, someFlags_0xcc) == 0xcc, "Offset of someFlags_0xcc not correct");
static_assert(offsetof(obj_tag, objectType) == 0xdb, "Offset of objectType not correct");

#endif // OBJECT_H_