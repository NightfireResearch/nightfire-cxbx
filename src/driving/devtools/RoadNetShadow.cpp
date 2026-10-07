#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "RoadNetShadow.h"

#include "../world/RoadNetwork.h"
#include "../../common/xbeOriginal.h"

#include <windows.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------------------------------------------
// NIGHTFIRE_ROADNETSHADOW=1, once on the first simulation tick: world/RoadNetwork.cpp against the originals, over
// the loaded track's road network.
//
// Two passes run the same tests, calling everything at the original addresses: the first with the ported
// functions' originals swapped back in, the second with our jumps. Each pass writes a log (results, output bytes, float and
// double bits; pointers into the track's data as they are); the logs must match line for line.
//
//   - every segment: lane type, both drivable widths (by pointer and by index), lane weights from either end (two
//     buffer shapes), the other node of either end, points along it at t in and out of 0-1 (both GetPointOnSegment
//     overloads, GetSegmentCurveStep on every segment), the nearest point to probes round it, clamped and not;
//   - lane indices at offsets swept across every segment, at the lane table's half-lane boundaries, and random;
//   - every node: its directional segment, excluding each of its segments, -1 and a random index;
//   - GetLinePointIntersect on random lines and points, including points past either end and on the line;
//   - GetSpecialRoads with several limits;
//   - Restart, and Init then Shutdown with the live tables set aside (the tables' contents, counts and flags
//     logged; the allocations compared by size).
//
// State: the static spline GetSegmentCurveStep keeps (saved before the first pass, put back before the second,
// compared after both); the statics, the segments' stamps and the roads' counts around Restart and Init.
// ---------------------------------------------------------------------------------------------------------------

namespace {

int g_cases, g_checks, g_diffs, g_faults;
std::string *g_log;

void Logf(const char *format, ...) {
    if (g_log == NULL)
        return;
    char line[8192];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    g_log->append(line);
    g_log->push_back('\n');
}

void LogBytes(const char *what, const void *p, size_t n) {
    if (g_log == NULL)
        return;
    g_log->append(what);
    g_log->push_back(' ');
    char h[4];
    for (size_t i = 0; i < n; i++) {
        snprintf(h, sizeof(h), "%02x", static_cast<const uint8_t *>(p)[i]);
        g_log->append(h);
    }
    g_log->push_back('\n');
}

uint32_t Bits(float f) {
    uint32_t u;
    memcpy(&u, &f, sizeof(u));
    return u;
}

unsigned long long Bits(double d) {
    unsigned long long u;
    memcpy(&u, &d, sizeof(u));
    return u;
}

// ---- the two sides

const unsigned kRanges[][2] = {
    { 0x000c8940, 0x000c9130 },     // Get .. GetLinePointIntersect
    { 0x000c9aa0, 0x000ca090 },     // GetAttachedDirectionalSegment .. GetSegmentCurveStep
    { 0x000cb3f0, 0x000cb4b0 },     // the index overloads, GetPointOnSegment
    { 0x000cbe70, 0x000cbf20 },     // GetPointOnSegment (segment)
};

struct Originals {
    bool on;
    explicit Originals(bool original) : on(original) {
        if (on)
            for (const auto &r : kRanges)
                XbeOriginal_RestoreRange(r[0], r[1], true);
    }
    ~Originals() {
        if (on)
            for (const auto &r : kRanges)
                XbeOriginal_RestoreRange(r[0], r[1], false);
    }
};

bool Safe(void (*thunk)(void *), void *context) {
#ifdef _MSC_VER
    __try {
        thunk(context);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_faults++;
        return false;
    }
#else
    thunk(context);
    return true;
#endif
}

// Runs f under SEH; a fault is logged (so the passes still line up) and counted.
template <class F>
void Guarded(const char *what, F f) {
    g_cases++;
    if (!Safe([](void *c) { (*static_cast<F *>(c))(); }, &f))
        Logf("%s FAULT", what);
}

struct Rng {
    uint32_t state;
    uint32_t Next() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }
    float Unit() { return float(Next() >> 8) * (1.0f / 16777216.0f); }
    float Range(float lo, float hi) { return lo + (hi - lo) * Unit(); }
    uint32_t Below(uint32_t n) { return n == 0 ? 0 : Next() % n; }
};

