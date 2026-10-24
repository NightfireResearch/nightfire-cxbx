// actionhelpers.h first, outside the guard: it includes this file and then drone/Drone.h, which needs these types,
// so a file that includes this one first must not get Drone.h before them
#include "../actionhelpers.h"
#include "../../common/xbeClass.h"
#include "../../driving/platform/Ptr32.h"   // the map file's pointer words

#ifndef AINETWORK_H_
#define AINETWORK_H_

// The AI navigation data shared by drones and bots, and the searches and routes over it. See
// docs/drone/bots-and-navigation/README.md, sections 2 to 4.

#pragma pack(push, 1)

typedef struct {
    _VECTOR pos;
    cel_tag *cel;
} CelPos_tag;

// MAPNODE_tag.flags. The low bits come from the map file; the high ones are the searches' scratch marks.
#define AINODE_WAIT         0x1         // a drone waits here once (NDrone2_ReachedDestNode)
#define AINODE_DOOR         0x2         // next to a door (NDrone_InitDoorNodes finds which)
#define AINODE_KICK         0x4         // next to a kickable object (NDrone_InitKickNodes)
#define AINODE_ACTION_ANIM  0x8
#define AINODE_MISSION_WAIT 0x20
#define AINODE_FIDDLE       0x40
#define AINODE_KICKED       0x8000      // a kick node whose object has been kicked
#define AINODE_MAYHEW_TALK_1 0x10000    // Drone_MayhewTalk says a line here: one of four
#define AINODE_MAYHEW_TALK_2 0x20000
#define AINODE_MAYHEW_TALK_3 0x40000
#define AINODE_MAYHEW_TALK_4 0x80000
#define AINODE_SPECIAL      0xf007f     // any of these: NDrone2_ReachedDestNode runs when a route gets there
#define AINODE_OPEN         0x40000000  // on a search's open or closed list
#define AINODE_CLOSED       0x80000000  // on the emitter's closed list (no search sets it)
#define AINODE_LISTED       (AINODE_OPEN | AINODE_CLOSED)
#define AINODE_LIST_MASK    0x3fffffff

// The target bits an AITarget_tag marks on the nodes it can be reached from; the bit above each marks the nodes from
// which the way is obstructed (a bound is crossed)
#define AITARGET_PLAYER     0x1000000   // NPCGlobals.playerTarget
#define AITARGET_ALERT      0x4000000   // NPCGlobals.alertTarget
#define AITARGET_GOAL       0x10000000  // a drone's own goal

// A node of an AI path or bound, read in place from the map file (AIPath_Parse patches it). Bounds' file nodes are
// these 0x20 bytes; paths' are AIPathNodeRecord.
typedef struct MAPNODE_tag {
    ushort index;           // 0x00 - written by AIPath_Parse
    ushort path;            // 0x02 - the path's (or bound's) index in its table
    uint flags;             // 0x04 - AINODE_*
    float headroom;         // 0x08 - bounds: how high the outline reaches above the node (AIBounds_Link2Cel)
    Ptr32<cel_tag> cel;     // 0x0c - AIPath_BindNodes
    _VECTOR pos;            // 0x10 - y raised 0.4 at load
    float unknown1c;        // 0x1c - 1.0 in every file
} MAPNODE_tag;

// A path's node as stored in the map file
typedef struct AIPathNodeRecord {
    MAPNODE_tag node;
    uint unknown20[8];
} AIPathNodeRecord;

// AILink_tag.flags
#define AILINK_CREEPING     0x10        // a route is walking along it (LinkCreep_ForNodes; LinkCreep_Calc clears it)
#define AILINK_DOOR         0x20        // between two door nodes, across a bound (NDrone_InitDoorNodes)
#define AILINK_KICK         0x40        // between two kick nodes, across a bound (NDrone_InitKickNodes)
#define AILINK_EXPLOSIVE    0x100       // explosives near it this frame (Drone_BuildDynamicAwarePoints)
#define AILINK_SMOKE        0x200       // smoke near it this frame

