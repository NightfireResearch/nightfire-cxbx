#ifndef DRIVING_WORLD_ROADNETWORK_H_
#define DRIVING_WORLD_ROADNETWORK_H_

// ---------------------------------------------------------------------------------------------------------------
// WRoadNetwork, the track's road network (0x000c8940-0x000c9fe0, 0x000cb3f0-0x000cb4b0, 0x000cbe70): the nodes,
// the segments between them, the intersections, and the roads the segments make up, as the track's "RNgp" CARP
// group holds them. Init points tables at the group's records ("rn", "rs", "ri", "rj", "rr", one record each, the
// counts in its "RNhd" record); the records stay where the CARP file has them. The AI cars, pedestrians, road
// spawns and the navigator (WRoadNav, RoadNav.h) read the records directly.
//
// The object itself holds nothing: Init news one byte for it, and the tables are statics (WRoadNetworkData). The
// record structs' names are ours; the allocations' labels call the intersections "Inters". Which way a segment's
// across vectors point, and what the unknown fields hold, the code here does not show.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "../data/CoordConvert.h"       // Coord3

// ---- the records

enum WRoadSegmentFlags : uint16_t {
    kSegmentNonDirectional = 0x0001,    // GetAttachedDirectionalSegment passes over it; its drivable widths are
                                        //   those of the directional segments at its ends
    kSegmentFlag02 = 0x0002,
    kSegmentFlag08 = 0x0008,            // its drivable widths are WRoadSegment::widths
    kSegmentLaneType3 = 0x0010,         // lane type 3 (GetSegmentLaneType), and 5 wide before its lanes
    kSegmentLaneType2 = 0x0020,         // with kSegmentLanes: lane type 2
    kSegmentFlag80 = 0x0080,
    kSegmentSpecial = 0x0100,           // GetSpecialRoads lists it (if unknown59 is set)
    kSegmentLeftMargin = 0x0200,        // 5 more drivable on the left
    kSegmentRightMargin = 0x0400,       // 5 more drivable on the right
    kSegmentLanes = 0x0800,             // lane type 1 (2 with kSegmentLaneType2), and 2.5 more drivable each side
    kSegmentFlag1000 = 0x1000,          // its drivable widths count half of both sides' lanes
    kSegmentCurved = 0x4000,            // a spline through its control points, not a straight line
    kSegmentFlag8000 = 0x8000,          // as kSegmentFlag08
};

// What a flagged segment keeps in place of its start control point
struct WRoadWidths {
    float left;
    float right;
    float unknown08;
};

// A segment ("rs", 0x64 bytes): a stretch of road from one node to another, its lanes either side.
struct WRoadSegment {
    union {                     // +0x00
        Coord3 startControl;    // a curved segment's spline: the control point beside its start node
        WRoadWidths widths;     // kSegmentFlag08 or kSegmentFlag8000: its drivable widths
    };
    uint16_t node[2];           // +0x0c its start and end nodes (WRoadNetworkData::nodes)
    Coord3 endControl;          // +0x10 the control point beside its end node
    int16_t intersection[2];    // +0x1c the intersections at its start and end (WRoadNetworkData::intersections)
    Coord3 startAcross;         // +0x20 unit, across the segment at its start node
    float length;               // +0x2c
    Coord3 endAcross;           // +0x30 at its end node
    int16_t road;               // +0x3c the road it is part of (WRoadNetworkData::roads), -1 none
    int16_t index;              // +0x3e its own
    Coord3 forward;             // +0x40 unit, from its start node to its end node
    float unknown4c;
    uint16_t flags;             // +0x50 WRoadSegmentFlags
    int8_t leftLanes;           // +0x52
    int8_t rightLanes;          // +0x53
    int16_t roadPosition[2];    // +0x54 its place along its road, counted from either end
    uint8_t unknown58;
    uint8_t unknown59;          // GetSpecialRoads: non-zero
    uint8_t unknown5a;          // WRoadNav::FindClosestSegmentInd compares it with the navigator's unknown04
    uint8_t unknown5b;
    uint32_t unknown5c;
    uint32_t queryStamp;        // +0x60 the last search that visited it (WRoadNetworkData::queryStamp)
};
static_assert(sizeof(WRoadSegment) == 0x64, "a road segment is 100 bytes");
static_assert(offsetof(WRoadSegment, node) == 0x0c && offsetof(WRoadSegment, intersection) == 0x1c &&
              offsetof(WRoadSegment, road) == 0x3c && offsetof(WRoadSegment, forward) == 0x40 &&
              offsetof(WRoadSegment, flags) == 0x50 && offsetof(WRoadSegment, roadPosition) == 0x54 &&
              offsetof(WRoadSegment, queryStamp) == 0x60, "road segment layout");