// ---- the functions under test, at their addresses (thiscall through __fastcall and a dummy EDX)

typedef WRoadNetwork *(*GetFn)();
typedef void (*LifecycleFn)();
typedef int (__fastcall *GetSpecialRoadsFn)(WRoadNetwork *, int, int *, int);
typedef int (__fastcall *LaneTypeFn)(WRoadNetwork *, int, WRoadSegment *);
typedef int (__fastcall *LaneIndexFn)(WRoadNetwork *, int, WRoadSegment *, float *);
typedef WRoadNode *(__fastcall *OppNodeFn)(WRoadNetwork *, int, WRoadSegment *, WRoadNode *);
typedef double (__fastcall *LinePointFn)(WRoadNetwork *, int, const Coord3 *, const Coord3 *, const Coord3 *,
                                         Coord3 *, bool);
typedef WRoadSegment *(*AttachedFn)(WRoadNode *, short);
typedef double (__fastcall *DrivableFn)(WRoadNetwork *, int, WRoadSegment *);
typedef double (__fastcall *DrivableIndexFn)(WRoadNetwork *, int, int);
typedef void (__fastcall *LaneWeightsFn)(WRoadNetwork *, int, int, int8_t, int8_t *, int, int);
typedef double (__fastcall *SegmentPointFn)(WRoadNetwork *, int, WRoadSegment *, const Coord3 *, Coord3 *, bool);
typedef void (__fastcall *PointBetweenFn)(WRoadNetwork *, int, const Coord3 *, const Coord3 *, WRoadSegment *, float,
                                          Coord3 *);
typedef void (__fastcall *PointOnSegmentFn)(WRoadNetwork *, int, WRoadSegment *, float, Coord3 *);

#define GetAt ((GetFn)0x000c8940)
#define InitAt ((LifecycleFn)0x000c8950)
#define RestartAt ((LifecycleFn)0x000c8de0)
#define GetSpecialRoadsAt ((GetSpecialRoadsFn)0x000c8e60)
#define GetSegmentLaneTypeAt ((LaneTypeFn)0x000c8eb0)
#define GetSegmentLaneIndexAt ((LaneIndexFn)0x000c8ef0)
#define GetSegmentOppNodeAt ((OppNodeFn)0x000c8fd0)
#define GetLinePointIntersectAt ((LinePointFn)0x000c9000)
#define GetAttachedDirectionalSegmentAt ((AttachedFn)0x000c9aa0)
#define ShutdownAt ((LifecycleFn)0x000c9af0)
#define LeftDrivableAt ((DrivableFn)0x000c9ba0)
#define RightDrivableAt ((DrivableFn)0x000c9cd0)
#define GetSegmentLaneWeightsAt ((LaneWeightsFn)0x000c9e00)
#define GetSegmentPointIntersectAt ((SegmentPointFn)0x000c9f70)
#define GetSegmentCurveStepAt ((PointBetweenFn)0x000c9fe0)
#define LeftDrivableIndexAt ((DrivableIndexFn)0x000cb3f0)
#define RightDrivableIndexAt ((DrivableIndexFn)0x000cb410)
#define GetPointBetweenAt ((PointBetweenFn)0x000cb430)
#define GetPointOnSegmentAt ((PointOnSegmentFn)0x000cbe70)

// GetSegmentCurveStep's static spline and its guard
uint8_t *const kSplineState = reinterpret_cast<uint8_t *>(0x0023e100);
const size_t kSplineStateSize = 0x74;

// ---- what the tests see of the network

WRoadNetworkData &Net() { return fgRoadNetworkData; }

Coord4 NodePosition(const WRoadSegment *segment, int end) {
    const Coord3 &p = Net().nodes[segment->node[end]]->position;
    return Coord4{ p.x, p.y, p.z, 0.0f };
}

const Coord3 *C3(const Coord4 *p) { return reinterpret_cast<const Coord3 *>(p); }

void LogPoint(const char *what, const Coord3 &p) {
    Logf("%s %08x %08x %08x", what, Bits(p.x), Bits(p.y), Bits(p.z));
}

const Coord3 kUnwritten = { -12345.0f, -12345.0f, -12345.0f };

// ---- the tests

