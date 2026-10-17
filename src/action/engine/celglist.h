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

// How a leaf box's vertices are stored: shorts times scale, plus offset
typedef struct COLLQUANT_tag {
    _VECTOR offset;
    float scale;
} COLLQUANT_tag;

// A box of a cel's collision tree (Ghidra: COLLBOX_tag). A box with childA negative is a leaf: its triangles are
// childB to triEnd, their vertices in one 64-byte block, as shorts times scale plus offset.
typedef struct COLLBOX_tag {
    _VECTOR boundMin;       // 0x00
    short childA;           // 0x0c - index in the box array; negative for a leaf
    short childB;           // 0x0e - a leaf's first triangle
    _VECTOR boundMax;       // 0x10
    short triEnd;           // 0x1c - a leaf: one past its last triangle
    ushort vertexBlock;     // 0x1e - a leaf: its vertices' block
    COLLQUANT_tag quant;    // 0x20
} COLLBOX_tag;
static_assert(sizeof(COLLBOX_tag) == 0x30, "COLLBOX_tag is wrong size");
static_assert(offsetof(COLLBOX_tag, boundMax) == 0x10, "Bad offset of boundMax");
static_assert(offsetof(COLLBOX_tag, quant) == 0x20, "Bad offset of quant");

// A triangle: three vertices and its plane, as offsets in shorts into its leaf's vertex block
typedef struct collDataB { ushort v[3]; ushort normal; } collDataB;
static_assert(sizeof(collDataB) == 8, "collDataB is 8 bytes");
// A vertex block: vertices (three shorts) and planes (three shorts of normal times 2^-14, then the float d)
typedef struct collDataC { uchar raw[64]; } collDataC;

// A cel's collision data: pointers into the map file's collision block (Ghidra: COLLDATA_tag). Allocated as 0x18
// bytes by parsemap_block_Coll_Data_New.
typedef struct COLLDATA_tag {
    COLLBOX_tag *collBoxes;     // 0x00
    collDataB *dataStartB;      // 0x04 - the triangles
    uchar *dataStartD;          // 0x08 - the triangles' materials (HITDATA_tag::materialType), one byte each
    collDataC *dataStartC;      // 0x0c - the vertex blocks
    short sizeofC;              // 0x10 - bytes, countC * 64, truncated to 16 bits
    short countB;               // 0x12 - triangles
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