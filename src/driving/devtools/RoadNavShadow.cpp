#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "RoadNavShadow.h"

#include "../world/RoadNav.h"
#include "../world/RoadNetwork.h"
#include "../engine/SimRandom.h"
#include "../../common/xbeOriginal.h"

#include <windows.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

// ---------------------------------------------------------------------------------------------------------------
// NIGHTFIRE_ROADNAVSHADOW=1, once on the first simulation tick: world/RoadNav.cpp against the originals, on the
// loaded track's road network.
//
// One navigator (made by the constructor under test) is placed with InitAtPoint at points round a spread of the
// track's segments, in each of the five modes (with random nextChoice, unknown04, unknownB1/B2, headings, and the
// no-lane flag), then driven: IncNavPosition with random steps, headings and target segments, mixed with
// ChangeLanes, UpdateLaneChange, ReverseNavDirection, InitAtSegment, PathShare/PathForwardRoadSegment, the four
// CalcNextSegment* called directly (with their out-flags aliasing the navigator, as IncNavPosition passes them, or
// not), SetBoundPos and SetControlPos on random segments, and FindClosestSegmentInd at random points.
//
// Every operation runs twice from the same state, calling the functions at their addresses: first with the
// ported functions' originals swapped back in, then with our jumps. The state is snapshotted before the first run, put
// back before the second, and compared after both: the navigator's 0xc0 bytes, its spline's 0x70, the
// simulation's random generator, the network's query stamp and every segment's stamp, the static spline
// WRoadNetwork::GetSegmentCurveStep keeps, and the heading vector (normalised in place by the steering modes);
// with the operation's outputs (results, out-parameters). The constructor and destructor are compared by content
// (the spline's address masked), Reset on a scrambled navigator.
//
// A mutation it catches: SetBoundPos choosing its node with `forward ? atStart : !atStart` puts the curve's ends
// at the wrong nodes, and the navigator's bytes at +0x50 differ after the first InitAtPoint.
// ---------------------------------------------------------------------------------------------------------------