void TestSegments(Rng &rng) {
    WRoadNetwork *network = GetAt();
    WRoadNetworkData &net = Net();
    static const float kTimes[] = { -0.5f, 0.0f, 0.1f, 0.25f, 0.5f, 0.75f, 0.9f, 1.0f, 1.5f };
    for (int i = 0; i < net.segmentCount; i++) {
        WRoadSegment *segment = net.segments[i];
        Logf("segment %d flags %04x", i, segment->flags);

        Guarded("lane type", [&] { Logf(" type %d", GetSegmentLaneTypeAt(network, 0, segment)); });
        Guarded("drivable", [&] {
            double left = LeftDrivableAt(network, 0, segment);
            double right = RightDrivableAt(network, 0, segment);
            double leftIndex = LeftDrivableIndexAt(network, 0, i);
            double rightIndex = RightDrivableIndexAt(network, 0, i);
            Logf(" drivable %016llx %016llx %016llx %016llx", Bits(left), Bits(right), Bits(leftIndex),
                 Bits(rightIndex));
        });
        for (int end = 0; end < 2; end++) {
            Guarded("lane weights", [&] {
                int8_t weights[64];
                memset(weights, 0x5a, sizeof(weights));
                GetSegmentLaneWeightsAt(network, 0, i, int8_t(end), weights, 64, 32);
                LogBytes(" weights", weights, sizeof(weights));
                memset(weights, 0x5a, sizeof(weights));
                GetSegmentLaneWeightsAt(network, 0, i, int8_t(end), weights, 40, 16);
                LogBytes(" weights40", weights, sizeof(weights));
            });
            Guarded("opposite node", [&] {
                WRoadNode *node = net.nodes[segment->node[end]];
                Logf(" opposite %p", static_cast<void *>(GetSegmentOppNodeAt(network, 0, segment, node)));
            });
        }
        Guarded("opposite of another", [&] {
            WRoadNode *other = net.nodes[rng.Below(uint32_t(net.nodeCount))];
            Logf(" opposite %p", static_cast<void *>(GetSegmentOppNodeAt(network, 0, segment, other)));
        });

        // points along it
        for (float t : kTimes) {
            Guarded("point on segment", [&] {
                Coord4 start = NodePosition(segment, 0);
                Coord4 end = NodePosition(segment, 1);
                Coord3 a = kUnwritten, b = kUnwritten, c = kUnwritten;
                GetPointOnSegmentAt(network, 0, segment, t, &a);
                GetPointBetweenAt(network, 0, C3(&start), C3(&end), segment, t, &b);
                GetSegmentCurveStepAt(network, 0, C3(&start), C3(&end), segment, t, &c);
                LogPoint(" on", a);
                LogPoint(" between", b);
                LogPoint(" curve", c);
            });
        }
        Guarded("point on segment random", [&] {
            float t = rng.Range(-0.2f, 1.2f);
            Coord3 a = kUnwritten;
            GetPointOnSegmentAt(network, 0, segment, t, &a);
            LogPoint(" on", a);
        });

        // probes round it
        for (int k = 0; k < 6; k++) {
            Guarded("segment point", [&] {
                Coord4 start = NodePosition(segment, 0);
                Coord4 end = NodePosition(segment, 1);
                float t = rng.Range(-0.5f, 1.5f);
                Coord4 probe = { start.x + (end.x - start.x) * t + rng.Range(-30.0f, 30.0f),
                                 start.y + (end.y - start.y) * t + rng.Range(-5.0f, 5.0f),
                                 start.z + (end.z - start.z) * t + rng.Range(-30.0f, 30.0f), 0.0f };
                for (int clamp = 0; clamp < 2; clamp++) {
                    Coord3 out = kUnwritten;
                    double at = GetSegmentPointIntersectAt(network, 0, segment, C3(&probe), &out, clamp != 0);
                    Logf(" nearest %016llx", Bits(at));
                    LogPoint(" nearest", out);
                }
            });
        }

        // lane indices across it
        Guarded("lane index sweep", [&] {
            std::string line = " lanes";
            char item[32];
            for (float offset = -32.0f; offset <= 32.0f; offset += 0.75f) {
                float value = offset;
                int lane = GetSegmentLaneIndexAt(network, 0, segment, &value);
                snprintf(item, sizeof(item), " %d:%08x", lane, Bits(value));
                line += item;
            }
            Logf("%s", line.c_str());
        });
        Guarded("lane index boundaries", [&] {
            std::string line = " boundaries";
            char item[32];
            for (int type = 0; type < 4; type++) {
                for (int lane = 0; lane < 6; lane++) {
                    float edge = net.laneOffsets[type][lane] + 2.5f;
                    const float candidates[] = { edge, -edge, nextafterf(edge, 0.0f), -nextafterf(edge, 100.0f),
                                                 rng.Range(-30.0f, 30.0f) };
                    for (float c : candidates) {
                        float value = c;
                        int index = GetSegmentLaneIndexAt(network, 0, segment, &value);
                        snprintf(item, sizeof(item), " %d:%08x", index, Bits(value));
                        line += item;
                    }
                }
            }
            float zeros[] = { 0.0f, -0.0f };
            for (float z : zeros) {
                float value = z;
                int index = GetSegmentLaneIndexAt(network, 0, segment, &value);
                snprintf(item, sizeof(item), " %d:%08x", index, Bits(value));
                line += item;
            }
            Logf("%s", line.c_str());
        });
    }
}

