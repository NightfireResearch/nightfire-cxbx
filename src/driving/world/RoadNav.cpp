#pragma fp_contract(off)

#include "RoadNav.h"
#include "Grid.h"
#include "../camera/CameraSpline.h"
#include "../engine/SimRandom.h"
#include "../engine/UMemory.hpp"
#include "../platform/RealMath.h"
#include "../../helpers.h"

#include <math.h>
#include <stddef.h>
#include <bit>

// ---------------------------------------------------------------------------------------------------------------
// WRoadNav: placing a navigator on the road network, moving it along, choosing the next segment at each node, and
// the curve it follows on a segment - straight between the segment's ends moved across by the lane offset, or a
// spline through control points on curved segments (the RCameraSpline the navigator owns). The road data and its
// queries are WRoadNetwork's (RoadNetwork.h).
// ---------------------------------------------------------------------------------------------------------------

#define RoadNetwork (fgRoadNetworkData.instance)
#define RoadNodes (fgRoadNetworkData.nodes)
#define RoadSegments (fgRoadNetworkData.segments)
#define RoadIntersections (fgRoadNetworkData.intersections)

#define SimulationRandom (*(SimRandom **)0x00233ff0)   // the Simulation's first word

// Two of WRoadNetwork's queries, called at their addresses where the point is a Coord4 (the ports take Coord3s)
#define WRoadNetwork_GetLinePointIntersect ((double (__fastcall *)(WRoadNetwork *, int, const void *, const void *, const void *, void *, bool))0x000c9000)
#define WRoadNetwork_GetSegmentPointIntersect ((double (__fastcall *)(WRoadNetwork *, int, WRoadSegment *, const void *, void *, bool))0x000c9f70)

#define Sprintf ((int (__cdecl *)(char *, const char *, ...))0x00132767)

static constexpr float kNoDirection = -2.0f;                // below any dot product of two unit vectors
static constexpr float kMinCurveLength = 0.01f;
static constexpr float kNoSegment = 20000.0f;               // FindClosestSegmentInd's starting distance
static constexpr float kRandomScale = 1.0f / 65536.0f;      // SimRandom's numbers to [0, 1)
static constexpr double kKeepChance = 0.3;                  // CalcNextSegmentLane keeps a candidate below this
static constexpr float kNearStart = 0.2f;                   // InitAtPoint: this far along, the side is the road's
static constexpr float kMinLaneOffset = 2.5f;               // InitAtPoint in kNavLane
static constexpr float kPavementInset = 1.0f;               // a pavement's offset in from the outer lane's
static constexpr float kKerbInset = 2.0f;                   // and further on a road with lanes on one side only
static_assert(std::bit_cast<uint32_t>(kNoDirection) == 0xc0000000, "");
static_assert(std::bit_cast<uint32_t>(kMinCurveLength) == 0x3c23d70a, "");
static_assert(std::bit_cast<uint32_t>(kNoSegment) == 0x469c4000, "");
static_assert(std::bit_cast<uint32_t>(kRandomScale) == 0x37800000, "");
static_assert(std::bit_cast<uint64_t>(kKeepChance) == 0x3fd3333333333333ull, "");
static_assert(std::bit_cast<uint32_t>(kNearStart) == 0x3e4ccccd, "");
static_assert(std::bit_cast<uint32_t>(kMinLaneOffset) == 0x40200000, "");

static const char kDebugLine[] = "%d %d %d <%.4f %.4f> <%.4f %.4f> %d <%.4f %.4f> <%.4f %.4f>";

static inline Coord4 Widened(const Coord3 &v) {
    Coord4 out = {v.x, v.y, v.z, 0.0f};
    return out;
}

static inline Coord4 Negated(const Coord3 &v) {
    Coord4 out = {-v.x, -v.y, -v.z, 0.0f};
    return out;
}

static inline Coord3 Xyz(const Coord4 &v) {
    Coord3 out = {v.x, v.y, v.z};
    return out;
}

// The other end of a segment from a node
static inline WRoadNode *FarNode(const WRoadSegment *road, const WRoadNode *node) {
    WRoadNode *first = RoadNodes[road->node[0]];
    return node == first ? RoadNodes[road->node[1]] : first;
}

// The node is the segment's node[0]: going along the segment from it is going forward
static inline bool StartsAt(const WRoadSegment *road, const WRoadNode *node) {
    return node == RoadNodes[road->node[0]];
}

static inline const float *LaneOffsets(WRoadSegment *road) {
    return fgRoadNetworkData.laneOffsets[RoadNetwork->GetSegmentLaneType(road)];
}

static inline int LaneCount(int lane) {
    return lane < 0 ? -lane : lane;
}

static double RandomFraction() {
    int roll = SimulationRandom->Generate();
    return roll * double(kRandomScale);
}

static void SetLaneOffset(WRoadNav *nav, float offset) {
    nav->laneOffset = offset;
    nav->laneChangeTo = offset;
    nav->laneChangeFrom = offset;
}

// The curve's ends at the offset and its control points (repeated at each place in the original)
static void SetCurve(WRoadNav *nav, WRoadSegment *road, float offset) {
    nav->SetBoundPos(road, offset, &nav->boundStart, true);
    nav->SetBoundPos(road, offset, &nav->boundEnd, false);
    nav->SetControlPos(road, &nav->startControl, true);
    nav->SetControlPos(road, &nav->endControl, false);
}