// A link between two nodes of a path, in the map file (0x10)
typedef struct AILink_tag {
    ushort index;           // 0x00
    ushort path;            // 0x02 - written by AIPath_Parse
    ushort flags;           // 0x04 - AILINK_*, 0 at load
    ushort used;            // 0x06 - routes being followed over it this frame (AINetwork_UpdateLinksUsedCount)
    ushort a;               // 0x08 - node indices
    ushort b;               // 0x0a
    float length;           // 0x0c - AIPath_Prepare
} AILink_tag;

// AIBoundLink_tag.flags
#define AIBOUND_PASSABLE    0x400       // a nav link crosses it (AINetwork_InitPassableBoundries)
#define AIBOUND_DOOR        0x800       // a link between two door nodes crosses it (NDrone_InitDoorNodes)
#define AIBOUND_KICK        0x1000      // a link between two kick nodes crosses it (NDrone_InitKickNodes)

// An edge of a bound's outline, in the map file (0x14)
typedef struct AIBoundLink_tag {
    ushort index;                   // 0x00
    ushort bound;                   // 0x02 - written by AIPath_Parse
    uint flags;                     // 0x04 - AIBOUND_*, 0 at load
    ushort a;                       // 0x08 - node indices
    ushort b;                       // 0x0a
    Ptr32<struct AIBoundLink_tag> nextA;    // 0x0c - the next bound link in end a's cel (AIBounds_Prepare)
    Ptr32<struct AIBoundLink_tag> nextB;    // 0x10 - and in end b's, when that is another cel
} AIBoundLink_tag;

// A node of a path at run time (AIPath_tag.runtimeNodes)
typedef struct AINode_tag {
    struct AINode_tag *nextInCel;   // 0x00 - list head at cel_tag.aiNodes
    MAPNODE_tag *fileNode;          // 0x04
    struct AINode_tag *nextSorted;  // 0x08 - the candidates of a node search, nearest first
    float searchDistance;           // 0x0c - 2D distance to the search's origin
    uchar parentLink;               // 0x10 - which of the parent's links led here
    uchar linkCount;                // 0x11
    ushort firstLink;               // 0x12 - first entry in the path's node->link list
    float g;                        // 0x14 - cost so far (the emitter starts it at 255)
    float h;                        // 0x18 - 2D distance to the destination
    float f;                        // 0x1c - g + h, the open list's sort key
    struct AINode_tag *parent;      // 0x20
    struct AINode_tag *nextInList;  // 0x24 - open or closed list
    obj_tag *door;                  // 0x28 - the door (or, for a kick node, the kickable object); a link between two
                                    //        door nodes is closed while Door_IsLocked
    char _pad2c[4];
} AINode_tag;

// A node of a bound at run time (AIBound_tag.runtimeNodes)
typedef struct AIBoundNode_tag {
    struct AIBoundNode_tag *nextInCel;  // 0x00 - list head at cel_tag.boundNodes
    MAPNODE_tag *fileNode;              // 0x04
} AIBoundNode_tag;

// AIPath_tag.flags
#define AIPATH_BOUND        0x1         // in the bound table
#define AIPATH_NAV          0x2         // a navigation graph: routes and emitters
#define AIPATH_PATROL       0x4
#define AIPATH_MISSION      0x8
#define AIPATH_PATROL_MOD   0x10        // a patrol from its first node takes the first link, not the second
#define AIPATH_NOT_NAV      (AIPATH_BOUND | AIPATH_PATROL | AIPATH_MISSION)

// An AI path (the table at 0x00274ccc, 50 entries)
typedef struct AIPath_tag {
    ushort index;                   // 0x00
    ushort _pad02;
    uchar *record;                  // 0x04 - the file record (its name at +0)
    uint flags;                     // 0x08 - AIPATH_*
    uint fromFile[8];               // 0x0c - copied from the file; always 0
    uint nodeCount;                 // 0x2c - also an emitter's size
    AIPathNodeRecord *fileNodes;    // 0x30
    AINode_tag *runtimeNodes;       // 0x34
    uint linkCount;                 // 0x38
    AILink_tag *links;              // 0x3c - in the file
    ushort *nodeLinks;              // 0x40 - node -> link index lists (AIPath_Prepare, 2 x linkCount)
    uint reachMask;                 // 0x44 - target bits some node has (set by NodesForPosition, tested by DoAStarPath)
} AIPath_tag;

