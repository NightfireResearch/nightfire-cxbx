#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "TargetShadow.h"

#include "../engine/UMemory.hpp"
#include "../world/SoundGroup.h"
#include "../world/Targeting.h"
#include "../../common/xbeOriginal.h"

#include <windows.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------------------------------------------
// NIGHTFIRE_TARGETSHADOW=1, once on the first simulation tick: world/Targeting.cpp and world/SoundGroup.cpp's
// containers against the originals.
//
// Two passes run the same tests, calling everything at the original addresses: the first with the ported
// functions' originals swapped back in (0x000cc8f0-0x000cedd0), the second with our jumps. Each pass writes a log (results as
// bits, whole records as bytes, trees and lists by content); the logs must match line for line. The two target
// counters the constructors bump are saved before the first pass and put back after each.
//
//   - IsPointOnScreen on screen points over and around the screen, and on the box's edges;
//   - GetScreenPos and GetOffScreenPos on world points round the live camera, ahead, behind, near its plane;
//   - DistFromScreenPos, DistFromScreenCenter, DistFromCamera, and the three comparisons, on targets with random
//     screen positions, positions and on-screen flags;
//   - WTargetable's point constructor (bytes over a filled buffer), UpdatePosition, UpdateVisibility, GetVelocity,
//     the references, the destructor; the physics-object constructor and GetVelocity on the player's car;
//   - on a copy of the live picker: ActivateTargeting, CycleToNextTarget, GetSelectedTarget, DeactivateTargeting
//     over targets of our own, GetWorldTarget;
//   - a WSoundMap through scripted random inserts, finds, erases and range erases, its whole tree logged after
//     every step, then destroyed; a std::list of pointers through erase(first, last) and _Incsize.
// The sound groups themselves (they make and delete real sounds) are tested in game.
// ---------------------------------------------------------------------------------------------------------------

