#ifndef CELGLIST_H
#define CELGLIST_H

#include "../actionhelpers.h"

#pragma pack(push, 1)

typedef struct {
    float x;
    float y;
    float z;
    float r;
} sphere_equ_tag;

typedef struct celglist_tag {
    char* name;
    void* gfx_struct;
    void* colldata;
    sphere_equ_tag boundSphere;
    _VECTOR extentMin;
    _VECTOR extentMax;
    uint32_t lodRelated;
    uint32_t applyFlagsToObject;
} celglist_tag;

static_assert(sizeof(celglist_tag) == 0x3c, "celglist_tag is wrong size");

#pragma pack(pop)

#endif // CELGLIST_H