#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "ColListShadow.h"

#include <windows.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vector>

#include "../world/CollisionManager.h"
#include "../world/World.h"
#include "../EventManager.hpp"
#include "../engine/UGroup.h"
#include "../engine/UMemory.hpp"
#include "../platform/RealMath.h"
#include "../../common/xbeOriginal.h"
#include "../../helpers.h"

// ---------------------------------------------------------------------------------------------------------------
// A shadow test of WCollisionMgr's lists, hit checks and containers (world/CollisionManager.cpp), run once from the
// first simulation tick when NIGHTFIRE_COLLISTSHADOW is set: the track's collision data is loaded by then. Each
// case runs the PORT and then the ORIGINAL (every entry of the package swapped back, 0x000c2ff0-0x000c5700) on the
// same inputs over the real world, with the state a call can write - the manager, every collision instance (the
// list queries stamp them), the window and article maps, the game's random seed - snapshotted before and put back
// between the two, and compares the results and the state after:
//
//   - GetInstanceList (segment; point with and without strips, flat or not), GetObjectLists (both), GetBarrierList,
//     CheckHitWorld and StepCheckHitWorld: random segments and points inside the track's bounds (taken from the
//     instances' positions), near the ground and through geometry, short and zero-length segments too. Lists
//     compare by content (pointers into the world as addresses), strip lists by content;
//   - CheckHitWindow, creating: hits built inside real window panes (and some beside them) in each windowed
//     instance's frame, repeated so the second hit on a pane takes the other path, into a fresh window map on each
//     side; Event::operator new and the EHitWindow constructor are replaced by recording fakes, and the events'
//     arguments, the infos, and the maps (shape, colours, keys, damage records) are compared;
//   - the window map alone: hundreds of inserts of random keys and erases of single nodes and short ranges, against
//     the original insert_unique and erase(first, last), comparing the trees after each step;
//   - SetCollisionArticle on real render instances with valid, invalid and -1 articles, into a fresh article map.
//
// WorldCollisionInfo's segment words (+0x20..+0x3f) are not compared after CheckHitWorld: the game's constructor
// copies them from its own uninitialised stack. Init, Restart, Shutdown and the constructor are left to the
// lockstep runs (loading, restarting and leaving a mission).
//
// One summary line: [collistshadow] ...: N cases, M checks, D differ.
// ---------------------------------------------------------------------------------------------------------------