// A node ("rn", 0x20 bytes): where segments meet.
struct WRoadNode {
    Coord3 position;            // +0x00
    int16_t index;              // +0x0c its own
    uint8_t segmentCount;       // +0x0e
    uint8_t unknown0f;
    uint16_t segments[8];       // +0x10 the segments that start or end at it
};
static_assert(sizeof(WRoadNode) == 0x20, "a road node is 32 bytes");

// An intersection ("ri", 0x30 bytes): the intersections next to it, and its segments.
struct WRoadIntersection {
    Coord3 position;            // +0x00
    int16_t index;              // +0x0c its own
    uint8_t neighbourCount;     // +0x0e
    uint8_t unknown0f;
    uint16_t neighbours[4];     // +0x10 intersections
    uint8_t segmentCount;       // +0x18
    uint8_t unknown19;
    int16_t segments[11];       // +0x1a
};
static_assert(sizeof(WRoadIntersection) == 0x30 && offsetof(WRoadIntersection, segments) == 0x1a,
              "a road intersection is 48 bytes");

// An "rj" record (0x10 bytes), one per intersection; Init tables them and nothing reads them here.
struct WRoadJunction {
    int32_t index;              // +0x00 its own
    uint8_t unknown04[0xc];
};
static_assert(sizeof(WRoadJunction) == 0x10, "an rj record is 16 bytes");

// A road ("rr", 0x30 bytes): segments one after another between two intersections.
struct WRoad {
    int32_t index;              // +0x00 its own
    int16_t intersection[2];    // +0x04 at its ends
    float length;               // +0x08
    char name[0x20];            // +0x0c
    uint8_t unknown2c;
    uint8_t unknown2d;
    uint8_t count2e;            // counted up and down by 0x00029c90 / 0x00029cb0; Init and Restart zero it
    uint8_t count2f;            // counted up and down by 0x00023990 / 0x000239b0; Init and Restart zero it
};
static_assert(sizeof(WRoad) == 0x30 && offsetof(WRoad, count2e) == 0x2e, "a road is 48 bytes");

// The group's "RNhd" record: how many of each record there are.
struct WRoadNetworkHeader {
    uint16_t nodeCount;         // +0x00 "rn"
    uint16_t segmentCount;      // +0x02 "rs"
    uint16_t intersectionCount; // +0x04 "ri"
    uint16_t roadCount;         // +0x06 "rr"
    uint16_t junctionCount;     // +0x08 "rj"
    uint16_t unknown0a[3];
};
static_assert(sizeof(WRoadNetworkHeader) == 0x10, "the road network's header is 16 bytes");

// ---- the network

class WRoadNetwork;

