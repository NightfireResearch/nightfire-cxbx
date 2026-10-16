// actionhelpers.h first, outside the guard: it includes this file after AINetwork.h, whose AIEmitter_tag the records
// below need, so a file that includes this one first must still get that order
#include "../../actionhelpers.h"

#ifndef DRONE_AIHINTS_H_
#define DRONE_AIHINTS_H_

// The AI hints a level places for the drones: cover nodes, AI points and AI volumes, and the door and kick marks on
// the AI paths' nodes.

#pragma pack(push, 1)

// The placements' keys (level_tag + 0x2c; names from Ghidra's level_tag_Drone* types)
typedef struct CoverCornerPlacement {
    char base[0x2c];
    uint sideFlags;             // 0x2c - 1 or 2; anything else is both
    uint standFlags;            // 0x30 - the same
    uint crouchFlags;           // 0x34 - the same
    uint coverAngleDeg;         // 0x38 - 0 for 45
    uint enableSwitchChannel;   // 0x3c
    uint disableSwitchChannel;  // 0x40
    uint unknown6;              // 0x44 - 0 for 8
    uint unknown7;              // 0x48 - 0 for 8
    char _unknown4c[0xb0 - 0x4c];
} CoverCornerPlacement;

typedef struct CoverLowPlacement {
    char base[0x2c];
    uint kind;                  // 0x2c - 2: the anims come from anim1 and anim2
    uint anim1;                 // 0x30 - index into Drone_CoverLowNode's table
    uint anim2;                 // 0x34
    uint flag2;                 // 0x38
    uint enableSwitchChannel;   // 0x3c
    uint disableSwitchChannel;  // 0x40
    char _unknown44[0xb0 - 0x44];
} CoverLowPlacement;

typedef struct AIPointPlacement {
    char base[0x2c];
    ushort pointId;             // 0x2c
    ushort _unknown2e;
    uint radius;                // 0x30
    uint group;                 // 0x34
    char _unknown38[0xb0 - 0x38];
} AIPointPlacement;

typedef struct AIVolumePlacement {
    char base[0x2c];
    uint visibilityPercent;     // 0x2c
    uint volumeType;            // 0x30
    uint unknown2;              // 0x34
    uint unknown3;              // 0x38 - its low byte is kept
    uint unknown4;              // 0x3c - its low 16 bits are kept
    char _unknown40[0xb0 - 0x40];
} AIVolumePlacement;

typedef enum {
    COVER_CORNER = 0,
    COVER_LOW = 1,
} CoverNodeType;

// CoverNode_tag.flags. Corner nodes take one or both of each pair from the placement's keys.
#define COVERNODE_LOW_ANIMS     0x1     // low: the anims come from the placement
#define COVERNODE_LOW_FLAG2     0x2     // low: the placement's flag2 was 1
#define COVERNODE_SIDE2         0x4     // sideFlags 2
#define COVERNODE_SIDE1         0x8     // sideFlags 1
#define COVERNODE_WIDER         0x10    // Drone_IsCoverNodeUsable allows wider angles
#define COVERNODE_STAND1        0x20
#define COVERNODE_STAND2        0x40
#define COVERNODE_CROUCH1       0x80    // tested on one side, against angles near +-pi
#define COVERNODE_CROUCH2       0x100   // and on the other, against angles near pi/2

// CoverNode_tag.usable: what Drone_IsCoverNodeUsable found for a target
#define COVERUSE_CROUCH2        0x2
#define COVERUSE_CROUCH1        0x4
#define COVERUSE_LOW            0x48
#define COVERUSE_CROUCH2_INNER  0x10    // within pi/2
#define COVERUSE_CROUCH1_INNER  0x20    // above -pi
#define COVERUSE_INNER_MASK     0x70    // cleared when the target can see the node
#define COVERUSE_TOO_CLOSE      0x80    // the target is within 4 m
#define COVERUSE_OUTSIDE_ANGLE  0x200
#define COVERUSE_SEEN           0x400