static void BuildSpline(WRoadNav *nav) {
    nav->spline->BuildSplineEx(&nav->boundStart, &nav->startControl, &nav->boundEnd, &nav->endControl);
}

// The position t along the curve
static void PlaceOnCurve(WRoadNav *nav, WRoadSegment *road) {
    if (road->flags & kSegmentCurved) {
        Coord4 point;
        nav->spline->EvaluateSpline(nav->t, &point);
        nav->position = Xyz(point);
        nav->roadPoint = Xyz(point);
    } else {
        RoadNetwork->GetPointOnSegment(&nav->boundStart, &nav->boundEnd, road, nav->t, &nav->position);
        nav->roadPoint = nav->position;
    }
}

// ---- construction

// FUNC_AT(0x000ca090)
WRoadNav* WRoadNav::Construct() {
    RCameraSpline *block = static_cast<RCameraSpline *>(UMemory::FastAlloc(sizeof(RCameraSpline), "RCameraSpline"));
    spline = block != NULL ? block->Construct() : NULL;
    Reset();
    return this;
}

// FUNC_AT(0x000ca100)
void WRoadNav::Destruct() {
    if (spline != NULL) {
        spline->Destruct();
        UMemory::FastFree(spline, sizeof(RCameraSpline));
    }
}

// FUNC_AT(0x000c9130)
void WRoadNav::Reset() {
    const Coord3 zero = {0.0f, 0.0f, 0.0f};
    position = zero;
    roadPoint = zero;
    direction = zero;
    boundStart = zero;
    boundEnd = zero;
    startControl = zero;
    endControl = zero;
    mode = 0;
    unknown04 = 0;
    forward = 0;
    segment = 0;
    t = 0.0f;
    atDeadEnd = 0;
    lane = 0;
    unknown96 = 0;
    laneOffset = 0.0f;
    laneChangeFrom = 0.0f;
    laneChangeTo = 0.0f;
    laneChangeFraction = 0.0f;
    laneChangeDistance = 0.0f;
    laneChangeLength = 0.0f;
    nextChoice = -1;
    unknownB1 = 0;
    unknownB2 = 0;
}

// ---- placing

// FUNC_AT(0x000cbf20)
int WRoadNav::FindClosestSegmentInd(const Coord4 *point, Coord3 *closest, float *along) {
    int best = -1;
    bool found = false;
    float nearest = kNoSegment;
    uint32_t row, column;
    TheGrid->RangeCheckROWCOL(point, &row, &column);
    WGridNode *cell = TheGrid->nodes[TheGrid->columns * row + column];
    if (cell == NULL || cell->staticCount[kGridRoadSegment] == 0)
        return 0;
    fgRoadNetworkData.queryStamp++;
    for (int i = 0; i < cell->staticCount[kGridRoadSegment]; i++) {
        int16_t index = cell->StaticIndices(kGridRoadSegment)[i];
        if (index >= fgRoadNetworkData.segmentCount)
            continue;
        WRoadSegment *road = RoadSegments[index];
        if (road->queryStamp == fgRoadNetworkData.queryStamp)
            continue;
        uint16_t flags = road->flags;
        road->queryStamp = fgRoadNetworkData.queryStamp;
        if (flags & kSegmentSpecial)
            continue;
        if (mode == kNavRandom) {
            if (!(flags & kSegmentFlag08) || unknown04 != road->unknown5a)
                continue;
        } else if (flags & kSegmentFlag08) {
            continue;
        }
        if (mode == kNavDirection5) {
            if (!(flags & kSegmentFlag8000))
                continue;
        } else if (flags & kSegmentFlag8000) {
            continue;
        }
        Coord3 onRoad;
        float roadT = float(WRoadNetwork_GetSegmentPointIntersect(RoadNetwork, 0, road, point, &onRoad, true));
        if (road->flags & kSegmentCurved)
            RoadNetwork->GetPointOnSegment(road, roadT, &onRoad);
        float distance = vec3distance(point, &onRoad);
        if (!found || distance < nearest) {
            nearest = distance;
            *closest = onRoad;
            best = index;
            *along = roadT;
            found = true;
        }
    }
    return best;
}