void TestNodes(Rng &rng) {
    WRoadNetworkData &net = Net();
    for (int i = 0; i < net.nodeCount; i++) {
        WRoadNode *node = net.nodes[i];
        Guarded("attached", [&] {
            std::string line;
            char item[32];
            snprintf(item, sizeof(item), "node %d:", i);
            line = item;
            for (int k = 0; k < node->segmentCount; k++) {
                WRoadSegment *found = GetAttachedDirectionalSegmentAt(node, short(node->segments[k]));
                snprintf(item, sizeof(item), " %p", static_cast<void *>(found));
                line += item;
            }
            WRoadSegment *any = GetAttachedDirectionalSegmentAt(node, -1);
            WRoadSegment *random = GetAttachedDirectionalSegmentAt(node, short(rng.Below(uint32_t(net.segmentCount))));
            snprintf(item, sizeof(item), " %p", static_cast<void *>(any));
            line += item;
            snprintf(item, sizeof(item), " %p", static_cast<void *>(random));
            line += item;
            Logf("%s", line.c_str());
        });
    }
}

void TestLines(Rng &rng) {
    WRoadNetwork *network = GetAt();
    WRoadNetworkData &net = Net();
    for (int k = 0; k < 2000; k++) {
        Guarded("line point", [&] {
            Coord4 start, end, point;
            if (k % 2 == 0 && net.nodeCount > 1) {
                const Coord3 &a = net.nodes[rng.Below(uint32_t(net.nodeCount))]->position;
                const Coord3 &b = net.nodes[rng.Below(uint32_t(net.nodeCount))]->position;
                start = Coord4{ a.x, a.y, a.z, 0.0f };
                end = Coord4{ b.x, b.y, b.z, 0.0f };
            } else {
                start = Coord4{ rng.Range(-1000.0f, 1000.0f), rng.Range(-20.0f, 20.0f), rng.Range(-1000.0f, 1000.0f),
                                0.0f };
                end = Coord4{ start.x + rng.Range(-100.0f, 100.0f), start.y + rng.Range(-5.0f, 5.0f),
                              start.z + rng.Range(-100.0f, 100.0f), 0.0f };
            }
            float t;
            switch (k % 5) {
            case 0: t = -rng.Unit(); break;            // before the start
            case 1: t = 1.0f + rng.Unit(); break;      // past the end
            case 2: t = 0.0f; break;                   // the start itself
            case 3: t = 1.0f; break;                   // the end itself
            default: t = rng.Unit(); break;
            }
            float side = (k % 7 == 0) ? 0.0f : rng.Range(-20.0f, 20.0f);
            point = Coord4{ start.x + (end.x - start.x) * t + side, start.y + (end.y - start.y) * t,
                            start.z + (end.z - start.z) * t - side, 0.0f };
            for (int clamp = 0; clamp < 2; clamp++) {
                Coord3 out = kUnwritten;
                double at = GetLinePointIntersectAt(network, 0, C3(&start), C3(&end), C3(&point), &out, clamp != 0);
                Logf("line %d %016llx %08x %08x %08x", k, Bits(at), Bits(out.x), Bits(out.y), Bits(out.z));
            }
        });
    }
}

