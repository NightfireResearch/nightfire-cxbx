#include "RoadNetwork.h"

#include <math.h>
#include <string.h>

#include "../../helpers.h"
#include "../engine/UGroup.h"
#include "../engine/UMemory.hpp"           // OperatorNew, OperatorDelete
#include "../platform/RealMath.h"
#include "../platform/X87.h"
#include "World.h"

#pragma fp_contract(off)

// ---------------------------------------------------------------------------------------------------------------
// WRoadNetwork (0x000c8940-0x000c9fe0, 0x000cb3f0-0x000cb4b0, 0x000cbe70), ported from the listing. RoadNetwork.h
// has the records. WRoadNav's methods sit between these in the game (RoadNav.cpp).
// ---------------------------------------------------------------------------------------------------------------

class RCameraSpline;

// ---- the game's code not ported yet
#define RCameraSpline_Construct ((RCameraSpline *(__fastcall *)(RCameraSpline *, int))0x0007acd0)
#define RCameraSpline_BuildSplineEx ((void (__fastcall *)(RCameraSpline *, int, const Coord3 *start, const Coord3 *startControl, const Coord3 *end, const Coord3 *endControl))0x0007a650)
#define RCameraSpline_EvaluateSpline ((void (__fastcall *)(RCameraSpline *, int, float t, Coord4 *point))0x0007a530)
#define Crt_atexit ((int (*)(void (*)()))0x00132a7b)

// GetSegmentCurveStep's static spline and its guard
#define CurveSpline ((RCameraSpline *)0x0023e100)
#define CurveSplineGuard U32_AT(0x0023e170)

// The lane tables in the game's data: lane offsets, rows of six by lane type, and lane weights, rows of 32 - one set
// for the tracks snow2a_mis4 and snow2a_race, one for the others
#define SnowLaneOffsets ((const float (*)[6])0x001ca000)
#define LaneOffsets ((const float (*)[6])0x001ca060)
#define SnowLaneWeights ((const int8_t (*)[32])0x001ca0c0)
#define LaneWeights ((const int8_t (*)[32])0x001ca200)

namespace {

// The track's CARP tags
constexpr uint32_t kRoadNetworkGroupTag = 0x524e6770;  // 'RNgp'
constexpr uint32_t kHeaderTag = 0x524e6864;            // 'RNhd'
constexpr uint32_t kNodeTag = 0x726e0000;              // 'rn' and the index
constexpr uint32_t kSegmentTag = 0x72730000;           // 'rs'
constexpr uint32_t kIntersectionTag = 0x72690000;      // 'ri'
constexpr uint32_t kJunctionTag = 0x726a0000;          // 'rj'
constexpr uint32_t kRoadTag = 0x72720000;              // 'rr'

const uintptr_t kCurveSplineAtExit = 0x0015d070;      // DestroyStatic_0023e100 (engine/StaticInit.cpp)

constexpr float kLaneWidth = 5.0f;
constexpr float kHalfLaneWidth = 2.5f;

// An indexed tag: the type with the index in its low half, -1 for the type over two spaces
uint32_t IndexedTag(uint32_t type, int index) {
    return index == -1 ? type | 0x2020 : type | index;
}

// One kind of the network's records into its table, by index; an index without a record keeps what the table had.
template <class T>
void TableRecords(UGroup *group, uint32_t type, T **table, int count) {
    for (int i = 0; i < count; i++) {
        UData *record = group->DataLocateTag(IndexedTag(type, i));
        if (record != group->DataEnd())
            table[i] = reinterpret_cast<T *>(record->Data());
    }
}

// The SSE interpolation the original inlines: each step rounds to float.
float Lerp(float from, float to, float t) {
    float difference = to - from;
    float scaled = t * difference;
    return scaled + from;
}

} // namespace

// FUNC_AT(0x000c8940)
WRoadNetwork* WRoadNetwork::Get() {
    return fgRoadNetworkData.instance;
}

