#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "ColQueryShadow.h"

#include "../engine/UMemory.hpp"
#include "../platform/RealMath.h"
#include "../world/CollisionQueries.h"
#include "../world/WorldPos.h"
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
// NIGHTFIRE_COLQUERYSHADOW=1, once on the first simulation tick: world/CollisionQueries.cpp against the originals,
// over the loaded track.
//
// Two passes run the same tests, calling everything at the original addresses: the first with the package's
// originals swapped back in (0x000bedd0-0x000c2ff0), the second with our jumps. Each pass writes a log (results,
// output bytes, float bits; pointers into the track's data as they are, fresh allocations by content); the logs
// must match line for line. The state the queries can write - the manager, the instances (the list queries stamp
// them), the scratch pad and FindFaceInTriStrip's large buffer - is saved before the first pass and put back
// before the second, and compared after both.
//
//   - the queries on probes over the track (a grid of points and random ones, on the ground found under them):
//     bump heights, heights, ground hits, every instance's face at and along segments (all strips and listed
//     strips), barrier hits from random segments and segments built across real barriers (the closest barrier,
//     with its normal, over instance lists, GetWorldNormal with and without the ground), the cylinder and box
//     objects near the probe (closest, with normals, box intersections, PopObjectOBB on copies of the lists),
//     ClosestCollisionInfo;
//   - the strips of the track's instances: triangle search under a point (three barrier masks), FindFaceInTriStrip
//     under several frames, lowest heights, normals (through FUN_000beff0's register interface), MakeStripFace;
//   - GetOBBObjectIntersection on random boxes and segments (through the top, the bottom, the quad's edges);
//   - the grid cell iterator over the grid's cells, every kind;
//   - both maps through scripted random inserts, erases, range erases and copies, their structure logged after
//     every step; the window map's minimum; every vector's _Insert_n down all three paths, reserve, insert and
//     the helpers.
//
// The lists the queries take are built once beforehand by COL_B's list functions, with the originals in.
// Masked: the bytes of WorldCollisionInfo that GetWorldNormal leaves as its stack had them.
// ---------------------------------------------------------------------------------------------------------------

