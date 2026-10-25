#ifndef LEAFGEN_H_
#define LEAFGEN_H_

#include "../../actionhelpers.h"

#pragma pack(push, 1)

// A falling leaf's extra data (OBJECTTYPE_LEAF)
typedef struct LEAF_tag {
    char _unknown00[4];
    _VECTOR vel;                // 0x04 - a frame
    _VECTOR spin;               // 0x10 - a frame
    short life;                 // 0x1c - frames left
    uchar fade;                 // 0x1e - taken off its alpha a frame; Leaf_Create leaves it 0
    char _unknown1f;
} LEAF_tag;

static_assert(sizeof(LEAF_tag) == 0x20, "LEAF_tag is 0x20 bytes");
static_assert(offsetof(LEAF_tag, life) == 0x1c, "Bad offset of LEAF_tag.life");

#pragma pack(pop)

obj_tag* LeafGen_Create(_VECTOR *pos, _VECTOR *rot, level_tag* lvl, celglist_tag *celgl);
void LeafGen_Update(obj_tag *obj);
void Leaf_Create(obj_tag *gen, ushort count);
void Leaf_Update(obj_tag *obj);

#endif // LEAFGEN_H_