namespace {

const unsigned kPackageLo = 0x000c2ff0;
const unsigned kPackageHi = 0x000c5700;   // (0x000c5700 is GameCallbacks.cpp's)

#define Orig_GetInstanceListSegment ((void (__fastcall *)(WCollisionMgr *, int, InstanceList *, const Coord4 *))0x000c3410)
#define Orig_GetInstanceListPoint ((void (__fastcall *)(WCollisionMgr *, int, InstanceList *, const Coord3 *, float, bool, bool))0x000c4510)
#define Orig_GetObjectListsSegment ((void (__fastcall *)(WCollisionMgr *, int, ObjectList *, ObjectList *, const Coord4 *))0x000c36a0)
#define Orig_GetObjectListsPoint ((void (__fastcall *)(WCollisionMgr *, int, ObjectList *, ObjectList *, const Coord3 *, float))0x000c37c0)
#define Orig_GetBarrierList ((void (__fastcall *)(WCollisionMgr *, int, BarrierList *, const InstanceList *, const Coord3 *, float))0x000c3880)
// The two answer 0 or 1 in the whole of EAX, and their callers test all of it: both sides are called through the
// address, typed int, so upper bytes left set in the port's EAX show as a difference.
#define Orig_CheckHitWorld ((int (__fastcall *)(WCollisionMgr *, int, const Coord4 *, WorldCollisionInfo *))0x000c3b40)
#define Orig_StepCheckHitWorld ((int (__fastcall *)(WCollisionMgr *, int, Coord4 *, float))0x000c4000)
#define Orig_CheckHitWindow ((bool (__fastcall *)(WCollisionMgr *, int, WorldCollisionInfo *, bool, int))0x000c4d70)
#define Orig_SetCollisionArticle ((bool (__fastcall *)(WCollisionMgr *, int, CARP::Instance *, uint32_t))0x000c2ff0)
#define Orig_WindowMapInsertUnique ((WindowMapInsert *(__fastcall *)(WindowMap *, int, WindowMapInsert *, const WindowMapValue *))0x000c4c60)
#define Orig_WindowMapEraseRange ((WindowMapNode **(__fastcall *)(WindowMap *, int, WindowMapNode **, WindowMapNode *, WindowMapNode *))0x000c52c0)

// helpers of the game's, the same on both sides
#define Game_CalcPosition ((void (__fastcall *)(WCollisionInstance *, int, Coord3 *))0x000be810)
#define Game_MakeMatrix ((void (__fastcall *)(WCollisionInstance *, int, MATRIX4 *, bool))0x000be8b0)
#define Game_FindWindowSet ((const WindowSet *(__fastcall *)(const WindowGroup *, int, uint32_t))0x000beb40)
#define Game_ConstructInfo ((WorldCollisionInfo *(__fastcall *)(WorldCollisionInfo *, int))0x0001d9f0)
#define Game_WindowMapBuyhead ((WindowMapNode *(*)())0x00053580)
#define Game_ArticleMapBuyhead ((ArticleMapNode *(*)())0x00094030)
#define Game_ArticleMapEraseRange ((ArticleMapNode **(__fastcall *)(ArticleMap *, int, ArticleMapNode **, ArticleMapNode *, ArticleMapNode *))0x000c2f30)

#define RandomSeed U32_AT(0x001c45c4)       // FUN_0001aab0's state, and its multiplier
#define RandomMultiplier U32_AT(0x001c45c8)


long g_cases, g_checks, g_differ, g_faults;
int g_reported;

void Check(bool same, const char *what, long index) {
    g_checks++;
    if (same)
        return;
    g_differ++;
    if (g_reported < 10) {
        g_reported++;
        printf("[collistshadow] DIFF %s, case %ld\n", what, index);
        fflush(stdout);
    }
}

// a small LCG of our own, so a run is the same every time
uint32_t g_seed = 0x2b7e1516;
uint32_t NextRandom() {
    g_seed = g_seed * 1664525u + 1013904223u;
    return g_seed;
}
float Uniform(float lo, float hi) {
    return lo + (hi - lo) * float(NextRandom() >> 8) * (1.0f / 16777216.0f);
}

// The originals' window: every patched entry in the package put back.
struct OriginalsWindow {
    OriginalsWindow() { XbeOriginal_RestoreRange(kPackageLo, kPackageHi, true); }
    ~OriginalsWindow() { XbeOriginal_RestoreRange(kPackageLo, kPackageHi, false); }
};

// A call that faults is counted, not fatal.
template <class F>
bool Guarded(const F &call) {
#ifdef _MSC_VER
    __try {
        call();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_faults++;
        return false;
    }
#else
    call();
#endif
    return true;
}

// What a call can write: the manager and the instances (their query stamps, and articles), the random seed.
struct State {
    uint8_t manager[sizeof(WCollisionMgr)];
    std::vector<uint8_t> instances;
    uint32_t seed;
};

void Save(State *s) {
    WCollisionMgr *m = fgCollisionMgr;
    memcpy(s->manager, m, sizeof(WCollisionMgr));
    s->instances.assign(reinterpret_cast<uint8_t *>(m->instances),
                        reinterpret_cast<uint8_t *>(m->instances + m->instanceCount));
    s->seed = RandomSeed;
}

void Load(const State &s) {
    WCollisionMgr *m = fgCollisionMgr;
    memcpy(m, s.manager, sizeof(WCollisionMgr));
    memcpy(m->instances, s.instances.data(), s.instances.size());
    RandomSeed = s.seed;
}

bool SameState(const State &a, const State &b) {
    return memcmp(a.manager, b.manager, sizeof(a.manager)) == 0 && a.instances == b.instances && a.seed == b.seed;
}

template <class T>
size_t Count(const GameVector<T> &v) {
    return v.first == NULL ? 0 : size_t(v.last - v.first);
}

template <class T>
bool SameContent(const GameVector<T> &a, const GameVector<T> &b) {
    size_t n = Count(a);
    return n == Count(b) && (n == 0 || memcmp(a.first, b.first, n * sizeof(T)) == 0);
}

template <class T>
void Clear(GameVector<T> *v) {
    v->allocator = 0;
    v->first = NULL;
    v->last = NULL;
    v->end = NULL;
}

template <class T>
void Free(GameVector<T> *v) {
    if (v->first != NULL)
        UMemory::FastFree(v->first, unsigned((v->end - v->first) * sizeof(T)));
    Clear(v);
}

void FreeInstances(InstanceList *list) {
    for (size_t i = 0; i < Count(*list); i++) {
        StripList *strips = list->first[i].strips;
        if (strips != NULL) {
            Free(strips);
            OperatorDelete(strips);
        }
    }
    Free(list);
}

bool SameInstances(const InstanceList &a, const InstanceList &b) {
    size_t n = Count(a);
    if (n != Count(b))
        return false;
    for (size_t i = 0; i < n; i++) {
        const InstanceListEntry &x = a.first[i], &y = b.first[i];
        if (x.instance != y.instance || (x.strips == NULL) != (y.strips == NULL))
            return false;
        if (x.strips != NULL && (x.strips->article != y.strips->article || !SameContent(*x.strips, *y.strips)))
            return false;
    }
    return true;
}

// ---- the track's bounds, from the instances' positions

float g_lo[3], g_hi[3];

bool FindBounds() {
    WCollisionMgr *m = fgCollisionMgr;
    bool any = false;
    for (uint32_t i = 0; i < m->instanceCount; i++) {
        WCollisionInstance *instance = &m->instances[i];
        if (instance->article == NULL)
            continue;
        Coord3 p;
        Game_CalcPosition(instance, 0, &p);
        const float v[3] = {p.x, p.y, p.z};
        for (int k = 0; k < 3; k++) {
            if (!any || v[k] < g_lo[k])
                g_lo[k] = v[k];
            if (!any || v[k] > g_hi[k])
                g_hi[k] = v[k];
        }
        any = true;
    }
    return any;
}

void RandomPoint(float *p) {
    p[0] = Uniform(g_lo[0], g_hi[0]);
    p[1] = Uniform(g_lo[1] - 5.0f, g_hi[1] + 10.0f);
    p[2] = Uniform(g_lo[2], g_hi[2]);
}

void RandomSegment(Coord4 *segment) {
    RandomPoint(&segment[0].x);
    segment[0].w = 1.0f;
    uint32_t kind = NextRandom() % 10;
    float length = kind == 0 ? 0.0f : kind == 1 ? Uniform(0.0f, 0.1f) : Uniform(0.5f, 80.0f);
    float dx = Uniform(-1.0f, 1.0f), dy = Uniform(-0.6f, 0.3f), dz = Uniform(-1.0f, 1.0f);
    segment[1].x = segment[0].x + dx * length;
    segment[1].y = segment[0].y + dy * length;
    segment[1].z = segment[0].z + dz * length;
    segment[1].w = 1.0f;
}

// ---- the list queries

void TestInstanceListSegment(const Coord4 *segment) {
    WCollisionMgr *m = fgCollisionMgr;
    State before, port, original;
    Save(&before);
    InstanceList p, o;
    Clear(&p);
    Clear(&o);
    bool okP = Guarded([&] { m->GetInstanceList(&p, segment); });
    Save(&port);
    Load(before);
    bool okO;
    {
        OriginalsWindow window;
        okO = Guarded([&] { Orig_GetInstanceListSegment(m, 0, &o, segment); });
    }
    Save(&original);
    Load(before);
    Check(okP == okO, "GetInstanceList(segment) faults", g_cases);
    Check(SameInstances(p, o), "GetInstanceList(segment) list", g_cases);
    Check(SameState(port, original), "GetInstanceList(segment) state", g_cases);
    FreeInstances(&p);
    FreeInstances(&o);
    g_cases++;
}

void TestInstanceListPoint(const Coord3 *point, float radius, bool wantStrips, bool flat) {
    WCollisionMgr *m = fgCollisionMgr;
    State before, port, original;
    Save(&before);
    InstanceList p, o;
    Clear(&p);
    Clear(&o);
    bool okP = Guarded([&] { m->GetInstanceList(&p, point, radius, wantStrips, flat); });
    Save(&port);
    Load(before);
    bool okO;
    {
        OriginalsWindow window;
        okO = Guarded([&] { Orig_GetInstanceListPoint(m, 0, &o, point, radius, wantStrips, flat); });
    }
    Save(&original);
    Load(before);
    Check(okP == okO, "GetInstanceList(point) faults", g_cases);
    Check(SameInstances(p, o), wantStrips ? "GetInstanceList(point, strips) list" : "GetInstanceList(point) list",
          g_cases);
    Check(SameState(port, original), "GetInstanceList(point) state", g_cases);
    FreeInstances(&p);
    FreeInstances(&o);
    g_cases++;
}

void TestObjectLists(const Coord4 *segment, const Coord3 *point, float radius) {
    WCollisionMgr *m = fgCollisionMgr;
    for (int variant = 0; variant < 2; variant++) {
        ObjectList pc, pb, oc, ob;
        Clear(&pc);
        Clear(&pb);
        Clear(&oc);
        Clear(&ob);
        State before, port, original;
        Save(&before);
        bool okP = Guarded([&] {
            if (variant == 0)
                m->GetObjectLists(&pc, &pb, segment);
            else
                m->GetObjectLists(&pc, &pb, point, radius);
        });
        Save(&port);
        Load(before);
        bool okO;
        {
            OriginalsWindow window;
            okO = Guarded([&] {
                if (variant == 0)
                    Orig_GetObjectListsSegment(m, 0, &oc, &ob, segment);
                else
                    Orig_GetObjectListsPoint(m, 0, &oc, &ob, point, radius);
            });
        }
        Save(&original);
        Load(before);
        Check(okP == okO, "GetObjectLists faults", g_cases);
        Check(SameContent(pc, oc), variant == 0 ? "GetObjectLists(segment) cylinders" : "GetObjectLists(point) cylinders", g_cases);
        Check(SameContent(pb, ob), variant == 0 ? "GetObjectLists(segment) boxes" : "GetObjectLists(point) boxes", g_cases);
        Check(SameState(port, original), "GetObjectLists state", g_cases);
        Free(&pc);
        Free(&pb);
        Free(&oc);
        Free(&ob);
        g_cases++;
    }
}

void TestBarrierList(const Coord3 *point, float radius) {
    WCollisionMgr *m = fgCollisionMgr;
    State before;
    Save(&before);
    InstanceList nearby;
    Clear(&nearby);
    Guarded([&] { m->GetInstanceList(&nearby, point, radius * 2.0f + 10.0f, false, false); });
    Load(before);
    BarrierList p, o;
    Clear(&p);
    Clear(&o);
    bool okP = Guarded([&] { m->GetBarrierList(&p, &nearby, point, radius); });
    bool okO;
    {
        OriginalsWindow window;
        okO = Guarded([&] { Orig_GetBarrierList(m, 0, &o, &nearby, point, radius); });
    }
    State after;
    Save(&after);
    Check(okP == okO, "GetBarrierList faults", g_cases);
    Check(SameContent(p, o), "GetBarrierList list", g_cases);
    Check(SameState(before, after), "GetBarrierList state", g_cases);
    Load(before);
    Free(&p);
    Free(&o);
    FreeInstances(&nearby);
    g_cases++;
}

// ---- the hit checks

bool SameInfo(const WorldCollisionInfo &a, const WorldCollisionInfo &b) {
    const uint8_t *x = reinterpret_cast<const uint8_t *>(&a), *y = reinterpret_cast<const uint8_t *>(&b);
    return memcmp(x, y, offsetof(WorldCollisionInfo, segmentStart)) == 0 &&
           memcmp(x + offsetof(WorldCollisionInfo, instance), y + offsetof(WorldCollisionInfo, instance),
                  sizeof(WorldCollisionInfo) - offsetof(WorldCollisionInfo, instance)) == 0;
}

void TestCheckHitWorld(const Coord4 *segment) {
    WCollisionMgr *m = fgCollisionMgr;
    WorldCollisionInfo start;
    memset(&start, 0, sizeof(start));
    Game_ConstructInfo(&start, 0);
    WorldCollisionInfo p = start, o = start;
    State before, port, original;
    Save(&before);
    int hitP = 0, hitO = 0;
    bool okP = Guarded([&] { hitP = Orig_CheckHitWorld(m, 0, segment, &p); });   // outside the window: the port
    Save(&port);
    Load(before);
    bool okO;
    {
        OriginalsWindow window;
        okO = Guarded([&] { hitO = Orig_CheckHitWorld(m, 0, segment, &o); });
    }
    Save(&original);
    Load(before);
    Check(okP == okO, "CheckHitWorld faults", g_cases);
    Check(hitP == hitO, "CheckHitWorld answer", g_cases);
    Check(SameInfo(p, o), "CheckHitWorld info", g_cases);
    Check(SameState(port, original), "CheckHitWorld state", g_cases);
    g_cases++;
}

void TestStepCheckHitWorld(const Coord4 *segment) {
    WCollisionMgr *m = fgCollisionMgr;
    Coord4 p[2] = {segment[0], segment[1]}, o[2] = {segment[0], segment[1]};
    State before, port, original;
    Save(&before);
    int hitP = 0, hitO = 0;
    bool okP = Guarded([&] { hitP = Orig_StepCheckHitWorld(m, 0, p, 16.0f); });   // outside the window: the port
    Save(&port);
    Load(before);
    bool okO;
    {
        OriginalsWindow window;
        okO = Guarded([&] { hitO = Orig_StepCheckHitWorld(m, 0, o, 16.0f); });
    }
    Save(&original);
    Load(before);
    Check(okP == okO, "StepCheckHitWorld faults", g_cases);
    Check(hitP == hitO, "StepCheckHitWorld answer", g_cases);
    Check(memcmp(p, o, sizeof(p)) == 0, "StepCheckHitWorld segment", g_cases);
    Check(SameState(port, original), "StepCheckHitWorld state", g_cases);
    g_cases++;
}

// ---- the window map: a tree's shape, colours and contents, in preorder

void Serialize(const WindowPaneNode *node, std::vector<uint32_t> *out) {
    if (node->isNil) {
        out->push_back(0xfffffffe);
        return;
    }
    out->push_back(uint32_t(uintptr_t(node->value.pane)));
    out->push_back(node->color);
    const uint32_t *hit = reinterpret_cast<const uint32_t *>(&node->value.hit);
    out->insert(out->end(), hit, hit + sizeof(WindowHit) / 4);
    Serialize(node->left, out);
    Serialize(node->right, out);
}

void Serialize(const WindowMapNode *node, std::vector<uint32_t> *out) {
    if (node->isNil) {
        out->push_back(0xffffffff);
        return;
    }
    out->push_back(uint32_t(uintptr_t(node->value.instance)));
    out->push_back(node->color);
    out->push_back(node->value.panes.size);
    Serialize(node->value.panes.head->parent, out);
    Serialize(node->left, out);
    Serialize(node->right, out);
}

std::vector<uint32_t> Serialize(const WindowMap &map) {
    std::vector<uint32_t> out;
    out.push_back(map.size);
    // the head's links: leftmost and rightmost, as keys
    out.push_back(map.head->left->isNil ? 0 : uint32_t(uintptr_t(map.head->left->value.instance)));
    out.push_back(map.head->right->isNil ? 0 : uint32_t(uintptr_t(map.head->right->value.instance)));
    Serialize(map.head->parent, &out);
    return out;
}

void InOrder(WindowMapNode *node, std::vector<WindowMapNode *> *out) {
    if (node->isNil)
        return;
    InOrder(node->left, out);
    out->push_back(node);
    InOrder(node->right, out);
}

void NewWindowMap(WindowMap *map) {
    map->allocator = 0;
    map->head = Game_WindowMapBuyhead();
    map->head->isNil = 1;
    map->head->parent = map->head;
    map->head->left = map->head;
    map->head->right = map->head;
    map->size = 0;
}

void DeleteWindowMap(WindowMap *map) {
    WindowMapNode *next;
    map->EraseRange(&next, map->head->left, map->head);
    UMemory::FastFree(map->head, sizeof(WindowMapNode));
    map->head = NULL;
    map->size = 0;
}

// Hundreds of inserts and erases, port against original.
void TestWindowMap() {
    WindowMap p, o;
    NewWindowMap(&p);
    NewWindowMap(&o);
    WindowPaneMap empty;
    empty.Construct();
    for (int i = 0; i < 400; i++) {
        WindowMapValue value;
        value.instance = reinterpret_cast<WCollisionInstance *>(uintptr_t(0x10000000u + (NextRandom() % 300) * 0x40));
        value.panes = empty;
        WindowMapInsert rp = {}, ro = {};
        bool okP = Guarded([&] { p.InsertUnique(&rp, &value); });
        bool okO;
        {
            OriginalsWindow window;
            okO = Guarded([&] { Orig_WindowMapInsertUnique(&o, 0, &ro, &value); });
        }
        Check(okP == okO && rp.inserted == ro.inserted, "window map insert answer", g_cases);
        if ((i & 15) == 15)
            Check(Serialize(p) == Serialize(o), "window map after inserts", g_cases);
        g_cases++;
    }
    Check(Serialize(p) == Serialize(o), "window map after inserts", g_cases);
    for (int i = 0; i < 160 && p.size > 0; i++) {
        std::vector<WindowMapNode *> np, no;
        InOrder(p.head->parent, &np);
        InOrder(o.head->parent, &no);
        if (np.size() != no.size())
            break;
        size_t first = NextRandom() % np.size();
        size_t count = (NextRandom() % 4 == 0) ? 1 + NextRandom() % 5 : 1;
        size_t last = first + count;
        WindowMapNode *lp = last >= np.size() ? p.head : np[last];
        WindowMapNode *lo = last >= no.size() ? o.head : no[last];
        WindowMapNode *rp = NULL, *ro = NULL;
        bool okP = Guarded([&] { p.EraseRange(&rp, np[first], lp); });
        bool okO;
        {
            OriginalsWindow window;
            okO = Guarded([&] { Orig_WindowMapEraseRange(&o, 0, &ro, no[first], lo); });
        }
        Check(okP == okO && (rp == p.head) == (ro == o.head), "window map erase answer", g_cases);
        Check(Serialize(p) == Serialize(o), "window map after an erase", g_cases);
        g_cases++;
    }
    // the whole tree at once
    {
        WindowMapNode *rp = NULL, *ro = NULL;
        Guarded([&] { p.EraseRange(&rp, p.head->left, p.head); });
        {
            OriginalsWindow window;
            Guarded([&] { Orig_WindowMapEraseRange(&o, 0, &ro, o.head->left, o.head); });
        }
        Check(Serialize(p) == Serialize(o) && rp == p.head && ro == o.head, "window map cleared", g_cases);
        g_cases++;
    }
    DeleteWindowMap(&p);
    DeleteWindowMap(&o);
    empty.Destruct();
}

// ---- CheckHitWindow, creating, with recording fakes for the event

struct EventRecord {
    Coord4 point;
    Coord4 normal;
    WindowHit hit;
    int flag;
    uint32_t size;
};

std::vector<EventRecord> g_events;
uint32_t g_lastNewSize;
alignas(16) uint8_t g_eventBytes[0x60];

void *FakeEventNew(size_t size) {
    g_lastNewSize = uint32_t(size);
    return g_eventBytes;
}

void __fastcall FakeHitWindow(void *self, int, Coord4 point, Coord4 normal, WindowHit hit, int flag) {
    (void)self;
    EventRecord r;
    r.point = point;
    r.normal = normal;
    r.hit = hit;
    r.flag = flag;
    r.size = g_lastNewSize;
    g_events.push_back(r);
}

struct Hook {
    uint32_t at;
    uint8_t saved[5];
    bool on;
};
Hook g_hooks[4];
int g_hookCount;

void HookInstall(uint32_t at, const void *to) {
    Hook &h = g_hooks[g_hookCount++];
    h.at = at;
    h.on = false;
    DWORD old;
    if (!VirtualProtect((void *)(uintptr_t)at, 5, PAGE_EXECUTE_READWRITE, &old))
        return;
    memcpy(h.saved, (void *)(uintptr_t)at, 5);
    uint8_t jump[5];
    jump[0] = 0xe9;
    int32_t rel = (int32_t)((uint32_t)(uintptr_t)to - (at + 5));
    memcpy(jump + 1, &rel, 4);
    memcpy((void *)(uintptr_t)at, jump, 5);
    VirtualProtect((void *)(uintptr_t)at, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)(uintptr_t)at, 5);
    h.on = true;
}