// FUNC_AT(0x000cc100)
void WRoadNav::InitAtPoint(const Coord3 *point, const Coord3 *heading, bool noLane) {
    Coord4 at = Widened(*point);
    Coord4 facing = Widened(*heading);
    float roadT = 0.0f;     // the original leaves its stack here when the point's cell has no segments
    int index = FindClosestSegmentInd(&at, &roadPoint, &roadT);
    if (index == -1) {
        segment = -1;
        return;
    }
    segment = index;
    atDeadEnd = 0;
    WRoadSegment *road = RoadSegments[index];
    Coord3 roadDirection = road->forward;
    Coord4 along = Widened(roadDirection);
    float dot = v3dotprod(&facing, &along);
    if (facing.x == 0.0f && facing.y == 0.0f && facing.z == 0.0f)
        dot = 1.0f;

    if ((mode == kNavRandom && !unknownB1 && !unknownB2) || !(dot < 0.0f)) {
        direction = roadDirection;
        forward = 1;
        t = roadT;
        boundStart = RoadNodes[road->node[0]]->position;
        boundEnd = RoadNodes[road->node[1]]->position;
    } else {
        forward = 0;
        direction = Xyz(Negated(roadDirection));
        t = float(fabs(1.0 - roadT));
        boundStart = RoadNodes[road->node[1]]->position;
        boundEnd = RoadNodes[road->node[0]]->position;
    }
    SetLaneOffset(this, 0.0f);
    unknown96 = 0;
    lane = 0;

    if (!noLane) {
        // The offset: the point's distance across the road in the ground plane (measured square to the road when
        // the point lies behind the curve's start), on the side the point is
        Coord4 nearestPoint = Widened(roadPoint);
        Coord4 fromStart;
        VU0_v4sub(&at, &boundStart, &fromStart);
        float offset;
        if (v3dotprod(&fromStart, &direction) < 0.0f) {
            Coord4 across = {direction.z, direction.y, -direction.x, 0.0f};
            float sideways = v3dotprod(&fromStart, &across);
            Coord4 foot;
            VU0_v4scaleadd(&across, sideways, &nearestPoint, &foot);
            offset = VU0_v3distancexz(&foot, &nearestPoint);
        } else {
            offset = VU0_v3distancexz(&at, &nearestPoint);
        }
        SetLaneOffset(this, offset);

        Coord4 side;
        if (t <= kNearStart) {
            side = {direction.z, direction.y, -direction.x, 0.0f};
        } else {
            Coord4 start = Widened(boundStart);
            Coord4 toRoad;
            VU0_v4sub(&nearestPoint, &start, &toRoad);
            VU0_v4unitxyz(&toRoad, &toRoad);
            side = {toRoad.z, toRoad.y, -toRoad.x, 0.0f};
        }
        Coord4 toPoint;
        VU0_v4sub(&at, &nearestPoint, &toPoint);
        VU0_v4unitxyz(&toPoint, &toPoint);
        if (v3dotprod(&side, &toPoint) < 0.0f)
            SetLaneOffset(this, -laneOffset);

        if (mode == kNavRandom) {
            // kept within the road's drivable width
            WRoadSegment *current = RoadSegments[segment];
            if (laneOffset < 0.0f) {
                float now = laneOffset;
                double limit = -RoadNetwork->GetSegmentLeftDrivableDist(current);
                if (!(limit > now))
                    limit = now;
                SetLaneOffset(this, float(limit));
            } else {
                double limit = RoadNetwork->GetSegmentRightDrivableDist(current);
                float now = laneOffset;
                if (!(limit < now))
                    limit = now;
                SetLaneOffset(this, float(limit));
            }
        }
        if (mode == kNavSidewalk) {
            // the pavement on the point's side
            if (laneOffset < 0.0f) {
                unknown96 = -1 - road->leftLanes;
                lane = -1 - road->leftLanes;
                double offset = 1.0 - LaneOffsets(road)[road->leftLanes + 1];
                SetLaneOffset(this, float(offset));
                if (road->rightLanes == 0)
                    SetLaneOffset(this, float(offset + kKerbInset));
            } else {
                unknown96 = road->rightLanes + 1;
                lane = road->rightLanes + 1;
                double offset = LaneOffsets(road)[road->rightLanes + 1] - 1.0;
                SetLaneOffset(this, float(offset));
                if (road->leftLanes == 0)
                    SetLaneOffset(this, float(offset - kKerbInset));
            }
        } else if (mode == kNavLane) {
            float offset = laneOffset;
            if (kMinLaneOffset > offset)
                offset = kMinLaneOffset;
            int number = RoadNetwork->GetSegmentLaneIndex(road, &offset);
            unknown96 = number;
            lane = number;
            SetLaneOffset(this, offset);
        }
    }

    SetCurve(this, road, laneOffset);
    Coord4 onCurve;
    t = float(WRoadNetwork_GetLinePointIntersect(RoadNetwork, 0, &boundStart, &boundEnd, &at, &onCurve, true));
    if (road->flags & kSegmentCurved)
        BuildSpline(this);
    PlaceOnCurve(this, road);
}

// FUNC_AT(0x000cb900)
void WRoadNav::InitAtSegment(short segmentIndex, int8_t laneNumber, float along, bool forwards) {
    segment = segmentIndex;
    atDeadEnd = 0;
    WRoadSegment *road = RoadSegments[segmentIndex];
    Coord3 roadDirection = road->forward;
    if (mode != kNavRandom && !forwards) {
        forward = 0;
        direction = Xyz(Negated(roadDirection));
        t = float(fabs(1.0 - along));
        boundStart = RoadNodes[road->node[1]]->position;
        boundEnd = RoadNodes[road->node[0]]->position;
    } else {
        direction = roadDirection;
        forward = 1;
        t = along;
        boundStart = RoadNodes[road->node[0]]->position;
        boundEnd = RoadNodes[road->node[1]]->position;
    }
    unknown96 = 0;
    lane = 0;
    SetLaneOffset(this, 0.0f);

    if (mode == kNavSidewalk) {
        if (laneNumber < 0) {
            unknown96 = -1 - road->leftLanes;
            lane = -1 - road->leftLanes;
            double offset = 1.0 - LaneOffsets(road)[road->leftLanes + 1];
            SetLaneOffset(this, float(offset));
            if (road->rightLanes == 0)
                SetLaneOffset(this, float(offset + kKerbInset));
        } else {
            unknown96 = road->rightLanes + 1;
            lane = road->rightLanes + 1;
            double offset = LaneOffsets(road)[road->rightLanes + 1] - 1.0;
            SetLaneOffset(this, float(offset));
            if (road->leftLanes == 0)
                SetLaneOffset(this, float(offset - kKerbInset));
        }
    } else {
        unknown96 = laneNumber;
        lane = laneNumber;
        if (laneNumber < 0)
            SetLaneOffset(this, -LaneOffsets(road)[-laneNumber]);
        else
            SetLaneOffset(this, LaneOffsets(road)[laneNumber]);
    }

    SetCurve(this, road, laneOffset);
    if (road->flags & kSegmentCurved) {
        // only roadPoint, here
        BuildSpline(this);
        Coord4 point;
        spline->EvaluateSpline(t, &point);
        roadPoint = Xyz(point);
    } else {
        RoadNetwork->GetPointOnSegment(&boundStart, &boundEnd, road, t, &position);
        roadPoint = position;
    }
}

