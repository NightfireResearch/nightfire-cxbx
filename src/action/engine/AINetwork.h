// actionhelpers.h first, outside the guard: it includes this file and then drone/Drone.h, which needs these types,
// so a file that includes this one first must not get Drone.h before them
#include "../actionhelpers.h"

#ifndef AINETWORK_H_
#define AINETWORK_H_

// The AI navigation data shared by drones and bots. See docs/drone/bots-and-navigation/README.md, section 3.

#pragma pack(push, 1)

typedef struct {
    _VECTOR pos;
    cel_tag *cel;
} CelPos_tag;

// A node of a path at run time (AIPath_tag.runtimeNodes: 0x30 each for paths; bounds use 8-byte nodes instead)
typedef struct AINode_tag {
    struct AINode_tag *nextInCel;   // 0x00 - list head at cel+0x24
    uchar *fileNode;                // 0x04 - index at +0, flags at +4 (target and list bits), cel at +0xc, pos at +0x10
    struct AINode_tag *nextSorted;  // 0x08 - the sorted candidate list (NodeSearchCel)
    float searchDistance;           // 0x0c - 2D distance to the search origin
    uchar parentLink;               // 0x10 - which link of the parent led here
    uchar linkCount;                // 0x11
    ushort firstLink;               // 0x12 - first entry in the path's node->link list
    float g;                        // 0x14 - cost so far (the emitter starts it at 255)
    float h;                        // 0x18 - 2D distance to the destination
    float f;                        // 0x1c - g + h, the open list's sort key
    struct AINode_tag *parent;      // 0x20
    struct AINode_tag *nextInList;  // 0x24 - open or closed list
    obj_tag *door;                  // 0x28 - a link between two door nodes is closed while Door_IsLocked
    char _pad2c[4];
} AINode_tag;

// An AI path or bound (the tables at 0x00274ccc and 0x00275ae0, 50 each; bounds use the first 0x40)
typedef struct AIPath_tag {
    ushort index;           // 0x00
    ushort _pad02;
    uchar *record;          // 0x04 - the file record (its name at +0)
    uint flags;             // 0x08 - 1 bound, 2 nav graph, 4 patrol loop, 8 one-way mission path, 0x10 patrol modifier
    uint fromFile[8];       // 0x0c - copied from the file; always 0
    uint nodeCount;         // 0x2c - also an emitter's size
    uchar *firstFileNode;   // 0x30
    void *runtimeNodes;     // 0x34 - AINode_tag[nodeCount] for paths
    uint linkCount;         // 0x38
    uchar *firstLink;       // 0x3c - in the file
    ushort *nodeLinks;      // 0x40 - node -> link index lists (AIPath_Prepare, 2 x linkCount); paths only
    uint reachMask;         // 0x44 - target bits (set by NodesForPosition, tested by DoAStarPath); paths only
} AIPath_tag;

// An AI bound (a walkable area's outline): the first 0x40 bytes of AIPath_tag, with the same meanings; its runtime
// nodes are 8 bytes each (+0 next in cel, +4 file node)
typedef struct AIBound_tag {
    ushort index;           // 0x00
    ushort _pad02;
    uchar *record;          // 0x04
    uint flags;             // 0x08 - 1
    uint fromFile[8];       // 0x0c
    uint nodeCount;         // 0x2c
    uchar *firstFileNode;   // 0x30
    void *runtimeNodes;     // 0x34
    uint linkCount;         // 0x38
    uchar *firstLink;       // 0x3c
} AIBound_tag;

// What routes are planned to (0x30). AINetwork_BuildAITarget skips the rebuild while the goal is within 2 m of built
typedef struct AITarget_tag {
    uint targetBit;         // 0x00 - the node flag bit this target marks
    obj_tag *obj;           // 0x04
    CelPos_tag goal;        // 0x08
    CelPos_tag built;       // 0x18 - where the marks were last built
    short reachable;        // 0x28 - nodes that reach the goal
    short obstructedOnly;   // 0x2a
    uint builtFrame;        // 0x2c
} AITarget_tag;