namespace {

int g_cases, g_faults;
bool g_counting;
std::string *g_log;

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

void Case() {
    if (g_counting)
        g_cases++;
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

struct Rng {
    uint32_t state;
    uint32_t Next() {
        state = state * 1664525u + 1013904223u;
        return state >> 8;
    }
    float Uniform(float lo, float hi) { return lo + (hi - lo) * float(Next() & 0xffff) / 65535.0f; }
    int Range(int lo, int hi) { return lo + int(Next() % uint32_t(hi - lo + 1)); }
};

// ---- what the tests read of the game

struct CameraFrame {
    uint8_t unknown00[0x10];
    MATRIX4 matrix;             // +0x10
};

struct CameraViewEntry {
    void *view;
    CameraFrame *camera;
};

struct RendererScreen {
    uint8_t unknown00[0x40];
    int32_t width;              // +0x40
    int32_t height;             // +0x44
};

#define ShadowCameraViews (*(CameraViewEntry **)0x001ec488)
#define ShadowRenderer (*(RendererScreen **)0x001ebff4)
#define ShadowPlayerPhysics (*(PhysicsObject ***)0x00234e40)
#define ShadowTargetableCount (*(int32_t *)0x0023e174)
#define ShadowTargetableStagger (*(int32_t *)0x0023e178)
#define ShadowPicker ((WTargetPicker *)0x0023e1b0)

// ---- the functions, at their original addresses

typedef bool (__fastcall *IsPointOnScreenFn)(WTargetPicker *, int, const ScreenPos *);
typedef ScreenPos *(__fastcall *ScreenPosFn)(WTargetPicker *, int, ScreenPos *, const Coord3 *);
typedef double (__fastcall *DistFromScreenPosFn)(WTargetable *, int, const ScreenPos *);
typedef double (__fastcall *DistFromScreenCenterFn)(WTargetable *, int);
typedef float (__fastcall *DistFromCameraFn)(WTargetable *, int, int);
typedef int (*CompareFn)(const void *, const void *);
typedef WTargetable *(__fastcall *ConstructPointFn)(WTargetable *, int, float, float, float);
typedef WTargetable *(__fastcall *ConstructPhysicsFn)(WTargetable *, int, PhysicsObject *);
typedef void (__fastcall *TargetMethodFn)(WTargetable *, int);
typedef bool (__fastcall *GetVelocityFn)(WTargetable *, int, Coord3 *);
typedef void (__fastcall *ActivateFn)(WTargetPicker *, int, int);
typedef void (__fastcall *PickerMethodFn)(WTargetPicker *, int);
typedef WTargetable *(__fastcall *PickerTargetFn)(WTargetPicker *, int);
typedef SoundMapNode *(*MapBuyHeadFn)();
typedef SoundMapInsert *(__fastcall *InsertUniqueFn)(WSoundMap *, int, SoundMapInsert *, const SoundMapValue *);
typedef SoundMapNode **(__fastcall *FindFn)(WSoundMap *, int, SoundMapNode **, const int32_t *);
typedef SoundMapNode **(__fastcall *EraseAtFn)(WSoundMap *, int, SoundMapNode **, SoundMapNode *);
typedef SoundMapNode **(__fastcall *EraseRangeFn)(WSoundMap *, int, SoundMapNode **, SoundMapNode *, SoundMapNode *);
typedef void (__fastcall *MapMethodFn)(WSoundMap *, int);
typedef PointerListNode *(__fastcall *ListBuyHeadFn)(PointerList *, int);
typedef PointerListNode *(__fastcall *ListBuyNodeFn)(PointerList *, int, PointerListNode *, PointerListNode *, void *const *);
typedef PointerListNode **(__fastcall *ListEraseFn)(PointerList *, int, PointerListNode **, PointerListNode *, PointerListNode *);
typedef void (__fastcall *ListIncreaseSizeFn)(PointerList *, int, uint32_t);

const IsPointOnScreenFn IsPointOnScreen = (IsPointOnScreenFn)0x000cdee0;
const ScreenPosFn GetScreenPos = (ScreenPosFn)0x000ce2b0;
const ScreenPosFn GetOffScreenPos = (ScreenPosFn)0x000cdc10;
const DistFromScreenPosFn DistFromScreenPos = (DistFromScreenPosFn)0x000cd890;
const DistFromScreenCenterFn DistFromScreenCenter = (DistFromScreenCenterFn)0x000cd8d0;
const DistFromCameraFn DistFromCamera = (DistFromCameraFn)0x000cd840;
const CompareFn kCompares[3] = {(CompareFn)0x000ce050, (CompareFn)0x000ce4f0, (CompareFn)0x000ce0a0};
const ConstructPointFn ConstructPoint = (ConstructPointFn)0x000cd930;
const ConstructPhysicsFn ConstructPhysics = (ConstructPhysicsFn)0x000cdba0;
const TargetMethodFn UpdatePosition = (TargetMethodFn)0x000cda90;
const TargetMethodFn UpdateVisibility = (TargetMethodFn)0x000cd670;
const TargetMethodFn AddReference = (TargetMethodFn)0x000cd920;
const TargetMethodFn RemoveReference = (TargetMethodFn)0x000cdb80;
const TargetMethodFn DestructTarget = (TargetMethodFn)0x000cd660;
const GetVelocityFn GetVelocity = (GetVelocityFn)0x000cd740;
const ActivateFn ActivateTargeting = (ActivateFn)0x000ce0f0;
const PickerMethodFn DeactivateTargeting = (PickerMethodFn)0x000ce150;
const PickerMethodFn CycleToNextTarget = (PickerMethodFn)0x000ce170;
const PickerTargetFn GetSelectedTarget = (PickerTargetFn)0x000cdfb0;
const PickerTargetFn GetWorldTarget = (PickerTargetFn)0x000cdfd0;
const MapBuyHeadFn MapBuyHead = (MapBuyHeadFn)0x00094030;
const InsertUniqueFn MapInsertUnique = (InsertUniqueFn)0x000cd2b0;
const FindFn MapFind = (FindFn)0x000ccbb0;
const EraseAtFn MapEraseAt = (EraseAtFn)0x000ccc20;
const EraseRangeFn MapEraseRange = (EraseRangeFn)0x000cd0d0;
const MapMethodFn MapDestruct = (MapMethodFn)0x000cd560;
const ListBuyHeadFn ListBuyHead = (ListBuyHeadFn)0x000b8490;
const ListBuyNodeFn ListBuyNode = (ListBuyNodeFn)0x000130e0;
const ListEraseFn ListErase = (ListEraseFn)0x000ce900;
const ListIncreaseSizeFn ListIncreaseSize = (ListIncreaseSizeFn)0x000ceca0;

// Records whose addresses must be the same in both passes (the picker's copy points at the targets).
alignas(16) uint8_t g_pickerBytes[sizeof(WTargetPicker)];
alignas(16) uint8_t g_targetBytes[4][sizeof(WTargetable)];

WTargetPicker *Picker() { return reinterpret_cast<WTargetPicker *>(g_pickerBytes); }
WTargetable *Target(int i) { return reinterpret_cast<WTargetable *>(g_targetBytes[i]); }

void LogScreen(const char *what, const ScreenPos *p) {
    Logf("%s %08x %08x", what, Bits(p->x), Bits(p->y));
}

// ---- the tests

void TestScreen(Rng *rng) {
    float width = float(ShadowRenderer->width);
    float height = float(ShadowRenderer->height);
    for (int i = 0; i < 3000; i++) {
        Case();
        ScreenPos p;
        if (i < 64) {
            // on and next to the box's edges
            float hx = (0.66f - 0.2f) * width + ((i & 4) ? 107.0f : 0.0f);
            float hy = (0.5f - 0.1f) * height;
            float nudge = (i & 8) ? 0.001f : ((i & 16) ? -0.001f : 0.0f);
            p.x = width * 0.5f + ((i & 1) ? hx : -hx) + nudge;
            p.y = height * 0.5f + ((i & 2) ? hy : -hy) + nudge;
            if (i & 32)
                p.x = width * 0.5f;
        } else {
            p.x = rng->Uniform(-0.5f * width, 1.5f * width);
            p.y = rng->Uniform(-0.5f * height, 1.5f * height);
        }
        Logf("on %d", IsPointOnScreen(ShadowPicker, 0, &p) ? 1 : 0);
    }
}

void TestWorldPoints(Rng *rng) {
    const MATRIX4 &m = ShadowCameraViews[0].camera->matrix;
    for (int i = 0; i < 3000; i++) {
        Case();
        float a = rng->Uniform(-300.0f, 300.0f);
        float b = rng->Uniform(-300.0f, 300.0f);
        float c = rng->Uniform(-300.0f, 300.0f);
        if (i % 10 == 1)
            c = rng->Uniform(-0.05f, 0.05f);   // near the camera's plane, along each axis in turn
        if (i % 10 == 2)
            a = rng->Uniform(-0.05f, 0.05f);
        if (i % 10 == 3)
            b = rng->Uniform(-0.05f, 0.05f);
        Coord3 p;
        p.x = m.mtx[3][0] + a * m.mtx[0][0] + b * m.mtx[1][0] + c * m.mtx[2][0];
        p.y = m.mtx[3][1] + a * m.mtx[0][1] + b * m.mtx[1][1] + c * m.mtx[2][1];
        p.z = m.mtx[3][2] + a * m.mtx[0][2] + b * m.mtx[1][2] + c * m.mtx[2][2];
        ScreenPos out = {-1.0f, -1.0f};
        ScreenPos *r = GetScreenPos(ShadowPicker, 0, &out, &p);
        Logf("screen %d", r == &out);
        LogScreen("  at", &out);
        out.x = out.y = -1.0f;
        r = GetOffScreenPos(ShadowPicker, 0, &out, &p);
        Logf("off %d", r == &out);
        LogScreen("  at", &out);
    }
}

void TestDistances(Rng *rng) {
    const MATRIX4 &m = ShadowCameraViews[0].camera->matrix;
    float width = float(ShadowRenderer->width);
    float height = float(ShadowRenderer->height);
    for (int i = 0; i < 1000; i++) {
        Case();
        for (int k = 0; k < 2; k++) {
            WTargetable *t = Target(k);
            memset(t, 0, sizeof(WTargetable));
            t->screenPos.x = rng->Uniform(-width, 2.0f * width);
            t->screenPos.y = rng->Uniform(-height, 2.0f * height);
            t->position.x = m.mtx[3][0] + rng->Uniform(-500.0f, 500.0f);
            t->position.y = m.mtx[3][1] + rng->Uniform(-100.0f, 100.0f);
            t->position.z = m.mtx[3][2] + rng->Uniform(-500.0f, 500.0f);
            t->onScreen = uint8_t(rng->Next() & 1);
        }
        if (i % 50 == 0)
            *Target(1) = *Target(0);   // equal distances
        ScreenPos p = {rng->Uniform(-width, 2.0f * width), rng->Uniform(-height, 2.0f * height)};
        Logf("dist %016llx %016llx %08x", Bits(DistFromScreenPos(Target(0), 0, &p)),
             Bits(DistFromScreenCenter(Target(0), 0)), Bits(DistFromCamera(Target(0), 0, 0)));
        WTargetable *pair[2] = {Target(0), Target(1)};
        Logf("cmp %d %d %d", kCompares[0](&pair[0], &pair[1]), kCompares[1](&pair[0], &pair[1]),
             kCompares[2](&pair[0], &pair[1]));
    }
}

void TestTargetables(Rng *rng) {
    const MATRIX4 &m = ShadowCameraViews[0].camera->matrix;
    for (int i = 0; i < 200; i++) {
        Case();
        WTargetable *t = Target(2);
        memset(t, 0xcd, sizeof(WTargetable));
        float x = m.mtx[3][0] + rng->Uniform(-300.0f, 300.0f);
        float y = m.mtx[3][1] + rng->Uniform(-30.0f, 30.0f);
        float z = m.mtx[3][2] + rng->Uniform(-300.0f, 300.0f);
        Logf("made %d", ConstructPoint(t, 0, x, y, z) == t);
        LogBytes("  point", t, sizeof(WTargetable));
        Logf("  counters %d %d", ShadowTargetableCount, ShadowTargetableStagger);
        for (int k = 0; k < 7; k++)
            UpdatePosition(t, 0);
        LogBytes("  updated", t, sizeof(WTargetable));
        UpdateVisibility(t, 0);
        Logf("  visible %d", t->visible);
        Coord3 v = {-1.0f, -1.0f, -1.0f};
        Logf("  velocity %d %08x %08x %08x", GetVelocity(t, 0, &v) ? 1 : 0, Bits(v.x), Bits(v.y), Bits(v.z));
        AddReference(t, 0);
        RemoveReference(t, 0);
        Logf("  refs %d", t->refCount);
        DestructTarget(t, 0);
        Logf("  count %d", ShadowTargetableCount);
    }

    PhysicsObject *car = ShadowPlayerPhysics != NULL ? *ShadowPlayerPhysics : NULL;
    if (car != NULL) {
        Case();
        WTargetable *t = Target(3);
        memset(t, 0xcd, sizeof(WTargetable));
        Logf("car %d", ConstructPhysics(t, 0, car) == t);
        LogBytes("  car", t, sizeof(WTargetable));
        Coord3 v = {-1.0f, -1.0f, -1.0f};
        Logf("  velocity %d %08x %08x %08x", GetVelocity(t, 0, &v) ? 1 : 0, Bits(v.x), Bits(v.y), Bits(v.z));
        UpdatePosition(t, 0);
        LogBytes("  updated", t, sizeof(WTargetable));
        DestructTarget(t, 0);
    }
}

void TestPicker(Rng *rng) {
    WTargetPicker *picker = Picker();
    for (int round = 0; round < 40; round++) {
        Case();
        memcpy(picker, ShadowPicker, sizeof(WTargetPicker));
        int count = rng->Range(0, 3);
        for (int k = 0; k < 3; k++) {
            memset(Target(k), 0, sizeof(WTargetable));
            Target(k)->onScreen = uint8_t(rng->Next() & 1);
            picker->sorted[k] = Target(k);
        }
        picker->sortedCount = count;
        picker->currentIndex = rng->Range(0, 3);
        picker->active = uint8_t(rng->Next() & 1);
        picker->mode = rng->Range(0, 1);
        ActivateTargeting(picker, 0, rng->Range(0, 1));
        LogBytes("activated", picker, sizeof(WTargetPicker));
        for (int k = 0; k < 5; k++) {
            CycleToNextTarget(picker, 0);
            WTargetable *selected = GetSelectedTarget(picker, 0);
            Logf("  cycle %d %d %d", picker->currentIndex, int(picker->selected - Target(0)),
                 selected == NULL ? -1 : int(selected - Target(0)));
        }
        DeactivateTargeting(picker, 0);
        LogBytes("  deactivated", picker, sizeof(WTargetPicker));
    }

    Case();
    int32_t count = ShadowTargetableCount;
    WTargetable *made = GetWorldTarget(picker, 0);
    if (made != NULL) {
        LogBytes("world target", made, sizeof(WTargetable));
        UMemory::FastFree(made, sizeof(WTargetable));
    }
    ShadowTargetableCount = count;
}

void LogTree(std::string *out, const WSoundMap *map, const SoundMapNode *node, int depth) {
    if (node->isNil)
        return;
    LogTree(out, map, node->left, depth + 1);
    char text[64];
    snprintf(text, sizeof(text), " %d:%x:%d:%d", node->value.id, unsigned(uintptr_t(node->value.sound)), node->color,
             depth);
    out->append(text);
    LogTree(out, map, node->right, depth + 1);
}

void LogMap(const char *what, const WSoundMap *map) {
    std::string s = what;
    char text[64];
    snprintf(text, sizeof(text), " size %u head %d:", map->size, map->head->color);
    s += text;
    LogTree(&s, map, map->head->parent, 0);
    s += map->head->left == map->head ? " L=end" : " L";
    if (map->head->left != map->head) {
        snprintf(text, sizeof(text), "%d", map->head->left->value.id);
        s += text;
    }
    if (map->head->right != map->head) {
        snprintf(text, sizeof(text), " R%d", map->head->right->value.id);
        s += text;
    }
    Logf("%s", s.c_str());
}

int Key(const SoundMapNode *node, const WSoundMap *map) {
    return node == map->head ? 9999 : node->value.id;
}

void TestSoundMap(Rng *rng) {
    WSoundMap map;
    map.allocator = 0;
    map.head = MapBuyHead();
    map.head->isNil = 1;
    map.head->parent = map.head;
    map.head->left = map.head;
    map.head->right = map.head;
    map.size = 0;
    for (int step = 0; step < 600; step++) {
        Case();
        int op = rng->Range(0, 9);
        int32_t key = rng->Range(-20, 40);
        if (op < 5) {
            SoundMapValue value = {key, reinterpret_cast<WSound *>(uintptr_t(key * 16 + step))};
            SoundMapInsert inserted = {NULL, false};
            MapInsertUnique(&map, 0, &inserted, &value);
            Logf("insert %d: %d %d", key, Key(inserted.node, &map), inserted.inserted ? 1 : 0);
        } else if (op < 7) {
            SoundMapNode *found = NULL;
            MapFind(&map, 0, &found, &key);
            Logf("find %d: %d", key, Key(found, &map));
        } else if (op < 9) {
            SoundMapNode *found = NULL;
            MapFind(&map, 0, &found, &key);
            if (found != map.head) {
                SoundMapNode *next = NULL;
                MapEraseAt(&map, 0, &next, found);
                Logf("erase %d: %d", key, Key(next, &map));
            }
        } else {
            // a range from the first key not below `key` to a few further on, or everything
            SoundMapNode *first = NULL;
            MapFind(&map, 0, &first, &key);
            SoundMapNode *last = first;
            for (int n = rng->Range(0, 3); n > 0 && last != map.head; n--)
                last = reinterpret_cast<SoundMapNode *>(TreeNext(reinterpret_cast<TreeNode *>(last)));
            if (step % 7 == 0) {
                first = map.head->left;
                last = map.head;
            }
            SoundMapNode *next = NULL;
            MapEraseRange(&map, 0, &next, first, last);
            Logf("erase range: %d", Key(next, &map));
        }
        LogMap("  map", &map);
    }
    MapDestruct(&map, 0);
    Logf("destroyed %p %u", static_cast<void *>(map.head), map.size);
}

void TestList(Rng *rng) {
    for (int round = 0; round < 50; round++) {
        Case();
        PointerList list;
        list.allocator = 0;
        list.head = ListBuyHead(&list, 0);
        list.size = 0;
        int count = rng->Range(0, 8);
        for (int i = 0; i < count; i++) {
            void *value = reinterpret_cast<void *>(uintptr_t(i + 1));
            PointerListNode *node = ListBuyNode(&list, 0, list.head, list.head->prev, &value);
            ListIncreaseSize(&list, 0, 1);
            list.head->prev = node;
            node->prev->next = node;
        }
        int from = rng->Range(0, count), length = rng->Range(0, count - from);
        PointerListNode *first = list.head->next;
        for (int i = 0; i < from; i++)
            first = first->next;
        PointerListNode *last = first;
        for (int i = 0; i < length; i++)
            last = last->next;
        PointerListNode *after = NULL;
        ListErase(&list, 0, &after, first, last);
        std::string s;
        for (PointerListNode *node = list.head->next; node != list.head; node = node->next) {
            char text[16];
            snprintf(text, sizeof(text), " %u", unsigned(uintptr_t(node->value)));
            s += text;
        }
        Logf("list %d %d: size %u after %d:%s", from, length, list.size, after == last, s.c_str());
        PointerListNode *ignored;
        ListErase(&list, 0, &ignored, list.head->next, list.head);
        Logf("  emptied %u", list.size);
        UMemory::FastFree(list.head, sizeof(PointerListNode));
    }
}

bool Safe(void (*test)(Rng *), Rng *rng) {
#ifdef _MSC_VER
    __try {
        test(rng);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_faults++;
        return false;
    }
#else
    test(rng);
    return true;
#endif
}

void RunPass(std::string *log, bool original) {
    g_log = log;
    if (original)
        XbeOriginal_RestoreRange(0x000cc8f0, 0x000cedd0, true);
    void (*const tests[])(Rng *) = {TestScreen, TestWorldPoints, TestDistances, TestTargetables, TestPicker,
                                    TestSoundMap, TestList};
    uint32_t seed = 0x7a26e7u;
    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        Rng rng = {seed + uint32_t(i) * 7919u};
        if (!Safe(tests[i], &rng))
            Logf("fault in test %u", unsigned(i));
    }
    if (original)
        XbeOriginal_RestoreRange(0x000cc8f0, 0x000cedd0, false);
    g_log = NULL;
}

void SplitLines(const std::string &text, std::vector<std::string> *lines) {
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos)
            end = text.size();
        lines->push_back(text.substr(start, end - start));
        start = end + 1;
    }
}

}  // namespace