void HooksRemove() {
    for (int i = g_hookCount; i-- > 0;) {
        Hook &h = g_hooks[i];
        if (!h.on)
            continue;
        DWORD old;
        VirtualProtect((void *)(uintptr_t)h.at, 5, PAGE_EXECUTE_READWRITE, &old);
        memcpy((void *)(uintptr_t)h.at, h.saved, 5);
        VirtualProtect((void *)(uintptr_t)h.at, 5, old, &old);
        FlushInstructionCache(GetCurrentProcess(), (void *)(uintptr_t)h.at, 5);
        h.on = false;
    }
    g_hookCount = 0;
}

struct WindowCase {
    WorldCollisionInfo info;
    int kind;
};

// Hits inside (and beside) real panes, built in the instance's frame and carried into the world's.
void BuildWindowCases(std::vector<WindowCase> *cases) {
    WCollisionMgr *m = fgCollisionMgr;
    for (uint32_t i = 0; i < m->instanceCount && cases->size() < 240; i++) {
        WCollisionInstance *instance = &m->instances[i];
        const CollisionArticle *article = instance->article;
        if (article == NULL || article->windowGroupCount == 0)
            continue;
        const WindowGroup *group =
            reinterpret_cast<const WindowGroup *>(article->Body() + article->barrierOffset + article->windowOffset);
        for (uint32_t g = 0; g < article->windowGroupCount; g++) {
            const WindowSet *set = Game_FindWindowSet(group, 0, instance->flags >> 2);
            if (set != NULL && set->paneCount != 0) {
                MATRIX4 toLocal, toWorld;
                Game_MakeMatrix(instance, 0, &toLocal, true);
                toWorld = toLocal;
                OrthoInverse(&toWorld);
                for (int n = 0; n < 3; n++) {
                    const WindowPane &pane = set->Panes()[NextRandom() % set->paneCount];
                    float start = pane.start / 8192.0f, end = start + pane.width / 8192.0f;
                    float top = pane.top / 256.0f, bottom = top - pane.height / 256.0f;
                    bool inside = NextRandom() % 4 != 0;
                    Coord4 s, e, h;
                    s.x = Uniform(-8.0f, 8.0f);
                    s.y = Uniform(bottom - 1.0f, top + 1.0f);
                    s.z = Uniform(-8.0f, 8.0f);
                    float dx = Uniform(-10.0f, 10.0f), dz = Uniform(-10.0f, 10.0f);
                    e.x = s.x + dx;
                    e.y = s.y + Uniform(-1.0f, 1.0f);
                    e.z = s.z + dz;
                    float t = inside ? Uniform(start, end) : Uniform(start - 0.5f, end + 0.5f);
                    h.x = s.x + t * dx;
                    h.y = inside ? Uniform(bottom, top) : Uniform(bottom - 1.0f, top + 1.0f);
                    h.z = s.z + t * dz;
                    s.w = e.w = h.w = 1.0f;
                    WindowCase c;
                    memset(&c, 0, sizeof(c));
                    VU0_MATRIX4_vect3mult(&s, &toWorld, &c.info.segmentStart);
                    VU0_MATRIX4_vect3mult(&e, &toWorld, &c.info.segmentEnd);
                    VU0_MATRIX4_vect3mult(&h, &toWorld, &c.info.point);
                    c.info.segmentStart.w = c.info.segmentEnd.w = c.info.point.w = 1.0f;
                    c.info.normal.x = Uniform(-1.0f, 1.0f);
                    c.info.normal.y = Uniform(-1.0f, 1.0f);
                    c.info.normal.z = Uniform(-1.0f, 1.0f);
                    c.info.normal.w = 1.0f;
                    c.info.instance = instance;
                    c.info.groupId = NextRandom() % 8 == 0 ? group->id + 1 : group->id;
                    c.info.hitType = NextRandom() % 16 == 0 ? kHitWorld : kHitBarrier;
                    c.kind = int(NextRandom() % 3);
                    cases->push_back(c);
                    if (NextRandom() % 3 == 0)
                        cases->push_back(c);   // the same pane again: the second hit on a broken pane
                }
            }
            if (group->size >= 0x8000)
                break;
            group = reinterpret_cast<const WindowGroup *>(reinterpret_cast<const uint8_t *>(group) + group->size);
        }
    }
}