// An AI bound (a walkable area's outline; the table at 0x00275ae0, 50 entries): the first 0x40 bytes of an
// AIPath_tag, with the same meanings
typedef struct AIBound_tag {
    ushort index;                   // 0x00
    ushort _pad02;
    uchar *record;                  // 0x04
    uint flags;                     // 0x08 - AIPATH_BOUND
    uint fromFile[8];               // 0x0c
    uint nodeCount;                 // 0x2c
    MAPNODE_tag *fileNodes;         // 0x30
    AIBoundNode_tag *runtimeNodes;  // 0x34
    uint linkCount;                 // 0x38
    AIBoundLink_tag *links;         // 0x3c
} AIBound_tag;

// What routes are planned to (0x30). AINetwork_BuildAITarget skips the rebuild while the goal is within 2 m of built
typedef struct AITarget_tag {
    uint targetBit;         // 0x00 - AITARGET_*: the node flag this target marks
    obj_tag *obj;           // 0x04
    CelPos_tag goal;        // 0x08
    CelPos_tag built;       // 0x18 - where the marks were last built
    short reachable;        // 0x28 - nodes that reach the goal
    short obstructedOnly;   // 0x2a
    uint builtFrame;        // 0x2c
} AITarget_tag;

// AIRoute_tag.flags
#define AIROUTE_MODE        0x7         // AIROUTE_ONCE, LOOP or PINGPONG; anything else clamps like ONCE
#define AIROUTE_ONCE        1
#define AIROUTE_LOOP        2
#define AIROUTE_PINGPONG    3
#define AIROUTE_BACKWARDS   0x8         // a ping-pong route on its way back
#define AIROUTE_VALID       0x10        // planned and still good
#define AIROUTE_PATROL_MOD  0x20        // from path flag 0x10
#define AIROUTE_DEST_MOVED  0x40        // AINetwork_AlterDestFor_DROUTE_Nearest moved the destination
#define AIROUTE_NO_DIRECT   0x80        // always use the graph, never walk straight there

// AIRoute_tag.status: what the last plan or step came to (CalcRoute and FollowRoute return it too)
typedef enum {
    AIROUTE_FOLLOWING = 0,      // a full route
    AIROUTE_PARTIAL = 1,        // only obstructed nodes reach the goal
    AIROUTE_DIRECT = 2,         // walking straight there
    AIROUTE_ARRIVED = 3,
    AIROUTE_NO_GOAL_NODES = 4,  // no node reaches the goal
    AIROUTE_OFF_NETWORK = 5,    // no node within 25 m of the start can be walked to
    AIROUTE_NO_ROUTE = 6,       // the open list ran out
    AIROUTE_NO_PATH = 7,
    AIROUTE_NO_NETWORK = 8,     // no target, no path, or no nodes
    AIROUTE_CREEP_FAILED = 9,
    AIROUTE_DOOR = 10,          // set by NDrone2_MoveToGoalPosition
    AIROUTE_RESET = 0xb,
    AIROUTE_CREEP_FAILED2 = 0xc,    // the creep failed with AIRoute_tag.unknown02 set
} AIRouteStatus;

// Walking a route's current link in 0.4 m steps (AIRoute_tag+0x58; LinkCreep_*). LinkCreep_Calc clears all of it past
// the end of the route.
typedef struct AICreep_tag {
    uchar active;           // 0x00
    uchar specialPending;   // 0x01 - the link starts at a special node (LinkCreep_AtSpecialNode)
    uchar unknown02;        // 0x02 - set by LinkCreep_CalcToRouteEnd, cleared by LinkCreep_ForNodes
    uchar unknown03;        // 0x03 - the same
    short link;             // 0x04 - the link being crept (AILINK_CREEPING set meanwhile)
    ushort _pad06;
    AIPath_tag *path;       // 0x08
    ushort from;            // 0x0c - route indices
    ushort to;              // 0x0e
    short step;             // 0x10
    short steps;            // 0x12 - int(length) * 2.5
    _VECTOR delta;          // 0x14 - one step, 0.4 m along the segment
    _VECTOR segmentStart;   // 0x20
    _VECTOR segmentEnd;     // 0x2c
} AICreep_tag;