// ---- the curve

// FUNC_AT(0x000c96b0)
void WRoadNav::SetBoundPos(WRoadSegment *road, float offset, Coord3 *pos, bool atStart) {
    int end = forward ? !atStart : atStart;
    Coord4 node = Widened(RoadNodes[road->node[end]]->position);
    Coord4 across;
    if (atStart)
        across = forward ? Widened(road->startAcross) : Negated(road->endAcross);
    else
        across = forward ? Widened(road->endAcross) : Negated(road->startAcross);
    VU0_v4scale(&across, offset, &across);
    VU0_v3add(&node, &across, pos);
}

// The segment's control point at that end, its offset from the node scaled by the curve's length over the
// segment's, from the curve's end.
// FUNC_AT(0x000cb280)
void WRoadNav::SetControlPos(WRoadSegment *road, Coord3 *pos, bool atStart) {
    if (!(road->flags & kSegmentCurved))
        return;
    int end = forward ? !atStart : atStart;
    Coord4 node = Widened(RoadNodes[road->node[end]]->position);
    *pos = end == 0 ? road->startControl : road->endControl;
    float curveLength = vec3distance(&boundStart, &boundEnd);
    if (kMinCurveLength > curveLength)
        curveLength = kMinCurveLength;
    double ratio = road->length / double(curveLength);
    float scale = kMinCurveLength > ratio ? kMinCurveLength : float(ratio);
    float reach = float(vec3distance(&node, pos) / double(scale));
    Coord4 toward;
    VU0_v4sub(pos, &node, &toward);
    VU0_v4unitxyz(&toward, &toward);
    Coord4 bound = Widened(atStart ? boundStart : boundEnd);
    VU0_v4scaleadd(&toward, reach, &bound, pos);
}

// ---- moving

// FUNC_AT(0x000cb4b0)
void WRoadNav::IncNavPosition(float step, Coord3 *heading, short targetSegment) {
    WRoadSegment *road;
    float fraction, length;
    for (;;) {
        road = RoadSegments[segment];
        length = vec3distance(&boundStart, &boundEnd);
        fraction = step / length;
        if (length <= 0.0f) {
            // a debug line the original formats and never uses
            WRoadNode *first = RoadNodes[road->node[0]];
            WRoadNode *second = RoadNodes[road->node[1]];
            char line[256];
            Sprintf(line, kDebugLine, mode, segment, road->index, first->position.x, first->position.z,
                    second->position.x, second->position.z, forward, boundStart.x, boundStart.z, boundEnd.x,
                    boundEnd.z);
        }
        if (double(fraction) + t < 1.0)
            break;

        // past the segment's end: on to the next segment with what is left of the step
        UpdateLaneChange(float((1.0 - t) * length));
        float newOffset = laneOffset;
        bool changed = false;
        short next = segment;
        switch (mode) {
        case kNavRandom:
            next = CalcNextSegmentRandom(heading, &forward, &changed);
            break;
        case kNavDirection:
        case kNavDirection5:
            next = CalcNextSegmentDirection(heading, &targetSegment, &forward, &changed);
            break;
        case kNavLane:
            next = CalcNextSegmentLane(&newOffset, &forward, &changed);
            break;
        case kNavSidewalk:
            next = CalcNextSegmentSidewalk(&newOffset, &forward, &changed);
            break;
        }
        if (segment == next)
            atDeadEnd = 1;
        segment = next;
        road = RoadSegments[next];
        direction = road->forward;
        if (!forward)
            direction = Xyz(Negated(direction));
        if (changed) {
            boundStart = boundEnd;
            SetBoundPos(road, newOffset, &boundEnd, false);
            SetLaneOffset(this, newOffset);
        } else {
            SetBoundPos(road, laneOffset, &boundStart, true);
            SetBoundPos(road, laneOffset, &boundEnd, false);
        }
        SetControlPos(road, &startControl, true);
        SetControlPos(road, &endControl, false);
        if (road->flags & kSegmentCurved)
            BuildSpline(this);
        roadPoint = boundStart;
        position = boundStart;
        step = float((fraction - (1.0 - t)) * length);
        t = 0.0f;
    }

    float reached = fraction + t;
    if (!(reached > 0.0f))
        reached = 0.0f;
    else if (!(reached < 1.0f))
        reached = 1.0f;
    t = reached;
    if (UpdateLaneChange(step)) {
        SetCurve(this, road, laneOffset);
        if (road->flags & kSegmentCurved)
            BuildSpline(this);
    }
    PlaceOnCurve(this, road);
}