// A cover node (NPCGlobals.coverNodes)
typedef struct CoverNode_tag {
    uchar type;                     // 0x00 - CoverNodeType
    uchar enableSwitchChannel;      // 0x01 - usable only while this channel is on, if not 0
    uchar disableSwitchChannel;     // 0x02 - not usable while this one is on, if not 0
    uchar lowAnim1;                 // 0x03 - low nodes
    uchar lowAnim2;                 // 0x04
    uchar unknown5;                 // 0x05 - corner nodes
    uchar unknown6;                 // 0x06
    uchar _pad07;
    float angle;                    // 0x08 - corner nodes: radians
    uint flags;                     // 0x0c - COVERNODE_*
    cel_tag *cel;                   // 0x10
    obj_tag *occupant;              // 0x14 - a drone here counts only for its own target
    _VECTOR pos;                    // 0x18
    _VECTOR rot;                    // 0x24
    AIEmitter_tag emitter;          // 0x30
    uint usable[8];                 // 0x58 - COVERUSE_*, per NPCGlobals.targets entry
} CoverNode_tag;
static_assert(sizeof(CoverNode_tag) == 0x78, "CoverNode_tag is 0x78 bytes");
static_assert(offsetof(CoverNode_tag, cel) == 0x10, "Wrong offset for CoverNode_tag.cel");
static_assert(offsetof(CoverNode_tag, emitter) == 0x30, "Wrong offset for CoverNode_tag.emitter");
static_assert(offsetof(CoverNode_tag, usable) == 0x58, "Wrong offset for CoverNode_tag.usable");

// An AI point (NPCGlobals.aiPoints). Its first 0x28 bytes have AIPoint_tag's layout (AINetwork.h).
typedef struct DroneAIPoint_tag {
    uint _unknown0;
    uchar active;                   // 0x04
    uchar _pad05;
    ushort id;                      // 0x06
    ushort unknown08;               // 0x08
    ushort _pad0a;
    uint group;                     // 0x0c
    float distance;                 // 0x10
    float radius;                   // 0x14
    _VECTOR pos;                    // 0x18
    cel_tag *cel;                   // 0x24
    _VECTOR rot;                    // 0x28
    AIEmitter_tag emitter;          // 0x34
} DroneAIPoint_tag;
static_assert(sizeof(DroneAIPoint_tag) == 0x5c, "DroneAIPoint_tag is 0x5c bytes");
static_assert(offsetof(DroneAIPoint_tag, cel) == 0x24, "Wrong offset for DroneAIPoint_tag.cel");
static_assert(offsetof(DroneAIPoint_tag, emitter) == 0x34, "Wrong offset for DroneAIPoint_tag.emitter");

// The tables Drone_PreLoad_Init points NPCGlobals at
#define MAX_COVER_NODES 256
#define MAX_AI_POINTS 256

// An AI volume: an object's data (OBJECTTYPE_DRONE_AIVOLUME), on AIBoxList
typedef struct DroneAIVolume_tag {
    LLNODE_tag node;                // 0x00
    obj_tag *obj;                   // 0x08
    _VECTOR halfSize;               // 0x0c - the model's box, scaled
    float visibility;               // 0x18 - the placement's percentage / 100
    ushort type;                    // 0x1c - what Drone_PointInAnyAIBox looks for
    ushort unknown1e;               // 0x1e
    uint unknown20;                 // 0x20
    uchar unknown24;                // 0x24
    uchar _unknown25[0xf4 - 0x25];
} DroneAIVolume_tag;
static_assert(sizeof(DroneAIVolume_tag) == 0xf4, "DroneAIVolume_tag is 0xf4 bytes");
static_assert(offsetof(DroneAIVolume_tag, type) == 0x1c, "Wrong offset for DroneAIVolume_tag.type");

#pragma pack(pop)

// XBE_GLOBAL(0x001dfa1c, 0xc)
#define AIBoxList (*(LLISTINFO_tag *)0x001dfa1c)

void Drone_CoverCornerNode(_VECTOR *pos, _VECTOR *rot, level_tag *lvl);
void Drone_CoverLowNode(_VECTOR *pos, _VECTOR *rot, level_tag *lvl);
void Drone_AIPoint(_VECTOR *pos, _VECTOR *rot, level_tag *lvl);
obj_tag* Drone_AIVolume_Create(_VECTOR *pos, _VECTOR *rot, _VECTOR *scale, level_tag *lvl, celglist_tag *glist);
void Drone_AIVolume_Delete(obj_tag *obj);
float Drone_VisibilityForPosition(_VECTOR *pos, ushort type);
bool Drone_PointInAnyAIBox(_VECTOR *pos, ushort type);
// The original takes the node in ESI and the target in EDI: the game reaches the adaptor, C++ calls the core
bool _Drone_IsCoverNodeUsable(uint target, CoverNode_tag *node);
void Drone_IsCoverNodeUsable(void);
void Drone_ProcessCoverNodes(void);
void NDrone_InitDoorNodes(void);
void NDrone_InitKickNodes(void);

#endif // DRONE_AIHINTS_H_