struct WindowRun {
    std::vector<bool> answers;
    std::vector<WorldCollisionInfo> infos;
    std::vector<EventRecord> events;
    std::vector<uint32_t> map, mapAfterErase;
    bool ok;
};

void RunWindowCases(const std::vector<WindowCase> &cases, bool original, WindowRun *run) {
    WCollisionMgr *m = fgCollisionMgr;
    WindowMap saved = m->windows;
    uint32_t seed = RandomSeed;
    NewWindowMap(&m->windows);
    g_events.clear();
    run->ok = true;
    for (size_t i = 0; i < cases.size(); i++) {
        WorldCollisionInfo info = cases[i].info;
        bool answer = false;
        bool ok;
        if (original) {
            OriginalsWindow window;
            ok = Guarded([&] { answer = Orig_CheckHitWindow(m, 0, &info, true, cases[i].kind); });
        } else {
            ok = Guarded([&] { answer = m->CheckHitWindow(&info, true, cases[i].kind); });
        }
        run->ok = run->ok && ok;
        run->answers.push_back(answer);
        run->infos.push_back(info);
    }
    run->events = g_events;
    run->map = Serialize(m->windows);
    // erase the second and third instances' entries, through erase(first, last)
    if (m->windows.size >= 3) {
        std::vector<WindowMapNode *> nodes;
        InOrder(m->windows.head->parent, &nodes);
        WindowMapNode *last = nodes.size() > 3 ? nodes[3] : m->windows.head;
        WindowMapNode *next = NULL;
        if (original) {
            OriginalsWindow window;
            Guarded([&] { Orig_WindowMapEraseRange(&m->windows, 0, &next, nodes[1], last); });
        } else {
            Guarded([&] { m->windows.EraseRange(&next, nodes[1], last); });
        }
    }
    run->mapAfterErase = Serialize(m->windows);
    DeleteWindowMap(&m->windows);
    m->windows = saved;
    RandomSeed = seed;
}