namespace {

int g_cases, g_checks, g_diffs, g_faults;
std::string *g_log;
bool g_counting;        // the first pass counts what the tests reach
int g_reached[32];

enum Reach {
    kReachGround, kReachHeight, kReachFaceAt, kReachFaceAlong, kReachBarrier, kReachBarrierNormal, kReachWorldNormal,
    kReachCylinder, kReachBox, kReachOBB, kReachStripTriangle, kReachTriStrip, kReachPop, kReachBoxIntersection,
    kReachCount
};

void Reached(int what, bool yes) {
    if (g_counting && yes)
        g_reached[what]++;
}

void Logf(const char *format, ...) {
    if (g_log == NULL)
        return;
    char line[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    g_log->append(line);
    g_log->push_back('\n');
}

void LogBytes(const char *what, const void *p, size_t n) {
    std::string s = what;
    s += ' ';
    char h[4];
    for (size_t i = 0; i < n; i++) {
        snprintf(h, sizeof(h), "%02x", static_cast<const uint8_t *>(p)[i]);
        s += h;
    }
    Logf("%s", s.c_str());
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

uint32_t Hash(const void *p, size_t n) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++)
        h = (h ^ static_cast<const uint8_t *>(p)[i]) * 16777619u;
    return h;
}

// ---- the two sides

struct Originals {
    bool on;
    explicit Originals(bool original) : on(original) {
        if (on)
            XbeOriginal_RestoreRange(0x000bedd0, 0x000c2ff0, true);
    }
    ~Originals() {
        if (on)
            XbeOriginal_RestoreRange(0x000bedd0, 0x000c2ff0, false);
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

typedef double (__fastcall *SurfaceBumpHeightFn)(WCollisionMgr *, int, const Coord3 *, const uint8_t *);
typedef void (*MakeStripFaceFn)(WWorldPos *, const StripVertex *, const MATRIX4 *);
typedef const StripVertex *(__fastcall *FindStripTriangleFn)(WCollisionMgr *, int, const Coord3 *,
                                                             const StripVertex *const *);
typedef const StripVertex *(__fastcall *FindFaceInTriStripFn)(WCollisionMgr *, int, const MATRIX4 *, const Coord3 *,
                                                              const StripVertex *const *, float *);
typedef bool (__fastcall *GetWorldHeightAtPointFn)(WCollisionMgr *, int, const Coord3 *, float *, int);
typedef bool (__fastcall *GetGroundCollisionFn)(WCollisionMgr *, int, const Coord4 *, WWorldPos *,
                                                WorldCollisionInfo *);
typedef void (__fastcall *ClosestCollisionInfoFn)(WCollisionMgr *, int, const Coord3 *, const WorldCollisionInfo *,
                                                  const WorldCollisionInfo *, WorldCollisionInfo *);
typedef float (__fastcall *LowestYFn)(const void *, int);
typedef bool (__fastcall *FindFaceAtFn)(WCollisionMgr *, int, const Coord3 *, const void *, WWorldPos *, float *);
typedef bool (__fastcall *FindFaceAlongFn)(WCollisionMgr *, int, const MATRIX4 *, const Coord3 *, const void *,
                                           WWorldPos *, float *);
typedef bool (__fastcall *GetOBBObjectIntersectionFn)(WCollisionMgr *, int, const Coord4 *, const MATRIX4 *,
                                                      const Coord4 *, Coord4 *);
typedef WCollisionObject *(__fastcall *ClosestObjectFn)(WCollisionMgr *, int, const Coord4 *, Coord4 *,
                                                        const ObjectList *);
typedef bool (__fastcall *BarrierQueryFn)(WCollisionMgr *, int, const void *, const Coord4 *, WorldCollisionInfo *);
typedef bool (__fastcall *ObjectCollisionFn)(WCollisionMgr *, int, const Coord4 *, const ObjectList *,
                                             WorldCollisionInfo *);
typedef bool (__fastcall *GetWorldNormalFn)(WCollisionMgr *, int, const InstanceList *, const BarrierList *,
                                            const Coord4 *, WorldCollisionInfo *, int);
typedef OBB *(__fastcall *PopObjectOBBFn)(WCollisionMgr *, int, ObjectList *, Coord3 *, uint16_t *);
typedef GridCellIterator *(__fastcall *IteratorConstructFn)(GridCellIterator *, int, WGridNode *, uint32_t);
typedef const uint16_t *(__fastcall *IteratorNextFn)(GridCellIterator *, int);

#define SurfaceBumpHeightAt ((SurfaceBumpHeightFn)0x000bedd0)
#define MakeStripFaceAt ((MakeStripFaceFn)0x000beeb0)
#define FindStripTriangleAt ((FindStripTriangleFn)0x000bef30)
#define FindFaceInTriStripAt ((FindFaceInTriStripFn)0x000bf0e0)
#define GetWorldHeightAtPointAt ((GetWorldHeightAtPointFn)0x000bf210)
#define GetGroundCollisionAt ((GetGroundCollisionFn)0x000bf2d0)
#define ClosestCollisionInfoAt ((ClosestCollisionInfoFn)0x000bf3a0)
#define LowestYAt ((LowestYFn)0x000bf480)
#define FindFaceInCInstStripsAt ((FindFaceAtFn)0x000bf4c0)
#define FindFaceInCInstStripsAlongAt ((FindFaceAlongFn)0x000bf680)
#define GetOBBObjectIntersectionAt ((GetOBBObjectIntersectionFn)0x000bf8c0)
#define FindFaceInCInstAt ((FindFaceAtFn)0x000bffe0)
#define FindFaceInCInstAlongAt ((FindFaceAlongFn)0x000c01d0)
#define ClosestCylObjectAt ((ClosestObjectFn)0x000c0440)
#define ClosestOBBObjectAt ((ClosestObjectFn)0x000c05f0)
#define ClosestBarrierAt ((BarrierQueryFn)0x000c0660)
#define BarrierNormalAt ((BarrierQueryFn)0x000c0890)
#define BarrierCollisionAt ((BarrierQueryFn)0x000c0b60)
#define GetWorldNormalAt ((GetWorldNormalFn)0x000c0e00)
#define CylObjectCollisionAt ((ObjectCollisionFn)0x000c0f80)
#define OBBObjectCollisionAt ((ObjectCollisionFn)0x000c1080)
#define PopObjectOBBAt ((PopObjectOBBFn)0x000c1380)
#define IteratorConstructAt ((IteratorConstructFn)0x000bfee0)
#define IteratorNextAt ((IteratorNextFn)0x000bff50)

// The register-interface normal (EAX the triangle, ESI the normal), called at its address.
__declspec(naked) void CallTriangleNormal(const StripVertex *triangle, float *normal) {
    __asm {
        push esi
        mov eax, dword ptr [esp + 8]
        mov esi, dword ptr [esp + 12]
        mov ecx, 0x000beff0
        call ecx
        pop esi
        ret
    }
}

// ---- the setup's helpers: other packages' functions, at their addresses

#define WCollisionInstance_MakeMatrix ((void (__fastcall *)(const WCollisionInstance *, int, MATRIX4 *, int))0x000be8b0)
#define WCollisionObject_MakeMatrix ((void (__fastcall *)(const WCollisionObject *, int, MATRIX4 *, int))0x000be6f0)
#define WWorldPos_Construct ((WWorldPos *(__fastcall *)(WWorldPos *, int))0x000d2fd0)
#define WWorldMath_MakeSegSpaceMatrix ((bool (*)(const void *, const void *, MATRIX4 *))0x000d2d70)
#define WCollisionMgr_GetInstanceList ((void (__fastcall *)(WCollisionMgr *, int, InstanceList *, const float *, float, int, int))0x000c4510)
#define WCollisionMgr_GetObjectLists ((void (__fastcall *)(WCollisionMgr *, int, ObjectList *, ObjectList *, const float *, float))0x000c37c0)
#define WCollisionMgr_GetBarrierList ((void (__fastcall *)(WCollisionMgr *, int, BarrierList *, const InstanceList *, const float *, float))0x000c3880)

#define fgCollisionMgr (*(WCollisionMgr **)0x00239a70)
#define fgGrid (*(GridView **)0x0023b3a0)
#define ScratchPad ((uint8_t *)0x00234f20)      // Simulation::GetScratchPadFreeZone's answer
#define LargeStripScratch ((uint8_t *)0x00239a80)

struct GridView {               // WGrid
    Coord4 origin;
    float cellSize;
    float inverseCellSize;
    uint32_t rows;
    uint32_t columns;
    WGridNode **cells;
};

WCollisionMgr *Manager() { return fgCollisionMgr; }

Coord4 Point(float x, float y, float z) {
    Coord4 p = { x, y, z, 1.0f };
    return p;
}

MATRIX4 Identity() {
    MATRIX4 m;
    memset(&m, 0, sizeof(m));
    m.mtx[0][0] = m.mtx[1][1] = m.mtx[2][2] = m.mtx[3][3] = 1.0f;
    return m;
}

// A rigid frame: turned about a random axis, moved.
MATRIX4 RandomFrame(Rng &rng, float spread) {
    float axis[4] = { rng.Range(-1, 1), rng.Range(-1, 1), rng.Range(-1, 1), 0 };
    if (axis[0] == 0 && axis[1] == 0 && axis[2] == 0)
        axis[1] = 1;
    float length = sqrtf(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
    for (int i = 0; i < 3; i++)
        axis[i] /= length;
    MATRIX4 m;
    MATRIX4_axisrotate(axis, rng.Range(0, 1), &m);
    m.mtx[0][3] = m.mtx[1][3] = m.mtx[2][3] = 0.0f;
    m.mtx[3][0] = rng.Range(-spread, spread);
    m.mtx[3][1] = rng.Range(-spread, spread);
    m.mtx[3][2] = rng.Range(-spread, spread);
    m.mtx[3][3] = 1.0f;
    return m;
}

// ---- the state the queries may write

struct Region {
    uint8_t *at;
    uint32_t size;
};

std::vector<Region> *g_regions;

void SaveState(std::vector<uint8_t> *into) {
    into->clear();
    for (const Region &r : *g_regions)
        into->insert(into->end(), r.at, r.at + r.size);
}

void RestoreState(const std::vector<uint8_t> &from) {
    size_t at = 0;
    for (const Region &r : *g_regions) {
        memcpy(r.at, &from[at], r.size);
        at += r.size;
    }
}

// ---- the probes and the lists built for them

struct Probe {
    Coord4 point;               // above the ground found under it
    float ground;
    bool hasGround;
    InstanceList withStrips;
    InstanceList plain;
    BarrierList barriers;
    ObjectList cylinders;
    ObjectList boxes;
};

std::vector<Probe> *g_probes;
uint32_t g_maxStripVertices;

template <class T>
uint32_t Count(const GameVector<T> &v) {
    return v.first == NULL ? 0 : uint32_t(v.last - v.first);
}

template <class T>
void FreeVector(GameVector<T> *v) {
    if (v->first != NULL)
        UMemory::FastFree(v->first, uint32_t(v->end - v->first) * sizeof(T));
    v->first = v->last = v->end = NULL;
}

void FreeInstanceList(InstanceList *list) {
    for (InstanceListEntry *e = list->first; list->first != NULL && e != list->last; e++) {
        if (e->strips != NULL) {
            FreeVector(e->strips);
            OperatorDelete(e->strips);
        }
    }
    FreeVector(list);
}

void BuildProbes() {
    WCollisionMgr *manager = Manager();
    GridView *grid = fgGrid;
    float width = grid->columns * grid->cellSize;
    float depth = grid->rows * grid->cellSize;
    Rng rng = { 0x2545f491 };
    std::vector<Coord4> points;
    for (int gx = 0; gx < 7; gx++)
        for (int gz = 0; gz < 7; gz++)
            points.push_back(Point(grid->origin.x + (gx + 0.5f) / 7 * width, 0, grid->origin.z + (gz + 0.5f) / 7 * depth));
    for (int i = 0; i < 40; i++)
        points.push_back(Point(grid->origin.x + rng.Range(0, width), 0, grid->origin.z + rng.Range(0, depth)));
    // and at the instances themselves, where the geometry is
    for (uint32_t i = 0; i < manager->instanceCount && i < 400; i += 1 + manager->instanceCount / 40) {
        const WCollisionInstance *instance = &manager->instances[i];
        points.push_back(Point(instance->position.x + rng.Range(-2, 2), 0, instance->position.z + rng.Range(-2, 2)));
    }

    Originals originals(true);
    for (Coord4 p : points) {
        Probe probe;
        memset(&probe, 0, sizeof(probe));
        Coord4 high = Point(p.x, 2000.0f, p.z);
        float ground = 0.0f;
        probe.hasGround = GetWorldHeightAtPointAt(manager, 0, reinterpret_cast<const Coord3 *>(&high), &ground, 0);
        probe.ground = probe.hasGround ? ground : 0.0f;
        probe.point = Point(p.x, probe.ground + 1.0f, p.z);
        WCollisionMgr_GetInstanceList(manager, 0, &probe.withStrips, &probe.point.x, 12.0f, 1, 0);
        WCollisionMgr_GetInstanceList(manager, 0, &probe.plain, &probe.point.x, 25.0f, 0, 0);
        WCollisionMgr_GetBarrierList(manager, 0, &probe.barriers, &probe.plain, &probe.point.x, 25.0f);
        WCollisionMgr_GetObjectLists(manager, 0, &probe.cylinders, &probe.boxes, &probe.point.x, 40.0f);
        g_probes->push_back(probe);
    }
}

void FreeProbes() {
    for (Probe &probe : *g_probes) {
        FreeInstanceList(&probe.withStrips);
        FreeInstanceList(&probe.plain);
        FreeVector(&probe.barriers);
        FreeVector(&probe.cylinders);
        FreeVector(&probe.boxes);
    }
    g_probes->clear();
}

// ---- logging the answers

void LogInfo(const char *what, const WorldCollisionInfo *info, bool maskStackBytes) {
    uint8_t copy[sizeof(WorldCollisionInfo)];
    memcpy(copy, info, sizeof(copy));
    if (maskStackBytes) {
        // GetWorldNormal's infos keep what their stack had in these (and, for a ground hit, the segment)
        memset(copy + 0x4c, 0, 4);
        memset(copy + 0x56, 0, 2);
        memset(copy + 0x5c, 0, 4);
        if (copy[0x53] == kHitWorld)
            memset(copy + 0x20, 0, 0x20);
    }
    LogBytes(what, copy, sizeof(copy));
}

void LogFace(const char *what, const WWorldPos *face) {
    LogBytes(what, face, sizeof(*face));
}

// A face WWorldPos::FindClosestFace filled: the fourth words of its first two corners are copied from an
// uninitialised stack slot of COL_D's (an address, different on each side), so they are left out.
void LogFoundFace(const char *what, const WWorldPos *face) {
    uint8_t copy[sizeof(WWorldPos)];
    memcpy(copy, face, sizeof(copy));
    memset(copy + 0x0c, 0, 4);
    memset(copy + 0x1c, 0, 4);
    LogBytes(what, copy, sizeof(copy));
}

uint32_t ScratchHash(uint32_t vertices) {
    uint32_t bytes = vertices * sizeof(StripVertex);
    if (vertices - 2 < 180u || vertices < 2)
        return Hash(ScratchPad + 0x3b0, bytes);
    return Hash(LargeStripScratch, bytes);
}

// ---- the queries at the probes

Coord4 Offset(const Coord4 &p, float dx, float dy, float dz) {
    return Point(p.x + dx, p.y + dy, p.z + dz);
}

void TestHeights(Rng &rng, const Probe &probe) {
    WCollisionMgr *manager = Manager();
    for (int type = 0; type <= 12; type++) {
        Coord4 at = Offset(probe.point, rng.Range(-50, 50), 0, rng.Range(-50, 50));
        uint8_t face = uint8_t(type);
        Guarded("bump", [&] {
            double h = SurfaceBumpHeightAt(manager, 0, reinterpret_cast<const Coord3 *>(&at), &face);
            Logf("bump %d %016llx", type, Bits(h));
        });
    }
    const float heights[] = { 1.0f, 2000.0f, -0.25f, 0.3f, rng.Range(-3, 3) };
    for (float dy : heights) {
        Coord4 at = Point(probe.point.x + rng.Range(-0.5f, 0.5f), probe.ground + dy, probe.point.z + rng.Range(-0.5f, 0.5f));
        Guarded("height", [&] {
            float h = -1234.0f;
            bool found = GetWorldHeightAtPointAt(manager, 0, reinterpret_cast<const Coord3 *>(&at), &h, 0);
            Reached(kReachHeight, found);
            Logf("height %d %08x", found, Bits(h));
        });
    }
    for (int i = 0; i < 4; i++) {
        Coord4 segment[2] = {
            Offset(probe.point, 0, i == 3 ? 4.0f : rng.Range(0.5f, 3), 0),
            Offset(probe.point, rng.Range(-1, 1), i == 3 ? 2.5f : -rng.Range(1, 4), rng.Range(-1, 1)),
        };
        Guarded("ground", [&] {
            WWorldPos face;
            WorldCollisionInfo info;
            memset(&face, 0xcd, sizeof(face));
            memset(&info, 0xcd, sizeof(info));
            WWorldPos_Construct(&face, 0);
            bool hit = GetGroundCollisionAt(manager, 0, segment, &face, &info);
            Reached(kReachGround, hit);
            Logf("ground %d", hit);
            LogFoundFace("ground face", &face);
            LogInfo("ground info", &info, false);
        });
    }
}

void TestFaces(Rng &rng, const Probe &probe) {
    WCollisionMgr *manager = Manager();
    const InstanceList *lists[2] = { &probe.withStrips, &probe.plain };
    for (const InstanceList *list : lists) {
        uint32_t n = Count(*list);
        for (uint32_t i = 0; i < n && i < 5; i++) {
            const InstanceListEntry *entry = &list->first[i];
            for (int k = 0; k < 4; k++) {
                Coord4 at = k == 0 ? probe.point
                          : k == 1 ? Offset(probe.point, 0, -0.8f, 0)
                          : k == 2 ? Offset(probe.point, 0, 9.0f, 0)
                                   : Offset(probe.point, rng.Range(-3, 3), rng.Range(-2, 2), rng.Range(-3, 3));
                Guarded("faceat", [&] {
                    WWorldPos face;
                    memset(&face, 0xcd, sizeof(face));
                    float h = -1234.0f;
                    bool found = FindFaceInCInstStripsAt(manager, 0, reinterpret_cast<const Coord3 *>(&at),
                                                         entry->instance, &face, &h);
                    Reached(kReachFaceAt, found);
                    Logf("faceat strips %d %08x", found, Bits(h));
                    LogFace("faceat face", &face);
                    memset(&face, 0xcd, sizeof(face));
                    h = -1234.0f;
                    found = FindFaceInCInstAt(manager, 0, reinterpret_cast<const Coord3 *>(&at), entry, &face, &h);
                    Logf("faceat listed %d %08x", found, Bits(h));
                    LogFace("faceat face", &face);
                });
            }
            for (int k = 0; k < 3; k++) {
                Coord4 from = Offset(probe.point, rng.Range(-2, 2), rng.Range(1, 4), rng.Range(-2, 2));
                Coord4 to = Offset(probe.point, rng.Range(-2, 2), -rng.Range(1, 4), rng.Range(-2, 2));
                MATRIX4 frame = Identity();
                if (!WWorldMath_MakeSegSpaceMatrix(&from, &to, &frame))
                    continue;
                Guarded("facealong", [&] {
                    WWorldPos face;
                    memset(&face, 0xcd, sizeof(face));
                    float h = -1234.0f;
                    bool found = FindFaceInCInstStripsAlongAt(manager, 0, &frame, reinterpret_cast<const Coord3 *>(&to),
                                                              entry->instance, &face, &h);
                    Reached(kReachFaceAlong, found);
                    Logf("facealong strips %d %08x %08x", found, Bits(h), ScratchHash(16));
                    LogFace("facealong face", &face);
                    memset(&face, 0xcd, sizeof(face));
                    h = -1234.0f;
                    found = FindFaceInCInstAlongAt(manager, 0, &frame, reinterpret_cast<const Coord3 *>(&to), entry,
                                                   &face, &h);
                    Logf("facealong listed %d %08x", found, Bits(h));
                    LogFace("facealong face", &face);
                });
            }
        }
    }
}

// Segments that cross barriers: across each listed barrier's middle (carried out of its instance's frame when the
// list keeps it there), and random ones through the probe.
void BarrierSegments(Rng &rng, const Probe &probe, std::vector<Coord4> *segments) {
    uint32_t n = Count(probe.barriers);
    for (uint32_t i = 0; i < n && i < 8; i++) {
        const BarrierListEntry *entry = &probe.barriers.first[i];
        const CollisionBarrier &b = entry->barrier;
        float t = rng.Range(0.1f, 0.9f);
        Coord4 middle = Point(b.x0 + (b.x1 - b.x0) * t, b.y0 + (b.y1 - b.y0) * rng.Range(0.2f, 0.8f),
                              b.z0 + (b.z1 - b.z0) * t);
        float nx = (b.z1 - b.z0) * b.inverseLength * 2.0f;
        float nz = -(b.x1 - b.x0) * b.inverseLength * 2.0f;
        Coord4 ends[2] = { Offset(middle, nx, 0, nz), Offset(middle, -nx, rng.Range(-0.2f, 0.2f), -nz) };
        if (entry->instance->flags & kInstanceTilted) {
            MATRIX4 back;
            WCollisionInstance_MakeMatrix(entry->instance, 0, &back, 1);
            OrthoInverse(&back);
            Coord4 world[2];
            VU0_MATRIX4_vect3mult(&ends[0], &back, &world[0]);
            VU0_MATRIX4_vect3mult(&ends[1], &back, &world[1]);
            ends[0] = world[0];
            ends[1] = world[1];
        }
        segments->push_back(ends[0]);
        segments->push_back(ends[1]);
    }
    for (int i = 0; i < 6; i++) {
        float angle = rng.Range(0, 6.2831853f);
        float length = rng.Range(4, 14);
        float y = probe.ground + rng.Range(0.2f, 3.0f);
        segments->push_back(Point(probe.point.x - cosf(angle) * length, y, probe.point.z - sinf(angle) * length));
        segments->push_back(Point(probe.point.x + cosf(angle) * length, y + rng.Range(-0.5f, 0.5f),
                                  probe.point.z + sinf(angle) * length));
    }
}

void TestBarriers(Rng &rng, const Probe &probe) {
    WCollisionMgr *manager = Manager();
    std::vector<Coord4> segments;
    BarrierSegments(rng, probe, &segments);
    for (size_t s = 0; s + 1 < segments.size(); s += 2) {
        const Coord4 *segment = &segments[s];
        Guarded("barrier", [&] {
            WorldCollisionInfo info;
            memset(&info, 0xcd, sizeof(info));
            bool hit = ClosestBarrierAt(manager, 0, &probe.barriers, segment, &info);
            Reached(kReachBarrier, hit);
            Logf("barrier closest %d", hit);
            LogInfo("barrier info", &info, false);
            memset(&info, 0xcd, sizeof(info));
            hit = BarrierCollisionAt(manager, 0, &probe.barriers, segment, &info);
            Logf("barrier collision %d", hit);
            LogInfo("barrier info", &info, false);
            memset(&info, 0xcd, sizeof(info));
            hit = BarrierNormalAt(manager, 0, &probe.plain, segment, &info);
            Reached(kReachBarrierNormal, hit);
            Logf("barrier normal %d", hit);
            LogInfo("barrier info", &info, false);
        });
        for (int ground = 0; ground < 3; ground++) {
            Guarded("worldnormal", [&] {
                WorldCollisionInfo info;
                memset(&info, 0xcd, sizeof(info));
                const InstanceList *instances = ground == 2 ? &probe.withStrips : &probe.plain;
                bool hit = GetWorldNormalAt(manager, 0, instances, &probe.barriers, segment, &info, ground != 0);
                Reached(kReachWorldNormal, hit);
                Logf("worldnormal %d %d", ground, hit);
                LogInfo("worldnormal info", &info, true);
            });
        }
    }
    // and down through the ground, for GetWorldNormal's ground half
    for (int i = 0; i < 3; i++) {
        Coord4 segment[2] = { Offset(probe.point, rng.Range(-1, 1), rng.Range(0.5f, 2), rng.Range(-1, 1)),
                              Offset(probe.point, rng.Range(-1, 1), -rng.Range(1.2f, 3), rng.Range(-1, 1)) };
        Guarded("worldnormal down", [&] {
            WorldCollisionInfo info;
            memset(&info, 0xcd, sizeof(info));
            bool hit = GetWorldNormalAt(manager, 0, i == 2 ? &probe.withStrips : &probe.plain, &probe.barriers,
                                        segment, &info, 1);
            Reached(kReachWorldNormal, hit);
            Logf("worldnormal down %d", hit);
            LogInfo("worldnormal info", &info, true);
        });
    }
}

void LogOBB(const char *what, const OBB *obb) {
    if (obb == NULL)
        Logf("%s none", what);
    else
        LogBytes(what, obb, 0xb0);
}

void TestPop(const ObjectList &source) {
    WCollisionMgr *manager = Manager();
    uint32_t n = Count(source);
    if (n == 0)
        return;
    ObjectList list;
    memset(&list, 0, sizeof(list));
    list.first = static_cast<WCollisionObject **>(UMemory::FastAlloc(n * sizeof(WCollisionObject *), "STL"));
    memcpy(list.first, source.first, n * sizeof(WCollisionObject *));
    list.last = list.end = list.first + n;
    for (uint32_t i = 0; i <= n; i++) {
        Guarded("pop", [&] {
            Coord3 velocity = { -1, -1, -1 };
            uint16_t tag = 0xcdcd;
            OBB *obb = PopObjectOBBAt(manager, 0, &list, &velocity, &tag);
            Reached(kReachPop, obb != NULL);
            Logf("pop %d %08x %08x %08x %04x %d", obb != NULL, Bits(velocity.x), Bits(velocity.y), Bits(velocity.z),
                 tag, int(list.last - list.first));
            LogOBB("pop obb", obb);
            if (obb != NULL)
                UMemory::FastFree(obb, 0xb0);
        });
    }
    FreeVector(&list);
}

void TestObjects(Rng &rng, const Probe &probe) {
    WCollisionMgr *manager = Manager();
    const ObjectList *lists[2] = { &probe.cylinders, &probe.boxes };
    for (int which = 0; which < 2; which++) {
        const ObjectList *list = lists[which];
        uint32_t n = Count(*list);
        std::vector<Coord4> segments;
        for (uint32_t i = 0; i < n && i < 6; i++) {
            const WCollisionObject *object = list->first[i];
            float y = object->position.y + object->halfExtents.y * rng.Range(0.1f, 1.9f);
            float reach = object->radius + 3.0f;
            float angle = rng.Range(0, 6.2831853f);
            segments.push_back(Point(object->position.x - cosf(angle) * reach, y, object->position.z - sinf(angle) * reach));
            segments.push_back(Point(object->position.x + cosf(angle) * reach, y + rng.Range(-0.3f, 0.3f),
                                     object->position.z + sinf(angle) * reach));
        }
        for (int i = 0; i < 3; i++) {
            segments.push_back(Offset(probe.point, rng.Range(-20, 20), rng.Range(-1, 3), rng.Range(-20, 20)));
            segments.push_back(Offset(probe.point, rng.Range(-20, 20), rng.Range(-1, 3), rng.Range(-20, 20)));
        }
        for (size_t s = 0; s + 1 < segments.size(); s += 2) {
            const Coord4 *segment = &segments[s];
            Guarded("objects", [&] {
                Coord4 hit;
                memset(&hit, 0xcd, sizeof(hit));
                WCollisionObject *object = (which == 0 ? ClosestCylObjectAt : ClosestOBBObjectAt)(manager, 0, segment, &hit, list);
                Reached(which == 0 ? kReachCylinder : kReachBox, object != NULL);
                Logf("objects %d %p", which, object);
                LogBytes("objects hit", &hit, sizeof(hit));
                WorldCollisionInfo info;
                memset(&info, 0xcd, sizeof(info));
                bool collided = (which == 0 ? CylObjectCollisionAt : OBBObjectCollisionAt)(manager, 0, segment, list, &info);
                Logf("objects collision %d", collided);
                LogInfo("objects info", &info, false);
            });
        }
        if (which == 1) {
            for (uint32_t i = 0; i < n && i < 6; i++) {
                const WCollisionObject *object = list->first[i];
                MATRIX4 frame;
                WCollisionObject_MakeMatrix(object, 0, &frame, 1);
                for (size_t s = 0; s + 1 < segments.size(); s += 2) {
                    const Coord4 *segment = &segments[s];
                    Guarded("box", [&] {
                        Coord4 hit;
                        memset(&hit, 0xcd, sizeof(hit));
                        bool found = GetOBBObjectIntersectionAt(manager, 0, segment, &frame, &object->halfExtents, &hit);
                        Logf("box %d", found);
                        LogBytes("box hit", &hit, sizeof(hit));
                    });
                }
            }
        }
        TestPop(*list);
    }
}

void TestClosest(Rng &rng, const Probe &probe) {
    WCollisionMgr *manager = Manager();
    for (int i = 0; i < 6; i++) {
        WorldCollisionInfo a, b, out;
        uint8_t *bytes[2] = { reinterpret_cast<uint8_t *>(&a), reinterpret_cast<uint8_t *>(&b) };
        for (uint8_t *p : bytes)
            for (size_t k = 0; k < sizeof(WorldCollisionInfo); k++)
                p[k] = uint8_t(rng.Next());
        a.point = Offset(probe.point, rng.Range(-5, 5), rng.Range(-5, 5), rng.Range(-5, 5));
        b.point = i == 5 ? a.point : Offset(probe.point, rng.Range(-5, 5), rng.Range(-5, 5), rng.Range(-5, 5));
        a.hitType = uint8_t(rng.Below(4));
        b.hitType = uint8_t(rng.Below(4));
        if (i == 0)
            a.hitType = b.hitType = kHitNone;
        Guarded("closest", [&] {
            memset(&out, 0xcd, sizeof(out));
            ClosestCollisionInfoAt(manager, 0, reinterpret_cast<const Coord3 *>(&probe.point), &a, &b, &out);
            LogBytes("closest", &out, sizeof(out));
        });
    }
}

// ---- the strips

void TestStrips(Rng &rng) {
    WCollisionMgr *manager = Manager();
    WCollisionMgr masks[3];
    memcpy(&masks[0], manager, sizeof(WCollisionMgr));
    memcpy(&masks[1], manager, sizeof(WCollisionMgr));
    memcpy(&masks[2], manager, sizeof(WCollisionMgr));
    masks[1].barrierMask = 0;
    masks[2].barrierMask = 0xff;
    uint32_t stride = 1 + manager->instanceCount / 60;
    for (uint32_t i = 0; i < manager->instanceCount; i += stride) {
        const WCollisionInstance *instance = &manager->instances[i];
        const CollisionArticle *article = instance->article;
        if (article == NULL)
            continue;
        MATRIX4 instanceFrame;
        WCollisionInstance_MakeMatrix(instance, 0, &instanceFrame, 1);
        for (uint32_t s = 0; s < article->stripCount && s < 6; s++) {
            const CollisionStrip *strip = &article->Strips()[s];
            const StripVertex *vertices = article->Vertices(strip);
            int count = vertices[0].count;
            if (count < 3 || uint32_t(count) > g_maxStripVertices)
                continue;
            int triangles = count - 2;
            for (int t = 0; t < triangles && t < 24; t++) {
                const StripVertex *triangle = &vertices[t];
                Guarded("strip triangle", [&] {
                    float lowest = LowestYAt(triangle, 0);
                    float normal[3] = { -1, -1, -1 };
                    CallTriangleNormal(triangle, normal);
                    Logf("strip lowest %08x normal %08x %08x %08x", Bits(lowest), Bits(normal[0]), Bits(normal[1]),
                         Bits(normal[2]));
                });
            }
            // points: the triangles' centres and around the sphere, a little above and below
            std::vector<Coord4> points;
            for (int t = 0; t < triangles && t < 8; t++) {
                const StripVertex *v = &vertices[t];
                points.push_back(Point((v[0].x + v[1].x + v[2].x) / 3, (v[0].y + v[1].y + v[2].y) / 3 + rng.Range(-0.8f, 1.5f),
                                       (v[0].z + v[1].z + v[2].z) / 3));
            }
            float radius = strip->radius / 16.0f;
            for (int k = 0; k < 6; k++)
                points.push_back(Point(strip->centre.x + rng.Range(-radius, radius), strip->centre.y + rng.Range(-radius, radius),
                                       strip->centre.z + rng.Range(-radius, radius)));
            for (const Coord4 &p : points) {
                Guarded("strip find", [&] {
                    for (int m = 0; m < 3; m++) {
                        const StripVertex *found = FindStripTriangleAt(&masks[m], 0, reinterpret_cast<const Coord3 *>(&p), &vertices);
                        Reached(kReachStripTriangle, found != NULL);
                        Logf("strip find %d %d", m, found == NULL ? -1 : int(found - vertices));
                    }
                });
            }
            MATRIX4 frames[3] = { Identity(), instanceFrame, RandomFrame(rng, 5.0f) };
            for (int f = 0; f < 3; f++) {
                for (const Coord4 &p : points) {
                    Coord4 carried;
                    VU0_MATRIX4_vect3mult(&p, &frames[f], &carried);
                    Guarded("tristrip", [&] {
                        float h = -1234.0f;
                        const StripVertex *found = FindFaceInTriStripAt(&masks[f], 0, &frames[f],
                                                                        reinterpret_cast<const Coord3 *>(&carried), &vertices, &h);
                        Reached(kReachTriStrip, found != NULL);
                        Logf("tristrip %d %d %08x %08x", f, found == NULL ? -1 : int(found - vertices), Bits(h),
                             ScratchHash(count));
                        if (found != NULL) {
                            WWorldPos face;
                            memset(&face, 0xcd, sizeof(face));
                            MakeStripFaceAt(&face, found, &frames[f]);
                            LogFace("tristrip face", &face);
                        }
                    });
                }
            }
        }
    }
    // degenerate and downward-facing triangles for the normal
    for (int i = 0; i < 40; i++) {
        StripVertex triangle[3];
        memset(triangle, 0, sizeof(triangle));
        for (int c = 0; c < 3; c++) {
            triangle[c].x = rng.Range(-4, 4);
            triangle[c].y = i % 5 == 0 ? 1.0f : rng.Range(-4, 4);
            triangle[c].z = rng.Range(-4, 4);
        }
        if (i % 7 == 0)
            triangle[2] = triangle[1];
        if (i % 11 == 0)
            triangle[1].y = triangle[0].y = triangle[2].y, triangle[1].x = triangle[0].x + 1e-3f;
        Guarded("normal", [&] {
            float normal[3] = { -1, -1, -1 };
            CallTriangleNormal(triangle, normal);
            float lowest = LowestYAt(triangle, 0);
            Logf("normal %08x %08x %08x %08x", Bits(normal[0]), Bits(normal[1]), Bits(normal[2]), Bits(lowest));
        });
    }
}

// ---- random boxes

void TestOBB(Rng &rng) {
    WCollisionMgr *manager = Manager();
    for (int i = 0; i < 600; i++) {
        MATRIX4 frame = RandomFrame(rng, 30.0f);
        if (i % 4 == 0)
            frame = Identity(), frame.mtx[3][0] = rng.Range(-30, 30), frame.mtx[3][2] = rng.Range(-30, 30);
        Coord4 extents = Point(rng.Range(0.2f, 8), rng.Range(0.2f, 8), rng.Range(0.2f, 8));
        Coord4 local[2];
        int kind = i % 5;
        for (int e = 0; e < 2; e++) {
            local[e] = Point(rng.Range(-2, 2) * extents.x, rng.Range(-2, 2) * extents.y, rng.Range(-2, 2) * extents.z);
            if (kind == 1)   // through the top
                local[e].y = e == 0 ? extents.y * 1.5f : extents.y * 0.5f;
            if (kind == 2)   // through the bottom
                local[e].y = e == 0 ? -extents.y * 1.5f : -extents.y * 0.2f;
            if (kind == 3)   // across, at mid height
                local[e].y = rng.Range(0, 1.8f) * extents.y;
        }
        Coord4 segment[2];
        VU0_MATRIX4_vect3mult(&local[0], &frame, &segment[0]);
        VU0_MATRIX4_vect3mult(&local[1], &frame, &segment[1]);
        Guarded("obb", [&] {
            Coord4 hit;
            memset(&hit, 0xcd, sizeof(hit));
            bool found = GetOBBObjectIntersectionAt(manager, 0, segment, &frame, &extents, &hit);
            Reached(kReachOBB, found);
            Logf("obb %d", found);
            LogBytes("obb hit", &hit, sizeof(hit));
        });
    }
}

// ---- the grid's cells

void TestGrid() {
    GridView *grid = fgGrid;
    uint32_t cells = grid->rows * grid->columns;
    uint32_t stride = 1 + cells / 3000;
    for (uint32_t c = 0; c < cells; c += stride) {
        WGridNode *cell = grid->cells[c];
        if (cell == NULL)
            continue;
        for (uint32_t kind = 0; kind < 4; kind++) {
            Guarded("grid", [&] {
                GridCellIterator it;
                memset(&it, 0xcd, sizeof(it));
                GridCellIterator *self = IteratorConstructAt(&it, 0, cell, kind);
                Logf("grid %u %u %d", c, kind, self == &it);
                LogBytes("grid it", &it, sizeof(it));
                for (int steps = 0; steps < 4096; steps++) {
                    const uint16_t *element = IteratorNextAt(&it, 0);
                    Logf("grid next %p %d", element, element == NULL ? -1 : *element);
                    LogBytes("grid it", &it, sizeof(it));
                    if (element == NULL)
                        break;
                }
            });
        }
    }
}

// ---- the maps

typedef WindowPaneNode *(*PaneBuyHeadFn)();
typedef WindowPaneInsert *(__fastcall *PaneInsertUniqueFn)(WindowPaneMap *, int, WindowPaneInsert *, const WindowPaneValue *);
typedef WindowPaneNode **(__fastcall *PaneEraseAtFn)(WindowPaneMap *, int, WindowPaneNode **, WindowPaneNode *);
typedef WindowPaneNode **(__fastcall *PaneEraseRangeFn)(WindowPaneMap *, int, WindowPaneNode **, WindowPaneNode *, WindowPaneNode *);
typedef void (__fastcall *PaneCopyTreeFn)(WindowPaneMap *, int, const WindowPaneMap *);
typedef WindowPaneNode *(*PaneMaxFn)(WindowPaneNode *);
typedef void (__fastcall *PaneDecrementFn)(WindowPaneIterator *, int);
typedef ArticleMapInsert *(__fastcall *ArticleInsertUniqueFn)(ArticleMap *, int, ArticleMapInsert *, const ArticleMapValue *);
typedef ArticleMapNode **(__fastcall *ArticleEraseAtFn)(ArticleMap *, int, ArticleMapNode **, ArticleMapNode *);
typedef ArticleMapNode **(__fastcall *ArticleEraseRangeFn)(ArticleMap *, int, ArticleMapNode **, ArticleMapNode *, ArticleMapNode *);
typedef WindowMapNode *(*WindowMapMinFn)(WindowMapNode *);

#define PaneBuyHeadAt ((PaneBuyHeadFn)0x000c0cb0)
#define PaneInsertUniqueAt ((PaneInsertUniqueFn)0x000c28b0)
#define PaneEraseAtAt ((PaneEraseAtFn)0x000c2200)
#define PaneEraseRangeAt ((PaneEraseRangeFn)0x000c2bb0)
#define PaneCopyTreeAt ((PaneCopyTreeFn)0x000c12f0)
#define PaneMaxAt ((PaneMaxFn)0x000bf440)
#define PaneDecrementAt ((PaneDecrementFn)0x000bfe10)
#define ArticleInsertUniqueAt ((ArticleInsertUniqueFn)0x000c2a80)
#define ArticleEraseAtAt ((ArticleEraseAtFn)0x000c24d0)
#define ArticleEraseRangeAt ((ArticleEraseRangeFn)0x000c2f30)
#define WindowMapMinAt ((WindowMapMinFn)0x000bf460)

// A tree's shape and contents, nodes named by their place in order (-1 the head).
template <class Node>
void CollectInOrder(Node *node, std::vector<Node *> *order) {
    if (node->isNil)
        return;
    CollectInOrder(node->left, order);
    order->push_back(node);
    CollectInOrder(node->right, order);
}

template <class Node>
int IndexOf(const std::vector<Node *> &order, Node *node) {
    for (size_t i = 0; i < order.size(); i++)
        if (order[i] == node)
            return int(i);
    return -1;
}

template <class Node>
void LogTree(const char *what, Node *head, uint32_t size) {
    std::vector<Node *> order;
    if (head->parent != head)
        CollectInOrder(head->parent, &order);
    Logf("%s size %u nodes %u head %d %d %d %u %u", what, size, unsigned(order.size()), IndexOf(order, head->parent),
         IndexOf(order, head->left), IndexOf(order, head->right), head->color, head->isNil);
    for (size_t i = 0; i < order.size(); i++) {
        Node *n = order[i];
        Logf("%s %u: %08x %08x c%u n%u p%d l%d r%d", what, unsigned(i), *reinterpret_cast<const uint32_t *>(&n->value),
             Hash(&n->value, sizeof(n->value)), n->color, n->isNil, IndexOf(order, n->parent), IndexOf(order, n->left),
             IndexOf(order, n->right));
    }
}

template <class Map, class Node>
Node *NewHead(Map *map) {
    Node *head = static_cast<Node *>(UMemory::FastAlloc(sizeof(Node), "STL"));
    memset(head, 0, sizeof(Node));
    head->isNil = 1;
    head->color = kTreeBlack;
    head->parent = head->left = head->right = head;
    memset(map, 0, sizeof(*map));
    map->head = head;
    return head;
}

template <class Node>
Node *NthNode(Node *head, uint32_t n) {
    std::vector<Node *> order;
    if (head->parent != head)
        CollectInOrder(head->parent, &order);
    return n < order.size() ? order[n] : head;
}

void TestPaneMap(Rng &rng) {
    for (int script = 0; script < 3; script++) {
        WindowPaneMap map;
        Guarded("pane head", [&] {
            WindowPaneNode *head = PaneBuyHeadAt();
            LogBytes("pane bought head", head, sizeof(*head));
            UMemory::FastFree(head, sizeof(*head));
        });
        NewHead<WindowPaneMap, WindowPaneNode>(&map);
        for (int op = 0; op < 250; op++) {
            uint32_t r = rng.Below(10);
            Guarded("pane op", [&] {
                if (r < 5) {
                    WindowPaneValue value;
                    for (size_t k = 0; k < sizeof(value); k++)
                        reinterpret_cast<uint8_t *>(&value)[k] = uint8_t(rng.Next());
                    value.pane = reinterpret_cast<const WindowPane *>(uintptr_t(rng.Below(96) * 8));
                    WindowPaneInsert result = { NULL, false };
                    PaneInsertUniqueAt(&map, 0, &result, &value);
                    std::vector<WindowPaneNode *> order;
                    CollectInOrder(map.head->parent, &order);
                    Logf("pane insert %d %d", IndexOf(order, result.node), result.inserted);
                } else if (r < 7 && map.size > 0) {
                    WindowPaneNode *next = NULL;
                    PaneEraseAtAt(&map, 0, &next, NthNode(map.head, rng.Below(map.size)));
                    std::vector<WindowPaneNode *> order;
                    CollectInOrder(map.head->parent, &order);
                    Logf("pane erase %d", next == map.head ? -2 : IndexOf(order, next));
                } else if (r < 8) {
                    uint32_t a = rng.Below(map.size + 1), b = rng.Below(map.size + 1);
                    if (a > b) {
                        uint32_t t = a;
                        a = b;
                        b = t;
                    }
                    if (rng.Below(4) == 0)
                        a = 0, b = map.size;
                    WindowPaneNode *next = NULL;
                    PaneEraseRangeAt(&map, 0, &next, NthNode(map.head, a), NthNode(map.head, b));
                    Logf("pane erase range %u %u %d", a, b, next == map.head);
                } else if (r < 9) {
                    WindowPaneMap copy;
                    NewHead<WindowPaneMap, WindowPaneNode>(&copy);
                    PaneCopyTreeAt(&copy, 0, &map);
                    LogTree("pane copy", copy.head, copy.size);
                    WindowPaneNode *ignored;
                    PaneEraseRangeAt(&copy, 0, &ignored, copy.head->left, copy.head);
                    UMemory::FastFree(copy.head, sizeof(WindowPaneNode));
                } else {
                    // stepping back from every node and the end; the largest under every node
                    std::vector<WindowPaneNode *> order;
                    if (map.head->parent != map.head)
                        CollectInOrder(map.head->parent, &order);
                    for (size_t k = 0; k <= order.size(); k++) {
                        WindowPaneIterator it = { k < order.size() ? order[k] : map.head };
                        PaneDecrementAt(&it, 0);
                        int max = k < order.size() ? IndexOf(order, PaneMaxAt(order[k])) : -2;
                        Logf("pane step %u %d %d", unsigned(k), it.node == map.head ? -1 : IndexOf(order, it.node), max);
                    }
                }
                LogTree("pane", map.head, map.size);
            });
        }
        Guarded("pane clear", [&] {
            WindowPaneNode *ignored;
            PaneEraseRangeAt(&map, 0, &ignored, map.head->left, map.head);
            LogTree("pane cleared", map.head, map.size);
        });
        UMemory::FastFree(map.head, sizeof(WindowPaneNode));
    }
}

void TestArticleMap(Rng &rng) {
    for (int script = 0; script < 3; script++) {
        ArticleMap map;
        NewHead<ArticleMap, ArticleMapNode>(&map);
        for (int op = 0; op < 250; op++) {
            uint32_t r = rng.Below(8);
            Guarded("article op", [&] {
                if (r < 5) {
                    ArticleMapValue value;
                    value.instance = reinterpret_cast<WCollisionInstance *>(uintptr_t(rng.Below(96) * 0x40));
                    value.article = reinterpret_cast<CollisionArticle *>(uintptr_t(rng.Next()));
                    ArticleMapInsert result = { NULL, false };
                    ArticleInsertUniqueAt(&map, 0, &result, &value);
                    Logf("article insert %d", result.inserted);
                } else if (r < 7 && map.size > 0) {
                    ArticleMapNode *next = NULL;
                    ArticleEraseAtAt(&map, 0, &next, NthNode(map.head, rng.Below(map.size)));
                    Logf("article erase %d", next == map.head);
                } else {
                    uint32_t a = rng.Below(map.size + 1), b = rng.Below(map.size + 1);
                    if (a > b) {
                        uint32_t t = a;
                        a = b;
                        b = t;
                    }
                    if (rng.Below(4) == 0)
                        a = 0, b = map.size;
                    ArticleMapNode *next = NULL;
                    ArticleEraseRangeAt(&map, 0, &next, NthNode(map.head, a), NthNode(map.head, b));
                    Logf("article erase range %u %u %d", a, b, next == map.head);
                }
                LogTree("article", map.head, map.size);
            });
        }
        Guarded("article clear", [&] {
            ArticleMapNode *ignored;
            ArticleEraseRangeAt(&map, 0, &ignored, map.head->left, map.head);
            LogTree("article cleared", map.head, map.size);
        });
        UMemory::FastFree(map.head, sizeof(ArticleMapNode));
    }
}

void TestWindowMapMin(Rng &rng) {
    WindowMapNode nodes[17];
    for (int round = 0; round < 20; round++) {
        memset(nodes, 0, sizeof(nodes));
        nodes[16].isNil = 1;
        for (int i = 0; i < 16; i++) {
            uint32_t to = i + 1 + rng.Below(17 - (i + 1));
            nodes[i].left = &nodes[to > 16 ? 16 : to];
            if (rng.Below(3) == 0)
                nodes[i].left = &nodes[16];
        }
        Guarded("window min", [&] {
            for (int i = 0; i < 16; i++)
                Logf("window min %d %d", i, int(WindowMapMinAt(&nodes[i]) - nodes));
        });
    }
}

// ---- the vectors

typedef void (__fastcall *BarrierInsertNFn)(BarrierList *, int, BarrierListEntry *, uint32_t, const BarrierListEntry *);
typedef BarrierListEntry **(__fastcall *BarrierInsertFn)(BarrierList *, int, BarrierListEntry **, BarrierListEntry *, const BarrierListEntry *);
typedef void (__fastcall *ObjectInsertNFn)(ObjectList *, int, WCollisionObject **, uint32_t, WCollisionObject *const *);
typedef void (__fastcall *StripInsertNFn)(StripList *, int, const CollisionStrip **, uint32_t, const CollisionStrip *const *);
typedef void (__fastcall *InstanceInsertNFn)(InstanceList *, int, InstanceListEntry *, uint32_t, const InstanceListEntry *);
typedef void (__fastcall *ReserveFn)(void *, int, uint32_t);
typedef BarrierListEntry **(*BarrierCopyBackwardFn)(BarrierListEntry **, BarrierListEntry *, BarrierListEntry *, BarrierListEntry *);
typedef void (*BarrierFillFn)(BarrierListEntry *, BarrierListEntry *, const BarrierListEntry *);
typedef void (*BarrierUninitializedFillFn)(BarrierListEntry *, uint32_t, const BarrierListEntry *);
typedef void (*InstanceUninitializedFillFn)(InstanceListEntry *, uint32_t, const InstanceListEntry *);
typedef void **(*PointerUninitializedCopyFn)(void *const *, void *const *, void **);
typedef void **(__fastcall *PointerUcopyFn)(PointerVector *, int, void *const *, void *const *, void **);
typedef void **(__fastcall *PointerUfillFn)(PointerVector *, int, void **, uint32_t, void *const *);
typedef BarrierListEntry *(__fastcall *BarrierUfillFn)(BarrierList *, int, BarrierListEntry *, uint32_t, const BarrierListEntry *);
typedef InstanceListEntry *(__fastcall *InstanceUfillFn)(InstanceList *, int, InstanceListEntry *, uint32_t, const InstanceListEntry *);

template <class T>
void LogVector(const char *what, const GameVector<T> &v) {
    Logf("%s size %u capacity %u", what, Count(v), v.first == NULL ? 0 : unsigned(v.end - v.first));
    if (v.first != NULL)
        LogBytes(what, v.first, Count(v) * sizeof(T));
}

template <class T>
void RandomFill(Rng &rng, T *value) {
    for (size_t k = 0; k < sizeof(T); k++)
        reinterpret_cast<uint8_t *>(value)[k] = uint8_t(rng.Next());
}

template <class Vector, class T, class Fn>
void VectorScript(Rng &rng, const char *what, Fn insertN) {
    Vector v;
    memset(&v, 0, sizeof(v));
    for (int op = 0; op < 120; op++) {
        uint32_t size = Count(v);
        int at = v.first == NULL ? -1 : int(rng.Below(size + 1));   // logged as an index: the storage moves
        T *where = at < 0 ? NULL : v.first + at;
        uint32_t count = rng.Below(4) == 0 ? rng.Below(20) : rng.Below(4);
        T value;
        RandomFill(rng, &value);
        Guarded(what, [&] {
            insertN(&v, where, count, &value);
            Logf("%s insert %d %u", what, at, count);
            LogVector(what, v);
        });
    }
    FreeVector(&v);
}

void TestVectors(Rng &rng) {
    VectorScript<BarrierList, BarrierListEntry>(rng, "barriers", [](BarrierList *v, BarrierListEntry *w, uint32_t n, const BarrierListEntry *x) {
        ((BarrierInsertNFn)0x000c18c0)(v, 0, w, n, x);
    });
    VectorScript<ObjectList, WCollisionObject *>(rng, "objects", [](ObjectList *v, WCollisionObject **w, uint32_t n, WCollisionObject *const *x) {
        ((ObjectInsertNFn)0x000c1bf0)(v, 0, w, n, x);
    });
    VectorScript<StripList, const CollisionStrip *>(rng, "strips", [](StripList *v, const CollisionStrip **w, uint32_t n, const CollisionStrip *const *x) {
        ((StripInsertNFn)0x000c2c70)(v, 0, w, n, x);
    });
    VectorScript<InstanceList, InstanceListEntry>(rng, "instances", [](InstanceList *v, InstanceListEntry *w, uint32_t n, const InstanceListEntry *x) {
        ((InstanceInsertNFn)0x000c1f30)(v, 0, w, n, x);
    });

    // insert (one element, answering where it went)
    {
        BarrierList v;
        memset(&v, 0, sizeof(v));
        for (int op = 0; op < 60; op++) {
            BarrierListEntry value;
            RandomFill(rng, &value);
            uint32_t size = Count(v);
            BarrierListEntry *where = v.first == NULL ? NULL : v.first + rng.Below(size + 1);
            Guarded("barrier insert", [&] {
                BarrierListEntry *at = NULL;
                ((BarrierInsertFn)0x000c2b40)(&v, 0, &at, where, &value);
                Logf("barrier insert %d", int(at - v.first));
                LogVector("barrier insert", v);
            });
        }
        FreeVector(&v);
    }

    // reserve
    {
        WGridCellList nodes;
        ObjectList objects;
        memset(&nodes, 0, sizeof(nodes));
        memset(&objects, 0, sizeof(objects));
        for (int op = 0; op < 40; op++) {
            uint32_t count = rng.Below(64);
            uint32_t value = rng.Next();
            Guarded("reserve", [&] {
                ((ReserveFn)0x000c27a0)(&nodes, 0, count);
                if (Count(nodes) < uint32_t(nodes.end - nodes.first))
                    *nodes.last++ = value;
                ((ReserveFn)0x000c2970)(&objects, 0, count);
                if (Count(objects) < uint32_t(objects.end - objects.first))
                    *objects.last++ = reinterpret_cast<WCollisionObject *>(uintptr_t(value));
                LogVector("reserve nodes", nodes);
                LogVector("reserve objects", objects);
            });
        }
        FreeVector(&nodes);
        FreeVector(&objects);
    }

    // the helpers on their own
    for (int round = 0; round < 20; round++) {
        BarrierListEntry entries[24], value;
        InstanceListEntry instances[24], instanceValue;
        void *pointers[24], *sources[24];
        RandomFill(rng, &entries);
        RandomFill(rng, &value);
        RandomFill(rng, &instances);
        RandomFill(rng, &instanceValue);
        RandomFill(rng, &pointers);
        RandomFill(rng, &sources);
        uint32_t a = rng.Below(6), b = a + rng.Below(6);
        Guarded("helpers", [&] {
            BarrierListEntry *end = NULL;
            ((BarrierCopyBackwardFn)0x000c0d60)(&end, &entries[a], &entries[b], &entries[b + rng.Below(2)]);
            Logf("helpers copy back %d", int(end - entries));
            ((BarrierCopyBackwardFn)0x000bfe70)(&end, &entries[b], &entries[b], &entries[a]);
            Logf("helpers copy back empty %d", int(end - entries));
            ((BarrierFillFn)0x000c0d30)(&entries[a], &entries[b], &value);
            ((BarrierUninitializedFillFn)0x000c0da0)(&entries[b], a, &value);
            ((BarrierUninitializedFillFn)0x000c0da0)(NULL, 1, &value);   // only the first element is tested for NULL
            LogBytes("helpers barriers", entries, sizeof(entries));
            ((InstanceUninitializedFillFn)0x000c0dd0)(&instances[a], b - a, &instanceValue);
            ((InstanceUninitializedFillFn)0x000c0dd0)(NULL, 1, &instanceValue);
            LogBytes("helpers instances", instances, sizeof(instances));
            void **p = ((PointerUninitializedCopyFn)0x000bfeb0)(&sources[a], &sources[b], &pointers[0]);
            Logf("helpers ucopy %d", int(p - pointers));
            p = ((PointerUninitializedCopyFn)0x000bfeb0)(&sources[0], &sources[1], NULL);   // only the first destination is tested for NULL
            Logf("helpers ucopy null %u", unsigned(reinterpret_cast<uintptr_t>(p)));
            PointerVector vector;
            memset(&vector, 0, sizeof(vector));
            p = ((PointerUcopyFn)0x000c1230)(&vector, 0, &sources[b], &sources[b + 3], &pointers[4]);
            Logf("helpers member ucopy %d", int(p - pointers));
            p = ((PointerUfillFn)0x000c1290)(&vector, 0, &pointers[a], b - a, &sources[11]);
            Logf("helpers member ufill %d", int(p - pointers));
            LogBytes("helpers pointers", pointers, sizeof(pointers));
            BarrierList barriers;
            memset(&barriers, 0, sizeof(barriers));
            BarrierListEntry *e = ((BarrierUfillFn)0x000c1260)(&barriers, 0, &entries[a], b - a, &value);
            Logf("helpers barrier ufill %d", int(e - entries));
            InstanceList list;
            memset(&list, 0, sizeof(list));
            InstanceListEntry *i = ((InstanceUfillFn)0x000c12c0)(&list, 0, &instances[b], a, &instanceValue);
            Logf("helpers instance ufill %d", int(i - instances));
            LogBytes("helpers barriers", entries, sizeof(entries));
            LogBytes("helpers instances", instances, sizeof(instances));
        });
    }
}

// ---- the whole track's barriers and objects, listed by the shadow itself (the probes' lists can come out
// empty), with segments aimed across each

struct TrackLists {
    BarrierList barriers;       // every barrier of every instance: in the world's frame unless the instance is tilted
    InstanceList instances;     // the instances that have barriers (strips NULL)
    ObjectList cylinders;
    ObjectList boxes;
    uint32_t objectCount;
};

TrackLists *g_track;

template <class T>
void PushBack(GameVector<T> *v, const T &value) {
    uint32_t size = Count(*v);
    uint32_t capacity = v->first == NULL ? 0 : uint32_t(v->end - v->first);
    if (size == capacity) {
        uint32_t grown = capacity == 0 ? 16 : capacity * 2;
        T *storage = static_cast<T *>(UMemory::FastAlloc(grown * sizeof(T), "STL"));
        if (size != 0)
            memcpy(storage, v->first, size * sizeof(T));
        FreeVector(v);
        v->first = storage;
        v->last = storage + size;
        v->end = storage + grown;
    }
    *v->last++ = value;
}

void BuildTrackLists() {
    WCollisionMgr *manager = Manager();
    TrackLists &t = *g_track;
    for (uint32_t i = 0; i < manager->instanceCount && Count(t.barriers) < 600; i++) {
        WCollisionInstance *instance = &manager->instances[i];
        const CollisionArticle *article = instance->article;
        if (article == NULL || article->barrierCount == 0)
            continue;
        InstanceListEntry listed = { instance, NULL };
        PushBack(&t.instances, listed);
        MATRIX4 back;
        WCollisionInstance_MakeMatrix(instance, 0, &back, 1);
        OrthoInverse(&back);
        for (uint32_t b = 0; b < article->barrierCount; b++) {
            BarrierListEntry entry;
            entry.barrier = article->Barriers()[b];
            entry.instance = instance;
            entry.index = b;
            if (!(instance->flags & kInstanceTilted)) {
                VU0_MATRIX4_vect3mult(&article->Barriers()[b].x0, &back, &entry.barrier.x0);
                VU0_MATRIX4_vect3mult(&article->Barriers()[b].x1, &back, &entry.barrier.x1);
            }
            PushBack(&t.barriers, entry);
        }
    }
    // the objects the grid lists (kind 2), each once
    GridView *grid = fgGrid;
    std::vector<uint8_t> seen;
    for (uint32_t c = 0; c < grid->rows * grid->columns; c++) {
        WGridNode *cell = grid->cells[c];
        if (cell == NULL)
            continue;
        const uint16_t *index = cell->StaticIndices(2);
        for (uint32_t k = 0; k < cell->staticCount[2]; k++) {
            uint16_t at = index[k];
            if (at >= seen.size())
                seen.resize(at + 1, 0);
            if (seen[at])
                continue;
            seen[at] = 1;
            t.objectCount++;
            WCollisionObject *object = &manager->objects[at];
            PushBack(object->isCylinder ? &t.cylinders : &t.boxes, object);
        }
    }
}

void FreeTrackLists() {
    FreeVector(&g_track->barriers);
    FreeVector(&g_track->instances);
    FreeVector(&g_track->cylinders);
    FreeVector(&g_track->boxes);
}

void TestTrackBarriers(Rng &rng) {
    WCollisionMgr *manager = Manager();
    const TrackLists &t = *g_track;
    uint32_t n = Count(t.barriers);
    for (uint32_t i = 0; i < n; i += 1 + n / 150) {
        const BarrierListEntry *entry = &t.barriers.first[i];
        const CollisionBarrier &b = entry->barrier;
        float along = rng.Range(0.1f, 0.9f);
        Coord4 middle = Point(b.x0 + (b.x1 - b.x0) * along, b.y0 + (b.y1 - b.y0) * rng.Range(0.2f, 0.8f),
                              b.z0 + (b.z1 - b.z0) * along);
        float nx = (b.z1 - b.z0) * b.inverseLength * 1.5f;
        float nz = -(b.x1 - b.x0) * b.inverseLength * 1.5f;
        Coord4 segment[2] = { Offset(middle, nx, 0, nz), Offset(middle, -nx, rng.Range(-0.1f, 0.1f), -nz) };
        if (i % 3 == 1) {   // the other way across
            Coord4 swap = segment[0];
            segment[0] = segment[1];
            segment[1] = swap;
        }
        if (entry->instance->flags & kInstanceTilted) {
            MATRIX4 back;
            WCollisionInstance_MakeMatrix(entry->instance, 0, &back, 1);
            OrthoInverse(&back);
            Coord4 world[2];
            VU0_MATRIX4_vect3mult(&segment[0], &back, &world[0]);
            VU0_MATRIX4_vect3mult(&segment[1], &back, &world[1]);
            segment[0] = world[0];
            segment[1] = world[1];
        }
        Guarded("track barrier", [&] {
            WorldCollisionInfo info;
            memset(&info, 0xcd, sizeof(info));
            bool hit = ClosestBarrierAt(manager, 0, &t.barriers, segment, &info);
            Reached(kReachBarrier, hit);
            Logf("track barrier closest %u %d", i, hit);
            LogInfo("track barrier info", &info, false);
            memset(&info, 0xcd, sizeof(info));
            hit = BarrierCollisionAt(manager, 0, &t.barriers, segment, &info);
            Logf("track barrier collision %d", hit);
            LogInfo("track barrier info", &info, false);
            memset(&info, 0xcd, sizeof(info));
            hit = BarrierNormalAt(manager, 0, &t.instances, segment, &info);
            Reached(kReachBarrierNormal, hit);
            Logf("track barrier normal %d", hit);
            LogInfo("track barrier info", &info, false);
            for (int ground = 0; ground < 2; ground++) {
                memset(&info, 0xcd, sizeof(info));
                hit = GetWorldNormalAt(manager, 0, &t.instances, &t.barriers, segment, &info, ground);
                Reached(kReachWorldNormal, hit);
                Logf("track worldnormal %d %d", ground, hit);
                LogInfo("track worldnormal info", &info, true);
            }
        });
    }
}

void TestTrackObjects(Rng &rng) {
    WCollisionMgr *manager = Manager();
    const TrackLists &t = *g_track;
    const ObjectList *lists[2] = { &t.cylinders, &t.boxes };
    for (int which = 0; which < 2; which++) {
        const ObjectList *list = lists[which];
        uint32_t n = Count(*list);
        for (uint32_t i = 0; i < n; i += 1 + n / 80) {
            const WCollisionObject *object = list->first[i];
            float y = object->position.y + object->halfExtents.y * rng.Range(0.1f, 1.9f);
            float reach = (which == 0 ? object->radius
                                      : fabsf(object->halfExtents.x) + fabsf(object->halfExtents.z)) + 2.0f;
            float angle = rng.Range(0, 6.2831853f);
            Coord4 segment[2] = {
                Point(object->position.x - cosf(angle) * reach, y, object->position.z - sinf(angle) * reach),
                Point(object->position.x + cosf(angle) * reach, y + rng.Range(-0.3f, 0.3f),
                      object->position.z + sinf(angle) * reach),
            };
            if (which == 1 && i % 2 == 1) {   // down through the box's top
                segment[0] = Point(object->position.x + rng.Range(-0.3f, 0.3f),
                                   object->position.y + 3 * fabsf(object->halfExtents.y) + 1,
                                   object->position.z + rng.Range(-0.3f, 0.3f));
                segment[1] = Point(object->position.x, object->position.y, object->position.z);
            }
            Guarded("track objects", [&] {
                Coord4 hit;
                memset(&hit, 0xcd, sizeof(hit));
                WCollisionObject *found =
                    (which == 0 ? ClosestCylObjectAt : ClosestOBBObjectAt)(manager, 0, segment, &hit, list);
                Reached(which == 0 ? kReachCylinder : kReachBox, found != NULL);
                Logf("track objects %d %u %p", which, i, found);
                LogBytes("track objects hit", &hit, sizeof(hit));
                WorldCollisionInfo info;
                memset(&info, 0xcd, sizeof(info));
                bool collided =
                    (which == 0 ? CylObjectCollisionAt : OBBObjectCollisionAt)(manager, 0, segment, list, &info);
                Logf("track objects collision %d", collided);
                LogInfo("track objects info", &info, false);
                if (which == 1) {
                    MATRIX4 frame;
                    WCollisionObject_MakeMatrix(object, 0, &frame, 1);
                    memset(&hit, 0xcd, sizeof(hit));
                    bool through =
                        GetOBBObjectIntersectionAt(manager, 0, segment, &frame, &object->halfExtents, &hit);
                    Reached(kReachBoxIntersection, through);
                    Logf("track box %d", through);
                    LogBytes("track box hit", &hit, sizeof(hit));
                }
            });
        }
        TestPop(*list);
    }
}

// ---- a pass

void RunPass(bool original, std::string *log) {
    g_log = log;
    Originals sides(original);
    Rng rng = { 0x6d2b79f5 };
    for (const Probe &probe : *g_probes) {
        Logf("probe %08x %08x %08x %d", Bits(probe.point.x), Bits(probe.point.y), Bits(probe.point.z), probe.hasGround);
        TestHeights(rng, probe);
        TestFaces(rng, probe);
        TestBarriers(rng, probe);
        TestObjects(rng, probe);
        TestClosest(rng, probe);
    }
    TestTrackBarriers(rng);
    TestTrackObjects(rng);
    TestStrips(rng);
    TestOBB(rng);
    TestGrid();
    TestPaneMap(rng);
    TestArticleMap(rng);
    TestWindowMapMin(rng);
    TestVectors(rng);
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

void ColQueryShadow_Run(void) {
    const char *env = getenv("NIGHTFIRE_COLQUERYSHADOW");
    if (env == NULL || atoi(env) == 0)
        return;
    WCollisionMgr *manager = Manager();
    if (manager == NULL || fgGrid == NULL || manager->instances == NULL) {
        printf("[colquery] no collision data loaded: nothing tested\n");
        fflush(stdout);
        return;
    }

    std::vector<Region> regions;
    std::vector<Probe> probes;
    g_regions = &regions;
    g_probes = &probes;

    // the longest strip, for the buffers FindFaceInTriStrip writes
    g_maxStripVertices = 0;
    for (uint32_t i = 0; i < manager->instanceCount; i++) {
        const CollisionArticle *article = manager->instances[i].article;
        if (article == NULL)
            continue;
        for (uint32_t s = 0; s < article->stripCount; s++) {
            int count = article->Vertices(&article->Strips()[s])->count;
            if (count > 0 && uint32_t(count) > g_maxStripVertices && count < 0x10000)
                g_maxStripVertices = uint32_t(count);
        }
    }
    regions.push_back(Region{ reinterpret_cast<uint8_t *>(manager), sizeof(WCollisionMgr) });
    regions.push_back(Region{ reinterpret_cast<uint8_t *>(manager->instances),
                              manager->instanceCount * uint32_t(sizeof(WCollisionInstance)) });
    regions.push_back(Region{ ScratchPad + 0x3b0, 182 * uint32_t(sizeof(StripVertex)) });
    if (g_maxStripVertices >= 182)
        regions.push_back(Region{ LargeStripScratch, g_maxStripVertices * uint32_t(sizeof(StripVertex)) });

    BuildProbes();
    TrackLists track;
    memset(&track, 0, sizeof(track));
    g_track = &track;
    {
        Originals originals(true);
        BuildTrackLists();
    }

    std::vector<uint8_t> before, afterOriginal, afterPort;
    SaveState(&before);
    std::string logOriginal, logPort;
    g_counting = true;
    RunPass(true, &logOriginal);
    g_counting = false;
    SaveState(&afterOriginal);
    RestoreState(before);
    RunPass(false, &logPort);
    SaveState(&afterPort);

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
                printf("[colquery] line %u differs:\n  original %.300s\n  port     %.300s\n", unsigned(i), x.c_str(),
                       y.c_str());
            g_diffs++;
        }
    }
    g_checks++;
    if (afterOriginal != afterPort) {
        size_t at = 0;
        for (const Region &r : regions) {
            if (memcmp(&afterOriginal[at], &afterPort[at], r.size) != 0)
                printf("[colquery] state differs in %p (+%u bytes)\n", r.at, r.size);
            at += r.size;
        }
        g_diffs++;
    }

    printf("[colquery] reached: ground %d, height %d, face at %d, face along %d, barrier %d, barrier normal %d, "
           "world normal %d, cylinder %d, box %d, obb %d, strip triangle %d, tri strip %d, pop %d (of %u probes, "
           "longest strip %u)\n",
           g_reached[kReachGround], g_reached[kReachHeight], g_reached[kReachFaceAt], g_reached[kReachFaceAlong],
           g_reached[kReachBarrier], g_reached[kReachBarrierNormal], g_reached[kReachWorldNormal],
           g_reached[kReachCylinder], g_reached[kReachBox], g_reached[kReachOBB], g_reached[kReachStripTriangle],
           g_reached[kReachTriStrip], g_reached[kReachPop], unsigned(probes.size()), g_maxStripVertices);
    printf("[colquery] track: %u barriers on %u instances, %u objects (%u cylinders, %u boxes); box intersections "
           "%d\n", Count(track.barriers), Count(track.instances), track.objectCount, Count(track.cylinders),
           Count(track.boxes), g_reached[kReachBoxIntersection]);
    printf("[colquery] collision queries and lists vs originals: %d cases, %d checks, %d differ%s\n", g_cases / 2,
           g_checks, g_diffs, g_faults ? " (with faults)" : "");
    if (g_faults)
        printf("[colquery] %d faults\n", g_faults);
    fflush(stdout);

    FreeProbes();
    FreeTrackLists();
    g_track = NULL;
    g_regions = NULL;
    g_probes = NULL;
}