// FUNC_AT(0x000c8950)
void WRoadNetwork::Init() {
    WRoadNetworkData &data = fgRoadNetworkData;
    if (data.instance != NULL)
        return;
    data.instance = static_cast<WRoadNetwork *>(OperatorNew(sizeof(WRoadNetwork)));

    const char *track = fgWorld->trackName;
    if (strcmp(track, "snow2a_mis4") == 0 || strcmp(track, "snow2a_race") == 0) {
        for (int i = 0; i < 6; i++)
            data.laneOffsets[i] = SnowLaneOffsets[i];
        for (int i = 0; i < 32; i++)
            data.laneWeights[i] = SnowLaneWeights[i];
    } else {
        for (int i = 0; i < 6; i++)
            data.laneOffsets[i] = LaneOffsets[i];
        for (int i = 0; i < 32; i++)
            data.laneWeights[i] = LaneWeights[i];
    }

    data.unknownAC = 1;
    data.loaded = 0;
    data.unknownAD = 0;
    data.junctionCount = 0;
    data.roadCount = 0;
    data.intersectionCount = 0;
    data.segmentCount = 0;
    data.nodeCount = 0;

    UGroup *group = fgWorld->group->GroupLocateTag(kRoadNetworkGroupTag);
    if (group == fgWorld->group->GetArray() + fgWorld->group->GroupCount())
        return;
    const WRoadNetworkHeader *header =
        reinterpret_cast<const WRoadNetworkHeader *>(group->DataLocateTag(kHeaderTag)->Data());
    data.nodeCount = header->nodeCount;
    data.segmentCount = header->segmentCount;
    data.intersectionCount = header->intersectionCount;
    data.junctionCount = header->junctionCount;
    data.roadCount = header->roadCount;
    if (data.nodeCount == 0) {
        data.loaded = 0;
        return;
    }

    data.nodes = static_cast<WRoadNode **>(UMemory::Alloc(data.nodeCount * sizeof(WRoadNode *), 0, "WRoadNet Nodes"));
    data.segments = static_cast<WRoadSegment **>(
        UMemory::Alloc(data.segmentCount * sizeof(WRoadSegment *), 0, "WRoadNet Segments"));
    data.intersections = static_cast<WRoadIntersection **>(
        UMemory::Alloc(data.intersectionCount * sizeof(WRoadIntersection *), 0, "WRoadNet Inters"));
    data.junctions = static_cast<WRoadJunction **>(
        UMemory::Alloc(data.junctionCount * sizeof(WRoadJunction *), 0, "WRoadNet Nodes"));
    data.roads = static_cast<WRoad **>(UMemory::Alloc(data.roadCount * sizeof(WRoad *), 0, "WRoadNetwork Nodes"));

    TableRecords(group, kNodeTag, data.nodes, data.nodeCount);

    for (int i = 0; i < data.segmentCount; i++) {
        UData *record = group->DataLocateTag(IndexedTag(kSegmentTag, i));
        if (record == group->DataEnd())
            continue;
        data.segments[i] = reinterpret_cast<WRoadSegment *>(record->Data());
        data.segments[i]->queryStamp = data.queryStamp;
        uint16_t flags = data.segments[i]->flags;
        if (!(flags & kSegmentFlag08) && !(flags & (kSegmentSpecial | kSegmentFlag8000)))
            data.unknownAD = 1;
    }

    TableRecords(group, kIntersectionTag, data.intersections, data.intersectionCount);
    TableRecords(group, kJunctionTag, data.junctions, data.junctionCount);

    for (int i = 0; i < data.roadCount; i++) {
        UData *record = group->DataLocateTag(IndexedTag(kRoadTag, i));
        if (record == group->DataEnd())
            continue;
        data.roads[i] = reinterpret_cast<WRoad *>(record->Data());
        data.roads[i]->count2e = 0;
        data.roads[i]->count2f = 0;
    }

    data.loaded = 1;
}

// FUNC_AT(0x000c8de0)
void WRoadNetwork::Restart() {
    WRoadNetworkData &data = fgRoadNetworkData;
    data.queryStamp = 0;
    for (int i = 0; i < data.segmentCount; i++)
        data.segments[i]->queryStamp = data.queryStamp;
    for (int i = 0; i < data.roadCount; i++) {
        data.roads[i]->count2e = 0;
        data.roads[i]->count2f = 0;
    }
}