// FUNC_AT(0x000cbcf0)
void WRoadNav::ChangeLanes(float offset, float distance) {
    if (distance > 0.0f) {
        float from = laneChangeTo;
        laneOffset = from;
        laneChangeTo = offset;
        laneChangeFrom = from;
        laneChangeFraction = 0.0f;
        laneChangeLength = distance;
        laneChangeDistance = 0.0f;
        return;
    }
    if (laneOffset == offset)
        return;
    SetLaneOffset(this, offset);
    WRoadSegment *road = RoadSegments[segment];
    SetCurve(this, road, offset);
    if (road->flags & kSegmentCurved)
        BuildSpline(this);
    PlaceOnCurve(this, road);
}

// FUNC_AT(0x000c99f0)
bool WRoadNav::UpdateLaneChange(float step) {
    if (laneOffset == laneChangeTo)
        return false;
    if (!(laneChangeLength > 0.0f)) {
        laneOffset = laneChangeTo;
        return true;
    }
    double travelled = double(step) + laneChangeDistance;
    laneChangeDistance = float(travelled);
    double done = travelled * (1.0 / laneChangeLength);
    laneChangeFraction = float(done);
    if (done >= 1.0) {
        laneOffset = laneChangeTo;
        laneChangeFrom = laneChangeTo;
        return true;
    }
    laneOffset = float((double(laneChangeTo) - laneChangeFrom) * done + laneChangeFrom);
    return true;
}

// FUNC_AT(0x000c95a0)
void WRoadNav::ReverseNavDirection() {
    forward = !forward;
    direction = Xyz(Negated(direction));
    Coord3 swap = boundStart;
    boundStart = boundEnd;
    boundEnd = swap;
    swap = startControl;
    startControl = endControl;
    endControl = swap;
    t = float(fabs(1.0 - t));
    SetLaneOffset(this, -laneOffset);
    lane = -lane;
    unknown96 = lane;
}

// ---- the path

// FUNC_AT(0x000c97b0)
bool WRoadNav::PathShareRoadSegment(short other) {
    WRoadSegment *mine = RoadSegments[segment];
    WRoadSegment *theirs = RoadSegments[other];
    if (mine->road == theirs->road)
        return true;
    return (mine->intersection[0] == theirs->intersection[0] && mine->intersection[1] == theirs->intersection[1]) ||
           (mine->intersection[1] == theirs->intersection[0] && mine->intersection[0] == theirs->intersection[1]);
}

// Ahead: on the same road further along, or at an intersection one or two links on from the one ahead.
// FUNC_AT(0x000c9810)
bool WRoadNav::PathForwardRoadSegment(short other, bool *sameRoad) {
    if (other < 0)
        return true;
    WRoadSegment *current = RoadSegments[segment];
    WRoadSegment *target = RoadSegments[other];
    if (sameRoad != NULL)
        *sameRoad = true;
    if (current->intersection[0] == current->intersection[1])
        return true;
    if (PathShareRoadSegment(other)) {
        int end = forward;
        return current->intersection[end] == target->intersection[end] && current->roadPosition[end] > target->roadPosition[end];
    }

    int16_t first = target->intersection[0];
    int16_t second = target->intersection[1];
    if (first == second && second == current->intersection[!forward])
        return false;
    int16_t behind = current->intersection[!forward];
    if (first == behind || second == behind)
        return false;
    int16_t ahead = current->intersection[forward];
    if (ahead >= 0) {
        if (first == second && second == ahead)
            return true;
        if (first == ahead || second == ahead)
            return true;
        WRoadIntersection *junction = RoadIntersections[ahead];
        for (int i = 0; i < junction->neighbourCount; i++) {
            int16_t link = junction->neighbours[i];
            if (link == behind)
                continue;
            if (first == link || second == link)
                return true;
            WRoadIntersection *next = RoadIntersections[link];
            for (int j = 0; j < next->neighbourCount; j++) {
                // the original tests the next intersection's neighbour at the outer index against behind
                int16_t skipped = next->neighbours[i];
                int16_t beyond = next->neighbours[j];
                if (skipped == behind)
                    continue;
                if (first == beyond || second == beyond)
                    return true;
            }
        }
    }
    if (sameRoad != NULL)
        *sameRoad = false;
    return false;
}

// ---- the next segment