// A route being planned or followed (Drone_tag.route1, the dynamic route, and route2, the patrol / mission route)
typedef struct AIRoute_tag {
    ushort flags;           // 0x00 - AIROUTE_*
    uchar unknown02;        // 0x02 - a failed creep returns status 0xc rather than 9 while it is set
    uchar _pad03;
    uchar status;           // 0x04 - AIRouteStatus
    uchar unknown05;        // 0x05 - cleared by every reset
    ushort unknown06;       // 0x06 - AINetwork_SetupNextNode stores the index here when it clamps past the end
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
    float remaining;        // 0x4c - AINetwork_GetRouteDistance
    float arriveRadius;     // 0x50 - arrived once remaining is below it
    float creepReach;       // 0x54 - LinkCreep_Increment moves the waypoint on only while it is nearer than this
    AICreep_tag creep;      // 0x58
    ushort *nodes;          // 0x90 - sized from the largest path's node count
    AIPath_tag *path;       // 0x94
    obj_tag *mover;         // 0x98
    obj_tag *targetObj;     // 0x9c
} AIRoute_tag;

// A point a drone is going to (Drone_tag+0x564)
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
    short startNode;        // 0x20 - the node the last flood started from (AINetwork_EmitPath)
    ushort _pad22;
    uchar allocated;        // 0x24
    uchar _pad25[3];
} AIEmitter_tag;

// A search for the nodes of one path near a position (AINetwork_NodeSearch), on the caller's stack
typedef struct AINodeSearch_tag {
    CelPos_tag *origin;     // 0x00
    _VECTOR *heuristicGoal; // 0x04 - for the found node's h; NULL for h = 0
    AIPath_tag *path;       // 0x08
    uchar toNode;           // 0x0c - 1: can the origin be walked to the node; 0: can the node be walked to the origin
    uchar _pad0d[3];
    AINode_tag *openList;   // 0x10 - with toNode, the found node goes onto this list (its head, a dummy node)
    obj_tag *objA;          // 0x14 - handed to the move tests
    obj_tag *objB;          // 0x18
    uint targetBit;         // 0x1c - without toNode, marked on the node found (and the bit above on an obstructed one)
    float radiusSq;         // 0x20 - 0 for 15 m
    short resultNode;       // 0x24 - with toNode, the node found
    ushort _pad26;
    int found;              // 0x28
    int obstructed;         // 0x2c
} AINodeSearch_tag;

#pragma pack(pop)

static_assert(sizeof(MAPNODE_tag) == 0x20, "MAPNODE_tag is 0x20 bytes");
static_assert(offsetof(MAPNODE_tag, pos) == 0x10, "Wrong offset for MAPNODE_tag.pos");
static_assert(sizeof(AIPathNodeRecord) == 0x40, "AIPathNodeRecord is 0x40 bytes");
static_assert(sizeof(AILink_tag) == 0x10, "AILink_tag is 0x10 bytes");
static_assert(sizeof(AIBoundLink_tag) == 0x14, "AIBoundLink_tag is 0x14 bytes");
LAYOUT_32(sizeof(AINode_tag) == 0x30, "AINode_tag is 0x30 bytes");
LAYOUT_32(offsetof(AINode_tag, nextInList) == 0x24, "Wrong offset for AINode_tag.nextInList");
LAYOUT_32(offsetof(AINode_tag, door) == 0x28, "Wrong offset for AINode_tag.door");
LAYOUT_32(sizeof(AIBoundNode_tag) == 0x8, "AIBoundNode_tag is 8 bytes");
LAYOUT_32(sizeof(AIPath_tag) == 0x48, "AIPath_tag is 0x48 bytes");
LAYOUT_32(offsetof(AIPath_tag, nodeCount) == 0x2c, "Wrong offset for AIPath_tag.nodeCount");
LAYOUT_32(offsetof(AIPath_tag, reachMask) == 0x44, "Wrong offset for AIPath_tag.reachMask");
LAYOUT_32(sizeof(AIBound_tag) == 0x40, "AIBound_tag is 0x40 bytes");
LAYOUT_32(sizeof(AITarget_tag) == 0x30, "AITarget_tag is 0x30 bytes");
LAYOUT_32(sizeof(AIRoute_tag) == 0xa0, "AIRoute_tag is 0xa0 bytes");
LAYOUT_32(offsetof(AIRoute_tag, target) == 0x48, "Wrong offset for AIRoute_tag.target");
LAYOUT_32(sizeof(AICreep_tag) == 0x38, "AICreep_tag is 0x38 bytes");
LAYOUT_32(offsetof(AIRoute_tag, creep) == 0x58, "Wrong offset for AIRoute_tag.creep");
LAYOUT_32(offsetof(AIRoute_tag, nodes) == 0x90, "Wrong offset for AIRoute_tag.nodes");
LAYOUT_32(sizeof(AIPoint_tag) == 0x28, "AIPoint_tag is 0x28 bytes");
LAYOUT_32(sizeof(AIEmitter_tag) == 0x28, "AIEmitter_tag is wrong size");
LAYOUT_32(sizeof(AINodeSearch_tag) == 0x30, "AINodeSearch_tag is 0x30 bytes");