bool SameEvents(const std::vector<EventRecord> &a, const std::vector<EventRecord> &b) {
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); i++)
        if (memcmp(&a[i], &b[i], sizeof(EventRecord)) != 0)
            return false;
    return true;
}

void TestCheckHitWindow() {
    std::vector<WindowCase> cases;
    BuildWindowCases(&cases);
    if (cases.empty())
        return;
    HookInstall(0x0005a5a0, (const void *)&FakeEventNew);
    if ((uint32_t)(uintptr_t)&Event::operator new != 0x0005a5a0)
        HookInstall((uint32_t)(uintptr_t)&Event::operator new, (const void *)&FakeEventNew);
    HookInstall(0x000403f0, (const void *)&FakeHitWindow);
    State before;
    Save(&before);
    WindowRun p, o;
    RunWindowCases(cases, false, &p);
    State port;
    Save(&port);
    Load(before);
    RunWindowCases(cases, true, &o);
    State original;
    Save(&original);
    Load(before);
    HooksRemove();

    Check(p.ok == o.ok, "CheckHitWindow faults", g_cases);
    long hits = 0;
    for (size_t i = 0; i < cases.size(); i++) {
        Check(p.answers[i] == o.answers[i], "CheckHitWindow answer", g_cases + long(i));
        Check(memcmp(&p.infos[i], &o.infos[i], sizeof(WorldCollisionInfo)) == 0, "CheckHitWindow info", g_cases + long(i));
        hits += o.answers[i];
    }
    Check(SameEvents(p.events, o.events), "CheckHitWindow events", g_cases);
    Check(p.map == o.map, "CheckHitWindow window map", g_cases);
    Check(p.mapAfterErase == o.mapAfterErase, "window map erase(first, last)", g_cases);
    Check(SameState(port, original), "CheckHitWindow state", g_cases);
    printf("[collistshadow] CheckHitWindow: %u cases, %ld hits, %u events\n", unsigned(cases.size()), hits,
           unsigned(o.events.size()));
    g_cases += long(cases.size());
}