// At random among the node's segments that lead on (not back along this one, nor arriving at the node as it
// does); with nextChoice, that one; with unknownB1 or unknownB2, the one nearest the heading.
// FUNC_AT(0x000c9240)
short WRoadNav::CalcNextSegmentRandom(Coord3 *heading, uint8_t *ahead, bool *unused) {
    (void)unused;
    int current = segment;
    WRoadSegment *road = RoadSegments[current];
    WRoadNode *node = RoadNodes[road->node[forward]];
    int next = current;
    if (nextChoice >= 0 && node->segmentCount > 2) {
        next = nextChoice < node->segmentCount ? node->segments[nextChoice] : node->segments[0];
        nextChoice = -1;
        return next;
    }
    if (node->segmentCount <= 1) {
        next = road->index;
        *ahead = !*ahead;
        return next;
    }

    if (unknownB1 || unknownB2) {
        float best = kNoDirection;
        for (int i = 0; i < node->segmentCount; i++) {
            int other = node->segments[i];
            if (other == segment)
                continue;
            WRoadSegment *candidate = RoadSegments[other];
            Coord4 along = candidate->node[1] == node->index ? Negated(candidate->forward)
                                                             : Widened(candidate->forward);
            VU0_v4unitxyz(heading, heading);
            VU0_v4unitxyz(&along, &along);
            float dot = v3dotprod(&along, heading);
            if (dot > best) {
                best = dot;
                next = other;
                *ahead = StartsAt(candidate, node);
            }
        }
        return next;
    }

    if (node->segmentCount > 2) {
        int choices = node->segmentCount;
        for (int i = 0; i < node->segmentCount; i++) {
            int other = node->segments[i];
            if (other == current || road->node[*ahead] == RoadSegments[other]->node[*ahead])
                choices--;
        }
        int roll = SimulationRandom->Generate();
        int pick = roll * choices >> 16;
        int seen = 0;
        for (int i = 0; i < node->segmentCount; i++) {
            int other = node->segments[i];
            if (other == segment || road->node[*ahead] == RoadSegments[other]->node[*ahead])
                continue;
            next = other;
            if (seen == pick)
                return next;
            seen++;
        }
        return next;
    }

    for (int i = 0; i < node->segmentCount; i++) {
        int other = node->segments[i];
        if (other != current && road->node[*ahead] != RoadSegments[other]->node[*ahead])
            return node->segments[i];
    }
    return next;
}

// Towards the target segment when it lies beyond a link of the intersection ahead; otherwise the first segment
// without kSegmentNonDirectional, or the one whose road beyond is nearest the heading.
// FUNC_AT(0x000ca120)
short WRoadNav::CalcNextSegmentDirection(Coord3 *heading, short *target, uint8_t *ahead, bool *unused) {
    (void)unused;
    WRoadSegment *road = RoadSegments[segment];
    WRoadNode *node = RoadNodes[road->node[forward]];
    float best = kNoDirection;
    short next = segment;
    WRoadSegment *attached = GetAttachedDirectionalSegment(node, segment);
    if (nextChoice >= 0 && node->segmentCount > 2) {
        next = node->segments[nextChoice];
        nextChoice = -1;
        return next;
    }
    if (node->segmentCount <= 1) {
        next = road->index;
        *ahead = !*ahead;
        return next;
    }
    if (attached != NULL) {
        next = attached->index;
        *ahead = StartsAt(attached, node);
        return next;
    }

    if (*target >= 0) {
        WRoadSegment *goal = RoadSegments[*target];
        int16_t aheadIndex = road->intersection[forward];
        if (aheadIndex >= 0) {
            WRoadIntersection *junction = RoadIntersections[aheadIndex];
            int16_t behind = road->intersection[!forward];
            for (int i = 0; i < junction->neighbourCount; i++) {
                int16_t link = junction->neighbours[i];
                if (link == behind)
                    continue;
                if (goal->intersection[0] != link && goal->intersection[1] != link)
                    continue;
                for (int j = 0; j < junction->segmentCount; j++) {
                    WRoadSegment *exit = RoadSegments[junction->segments[j]];
                    if (exit->intersection[0] != link && exit->intersection[1] != link)
                        continue;
                    // the node's segment whose far node the exit leaves from (the last such)
                    for (int k = 0; k < node->segmentCount; k++) {
                        WRoadSegment *candidate = RoadSegments[node->segments[k]];
                        WRoadNode *farEnd = FarNode(candidate, node);
                        for (int m = 0; m < farEnd->segmentCount; m++) {
                            if (farEnd->segments[m] == exit->index)
                                next = candidate->index;
                        }
                    }
                    *ahead = StartsAt(RoadSegments[next], node);
                    return next;
                }
            }
        }
    }

    for (int i = 0; i < node->segmentCount; i++) {
        if (node->segments[i] == segment)
            continue;
        WRoadSegment *candidate = RoadSegments[node->segments[i]];
        if (!(candidate->flags & kSegmentNonDirectional)) {
            next = node->segments[i];
            *ahead = StartsAt(RoadSegments[next], node);
            return next;
        }
        WRoadNode *farEnd = FarNode(candidate, node);
        WRoadSegment *beyond = GetAttachedDirectionalSegment(farEnd, -1);
        Coord4 along = farEnd != RoadNodes[beyond->node[0]] ? Negated(beyond->forward) : Widened(beyond->forward);
        VU0_v4unitxyz(heading, heading);
        VU0_v4unitxyz(&along, &along);
        float dot = v3dotprod(&along, heading);
        if (dot > best) {
            best = dot;
            next = node->segments[i];
            *ahead = StartsAt(RoadSegments[next], node);
        }
    }
    return next;
}

