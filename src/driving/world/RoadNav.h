#pragma once

#include <stddef.h>
#include <stdint.h>

#include "RoadNetwork.h"
#include "../data/CoordConvert.h"

// ---------------------------------------------------------------------------------------------------------------
// A navigator on the road network (Ghidra: WRoadNav, 0xc0 bytes): a place on one of the network's segments - the
// segment, which way along it, how far (0 to 1) - and the lane offset across it, with the curve the place follows
// between the segment's two ends at that offset. IncNavPosition moves it along, choosing the next segment at each
// node by the navigator's mode: at random, towards a heading, keeping a lane, or along the pavements. The AI
// vehicles, pedestrians and helicopters and the player camera each hold one. See RoadNav.cpp.
//
// The road data (segments, nodes, intersections) is WRoadNetwork's (RoadNetwork.h). The field names are ours, but
// for spline and laneOffset (Ghidra's).
// ---------------------------------------------------------------------------------------------------------------

class RCameraSpline;

// ---- the navigator

// How a navigator chooses its next segment (by the method IncNavPosition calls)
enum WRoadNavMode : int32_t {
    kNavLane = 1,               // CalcNextSegmentLane
    kNavSidewalk = 2,           // CalcNextSegmentSidewalk
    kNavDirection = 3,          // CalcNextSegmentDirection
    kNavRandom = 4,             // CalcNextSegmentRandom
    kNavDirection5 = 5,         // CalcNextSegmentDirection, and only kSegmentFlag8000 segments
};

class WRoadNav {
public:
    int32_t mode;               // +0x00 WRoadNavMode
    uint8_t unknown04;          // +0x04 in mode 4, the segments' unknown5a it keeps to
    uint8_t unknown05[0x0b];
    Coord3 position;            // +0x10 where it is: on the curve at its lane offset
    uint32_t unknown1c;
    Coord3 roadPoint;           // +0x20 InitAtPoint's nearest point of the segment, then as position
    uint32_t unknown2c;
    Coord3 direction;           // +0x30 the segment's direction, reversed when going from node[1] to node[0]
    uint8_t forward;            // +0x3c 1 going from node[0] to node[1]: the node ahead is node[forward]
    uint8_t unknown3d;
    int16_t segment;            // +0x3e -1 when InitAtPoint found none
    float t;                    // +0x40 how far along, 0 to 1 in its direction of travel
    uint8_t unknown44[0x0c];
    Coord3 boundStart;          // +0x50 the curve's ends: the segment's ends at the lane offset
    uint32_t unknown5c;
    Coord3 boundEnd;            // +0x60
    uint32_t unknown6c;
    Coord3 startControl;        // +0x70 and its control points
    uint32_t unknown7c;
    Coord3 endControl;          // +0x80
    uint32_t unknown8c;
    RCameraSpline *spline;      // +0x90 the curve, on kSegmentCurved segments
    uint8_t atDeadEnd;          // +0x94 the last segment change kept the segment
    int8_t lane;                // +0x95 negative on the left
    int8_t unknown96;           // +0x96 set with lane
    uint8_t unknown97;
    float laneOffset;           // +0x98 across the segment from its centre line
    float laneChangeFrom;       // +0x9c a lane change: from this offset
    float laneChangeTo;         // +0xa0 to this one
    float laneChangeFraction;   // +0xa4 done
    float laneChangeDistance;   // +0xa8 travelled
    float laneChangeLength;     // +0xac to travel; 0: at once
    int8_t nextChoice;          // +0xb0 which of the next node's segments to take, -1 none
    uint8_t unknownB1;          // +0xb1 either one: CalcNextSegmentRandom chooses by heading
    uint8_t unknownB2;          // +0xb2
    uint8_t unknownB3[0x0d];

    WRoadNav* Construct();                                                          // 0x000ca090
    void Destruct();                                                                // 0x000ca100
    void Reset();                                                                   // 0x000c9130

    // Placing it: at the segment nearest a point, facing along a heading (segment -1 when there is none; with
    // noLane the lane offset stays 0)                                                0x000cc100
    void InitAtPoint(const Coord3 *point, const Coord3 *heading, bool noLane);
    // At a segment, lane and distance along, forwards (always in mode 4) or backwards   0x000cb900
    void InitAtSegment(short segmentIndex, int8_t laneNumber, float along, bool forwards);
    // The nearest segment to a point in the point's grid cell, its nearest point and how far along that is;
    // -1 when none qualifies, 0 for an empty cell                                    0x000cbf20
    int FindClosestSegmentInd(const Coord4 *point, Coord3 *closest, float *along);

    // A curve end: the node at that end moved across the road by the offset          0x000c96b0
    void SetBoundPos(WRoadSegment *road, float offset, Coord3 *pos, bool atStart);
    // A curve control point, scaled to the curve's length (kSegmentCurved segments only)   0x000cb280
    void SetControlPos(WRoadSegment *road, Coord3 *pos, bool atStart);

    // Moving along by a distance, past as many nodes as it takes; the heading and the target segment are for the
    // modes that steer                                                               0x000cb4b0
    void IncNavPosition(float step, Coord3 *heading, short targetSegment);
    // A lane change to an offset over a distance (at once when the distance is not positive)   0x000cbcf0
    void ChangeLanes(float offset, float distance);
    // Advances a lane change by a distance; false when none is under way             0x000c99f0
    bool UpdateLaneChange(float step);
    void ReverseNavDirection();                                                     // 0x000c95a0

    // The segment shares a road or both intersections with the navigator's           0x000c97b0
    bool PathShareRoadSegment(short other);
    // The segment lies ahead (true also for none); sameRoad is cleared when it is found to lie nowhere near
    // 0x000c9810
    bool PathForwardRoadSegment(short other, bool *sameRoad);

    // The next segment at the node ahead, for each mode. They answer the segment's number and set *ahead to the
    // way along it the navigator will go; the lane modes also set the new lane offset and *changed when the
    // curve's ends must be made again.
    short CalcNextSegmentRandom(Coord3 *heading, uint8_t *ahead, bool *unused);                     // 0x000c9240
    short CalcNextSegmentDirection(Coord3 *heading, short *target, uint8_t *ahead, bool *unused);   // 0x000ca120
    short CalcNextSegmentLane(float *offset, uint8_t *ahead, bool *changed);                        // 0x000ca530
    short CalcNextSegmentSidewalk(float *offset, uint8_t *ahead, bool *changed);                    // 0x000cad40
};
static_assert(sizeof(WRoadNav) == 0xc0, "WRoadNav is 0xc0 bytes");
static_assert(offsetof(WRoadNav, segment) == 0x3e && offsetof(WRoadNav, boundStart) == 0x50 &&
              offsetof(WRoadNav, spline) == 0x90 && offsetof(WRoadNav, laneOffset) == 0x98 &&
              offsetof(WRoadNav, nextChoice) == 0xb0, "WRoadNav layout");