namespace {

int g_cases, g_checks, g_diffs, g_faults, g_details;
bool g_verbose;   // NIGHTFIRE_ROADNAVSHADOW=2: each case named before it runs, to find one that does not return

const unsigned kRanges[][2] = {
    { 0x000c9130, 0x000c9aa0 },     // Reset .. UpdateLaneChange
    { 0x000ca090, 0x000cb3f0 },     // the constructor .. SetControlPos
    { 0x000cb4b0, 0x000cbe70 },     // IncNavPosition, InitAtSegment, ChangeLanes
    { 0x000cbf20, 0x000cc840 },     // FindClosestSegmentInd, InitAtPoint
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

typedef void *(__fastcall *ConstructFn)(void *, int);
typedef void (__fastcall *PlainFn)(void *, int);
typedef void (__fastcall *InitAtPointFn)(void *, int, const Coord3 *, const Coord3 *, bool);
typedef void (__fastcall *InitAtSegmentFn)(void *, int, short, int8_t, float, bool);
typedef int (__fastcall *FindClosestFn)(void *, int, const Coord4 *, Coord3 *, float *);
typedef void (__fastcall *SetBoundPosFn)(void *, int, WRoadSegment *, float, Coord3 *, bool);
typedef void (__fastcall *SetControlPosFn)(void *, int, WRoadSegment *, Coord3 *, bool);
typedef void (__fastcall *IncNavFn)(void *, int, float, Coord3 *, short);
typedef void (__fastcall *ChangeLanesFn)(void *, int, float, float);
typedef uint8_t (__fastcall *UpdateLaneChangeFn)(void *, int, float);
typedef uint8_t (__fastcall *PathShareFn)(void *, int, short);
typedef uint8_t (__fastcall *PathForwardFn)(void *, int, short, bool *);
typedef short (__fastcall *CalcRandomFn)(void *, int, Coord3 *, uint8_t *, bool *);
typedef short (__fastcall *CalcDirectionFn)(void *, int, Coord3 *, short *, uint8_t *, bool *);
typedef short (__fastcall *CalcLaneFn)(void *, int, float *, uint8_t *, bool *);

#define ResetAt ((PlainFn)0x000c9130)
#define CalcRandomAt ((CalcRandomFn)0x000c9240)
#define ReverseAt ((PlainFn)0x000c95a0)
#define SetBoundPosAt ((SetBoundPosFn)0x000c96b0)
#define PathShareAt ((PathShareFn)0x000c97b0)
#define PathForwardAt ((PathForwardFn)0x000c9810)
#define UpdateLaneChangeAt ((UpdateLaneChangeFn)0x000c99f0)
#define ConstructAt ((ConstructFn)0x000ca090)
#define DestructAt ((PlainFn)0x000ca100)
#define CalcDirectionAt ((CalcDirectionFn)0x000ca120)
#define CalcLaneAt ((CalcLaneFn)0x000ca530)
#define CalcSidewalkAt ((CalcLaneFn)0x000cad40)
#define SetControlPosAt ((SetControlPosFn)0x000cb280)
#define IncNavAt ((IncNavFn)0x000cb4b0)
#define InitAtSegmentAt ((InitAtSegmentFn)0x000cb900)
#define ChangeLanesAt ((ChangeLanesFn)0x000cbcf0)
#define FindClosestAt ((FindClosestFn)0x000cbf20)
#define InitAtPointAt ((InitAtPointFn)0x000cc100)

SimRandom *Random() { return *reinterpret_cast<SimRandom **>(0x00233ff0); }

uint8_t *const kCurveState = reinterpret_cast<uint8_t *>(0x0023e100);   // GetSegmentCurveStep's spline
const size_t kCurveStateSize = 0x74;
const size_t kSplineSize = 0x70;

alignas(16) uint8_t g_navBytes[sizeof(WRoadNav)];
WRoadNav *const g_nav = reinterpret_cast<WRoadNav *>(g_navBytes);
alignas(16) Coord4 g_heading;

WRoadNetworkData &Net() { return fgRoadNetworkData; }

// ---- state and outputs

struct Snapshot {
    uint8_t nav[sizeof(WRoadNav)];
    uint8_t spline[kSplineSize];
    SimRandom random;
    uint32_t queryStamp;
    std::vector<uint32_t> stamps;
    uint8_t curve[kCurveStateSize];
    Coord4 heading;
};

void Take(Snapshot *s) {
    memcpy(s->nav, g_nav, sizeof(s->nav));
    memcpy(s->spline, g_nav->spline, sizeof(s->spline));
    s->random = *Random();
    s->queryStamp = Net().queryStamp;
    s->stamps.resize(size_t(Net().segmentCount));
    for (int i = 0; i < Net().segmentCount; i++)
        s->stamps[size_t(i)] = Net().segments[i]->queryStamp;
    memcpy(s->curve, kCurveState, sizeof(s->curve));
    s->heading = g_heading;
}

void Put(const Snapshot &s) {
    memcpy(g_nav, s.nav, sizeof(s.nav));
    memcpy(g_nav->spline, s.spline, sizeof(s.spline));
    *Random() = s.random;
    Net().queryStamp = s.queryStamp;
    for (int i = 0; i < Net().segmentCount; i++)
        Net().segments[i]->queryStamp = s.stamps[size_t(i)];
    memcpy(kCurveState, s.curve, sizeof(s.curve));
    g_heading = s.heading;
}

struct Outputs {
    uint8_t bytes[64];
    size_t size;
    bool fault;

    void Add(const void *p, size_t n) {
        if (size + n <= sizeof(bytes)) {
            memcpy(bytes + size, p, n);
            size += n;
        }
    }
};

const char *g_op;
int g_case;

const int kDetailLimit = 40;
char g_inputs[256];     // the case's inputs, as its caller describes them

void Detail(const char *region, size_t offset, unsigned original, unsigned port) {
    if (g_details++ < kDetailLimit)
        printf("[roadnav] case %d %s: %s +0x%x original %02x port %02x\n", g_case, g_op, region, unsigned(offset),
               original, port);
}

void CompareBytes(const char *region, const void *a, const void *b, size_t n) {
    g_checks++;
    if (memcmp(a, b, n) == 0)
        return;
    g_diffs++;
    const uint8_t *x = static_cast<const uint8_t *>(a), *y = static_cast<const uint8_t *>(b);
    for (size_t i = 0; i < n; i++) {
        if (x[i] != y[i]) {
            Detail(region, i, x[i], y[i]);
            break;
        }
    }
}

// ---- what a differing case prints (the first few of each operation)

struct OpCount {
    const char *op;
    int described;
};
OpCount g_described[24];

bool ShouldDescribe(const char *op) {
    for (OpCount &c : g_described) {
        if (c.op == NULL)
            c.op = op;
        if (strcmp(c.op, op) == 0)
            return c.described++ < 3;
    }
    return false;
}

void PrintVector(const char *name, const Coord3 &v) {
    printf(" %s(%.6g %.6g %.6g)", name, v.x, v.y, v.z);
}

void PrintNavigator(const char *side, const WRoadNav *nav) {
    printf("[roadnav]   %s: seg %d fwd %d lane %d/%d off %.6g from %.6g to %.6g frac %.6g dist %.6g len %.6g t %.6g "
           "next %d deadend %d", side, nav->segment, nav->forward, nav->lane, nav->unknown96, nav->laneOffset,
           nav->laneChangeFrom, nav->laneChangeTo, nav->laneChangeFraction, nav->laneChangeDistance,
           nav->laneChangeLength, nav->t, nav->nextChoice, nav->atDeadEnd);
    PrintVector("pos", nav->position);
    PrintVector("road", nav->roadPoint);
    PrintVector("dir", nav->direction);
    PrintVector("start", nav->boundStart);
    PrintVector("end", nav->boundEnd);
    PrintVector("c0", nav->startControl);
    PrintVector("c1", nav->endControl);
    printf("\n");
}

void PrintSegment(const char *what, int index) {
    if (index < 0 || index >= Net().segmentCount) {
        printf("[roadnav]   %s %d: none\n", what, index);
        return;
    }
    const WRoadSegment *s = Net().segments[index];
    printf("[roadnav]   %s %d: flags %04x%s nodes %d,%d inters %d,%d lanes L%d R%d mask %02x len %.6g\n", what, index,
           s->flags, (s->flags & kSegmentCurved) ? " curved" : "", s->node[0], s->node[1], s->intersection[0],
           s->intersection[1], s->leftLanes, s->rightLanes, s->unknown58, s->length);
}

void PrintNode(const char *what, int index) {
    if (index < 0 || index >= Net().nodeCount)
        return;
    const WRoadNode *n = Net().nodes[index];
    printf("[roadnav]   %s %d (%.6g %.6g %.6g):", what, index, n->position.x, n->position.y, n->position.z);
    for (int i = 0; i < n->segmentCount && i < 8; i++) {
        int k = n->segments[i];
        if (k < Net().segmentCount)
            printf(" %d[%04x L%d R%d]", k, Net().segments[k]->flags, Net().segments[k]->leftLanes,
                   Net().segments[k]->rightLanes);
    }
    printf("\n");
}

// The 16 bytes round the first difference, as bits and floats, original | port
void PrintWindow(const char *region, const uint8_t *a, const uint8_t *b, size_t n) {
    size_t i = 0;
    while (i < n && a[i] == b[i])
        i++;
    if (i == n)
        return;
    size_t from = i & ~size_t(3);
    size_t to = from + 16 < n ? from + 16 : n;
    printf("[roadnav]   %s +0x%x:", region, unsigned(from));
    for (size_t k = from; k + 4 <= to; k += 4) {
        float x, y;
        uint32_t bx, by;
        memcpy(&x, a + k, 4);
        memcpy(&y, b + k, 4);
        memcpy(&bx, a + k, 4);
        memcpy(&by, b + k, 4);
        printf(" [%08x %.7g | %08x %.7g]", bx, x, by, y);
    }
    printf("\n");
}

void Describe(const char *what, const Snapshot &before, const Snapshot &original, const Snapshot &port,
              const Outputs &outOriginal, const Outputs &outPort) {
    const WRoadNav *nav = reinterpret_cast<const WRoadNav *>(before.nav);
    printf("[roadnav] case %d %s differs: mode %d u04 %d b1 %d b2 %d | %s\n", g_case, what, nav->mode, nav->unknown04,
           nav->unknownB1, nav->unknownB2, g_inputs);
    PrintNavigator("before  ", nav);
    if (nav->segment >= 0 && nav->segment < Net().segmentCount) {
        const WRoadSegment *s = Net().segments[nav->segment];
        PrintSegment("segment", nav->segment);
        PrintNode("node ahead", s->node[nav->forward ? 1 : 0]);
        PrintNode("node behind", s->node[nav->forward ? 0 : 1]);
    }
    PrintNavigator("original", reinterpret_cast<const WRoadNav *>(original.nav));
    PrintNavigator("port    ", reinterpret_cast<const WRoadNav *>(port.nav));
    int a = reinterpret_cast<const WRoadNav *>(original.nav)->segment;
    int b = reinterpret_cast<const WRoadNav *>(port.nav)->segment;
    PrintSegment("original's segment", a);
    if (b != a)
        PrintSegment("port's segment", b);
    PrintWindow("navigator", original.nav, port.nav, sizeof(original.nav));
    PrintWindow("spline", original.spline, port.spline, sizeof(original.spline));
    if (outOriginal.size == outPort.size)
        PrintWindow("outputs", outOriginal.bytes, outPort.bytes, outPort.size);
    printf("[roadnav]   outputs original");
    for (size_t i = 0; i < outOriginal.size; i++)
        printf(" %02x", outOriginal.bytes[i]);
    printf(" port");
    for (size_t i = 0; i < outPort.size; i++)
        printf(" %02x", outPort.bytes[i]);
    printf("\n");
    fflush(stdout);
}

template <class F>
bool Safe(F *op, Outputs *out) {
#ifdef _MSC_VER
    __try {
        (*op)(out);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
#else
    (*op)(out);
    return true;
#endif
}

// Runs the operation from the same state on both sides and compares. False if either side faulted.
template <class F>
bool Check(const char *what, F op) {
    g_op = what;
    g_case++;
    g_cases++;
    int diffsBefore = g_diffs;
    Snapshot before, afterOriginal, afterPort;
    Take(&before);
    Outputs original = {}, port = {};
    if (g_verbose) {
        printf("[roadnav] case %d %s: original\n", g_case, what);
        fflush(stdout);
    }
    {
        Originals swap(true);
        original.fault = !Safe(&op, &original);
    }
    Take(&afterOriginal);
    Put(before);
    if (g_verbose) {
        printf("[roadnav] case %d %s: port\n", g_case, what);
        fflush(stdout);
    }
    port.fault = !Safe(&op, &port);
    Take(&afterPort);

    if (original.fault || port.fault)
        g_faults++;
    g_checks++;
    if (original.fault != port.fault) {
        g_diffs++;
        Detail("fault", 0, original.fault, port.fault);
    }
    CompareBytes("navigator", afterOriginal.nav, afterPort.nav, sizeof(afterPort.nav));
    CompareBytes("spline", afterOriginal.spline, afterPort.spline, sizeof(afterPort.spline));
    CompareBytes("random", &afterOriginal.random, &afterPort.random, sizeof(afterPort.random));
    CompareBytes("query stamp", &afterOriginal.queryStamp, &afterPort.queryStamp, sizeof(afterPort.queryStamp));
    if (!afterPort.stamps.empty())
        CompareBytes("segment stamps", afterOriginal.stamps.data(), afterPort.stamps.data(),
                     afterPort.stamps.size() * sizeof(uint32_t));
    CompareBytes("curve spline", afterOriginal.curve, afterPort.curve, sizeof(afterPort.curve));
    CompareBytes("heading", &afterOriginal.heading, &afterPort.heading, sizeof(afterPort.heading));
    g_checks++;
    if (original.size != port.size) {
        g_diffs++;
        Detail("output size", 0, unsigned(original.size), unsigned(port.size));
    } else {
        g_checks--;
        CompareBytes("outputs", original.bytes, port.bytes, port.size);
    }
    if (g_diffs != diffsBefore && ShouldDescribe(what))
        Describe(what, before, afterOriginal, afterPort, original, port);
    g_inputs[0] = 0;
    return !original.fault && !port.fault;
}

// ---- inputs

WRoadSegment *RandomSegment(Rng &rng) {
    return Net().segments[rng.Below(uint32_t(Net().segmentCount))];
}

Coord3 NearSegment(Rng &rng, const WRoadSegment *segment, float spread) {
    const Coord3 &a = Net().nodes[segment->node[0]]->position;
    const Coord3 &b = Net().nodes[segment->node[1]]->position;
    float along = rng.Range(-0.2f, 1.2f);
    Coord3 p = { a.x + (b.x - a.x) * along + rng.Range(-spread, spread), a.y + (b.y - a.y) * along + rng.Range(-2.0f, 2.0f),
                 a.z + (b.z - a.z) * along + rng.Range(-spread, spread) };
    return p;
}

Coord4 RandomHeading(Rng &rng, const WRoadNav *nav) {
    switch (rng.Below(6)) {
    case 0:
        return Coord4{ 0.0f, 0.0f, 0.0f, 0.0f };
    case 1:
        return Coord4{ nav->direction.x, nav->direction.y, nav->direction.z, 0.0f };
    case 2:
        return Coord4{ -nav->direction.x, -nav->direction.y, -nav->direction.z, 0.0f };
    default:
        return Coord4{ rng.Range(-1.0f, 1.0f), rng.Range(-0.1f, 0.1f), rng.Range(-1.0f, 1.0f), 0.0f };
    }
}

short RandomTarget(Rng &rng) {
    return rng.Below(3) == 0 ? short(-1) : short(rng.Below(uint32_t(Net().segmentCount)));
}

bool ValidSegment() {
    return g_nav->segment >= 0 && g_nav->segment < Net().segmentCount;
}

// ---- the tests

void TestLifecycle(Rng &rng) {
    alignas(16) uint8_t a[sizeof(WRoadNav)], b[sizeof(WRoadNav)];
    uint8_t splineA[kSplineSize], splineB[kSplineSize];
    g_op = "construct";
    g_case++;
    g_cases++;
    {
        Originals swap(true);
        memset(a, 0xcd, sizeof(a));
        ConstructAt(a, 0);
        memcpy(splineA, reinterpret_cast<WRoadNav *>(a)->spline, sizeof(splineA));
        DestructAt(a, 0);
    }
    memset(b, 0xcd, sizeof(b));
    ConstructAt(b, 0);
    memcpy(splineB, reinterpret_cast<WRoadNav *>(b)->spline, sizeof(splineB));
    DestructAt(b, 0);
    reinterpret_cast<WRoadNav *>(a)->spline = NULL;
    reinterpret_cast<WRoadNav *>(b)->spline = NULL;
    CompareBytes("constructed", a, b, sizeof(a));
    CompareBytes("constructed spline", splineA, splineB, sizeof(splineA));

    // Reset over a scrambled navigator (its spline kept)
    Snapshot saved;
    Take(&saved);
    RCameraSpline *spline = g_nav->spline;
    for (size_t i = 0; i < sizeof(g_navBytes); i++)
        g_navBytes[i] = uint8_t(rng.Next());
    g_nav->spline = spline;
    Check("Reset", [](Outputs *) { ResetAt(g_nav, 0); });
    Put(saved);
}

void Place(Rng &rng, WRoadSegment *segment, int mode) {
    g_nav->mode = mode;
    g_nav->unknown04 = (mode == kNavRandom && rng.Below(4) != 0) ? segment->unknown5a : uint8_t(rng.Below(4));
    g_nav->unknownB1 = rng.Below(8) == 0;
    g_nav->unknownB2 = rng.Below(12) == 0;
    g_nav->nextChoice = rng.Below(6) == 0 ? int8_t(rng.Below(5)) : int8_t(-1);
    Coord3 point = NearSegment(rng, segment, rng.Below(4) == 0 ? 40.0f : 8.0f);
    Coord4 heading4 = RandomHeading(rng, g_nav);
    Coord3 heading = { heading4.x, heading4.y, heading4.z };
    bool noLane = rng.Below(5) == 0;
    snprintf(g_inputs, sizeof(g_inputs), "point (%.6g %.6g %.6g) heading (%.6g %.6g %.6g) noLane %d near segment %d",
             point.x, point.y, point.z, heading.x, heading.y, heading.z, noLane, segment->index);
    Check("InitAtPoint", [&](Outputs *) { InitAtPointAt(g_nav, 0, &point, &heading, noLane); });
}

// One random operation on a placed navigator; false to stop driving it
// One operation on the navigator. Returns false when the placement should end: on a fault, and after the calls the
// game only makes inside the navigator's own steps (a CalcNextSegment* on its own, SetBoundPos/SetControlPos on a
// random segment). Those leave states the game never makes, from which the original IncNavPosition can loop for
// ever (seen on mission 1 after a CalcNextSegmentSidewalk); they are compared, but not driven on from.
bool Drive(Rng &rng, bool stepping) {
    uint32_t pick = rng.Below(100);
    if (pick < 55) {
        if (!stepping)
            return true;
        float step = rng.Below(10) == 0 ? rng.Range(25.0f, 120.0f) : rng.Range(0.0f, 25.0f);
        if (rng.Below(20) == 0)
            step = 0.0f;
        g_heading = RandomHeading(rng, g_nav);
        short target = RandomTarget(rng);
        snprintf(g_inputs, sizeof(g_inputs), "step %.7g heading (%.6g %.6g %.6g) target %d", step, g_heading.x,
                 g_heading.y, g_heading.z, target);
        return Check("IncNavPosition", [&](Outputs *) {
            IncNavAt(g_nav, 0, step, reinterpret_cast<Coord3 *>(&g_heading), target);
        });
    }
    if (pick < 62) {
        float offset = rng.Range(-9.0f, 9.0f);
        float distance = rng.Below(3) == 0 ? (rng.Below(2) ? 0.0f : -1.0f) : rng.Range(0.0f, 30.0f);
        snprintf(g_inputs, sizeof(g_inputs), "offset %.7g distance %.7g", offset, distance);
        return Check("ChangeLanes", [&](Outputs *) { ChangeLanesAt(g_nav, 0, offset, distance); });
    }
    if (pick < 66) {
        float step = rng.Range(0.0f, 10.0f);
        snprintf(g_inputs, sizeof(g_inputs), "step %.7g", step);
        return Check("UpdateLaneChange", [&](Outputs *out) {
            uint8_t result = UpdateLaneChangeAt(g_nav, 0, step);
            out->Add(&result, 1);
        });
    }
    if (pick < 70)
        return Check("ReverseNavDirection", [](Outputs *) { ReverseAt(g_nav, 0); });
    if (pick < 73) {
        short segment = short(rng.Below(uint32_t(Net().segmentCount)));
        int8_t lane = int8_t(int(rng.Below(7)) - 3);
        float along = rng.Below(5) == 0 ? rng.Range(-0.5f, 1.5f) : rng.Unit();
        bool forwards = rng.Below(2) != 0;
        snprintf(g_inputs, sizeof(g_inputs), "segment %d lane %d along %.7g forwards %d", segment, lane, along,
                 forwards);
        return Check("InitAtSegment", [&](Outputs *) { InitAtSegmentAt(g_nav, 0, segment, lane, along, forwards); });
    }
    if (pick < 78) {
        short other = short(rng.Below(uint32_t(Net().segmentCount)));
        short forwardOther = RandomTarget(rng);
        bool withFlag = rng.Below(4) != 0;
        snprintf(g_inputs, sizeof(g_inputs), "share %d forward %d flag %d", other, forwardOther, withFlag);
        Check("PathShareRoadSegment", [&](Outputs *out) {
            uint8_t result = PathShareAt(g_nav, 0, other);
            out->Add(&result, 1);
        });
        return Check("PathForwardRoadSegment", [&](Outputs *out) {
            bool flag;
            memset(&flag, 0x55, 1);
            uint8_t result = PathForwardAt(g_nav, 0, forwardOther, withFlag ? &flag : NULL);
            out->Add(&result, 1);
            out->Add(&flag, 1);
        });
    }
    if (pick < 88) {
        // a CalcNextSegment* on its own, the out-flag aliasing the navigator or not
        bool alias = rng.Below(2) != 0;
        int which = int(rng.Below(4));
        g_heading = RandomHeading(rng, g_nav);
        short target = RandomTarget(rng);
        if (rng.Below(4) == 0)
            g_nav->nextChoice = int8_t(rng.Below(5));
        snprintf(g_inputs, sizeof(g_inputs), "alias %d target %d heading (%.6g %.6g %.6g)", alias, target,
                 g_heading.x, g_heading.y, g_heading.z);
        Check(which == 0 ? "CalcNextSegmentRandom" : which == 1 ? "CalcNextSegmentDirection"
                        : which == 2 ? "CalcNextSegmentLane" : "CalcNextSegmentSidewalk", [&](Outputs *out) {
            uint8_t local = g_nav->forward;
            uint8_t *ahead = alias ? &g_nav->forward : &local;
            bool changed = false;
            float offset = -12345.0f;
            short targetCopy = target;
            short result;
            Coord3 *heading = reinterpret_cast<Coord3 *>(&g_heading);
            switch (which) {
            case 0: result = CalcRandomAt(g_nav, 0, heading, ahead, &changed); break;
            case 1: result = CalcDirectionAt(g_nav, 0, heading, &targetCopy, ahead, &changed); break;
            case 2: result = CalcLaneAt(g_nav, 0, &offset, ahead, &changed); break;
            default: result = CalcSidewalkAt(g_nav, 0, &offset, ahead, &changed); break;
            }
            out->Add(&result, sizeof(result));
            out->Add(&local, 1);
            out->Add(&changed, 1);
            out->Add(&offset, sizeof(offset));
        });
        return false;   // called out of turn: the navigator is not driven on from here (see Drive)
    }
    if (pick < 94) {
        WRoadSegment *segment = RandomSegment(rng);
        float offset = rng.Range(-10.0f, 10.0f);
        bool atStart = rng.Below(2) != 0;
        bool control = rng.Below(2) != 0;
        snprintf(g_inputs, sizeof(g_inputs), "segment %d offset %.7g atStart %d", segment->index, offset, atStart);
        Check(control ? "SetControlPos" : "SetBoundPos", [&](Outputs *out) {
            alignas(16) Coord4 pos = { 1.0f, 2.0f, 3.0f, 4.0f };
            if (control)
                SetControlPosAt(g_nav, 0, segment, reinterpret_cast<Coord3 *>(&pos), atStart);
            else
                SetBoundPosAt(g_nav, 0, segment, offset, reinterpret_cast<Coord3 *>(&pos), atStart);
            out->Add(&pos, sizeof(pos));
        });
        return false;   // on a random segment: as above
    }
    {
        WRoadSegment *segment = RandomSegment(rng);
        Coord3 around = NearSegment(rng, segment, rng.Below(3) == 0 ? 60.0f : 10.0f);
        alignas(16) Coord4 point = { around.x, around.y, around.z, 0.0f };
        uint8_t mode = uint8_t(1 + rng.Below(5));
        uint8_t tag = segment->unknown5a;
        snprintf(g_inputs, sizeof(g_inputs), "point (%.6g %.6g %.6g) mode %d tag %d", point.x, point.y, point.z, mode,
                 tag);
        return Check("FindClosestSegmentInd", [&](Outputs *out) {
            int savedMode = g_nav->mode;
            uint8_t savedTag = g_nav->unknown04;
            g_nav->mode = mode;
            g_nav->unknown04 = tag;
            alignas(16) Coord4 closest = { -1.0f, -1.0f, -1.0f, -1.0f };
            float along = -7.0f;
            int result = FindClosestAt(g_nav, 0, &point, reinterpret_cast<Coord3 *>(&closest), &along);
            g_nav->mode = savedMode;
            g_nav->unknown04 = savedTag;
            out->Add(&result, sizeof(result));
            out->Add(&closest, sizeof(closest));
            out->Add(&along, sizeof(along));
        });
    }
}

}  // namespace

void RoadNavShadow_Run(void) {
    const char *env = getenv("NIGHTFIRE_ROADNAVSHADOW");
    if (env == NULL || atoi(env) == 0)
        return;
    g_verbose = atoi(env) >= 2;
    WRoadNetworkData &net = Net();
    if (!net.loaded || net.segmentCount <= 0 || net.nodes == NULL || net.segments == NULL) {
        printf("[roadnav] no road network loaded: nothing tested\n");
        fflush(stdout);
        return;
    }

    // A segment whose ends meet would make IncNavPosition loop for ever on both sides; drive only without them
    int degenerate = 0;
    for (int i = 0; i < net.segmentCount; i++) {
        const WRoadSegment *s = net.segments[i];
        const Coord3 &a = net.nodes[s->node[0]]->position, &b = net.nodes[s->node[1]]->position;
        float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
        if (dx * dx + dy * dy + dz * dz < 1e-4f)
            degenerate++;
    }
    bool stepping = degenerate == 0;

    // GetSegmentCurveStep's first call constructs its spline (an allocation) and registers its destructor: make it
    // here, once, so that the original's run and the port's both find it made
    if (!(*reinterpret_cast<uint32_t *>(kCurveState + 0x70) & 1)) {
        WRoadSegment *segment = net.segments[0];
        Coord3 point;
        WRoadNetwork::Get()->GetSegmentCurveStep(&net.nodes[segment->node[0]]->position,
                                                 &net.nodes[segment->node[1]]->position, segment, 0.5f, &point);
    }

    Rng rng = { 0x5eed1234u };
    memset(g_navBytes, 0, sizeof(g_navBytes));
    ConstructAt(g_nav, 0);
    TestLifecycle(rng);

    int placements = net.segmentCount < 150 ? net.segmentCount * 2 : 300;
    for (int k = 0; k < placements; k++) {
        WRoadSegment *segment = net.segments[(k * 7919) % net.segmentCount];
        int mode = 1 + k % 5;
        Place(rng, segment, mode);
        if (!ValidSegment())
            continue;
        for (int step = 0; step < 40; step++) {
            if (!Drive(rng, stepping) || !ValidSegment())
                break;
        }
    }

    DestructAt(g_nav, 0);
    printf("[roadnav] road navigation vs originals: %d cases, %d checks, %d differ\n", g_cases, g_checks, g_diffs);
    if (g_faults > 0)
        printf("[roadnav] %d cases faulted (both sides alike unless listed above)\n", g_faults);
    if (!stepping)
        printf("[roadnav] %d segments with coincident ends: IncNavPosition not driven\n", degenerate);
    fflush(stdout);
}