// Keeping to a lane: the candidates are the node's segments whose road beyond has a lane on the navigator's side;
// one is taken, with its lane, when the node's shape and the lanes allow, and kept unless a draw sends the search
// on to the next.
// FUNC_AT(0x000ca530)
short WRoadNav::CalcNextSegmentLane(float *offset, uint8_t *ahead, bool *changed) {
    short current = segment;
    WRoadSegment *road = RoadSegments[current];
    WRoadNode *node = RoadNodes[road->node[forward]];
    int8_t newLane = lane;
    short next = current;
    WRoadSegment *attached = GetAttachedDirectionalSegment(node, current);
    int count = node->segmentCount;

    // at a three-way node, exactly one of the other two segments has a kSegmentFlag02 road beyond
    bool oneBlocked = false;
    if (count == 3) {
        int blocked = 0;
        for (int i = 0; i < 3; i++) {
            WRoadSegment *candidate = RoadSegments[node->segments[i]];
            if (candidate->index == current)
                continue;
            WRoadSegment *beyond = GetAttachedDirectionalSegment(FarNode(candidate, node), -1);
            if (beyond != NULL && (beyond->flags & kSegmentFlag02))
                blocked++;
        }
        if (blocked == 1)
            oneBlocked = true;
    }

    if (count <= 1) {
        next = road->index;
        *ahead = !*ahead;
    } else if (attached != NULL) {
        next = attached->index;
        *ahead = StartsAt(attached, node);
    } else {
        for (int i = 0; i < node->segmentCount; i++) {
            int candidateIndex = node->segments[i];
            if (candidateIndex == segment)
                continue;
            WRoadSegment *candidate = RoadSegments[candidateIndex];
            if (candidate->flags & kSegmentFlag02)
                continue;
            WRoadNode *farEnd = FarNode(candidate, node);
            WRoadSegment *beyond = GetAttachedDirectionalSegment(farEnd, -1);
            if (beyond->flags & kSegmentFlag02)
                continue;

            int side = fgRoadNetworkData.unknownAC != 0;
            if (farEnd != RoadNodes[beyond->node[0]])
                side = !side;
            if (fgRoadNetworkData.unknownAC != 0 && (beyond->flags & kSegmentFlag1000))
                side = !side;
            bool noLane = true;
            for (int k = 0; k < beyond->rightLanes + beyond->leftLanes; k++) {
                if (side == ((1 << k) & beyond->unknown58))
                    noLane = false;
            }
            if (noLane)
                continue;

            if ((node->segmentCount == 4 && i == 2) || node->segmentCount == 2) {
                next = candidateIndex;
                newLane = lane;
                *ahead = StartsAt(RoadSegments[next], node);
                *changed = true;
                if (RandomFraction() < kKeepChance)
                    break;
            }

            bool lanes20 = (road->flags & kSegmentLaneType2) != 0;
            bool here1000 = (road->flags & kSegmentFlag1000) != 0;
            bool there1000 = (beyond->flags & kSegmentFlag1000) != 0;
            bool last = (node->segmentCount == 3 && i == 2) || (node->segmentCount == 4 && i == 3);
            if (fgRoadNetworkData.unknownAC == 0) {
                if (i == 1) {
                    if (oneBlocked) {
                        newLane = lane;
                    } else if (lanes20 && !here1000 && !there1000) {
                        newLane = lane;
                    } else if (lanes20 && here1000 && !there1000) {
                        newLane = lane != 0 && lane == -road->leftLanes ? -beyond->leftLanes : -1;
                    } else if (lanes20 && !here1000 && there1000) {
                        newLane = lane != 0 && lane == -road->leftLanes ? -beyond->leftLanes : 1;
                    } else {
                        if (lane != 0 && lane != -road->leftLanes)
                            continue;
                        newLane = -beyond->leftLanes;
                    }
                } else if (last) {
                    if (oneBlocked) {
                        newLane = lane;
                    } else if (lanes20 && !here1000 && !there1000) {
                        newLane = lane;
                    } else if (lanes20 && !here1000 && there1000) {
                        continue;
                    } else {
                        if (lane != -1)
                            continue;
                        newLane = -1;
                    }
                } else {
                    continue;
                }
            } else {
                if (last) {
                    if (oneBlocked) {
                        newLane = lane;
                    } else if (lanes20 && !here1000 && !there1000) {
                        newLane = lane;
                    } else if (lanes20 && here1000 && !there1000) {
                        newLane = lane != 0 && lane == road->rightLanes ? beyond->rightLanes : 1;
                    } else if (lanes20 && !here1000 && there1000) {
                        newLane = lane != 0 && lane == road->rightLanes ? beyond->rightLanes : -1;
                    } else {
                        if (lane != 0 && lane != road->rightLanes)
                            continue;
                        newLane = beyond->rightLanes;
                    }
                } else if (i == 1) {
                    if (oneBlocked) {
                        newLane = lane;
                    } else if (lanes20 && !here1000 && !there1000) {
                        newLane = lane;
                    } else if (lanes20 && !here1000 && there1000) {
                        continue;
                    } else {
                        if (lane != 1)
                            continue;
                        newLane = 1;
                    }
                } else {
                    continue;
                }
            }
            next = candidateIndex;
            *ahead = StartsAt(RoadSegments[next], node);
            *changed = true;
            if (RandomFraction() < kKeepChance)
                break;
        }
    }

    unknown96 = newLane;
    lane = newLane;
    WRoadSegment *chosen = RoadSegments[next];
    WRoadSegment *lanes = chosen;
    if (chosen->flags & kSegmentNonDirectional)
        lanes = GetAttachedDirectionalSegment(FarNode(chosen, node), -1);
    *offset = LaneOffsets(lanes)[LaneCount(newLane)];
    if (newLane < 0)
        *offset = -*offset;
    if (*offset != laneOffset && next != current)
        *changed = true;
    return next;
}