void TestSpecialRoads() {
    WRoadNetwork *network = GetAt();
    static const int kLimits[] = { 1, 2, 3, 32, 100000 };
    for (int limit : kLimits) {
        Guarded("special roads", [&] {
            std::vector<int> found(size_t(limit < 4096 ? limit : 4096) + 4, -7);
            int max = limit < 4096 ? limit : 4096;
            int count = GetSpecialRoadsAt(network, 0, found.data(), max);
            LogBytes("special", found.data(), found.size() * sizeof(int));
            Logf("special %d of %d", count, max);
        });
    }
}

// The live segments' stamps and roads' counts, which Restart and Init write
struct LiveRecords {
    std::vector<uint32_t> stamps;
    std::vector<uint8_t> counts;

    void Save() {
        WRoadNetworkData &net = Net();
        stamps.clear();
        counts.clear();
        for (int i = 0; i < net.segmentCount; i++)
            stamps.push_back(net.segments[i]->queryStamp);
        for (int i = 0; i < net.roadCount; i++) {
            counts.push_back(net.roads[i]->count2e);
            counts.push_back(net.roads[i]->count2f);
        }
    }
    void Restore(const WRoadNetworkData &net) const {
        for (int i = 0; i < net.segmentCount; i++)
            net.segments[i]->queryStamp = stamps[size_t(i)];
        for (int i = 0; i < net.roadCount; i++) {
            net.roads[i]->count2e = counts[size_t(2 * i)];
            net.roads[i]->count2f = counts[size_t(2 * i + 1)];
        }
    }
};

void LogStatics(const char *what, const WRoadNetworkData &net) {
    Logf("%s counts %d %d %d %d %d flags %d %d %d stamp %u", what, net.nodeCount, net.segmentCount,
         net.intersectionCount, net.junctionCount, net.roadCount, net.unknownAC, net.unknownAD, net.loaded,
         net.queryStamp);
    LogBytes(" lane tables", &net, offsetof(WRoadNetworkData, junctionCount));
}

void TestRestart() {
    WRoadNetworkData saved = Net();
    LiveRecords live;
    live.Save();
    Guarded("restart", [&] {
        RestartAt();
        WRoadNetworkData &net = Net();
        LogStatics("restart", net);
        for (int i = 0; i < net.segmentCount; i++)
            Logf(" stamp %d %u", i, net.segments[i]->queryStamp);
        for (int i = 0; i < net.roadCount; i++)
            Logf(" road %d %u %u", i, net.roads[i]->count2e, net.roads[i]->count2f);
    });
    Net() = saved;
    live.Restore(saved);
}

void TestInitShutdown() {
    WRoadNetworkData saved = Net();
    LiveRecords live;
    live.Save();
    Guarded("init", [&] {
        WRoadNetworkData &net = Net();
        net.instance = NULL;
        net.nodes = NULL;
        net.segments = NULL;
        net.intersections = NULL;
        net.junctions = NULL;
        net.roads = NULL;
        net.nodeCount = net.segmentCount = net.intersectionCount = net.junctionCount = net.roadCount = -1;
        net.unknownAC = net.unknownAD = net.loaded = 0x77;
        InitAt();
        LogStatics("init", net);
        Logf(" instance %d", net.instance != NULL);
        if (net.loaded) {
            LogBytes(" nodes", net.nodes, size_t(net.nodeCount) * sizeof(void *));
            LogBytes(" segments", net.segments, size_t(net.segmentCount) * sizeof(void *));
            LogBytes(" intersections", net.intersections, size_t(net.intersectionCount) * sizeof(void *));
            LogBytes(" junctions", net.junctions, size_t(net.junctionCount) * sizeof(void *));
            LogBytes(" roads", net.roads, size_t(net.roadCount) * sizeof(void *));
            for (int i = 0; i < net.segmentCount; i++)
                if (net.segments[i]->queryStamp != net.queryStamp)
                    Logf(" segment %d stamp %u", i, net.segments[i]->queryStamp);
            for (int i = 0; i < net.roadCount; i++)
                if (net.roads[i]->count2e != 0 || net.roads[i]->count2f != 0)
                    Logf(" road %d counts %u %u", i, net.roads[i]->count2e, net.roads[i]->count2f);
        }
        ShutdownAt();
        Logf("shutdown %p %p %p %p %p %p", static_cast<void *>(net.instance), static_cast<void *>(net.nodes),
             static_cast<void *>(net.segments), static_cast<void *>(net.intersections),
             static_cast<void *>(net.junctions), static_cast<void *>(net.roads));
    });
    Net() = saved;
    live.Restore(saved);
}

