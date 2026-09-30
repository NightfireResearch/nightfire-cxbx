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

// A collision box (Ghidra: COLLBOX_tag). Only the bounds are used so far.
typedef struct COLLBOX_tag {
    _VECTOR boundMin;       // 0x00
    short childA;           // 0x0c
    short childB;           // 0x0e
    _VECTOR boundMax;       // 0x10
    short maxIndexOfB;      // 0x1c
    ushort maybeWhichC;     // 0x1e
    float someX;            // 0x20
    float someY;            // 0x24
    float someZ;            // 0x28
    float someMaybeScale;   // 0x2c
} COLLBOX_tag;
static_assert(sizeof(COLLBOX_tag) == 0x30, "COLLBOX_tag is wrong size");
static_assert(offsetof(COLLBOX_tag, boundMax) == 0x10, "Bad offset of boundMax");

// Contents unknown; only the sizes are known (from the block layout and Ghidra)
typedef struct collDataB { uchar raw[8]; } collDataB;
typedef struct collDataC { uchar raw[64]; } collDataC;

// A cel's collision data: pointers into the map file's collision block (Ghidra: COLLDATA_tag). Allocated as 0x18
// bytes by parsemap_block_Coll_Data_New.
typedef struct COLLDATA_tag {
    COLLBOX_tag *collBoxes;     // 0x00
    collDataB *dataStartB;      // 0x04
    void *dataStartD;           // 0x08
    collDataC *dataStartC;      // 0x0c
    short sizeofC;              // 0x10 - bytes, countC * 64, truncated to 16 bits
    short countB;               // 0x12
    ushort numCollBoxes;        // 0x14
} COLLDATA_tag;
static_assert(sizeof(COLLDATA_tag) == 0x16, "COLLDATA_tag is wrong size");
static_assert(offsetof(COLLDATA_tag, sizeofC) == 0x10, "Bad offset of sizeofC");
static_assert(offsetof(COLLDATA_tag, numCollBoxes) == 0x14, "Bad offset of numCollBoxes");

typedef struct celglist_tag {
    char* name;
    int geom_idx;
    COLLDATA_tag* colldata;
    sphere_equ_tag boundSphere;
    _VECTOR extentMin;
    _VECTOR extentMax;
    uint32_t lodRelated;
    uint32_t applyFlagsToObject;
} celglist_tag;

static_assert(sizeof(celglist_tag) == 0x3c, "celglist_tag is wrong size");

#pragma pack(pop)

#endif // CELGLIST_H