// Along the pavements: on along the road while it has a pavement on the navigator's side, else across to a road
// beyond the junction ahead, else back along the other side.
// FUNC_AT(0x000cad40)
short WRoadNav::CalcNextSegmentSidewalk(float *offset, uint8_t *ahead, bool *changed) {
    short current = segment;
    WRoadSegment *road = RoadSegments[current];
    int end = forward;
    WRoadNode *node = RoadNodes[road->node[end]];
    int8_t newLane = lane;
    short next = current;
    bool onLeft = laneOffset < 0.0f;
    WRoadSegment *attached = GetAttachedDirectionalSegment(node, current);
    bool noPavement = false;
    if (attached != NULL) {
        if (!(attached->flags & (onLeft ? kSegmentLeftMargin : kSegmentRightMargin)))
            noPavement = true;
    } else if (node->segmentCount == 2) {
        for (int i = 0; i < 2; i++) {
            WRoadSegment *other = RoadSegments[node->segments[i]];
            if (other->index != current && (other->flags & kSegmentNonDirectional))
                attached = other;
        }
    }
    bool deadEnd = attached != NULL && (attached->flags & kSegmentFlag80) &&
                   (RoadNodes[attached->node[0]]->segmentCount == 1 || RoadNodes[attached->node[1]]->segmentCount == 1);

    bool turnedBack = false;
    if (attached != NULL && node->segmentCount > 1 && !noPavement) {
        if (!(road->flags & kSegmentNonDirectional) && (!(attached->flags & kSegmentFlag80) || deadEnd)) {
            next = attached->index;
            *ahead = StartsAt(attached, node);
        } else {
            WRoadNode *farEnd = FarNode(GetAttachedDirectionalSegment(node, current), node);
            int farCount = farEnd->segmentCount;
            if (farCount == 2) {
                next = attached->index;
                *ahead = StartsAt(RoadSegments[next], node);
            } else {
                // all but two of the far node's other segments have a kSegmentFlag02 road beyond
                bool twoOpen = false;
                if (farCount >= 3) {
                    int blocked = 0;
                    for (int j = 0; j < farCount; j++) {
                        WRoadSegment *exit = RoadSegments[farEnd->segments[j]];
                        if (exit->index == current)
                            continue;
                        WRoadNode *opposite = RoadNetwork->GetSegmentOppNode(exit, farEnd);
                        WRoadSegment *beyond = GetAttachedDirectionalSegment(opposite, -1);
                        if (beyond != NULL && (beyond->flags & kSegmentFlag02))
                            blocked++;
                    }
                    if (blocked == farCount - 2)
                        twoOpen = true;
                }
                for (int j = 0; j < farCount; j++) {
                    WRoadSegment *exit = RoadSegments[farEnd->segments[j]];
                    if (!(exit->flags & kSegmentNonDirectional) || (exit->flags & kSegmentFlag02))
                        continue;
                    WRoadNode *beyondNode = FarNode(exit, farEnd);
                    WRoadSegment *beyond = GetAttachedDirectionalSegment(beyondNode, -1);
                    bool same = beyondNode == RoadNodes[beyond->node[end]];
                    if (!(beyond->flags & (onLeft != same ? kSegmentLeftMargin : kSegmentRightMargin)))
                        continue;
                    // the road beyond the exit is taken, its lanes counted
                    bool lastExit = (farCount == 3 && j == 2) || (farCount == 4 && j == 3);
                    if (twoOpen) {
                        next = beyond->index;
                        newLane = onLeft ? -1 - beyond->leftLanes : beyond->rightLanes + 1;
                    } else if (lastExit) {
                        if (onLeft)
                            continue;
                        next = beyond->index;
                        newLane = beyond->rightLanes + 1;
                    } else if (j == 1 && onLeft) {
                        next = beyond->index;
                        newLane = -1 - beyond->leftLanes;
                    } else {
                        continue;
                    }
                    *ahead = StartsAt(beyond, beyondNode);
                    break;
                }
            }
        }
    } else {
        next = road->index;
        *ahead = !*ahead;
        newLane = -newLane;
        turnedBack = true;
    }

    if (next == segment && !turnedBack) {
        *ahead = !*ahead;
        next = road->index;
        newLane = -newLane;
    }
    unknown96 = newLane;
    lane = newLane;
    WRoadSegment *chosen = RoadSegments[next];
    WRoadSegment *lanes = chosen;
    if (chosen->flags & kSegmentNonDirectional)
        lanes = GetAttachedDirectionalSegment(FarNode(chosen, node), -1);
    double inset = LaneOffsets(lanes)[LaneCount(newLane)] - double(kPavementInset);
    *offset = float(inset);
    if (lanes->leftLanes == 0 || lanes->rightLanes == 0)
        *offset = float(inset - kKerbInset);
    if (newLane < 0)
        *offset = -*offset;
    *changed = true;
    return next;
}