// FUNC_AT(0x000c8e60)
int WRoadNetwork::GetSpecialRoads(int *segments, int max) {
    int found = 0;
    for (int i = 0; i < fgRoadNetworkData.segmentCount; i++) {
        WRoadSegment *segment = fgRoadNetworkData.segments[i];
        if ((segment->flags & kSegmentSpecial) && segment->unknown59 != 0) {
            *segments++ = i;
            found++;
            if (found == max)
                break;
        }
    }
    return found;
}

// FUNC_AT(0x000c8eb0)
int WRoadNetwork::GetSegmentLaneType(WRoadSegment *segment) {
    uint16_t flags = segment->flags;
    if (flags & kSegmentLaneType3)
        return 3;
    if (flags & kSegmentLaneType2)
        return (flags & kSegmentLanes) ? 2 : 0;
    return (flags & kSegmentLanes) ? 1 : 0;
}

// The first lane whose offset reaches past `offset` by less than half a lane, no further out than the segment's
// lanes on that side.
// FUNC_AT(0x000c8ef0)
int WRoadNetwork::GetSegmentLaneIndex(WRoadSegment *segment, float *offset) {
    const float *laneOffsets = fgRoadNetworkData.laneOffsets[GetSegmentLaneType(segment)];
    float distance = fabsf(*offset);
    int lane = 0;
    float laneOffset = 0.0f;
    for (int i = 1; i < 6; i++) {
        if ((double)kHalfLaneWidth + laneOffsets[i] > distance) {
            lane = i;
            laneOffset = laneOffsets[i];
            break;
        }
    }
    if (*offset < 0.0f && lane > segment->leftLanes) {
        lane = segment->leftLanes;
        laneOffset = laneOffsets[lane];
    } else if (*offset > 0.0f && lane > segment->rightLanes) {
        lane = segment->rightLanes;
        laneOffset = laneOffsets[lane];
    }
    if (*offset < 0.0f) {
        lane = -lane;
        laneOffset = -laneOffset;
    }
    *offset = laneOffset;
    return lane;
}

// FUNC_AT(0x000c8fd0)
WRoadNode* WRoadNetwork::GetSegmentOppNode(WRoadSegment *segment, WRoadNode *node) {
    WRoadNode *start = fgRoadNetworkData.nodes[segment->node[0]];
    WRoadNode *end = fgRoadNetworkData.nodes[segment->node[1]];
    return node == start ? end : start;
}

// FUNC_AT(0x000c9000)
double WRoadNetwork::GetLinePointIntersect(const Coord3 *start, const Coord3 *end, const Coord3 *point, Coord3 *out,
                                           bool clamp) {
    Coord4 along;
    Coord4 toPoint;
    VU0_v4sub(end, start, &along);
    VU0_v4unitxyz(&along, &along);
    VU0_v4sub(point, start, &toPoint);
    float distance = vec3distance(point, start);
    VU0_v4unitxyz(&toPoint, &toPoint);
    float projected = (float)((double)v3dotprod(&along, &toPoint) * distance);
    float length = vec3distance(start, end);
    if (clamp) {
        if (projected >= length) {
            *out = *end;
            return 1.0;
        }
        if (projected <= 0.0f) {
            *out = *start;
            return 0.0;
        }
    }
    VU0_v4unitxyz(&along, &along);
    VU0_v4scale(&along, projected, &along);
    VU0_v3add(start, &along, out);
    return (double)projected / length;
}

// FUNC_AT(0x000c9aa0)
WRoadSegment* GetAttachedDirectionalSegment(WRoadNode *node, short excluded) {
    for (int i = 0; i < node->segmentCount; i++) {
        WRoadSegment *segment = fgRoadNetworkData.segments[node->segments[i]];
        if (segment->index != excluded && !(segment->flags & kSegmentNonDirectional))
            return segment;
    }
    return NULL;
}