// The level's AI paths and bounds (AIPath_Parse, map block 0x05). Neither table's count is checked.
extern uint32_t AIPathCount;
extern AIPath_tag AIPaths[50];
extern uint32_t AIBoundCount;
extern AIBound_tag AIBounds[50];

// The largest node and link counts of the level's paths (AIPath_Parse): the drones' route node lists and the shared
// safety emitter are sized from the first
extern uint32_t AIPath_MaxNodes;
extern uint32_t AIPath_MaxLinks;

// The move test's limits (NDrone2_MoveTest; AIPath_Init clears the first, Drone_PostLoad_Init sets them per level)
extern float MoveTest_MaxSlope;  // radians
extern float MoveTest_StepHeight;  // a rise up to this is a step, whatever the slope
extern float MoveTest_MaxRise;
extern float MoveTest_UnknownB8;  // only Drone_PostLoad_Init writes it (30), nothing reads it
extern float MoveTest_MaxDistanceSq;

// Routes and their state
uchar AINetwork_RouteIsValid(AIRoute_tag *route);
void AINetwork_InvalidateRoute(AIRoute_tag *route);
void AINetwork_ValidateRoute(AIRoute_tag *route);
short AINetwork_RouteNodeOffset(short current, short offset, AIRoute_tag *route);
void AINetwork_ResetRoute(AIRoute_tag *route, obj_tag *mover, obj_tag *targetObj);
ushort AINetwork_FindLink(AIPath_tag *path, ushort node, short other);
double AINetwork_GetRouteDistance(AIRoute_tag *route);
double AINetwork_GetRouteDistance3D(AIRoute_tag *route);
double AINetwork_GetRouteDistanceBetweenNodes(AIRoute_tag *route, short from, short to);

// Searches
bool AINetwork_NodeSearchCel(AINodeSearch_tag *search);
bool AINetwork_NodeSearch(AINodeSearch_tag *search);
uint AINetwork_DoAStarPath(AIRoute_tag *route, obj_tag *objA, obj_tag *objB, uint targetBit);
AIPath_tag* AINetwork_NavPathForPosition(_VECTOR *pos, cel_tag *cel, char moveTest);
void AINetwork_NodesForPosition(AITarget_tag *target, CelPos_tag *celPos, obj_tag *obj, float radius);
void AINetwork_BuildAITarget(AITarget_tag *target, obj_tag *obj, float radius);
void AINetwork_SetupGoalPosition(AITarget_tag *target, AIPoint_tag *point, CelPos_tag *celPos, float radius);
bool AINetwork_SetupGoalPositionToObj(AITarget_tag *target, AIPoint_tag *point, obj_tag *obj, float radius);

// Bounds
bool AINetwork_TestRayCels(_VECTOR *from, cel_tag *fromCel, _VECTOR *to, cel_tag *toCel);
bool AINetwork_HitBoundry(_VECTOR *closest, AIBoundLink_tag *link, _VECTOR *pos, float radiusSq, float *distSq,
                          uint skipFlags);
bool AINetwork_LineIntersectsBoundry(AIBoundLink_tag *link, _VECTOR *from, _VECTOR *to, _VECTOR *hit, float *nearest,
                                     char raiseHeadroom);