// ---- SetCollisionArticle, into a fresh article map

void SerializeArticles(const ArticleMapNode *node, std::vector<uint32_t> *out) {
    if (node->isNil) {
        out->push_back(0xffffffff);
        return;
    }
    out->push_back(uint32_t(uintptr_t(node->value.instance)));
    out->push_back(uint32_t(uintptr_t(node->value.article)));
    out->push_back(node->color);
    SerializeArticles(node->left, out);
    SerializeArticles(node->right, out);
}

void TestSetCollisionArticle() {
    WCollisionMgr *m = fgCollisionMgr;
    if (fgWorld == NULL)
        return;
    int tested = 0;
    for (uint32_t i = 0; i < m->instanceCount && tested < 60; i += 1 + NextRandom() % 7) {
        CARP::Instance *render = &fgWorld->instances[m->instances[i].renderIndex];
        int count = -1;
        Guarded([&] { count = ArticleOf(render)->model->group->DataCountType(0x63612020); });
        if (count < 0)
            continue;
        tested++;
        const uint32_t ids[5] = {0xffffffffu, 0, uint32_t(count), count > 0 ? uint32_t(count - 1) : 1u,
                                 count > 0 ? NextRandom() % uint32_t(count) : 2u};
        for (int k = 0; k < 5; k++) {
            State before, port, original;
            Save(&before);
            ArticleMap saved = m->articles;
            bool answer[2] = {false, false};
            bool ok[2];
            std::vector<uint32_t> map[2];
            for (int side = 0; side < 2; side++) {
                m->articles.allocator = 0;
                m->articles.head = Game_ArticleMapBuyhead();
                m->articles.head->isNil = 1;
                m->articles.head->parent = m->articles.head;
                m->articles.head->left = m->articles.head;
                m->articles.head->right = m->articles.head;
                m->articles.size = 0;
                if (side == 0) {
                    ok[side] = Guarded([&] { answer[side] = m->SetCollisionArticle(render, ids[k]); });
                } else {
                    OriginalsWindow window;
                    ok[side] = Guarded([&] { answer[side] = Orig_SetCollisionArticle(m, 0, render, ids[k]); });
                }
                map[side].push_back(m->articles.size);
                SerializeArticles(m->articles.head->parent, &map[side]);
                ArticleMapNode *next;
                Game_ArticleMapEraseRange(&m->articles, 0, &next, m->articles.head->left, m->articles.head);
                UMemory::FastFree(m->articles.head, sizeof(ArticleMapNode));
                m->articles = saved;
                Save(side == 0 ? &port : &original);
                Load(before);
            }
            Check(ok[0] == ok[1] && answer[0] == answer[1], "SetCollisionArticle answer", g_cases);
            Check(map[0] == map[1], "SetCollisionArticle article map", g_cases);
            Check(SameState(port, original), "SetCollisionArticle instances", g_cases);
            g_cases++;
        }
    }
}

}  // namespace