// FUNC_AT(0x000c9af0)
void WRoadNetwork::Shutdown() {
    WRoadNetworkData &data = fgRoadNetworkData;
    if (data.instance == NULL)
        return;
    if (data.nodes != NULL)
        UMemory::Free(data.nodes);
    if (data.segments != NULL)
        UMemory::Free(data.segments);
    if (data.intersections != NULL)
        UMemory::Free(data.intersections);
    if (data.junctions != NULL)
        UMemory::Free(data.junctions);
    if (data.roads != NULL)
        UMemory::Free(data.roads);
    data.nodes = NULL;
    data.segments = NULL;
    data.intersections = NULL;
    data.junctions = NULL;
    data.roads = NULL;
    if (data.instance != NULL)
        OperatorDelete(data.instance);
    data.instance = NULL;
}

// A non-directional segment's drivable width on the left is the widest of the directional segments' at its two
// ends, 5 if neither has one; a width of 0 reads as 5.
// FUNC_AT(0x000c9ba0)
double WRoadNetwork::GetSegmentLeftDrivableDist(WRoadSegment *segment) {
    uint16_t flags = segment->flags;
    double width = 0.0;
    if ((flags & kSegmentFlag08) || (flags & kSegmentFlag8000)) {
        width = segment->widths.left;
    } else if (flags & kSegmentNonDirectional) {
        WRoadNode *ends[2] = { fgRoadNetworkData.nodes[segment->node[0]], fgRoadNetworkData.nodes[segment->node[1]] };
        float widest = -10000.0f;
        for (int i = 0; i < 2; i++) {
            WRoadSegment *attached = GetAttachedDirectionalSegment(ends[i], segment->index);
            if (attached != NULL) {
                double attachedWidth = GetSegmentLeftDrivableDist(attached);
                if (!(widest > attachedWidth))
                    widest = (float)attachedWidth;
            }
        }
        if (!(widest > -10000.0f))
            return kLaneWidth;
        width = widest;
    } else {
        if (flags & kSegmentLaneType3)
            width = kLaneWidth;
        if (flags & kSegmentLanes)
            width += kHalfLaneWidth;
        if (flags & kSegmentLeftMargin)
            width += kLaneWidth;
        if (flags & kSegmentFlag1000)
            width += (double)(segment->rightLanes + segment->leftLanes) * kLaneWidth * 0.5;
        else
            width += (double)segment->leftLanes * kLaneWidth;
    }
    if (width == 0.0)
        width = kLaneWidth;
    return width;
}

// The same on the right.
// FUNC_AT(0x000c9cd0)
double WRoadNetwork::GetSegmentRightDrivableDist(WRoadSegment *segment) {
    uint16_t flags = segment->flags;
    double width = 0.0;
    if ((flags & kSegmentFlag08) || (flags & kSegmentFlag8000)) {
        width = segment->widths.right;
    } else if (flags & kSegmentNonDirectional) {
        WRoadNode *ends[2] = { fgRoadNetworkData.nodes[segment->node[0]], fgRoadNetworkData.nodes[segment->node[1]] };
        float widest = -10000.0f;
        for (int i = 0; i < 2; i++) {
            WRoadSegment *attached = GetAttachedDirectionalSegment(ends[i], segment->index);
            if (attached != NULL) {
                double attachedWidth = GetSegmentRightDrivableDist(attached);
                if (!(widest > attachedWidth))
                    widest = (float)attachedWidth;
            }
        }
        if (!(widest > -10000.0f))
            return kLaneWidth;
        width = widest;
    } else {
        if (flags & kSegmentLaneType3)
            width = kLaneWidth;
        if (flags & kSegmentLanes)
            width += kHalfLaneWidth;
        if (flags & kSegmentRightMargin)
            width += kLaneWidth;
        if (flags & kSegmentFlag1000)
            width += (double)(segment->rightLanes + segment->leftLanes) * kLaneWidth * 0.5;
        else
            width += (double)segment->rightLanes * kLaneWidth;
    }
    if (width == 0.0)
        width = kLaneWidth;
    return width;
}