void RunPass(bool original, std::string *log) {
    g_log = log;
    Originals originals(original);
    Rng rng = { 0x2545f491u };
    TestSegments(rng);
    TestNodes(rng);
    TestLines(rng);
    TestSpecialRoads();
    TestRestart();
    TestInitShutdown();
    g_log = NULL;
}

void SplitLines(const std::string &text, std::vector<std::string> *lines) {
    size_t at = 0;
    while (at < text.size()) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos)
            end = text.size();
        lines->push_back(text.substr(at, end - at));
        at = end + 1;
    }
}

}  // namespace

void RoadNetShadow_Run(void) {
    const char *env = getenv("NIGHTFIRE_ROADNETSHADOW");
    if (env == NULL || atoi(env) == 0)
        return;
    WRoadNetworkData &net = Net();
    if (net.instance == NULL || !net.loaded || net.segmentCount <= 0) {
        printf("[roadnet] no road network loaded: nothing tested\n");
        fflush(stdout);
        return;
    }

    int curved = 0, widths = 0, nonDirectional = 0;
    for (int i = 0; i < net.segmentCount; i++) {
        uint16_t flags = net.segments[i]->flags;
        curved += (flags & kSegmentCurved) != 0;
        widths += (flags & (kSegmentFlag08 | kSegmentFlag8000)) != 0;
        nonDirectional += (flags & kSegmentNonDirectional) != 0;
    }

    // The spline's first use constructs it and registers its destructor: let the original do that once, before
    // either pass, so both passes find it made.
    if (!(*reinterpret_cast<uint32_t *>(kSplineState + 0x70) & 1)) {
        Originals originals(true);
        WRoadSegment *segment = net.segments[0];
        Coord4 start = NodePosition(segment, 0);
        Coord4 end = NodePosition(segment, 1);
        Coord3 out;
        Guarded("spline construction", [&] {
            GetSegmentCurveStepAt(GetAt(), 0, C3(&start), C3(&end), segment, 0.5f, &out);
        });
        g_cases = 0;
    }

    std::vector<uint8_t> before(kSplineState, kSplineState + kSplineStateSize);
    std::string logOriginal, logPort;
    RunPass(true, &logOriginal);
    std::vector<uint8_t> afterOriginal(kSplineState, kSplineState + kSplineStateSize);
    memcpy(kSplineState, before.data(), kSplineStateSize);
    RunPass(false, &logPort);
    std::vector<uint8_t> afterPort(kSplineState, kSplineState + kSplineStateSize);

    std::vector<std::string> a, b;
    SplitLines(logOriginal, &a);
    SplitLines(logPort, &b);
    size_t n = a.size() > b.size() ? a.size() : b.size();
    for (size_t i = 0; i < n; i++) {
        g_checks++;
        const std::string none = "(missing)";
        const std::string &x = i < a.size() ? a[i] : none;
        const std::string &y = i < b.size() ? b[i] : none;
        if (x != y) {
            if (g_diffs < 10)
                printf("[roadnet] line %u differs:\n  original %.300s\n  port     %.300s\n", unsigned(i), x.c_str(),
                       y.c_str());
            g_diffs++;
        }
    }
    g_checks++;
    if (afterOriginal != afterPort) {
        printf("[roadnet] the curve spline's state differs\n");
        g_diffs++;
    }

    printf("[roadnet] network: %d nodes, %d segments (%d curved, %d with widths, %d non-directional), %d "
           "intersections, %d roads\n", net.nodeCount, net.segmentCount, curved, widths, nonDirectional,
           net.intersectionCount, net.roadCount);
    printf("[roadnet] road network vs originals: %d cases, %d checks, %d differ%s\n", g_cases / 2, g_checks, g_diffs,
           g_faults ? " (with faults)" : "");
    if (g_faults)
        printf("[roadnet] %d faults\n", g_faults);
    fflush(stdout);
}