bool AINetwork_GetBoundsPushVectorForSphere(_VECTOR *pos, cel_tag *cel, float radius, _VECTOR *push, uint flags);
uchar AINetwork_BoundsTest(_VECTOR *from, cel_tag *fromCel, _VECTOR *to, cel_tag *toCel, _VECTOR *hit, char strict);
bool AINetwork_BoundsNodeTest(_VECTOR *from, cel_tag *fromCel, _VECTOR *to, cel_tag *toCel, float radius);
bool AINetwork_FurthestPosition(_VECTOR *from, _VECTOR *to, _VECTOR *furthest);
bool AINetwork_ModIntersectedLinkFlags_Bounds(_VECTOR *from, _VECTOR *to, uint setFlags, uint skipFlags,
                                              _VECTOR *hit, char raiseHeadroom);
bool AINetwork_InitPassableBoundries(void);

// Link flags
void AINetwork_ClearLinkFlags(ushort flags);
void AINetwork_UpdateLinksUsedCount(AIRoute_tag *route);
void AINetwork_SetLinksFlagInCircle(_VECTOR *centre, cel_tag *cel, float radius, ushort setFlags, ushort clearFlags);

// Emitters
void AINetwork_AllocEmitter(AIEmitter_tag *emitter, uint size);
void AINetwork_FreeEmitter(AIEmitter_tag *emitter);
uint AINetwork_EmitPath(AIEmitter_tag *emitter, float maxCost);
bool AINetwork_Emitter_GetNodeAtDistance(short node, float distance, AIEmitter_tag *emitter, MAPNODE_tag **found);
bool AINetwork_InitEmitter(obj_tag *obj, _VECTOR *pos, cel_tag *cel, AIEmitter_tag *emitter, AIPath_tag *path);
bool AINetwork_InitEmitter2(obj_tag *obj, _VECTOR *pos, cel_tag *cel, AIEmitter_tag *emitter, AIPath_tag *path);

// Planning a route
bool AINetwork_AlterDestFor_DROUTE_Nearest(AIRoute_tag *route);
uint AINetwork_OptimiseRoute(AIRoute_tag *route, char dropFirst, uint status, char dropLast);
bool AINetwork_SetupRouteToObject(obj_tag *mover, obj_tag *targetObj, AITarget_tag *target, AIPoint_tag *point,
                                  float radius, AIRoute_tag *route);
bool AINetwork_SetupRouteToPosition(AIRoute_tag *route, CelPos_tag *dest, obj_tag *mover, AITarget_tag *target,
                                    AIPoint_tag *point, float radius);
bool AINetwork_VerifyRouteTarget(obj_tag *mover, obj_tag *targetObj, AITarget_tag *target, AIPoint_tag *point,
                                 float radius, AIRoute_tag *route);
bool AINetwork_VerifyRouteToPosition(obj_tag *mover, CelPos_tag *dest, AITarget_tag *target, AIPoint_tag *point,
                                     float radius, AIRoute_tag *route);
uint AINetwork_CalcRoute(AIRoute_tag *route, obj_tag *mover, obj_tag *targetObj);
uint AINetwork_CalcRouteToObject(obj_tag *mover, obj_tag *targetObj, AIRoute_tag *route, AITarget_tag *target,
                                 AIPoint_tag *point, float radius, uint unused);
uint AINetwork_CalcRouteToPosition(obj_tag *mover, CelPos_tag *dest, AIRoute_tag *route, AITarget_tag *target,
                                   AIPoint_tag *point, float radius, uint unused);

// Following a route
uint AINetwork_FollowRoute(AIRoute_tag *route, DCVars_tag *dcv);
bool AINetwork_SetupNextNode(AIRoute_tag *route, short direction);
bool LinkCreep_Setup(AIRoute_tag *route, _VECTOR *from, _VECTOR *to, char fromEnd);
bool LinkCreep_ForNodes(AIRoute_tag *route, int from, int to, char fromEnd);
bool LinkCreep_Calc(AIRoute_tag *route, char fromEnd);
uint LinkCreep_CalcToRouteEnd(AIRoute_tag *route);
bool LinkCreep_Dest(AIRoute_tag *route, short offset, CelPos_tag *out);
bool LinkCreep_AtSpecialNode(AIRoute_tag *route);
bool LinkCreep_Increment(AIRoute_tag *route, uchar *blocked);
bool LinkCreep_Decrement(AIRoute_tag *route);
bool LinkCreep_Handler(AIRoute_tag *route, DCVars_tag *dcv);

#endif // AINETWORK_H_