// The row: two per lane type (up to two lanes, more), 8 for a single lane, 9 for lane type 0 without
// kSegmentLeftMargin. A segment with drivable widths instead marks the lanes past them -1, the rest 0.
// FUNC_AT(0x000c9e00)
void WRoadNetwork::GetSegmentLaneWeights(int segmentIndex, int8_t end, int8_t *weights, int size, int count) {
    WRoadSegment *segment = fgRoadNetworkData.segments[segmentIndex];
    if ((segment->flags & kSegmentFlag08) || (segment->flags & kSegmentFlag8000)) {
        int firstWidth = Ftol(end ? segment->widths.left : segment->widths.right);
        for (int i = 0; i < count; i++)
            weights[i] = i > count - firstWidth ? 0 : -1;
        int lastWidth = Ftol(end ? segment->widths.right : segment->widths.left);
        for (int i = size - count; i < size; i++)
            weights[i] = i < lastWidth + count ? 0 : -1;
        return;
    }

    if (segment->flags & kSegmentNonDirectional) {
        WRoadSegment *attached = GetAttachedDirectionalSegment(fgRoadNetworkData.nodes[segment->node[end]], -1);
        if (attached != NULL)
            segment = attached;
    }
    uint16_t flags = segment->flags;
    int row = GetSegmentLaneType(segment) * 2;
    int lanes = segment->rightLanes + segment->leftLanes;
    if (lanes > 2)
        row++;
    else if (lanes == 1)
        row = 8;
    if (!(flags & kSegmentLeftMargin) && row == 0)
        row = 9;
    for (int i = 0; i < count; i++)
        weights[i] = fgRoadNetworkData.laneWeights[row][i];
    for (int i = 0; i < count; i++)
        weights[size - count + i] = fgRoadNetworkData.laneWeights[row][count - 1 - i];
}

// FUNC_AT(0x000c9f70)
double WRoadNetwork::GetSegmentPointIntersect(WRoadSegment *segment, const Coord3 *point, Coord3 *out, bool clamp) {
    Coord3 start = fgRoadNetworkData.nodes[segment->node[0]]->position;
    Coord3 end = fgRoadNetworkData.nodes[segment->node[1]]->position;
    return GetLinePointIntersect(&start, &end, point, out, clamp);
}

// FUNC_AT(0x000c9fe0)
void WRoadNetwork::GetSegmentCurveStep(const Coord3 *start, const Coord3 *end, WRoadSegment *segment, float t,
                                       Coord3 *out) {
    if (!(CurveSplineGuard & 1)) {
        CurveSplineGuard |= 1;
        RCameraSpline_Construct(CurveSpline, 0);
        Crt_atexit(reinterpret_cast<void (*)()>(kCurveSplineAtExit));
    }
    RCameraSpline_BuildSplineEx(CurveSpline, 0, start, &segment->startControl, end, &segment->endControl);
    Coord4 point;
    RCameraSpline_EvaluateSpline(CurveSpline, 0, t, &point);
    out->x = point.x;
    out->y = point.y;
    out->z = point.z;
}

// FUNC_AT(0x000cb3f0)
double WRoadNetwork::GetSegmentLeftDrivableDist(int segment) {
    return Get()->GetSegmentLeftDrivableDist(fgRoadNetworkData.segments[segment]);
}

// FUNC_AT(0x000cb410)
double WRoadNetwork::GetSegmentRightDrivableDist(int segment) {
    return Get()->GetSegmentRightDrivableDist(fgRoadNetworkData.segments[segment]);
}

// FUNC_AT(0x000cb430)
void WRoadNetwork::GetPointOnSegment(const Coord3 *start, const Coord3 *end, WRoadSegment *segment, float t, Coord3 *out) {
    if (segment->flags & kSegmentCurved) {
        GetSegmentCurveStep(start, end, segment, t, out);
        return;
    }
    out->x = Lerp(start->x, end->x, t);
    out->y = Lerp(start->y, end->y, t);
    out->z = Lerp(start->z, end->z, t);
}

// FUNC_AT(0x000cbe70)
void WRoadNetwork::GetPointOnSegment(WRoadSegment *segment, float t, Coord3 *out) {
    if (t > 1.0f)
        t = 1.0f;
    else if (t < 0.0f)
        t = 0.0f;
    Coord3 start = fgRoadNetworkData.nodes[segment->node[0]]->position;
    Coord3 end = fgRoadNetworkData.nodes[segment->node[1]]->position;
    GetPointOnSegment(&start, &end, segment, t, out);
}