// WRoadNetwork's statics (0x0023e030-0x0023e0fc), as Init sets them
struct WRoadNetworkData {
    const int8_t *laneWeights[32];  // +0x00 rows of 32: GetSegmentLaneWeights's rows 0-9
    const float *laneOffsets[6];    // +0x80 rows of six: lane n's offset from the middle, by lane type (rows 0-3)
    int junctionCount;              // +0x98
    int intersectionCount;          // +0x9c
    int roadCount;                  // +0xa0
    int segmentCount;               // +0xa4
    int nodeCount;                  // +0xa8
    uint8_t unknownAC;              // +0xac Init sets it; the navigator's lane choice and AIRoadSpawn read it
    uint8_t unknownAD;              // +0xad set when a segment has none of kSegmentFlag08, kSegmentSpecial,
                                    //       kSegmentFlag8000; AIRoadSpawn::RefreshSpawnData reads it
    uint8_t loaded;                 // +0xae the tables are filled
    uint8_t unknownAF;
    WRoadNetwork *instance;         // +0xb0 Get's answer
    WRoadNode **nodes;              // +0xb4
    WRoadSegment **segments;        // +0xb8
    WRoadIntersection **intersections;  // +0xbc
    WRoadJunction **junctions;      // +0xc0
    WRoad **roads;                  // +0xc4
    uint32_t queryStamp;            // +0xc8 a search counts it up and stamps the segments it visits
};
static_assert(sizeof(WRoadNetworkData) == 0xcc && offsetof(WRoadNetworkData, instance) == 0xb0 &&
              offsetof(WRoadNetworkData, queryStamp) == 0xc8, "the road network's statics");

#define fgRoadNetworkData (*(WRoadNetworkData *)0x0023e030)

// Distances and offsets the original leaves on the x87 stack are answered as double. A segment's points: t 0 at its
// start node, 1 at its end node.
class WRoadNetwork {
public:
    static WRoadNetwork* Get();                                                             // 0x000c8940
    static void Init();                                                                     // 0x000c8950
    static void Restart();                                                                  // 0x000c8de0
    static void Shutdown();                                                                 // 0x000c9af0

    // The segments with kSegmentSpecial and unknown59 set, by index, at most `max`; answers how many.  0x000c8e60
    int GetSpecialRoads(int *segments, int max);
    // 0-3: the row of the lane tables the segment's flags pick                                       0x000c8eb0
    int GetSegmentLaneType(WRoadSegment *segment);
    // The lane at a sideways offset (negative: the left), the offset moved to the lane's               0x000c8ef0
    int GetSegmentLaneIndex(WRoadSegment *segment, float *offset);
    // The segment's other node                                                                        0x000c8fd0
    WRoadNode* GetSegmentOppNode(WRoadSegment *segment, WRoadNode *node);
    // The point of the line from start to end nearest `point` (clamped to the ends if asked), and     0x000c9000
    // its t
    double GetLinePointIntersect(const Coord3 *start, const Coord3 *end, const Coord3 *point, Coord3 *out,
                                 bool clamp);
    double GetSegmentLeftDrivableDist(WRoadSegment *segment);                               // 0x000c9ba0
    double GetSegmentRightDrivableDist(WRoadSegment *segment);                              // 0x000c9cd0
    // A row of `count` lane weights into weights[0..count), and the row reversed into the last        0x000c9e00
    // `count` of `size`; `end` picks the node a non-directional segment borrows its lanes at
    void GetSegmentLaneWeights(int segment, int8_t end, int8_t *weights, int size, int count);
    // GetLinePointIntersect along the segment's nodes                                                 0x000c9f70
    double GetSegmentPointIntersect(WRoadSegment *segment, const Coord3 *point, Coord3 *out, bool clamp);
    // A curved segment's point at t, through its control points                                       0x000c9fe0
    void GetSegmentCurveStep(const Coord3 *start, const Coord3 *end, WRoadSegment *segment, float t, Coord3 *out);
    double GetSegmentLeftDrivableDist(int segment);                                         // 0x000cb3f0
    double GetSegmentRightDrivableDist(int segment);                                        // 0x000cb410
    // The point at t from start to end, along the segment's curve if it has one                       0x000cb430
    void GetPointOnSegment(const Coord3 *start, const Coord3 *end, WRoadSegment *segment, float t, Coord3 *out);
    // The same between the segment's nodes, t clamped to 0-1                                          0x000cbe70
    void GetPointOnSegment(WRoadSegment *segment, float t, Coord3 *out);
};
static_assert(sizeof(WRoadNetwork) == 1, "Init news one byte for the road network");

// The first segment at the node, other than `excluded`, without kSegmentNonDirectional; NULL if none.  0x000c9aa0
WRoadSegment* GetAttachedDirectionalSegment(WRoadNode *node, short excluded);

#endif // DRIVING_WORLD_ROADNETWORK_H_
