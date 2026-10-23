#ifndef BREAK_H
#define BREAK_H

#include "../../actionhelpers.h"
#include "../../../common/xbeClass.h"

struct TriListVertex;

typedef enum {
    BreakType_Window = 2,       // shatters into shards; anything else breaks into debris
    BreakType_FORCE_U32 = 0x7FFFFFFF
} BreakType;

#pragma pack(push, 1)

// A breakable object's extra data (OBJECTTYPE_BREAK, 0xc bytes)
typedef struct {
    BreakType breakType;        // 0x00 - key 0
    float health;               // 0x04 - key 1; broken below 0
    ushort mask;                // 0x08 - the damage classes it ignores: the complement of key 2 (0 for none)
    ushort _pad0a;
} ObjData_Break;

static_assert(sizeof(ObjData_Break) == 0xc, "Size of ObjData_Break is wrong");

// A breakable's placement keys (level_tag)
typedef struct {
    char _unknown00[0x2c];
    uint breakType;             // 0x2c
    uint health;                // 0x30
    uint mask;                  // 0x34 - the damage classes that break it, 0 for all
} BreakPlacement;

static_assert(offsetof(BreakPlacement, mask) == 0x34, "Bad offset of BreakPlacement.mask");

// A piece of broken glass on ShardList (0x84 bytes): it waits, then flies, spins and falls until its life is used up
typedef struct SHARD_tag {
    LLNODE_tag node;            // 0x00
    void *vertices;             // 0x08 - psiCreateShard's
    uint texture;               // 0x0c
    float life;                 // 0x10 - goes down by REC_FRAME_RATE an update once it flies
    float delay;                // 0x14 - goes down by REC_FRAME_RATE an update before it flies
    _VECTOR pos;                // 0x18
    _VECTOR rot;                // 0x24
    _VECTOR vel;                // 0x30
    _VECTOR spin;               // 0x3c
    char _unknown48[0x84 - 0x48];
} SHARD_tag;

LAYOUT_32(sizeof(SHARD_tag) == 0x84, "SHARD_tag is 0x84 bytes");

#pragma pack(pop)

void InitShards(void);
void Break_Update(obj_tag *obj);
void Break_ApplyDamage(obj_tag *obj, HITDATA_tag *hit);
void Break_DoBreak(_VECTOR *pos);
void Break_Kill(obj_tag *obj, HITDATA_tag *hit);
void Break_ShatterWindow(obj_tag *obj, HITDATA_tag *hit);
void ShatterPolyRecurse(int depth, TriListVertex *verts, obj_tag *obj, HITDATA_tag *hit, uint texture);
void UpdateAllShards(void);
void DrawAllShards(void);
void Debris_Create_Loop(COLLDATA_tag *coll, HITDATA_tag *hit, obj_tag *obj);
obj_tag* Break_Create(_VECTOR *pos, _VECTOR *rot, celglist_tag *celgl, level_tag *lvl);

#endif // BREAK_H