void ColListShadow_Run(void) {
    const char *env = getenv("NIGHTFIRE_COLLISTSHADOW");
    if (env == NULL || atoi(env) == 0)
        return;
    static bool ran;
    if (ran)
        return;
    ran = true;
    if (fgCollisionMgr == NULL || !FindBounds()) {
        printf("[collistshadow] no collision manager or no instances yet: nothing tested\n");
        fflush(stdout);
        return;
    }

    for (int i = 0; i < 300; i++) {
        Coord4 segment[2];
        RandomSegment(segment);
        Coord4 point4 = {segment[0].x, segment[0].y, segment[0].z, 1.0f};
        const Coord3 *point = reinterpret_cast<const Coord3 *>(&point4);
        float radius = (i % 5 == 0) ? Uniform(0.0f, 2.0f) : Uniform(1.0f, 40.0f);
        TestInstanceListSegment(segment);
        TestInstanceListPoint(point, radius, false, false);
        TestInstanceListPoint(point, radius, true, (i & 1) != 0);
        TestObjectLists(segment, point, radius);
        TestBarrierList(point, radius);
        TestCheckHitWorld(segment);
        if (i % 6 == 0)
            TestStepCheckHitWorld(segment);
    }
    TestCheckHitWindow();
    TestWindowMap();
    TestSetCollisionArticle();

    printf("[collistshadow] WCollisionMgr lists, hit checks, window map and SetCollisionArticle: %ld cases, %ld "
           "checks, %ld differ (%ld calls faulted)\n", g_cases, g_checks, g_differ, g_faults);
    fflush(stdout);
}