// A route being planned or followed (Drone_tag.route1, the dynamic route, and route2, the patrol / mission route)
typedef struct AIRoute_tag {
    ushort flags;           // 0x00 - &7 mode (1 once, 2 loop, 3 ping-pong), 8 backwards, 0x10 valid, 0x40 moved, 0x80 never direct
    ushort _pad02;
    uchar status;           // 0x04 - the last result code (0 following ... 0xc)
    uchar _pad05[3];
    uint targetFrame;       // 0x08 - AITarget.builtFrame when planned: re-planned when the target is newer
    ushort prevStartNode;   // 0x0c
    ushort startNode;       // 0x0e
    ushort endNode;         // 0x10
    ushort _pad12;
    CelPos_tag start;       // 0x14 - the mover's feet
    CelPos_tag dest;        // 0x24
    CelPos_tag waypoint;    // 0x34 - what the drone steers at
    short current;          // 0x44 - index into the node list (-1 before the first)
    ushort count;           // 0x46
    AITarget_tag *target;   // 0x48
    float remaining;        // 0x4c
    float arriveRadius;     // 0x50 - arrived (status 3) once remaining is below it
    uint _unknown54;
    uchar creepActive;      // 0x58
    uchar specialPending;   // 0x59 - a special node was reached (NDrone2_ReachedDestNode)
    uchar creepFlags[2];    // 0x5a
    short creepLink;        // 0x5c - the link being crept (link flag 0x10 set meanwhile)
    ushort _pad5e;
    AIPath_tag *creepPath;  // 0x60
    ushort creepFrom;       // 0x64 - route indices
    ushort creepTo;         // 0x66
    short creepStep;        // 0x68
    short creepSteps;       // 0x6a - int(length) * 2.5
    _VECTOR step;           // 0x6c - 0.4 m
    _VECTOR segmentStart;   // 0x78
    _VECTOR segmentEnd;     // 0x84
    ushort *nodes;          // 0x90 - sized from the largest path's node count
    AIPath_tag *path;       // 0x94
    obj_tag *mover;         // 0x98
    obj_tag *targetObj;     // 0x9c
} AIRoute_tag;

// A point of interest a drone can go to (Drone_tag+0x564)
typedef struct AIPoint_tag {
    uint _unknown0;
    uchar set;              // 0x04
    uchar _pad05[0xb];
    float distance;         // 0x10 - DistanceToAIPoint
    float radius;           // 0x14
    _VECTOR pos;            // 0x18
    cel_tag *cel;           // 0x24
} AIPoint_tag;

// A distance field over one path: one flood per point of interest, a byte per node (roughly metres to it)
typedef struct {
    obj_tag *obj;           // 0x00
    CelPos_tag celPos;      // 0x04
    AIPath_tag *path;       // 0x14
    uint size;              // 0x18 - the path's node count
    uchar *data;            // 0x1c
    short startNode;        // 0x20
    ushort _pad22;
    uchar allocated;        // 0x24
    uchar _pad25[3];
} AIEmitter_tag;

#pragma pack(pop)

static_assert(sizeof(AINode_tag) == 0x30, "AINode_tag is 0x30 bytes");
static_assert(offsetof(AINode_tag, door) == 0x28, "Wrong offset for AINode_tag.door");
static_assert(sizeof(AIPath_tag) == 0x48, "AIPath_tag is 0x48 bytes");
static_assert(offsetof(AIPath_tag, nodeCount) == 0x2c, "Wrong offset for AIPath_tag.nodeCount");
static_assert(sizeof(AIBound_tag) == 0x40, "AIBound_tag is 0x40 bytes");
static_assert(sizeof(AITarget_tag) == 0x30, "AITarget_tag is 0x30 bytes");
static_assert(sizeof(AIRoute_tag) == 0xa0, "AIRoute_tag is 0xa0 bytes");
static_assert(offsetof(AIRoute_tag, target) == 0x48, "Wrong offset for AIRoute_tag.target");
static_assert(offsetof(AIRoute_tag, creepPath) == 0x60, "Wrong offset for AIRoute_tag.creepPath");
static_assert(offsetof(AIRoute_tag, nodes) == 0x90, "Wrong offset for AIRoute_tag.nodes");
static_assert(sizeof(AIPoint_tag) == 0x28, "AIPoint_tag is 0x28 bytes");
static_assert(sizeof(AIEmitter_tag) == 0x28, "AIEmitter_tag is wrong size");

// The level's AI paths and bounds (AIPath_Parse, map block 0x05). Neither table's count is checked.
// XBE_GLOBAL(0x00274cc8, 0x4)
#define AIPathCount U32_AT(0x00274cc8)
// XBE_GLOBAL(0x00274ccc, 0xe10)
#define AIPaths (*(AIPath_tag(*)[50])0x00274ccc)
// XBE_GLOBAL(0x00275adc, 0x4)
#define AIBoundCount U32_AT(0x00275adc)
// XBE_GLOBAL(0x00275ae0, 0xc80)
#define AIBounds (*(AIBound_tag(*)[50])0x00275ae0)

uchar AINetwork_RouteIsValid(AIRoute_tag *route);
void AINetwork_AllocEmitter(AIEmitter_tag *emitter, uint size);
void AINetwork_FreeEmitter(AIEmitter_tag *emitter);

#endif // AINETWORK_H_