void TargetShadow_Run(void) {
    const char *setting = getenv("NIGHTFIRE_TARGETSHADOW");
    if (setting == NULL || atoi(setting) == 0)
        return;
    if (ShadowCameraViews == NULL || ShadowCameraViews[0].view == NULL || ShadowCameraViews[0].camera == NULL ||
        ShadowRenderer == NULL) {
        printf("[target] no camera view yet - skipped\n");
        fflush(stdout);
        return;
    }

    int32_t count = ShadowTargetableCount, stagger = ShadowTargetableStagger;
    std::string original, ours;
    g_counting = true;
    RunPass(&original, true);
    ShadowTargetableCount = count;
    ShadowTargetableStagger = stagger;
    g_counting = false;
    RunPass(&ours, false);
    ShadowTargetableCount = count;
    ShadowTargetableStagger = stagger;

    std::vector<std::string> a, b;
    SplitLines(original, &a);
    SplitLines(ours, &b);
    size_t lines = a.size() > b.size() ? a.size() : b.size();
    int differ = 0;
    for (size_t i = 0; i < lines; i++) {
        const std::string *x = i < a.size() ? &a[i] : NULL;
        const std::string *y = i < b.size() ? &b[i] : NULL;
        if (x != NULL && y != NULL && *x == *y)
            continue;
        if (differ < 10)
            printf("[target]   line %u: original \"%.200s\" ours \"%.200s\"\n", unsigned(i),
                   x != NULL ? x->c_str() : "(none)", y != NULL ? y->c_str() : "(none)");
        differ++;
    }
    printf("[target] targeting maths, targets, picker steps, sound map, list: %d cases, %u checks, %d differ "
           "(%d faults)\n", g_cases, unsigned(lines), differ, g_faults);
    fflush(stdout);
}
