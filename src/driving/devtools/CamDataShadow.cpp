#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "CamDataShadow.h"

#include "../camera/CameraIniLoader.h"
#include "../camera/CameraSpline.h"
#include "../camera/DirectorQueue.h"
#include "../camera/PlayerCamera.h"
#include "../camera/PlayerCamState.h"
#include "../data/Dafi.h"
#include "../data/IniFiles.h"
#include "../eagl/RenderMethod.h"
#include "../engine/UFileLoader.h"
#include "../engine/UMemory.hpp"
#include "../physics/RigidBody.h"
#include "../../common/xbeOriginal.h"
#include "../../common/xbeOverload.h"
#include "../../helpers.h"

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
// NIGHTFIRE_CAMDATASHADOW=1, once on the first simulation tick: camera/CameraIniLoader.cpp, CameraSpline.cpp,
// DirectorQueue.cpp and PlayerCamState.cpp against the originals.
//
// Every case runs twice from the same state, calling the functions at their addresses: first with the originals of
// the address range swapped back in (XbeOriginal_RestoreRange), then with our jumps; outputs and every byte written
// are compared.
//   - RCameraMath: each function on random vectors (unit, zero, large, NaN) and angles.
//   - RCameraSpline: sequences of operations on two splines, one driven by the originals and one by the ports (the
//     constructor, point list additions, BuildSpline onto list points and off them, with zero directions and no
//     rotations, BuildSplineEx, both EvaluateSplines, EvaluateTangent, FindPointInList, the list's erase, buy and
//     size members, ClearSplinePtList, the destructor), splines and their lists compared after each.
//   - The linker's strays: GameSymbolTable's destructors, UDataRecord::MatchTag, NoSymbolCallback.
//   - RCameraIniLoader: ClampCameraValue, FUN_00078590 (through a thunk that sets EDI), CheckCameraAgainstCar and
//     ResolveWeaponNames on random lists, and LoadFile on the disc's camera.ini (the mission archive reopened) for
//     the player's car as it is and as other cars of the file - a stand-in vtable on the player's car answers the
//     type, class and secondary type - comparing every global and table it fills.
//   - RDirectorQueue: sequences of appends, pops, erases, ProcessDirectorLogic and restarts on two queues over
//     stand-in cameras, the RPlayerCamera and RWorldCamera methods they call replaced by recording fakes.
//   - RPlayerCamState: the handlers on random states and inputs, the simulation's state, the mission manager's
//     fields and the track name perturbed, the camera's and the HUD's methods replaced by recording fakes.
//   - The renderer's strays: the reverse draw list's vector members, the TextureDOF state's constructor and
//     destructor.
//
// Masked: the heli and auto-drive arms' fields the file never gives (stack garbage in the original, zero in the
// port), and the heap's padding words in the tables.
//
// A mutation it catches: BuildSpline keeping points[0] where it takes the old points[1] (a segment past the path's
// first) leaves points[0] different at the first BuildSpline onto a list point after the first; ClampCameraValue
// answering min above max differs on the clamp cases and in every auto-drive arm's limits.
// ---------------------------------------------------------------------------------------------------------------

namespace {

int g_cases, g_checks, g_diffs, g_faults, g_details;

const unsigned kIniRange[2] = { 0x00078550, 0x0007a1e0 };
const unsigned kSplineRange[2] = { 0x0007a1e0, 0x0007af00 };
const unsigned kQueueRange[2] = { 0x0007c160, 0x0007c9e0 };
const unsigned kStateRange[2] = { 0x00089b70, 0x0008a3f0 };

struct Originals {
    bool on;
    unsigned lo, hi;
    Originals(bool original, const unsigned *range) : on(original), lo(range[0]), hi(range[1]) {
        if (on)
            XbeOriginal_RestoreRange(lo, hi, true);
    }
    ~Originals() {
        if (on)
            XbeOriginal_RestoreRange(lo, hi, false);
    }
};

template <class F>
bool Safe(F *op) {
#ifdef _MSC_VER
    __try {
        (*op)();
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
#else
    (*op)();
    return true;
#endif
}

template <class F>
bool RunSide(bool original, const unsigned *range, F *op) {
    Originals scope(original, range);
    return Safe(op);
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
    bool Chance(uint32_t percent) { return Below(100) < percent; }
};

void Detail(const char *format, ...) {
    if (g_details++ >= 10)
        return;
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    fflush(stdout);
}

void Fault(const char *what, int index) {
    g_faults++;
    g_diffs++;
    Detail("[camdata] %s #%d faulted\n", what, index);
}

// n bytes, skipping those with mask[i] set
bool Compare(const char *what, int index, const void *a, const void *b, size_t n, const uint8_t *mask = NULL) {
    g_checks++;
    const uint8_t *x = static_cast<const uint8_t *>(a);
    const uint8_t *y = static_cast<const uint8_t *>(b);
    for (size_t i = 0; i < n; i++) {
        if ((mask == NULL || mask[i] == 0) && x[i] != y[i]) {
            g_diffs++;
            Detail("[camdata] %s #%d differs at +0x%x: original %02x, port %02x\n", what, index, unsigned(i), x[i],
                   y[i]);
            return false;
        }
    }
    return true;
}

template <class T>
bool CompareValue(const char *what, int index, const T &a, const T &b) {
    return Compare(what, index, &a, &b, sizeof(T));
}

template <class F>
F At(uint32_t address) {
    return XbeOriginal<F>(address);
}

float RandomFloat(Rng &rng) {
    switch (rng.Below(12)) {
    case 0: return 0.0f;
    case 1: return -0.0f;
    case 2: return 1.0f;
    case 3: return -1.0f;
    case 4: return rng.Range(-1000.0f, 1000.0f);
    case 5: return rng.Chance(10) ? NAN : 0.5f;
    default: return rng.Range(-2.0f, 2.0f);
    }
}

Coord4 RandomVector(Rng &rng) {
    Coord4 v = { rng.Range(-50.0f, 50.0f), rng.Range(-50.0f, 50.0f), rng.Range(-50.0f, 50.0f), RandomFloat(rng) };
    switch (rng.Below(10)) {
    case 0:
        v.x = v.y = v.z = 0.0f;
        break;
    case 1: {
        float length = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
        if (length > 0.0f) {
            v.x /= length;
            v.y /= length;
            v.z /= length;
        }
        break;
    }
    case 2:
        v.y = 0.0f;
        break;
    case 3:
        v.x = RandomFloat(rng);
        break;
    }
    return v;
}

void Fill(Rng &rng, void *out, size_t n) {
    uint8_t *p = static_cast<uint8_t *>(out);
    for (size_t i = 0; i < n; i++)
        p[i] = uint8_t(rng.Next());
}

// ---- RCameraMath

void MathCases(Rng &rng) {
    typedef void (*QuatFn)(Coord4 *, float, const Coord4 *);
    typedef void (*Mat4Fn)(MATRIX4 *, float, const Coord4 *);
    typedef void (*UnitFn)(const Coord4 *, MATRIX4 *);
    typedef void (*UnitAtFn)(const Coord4 *, const Coord4 *, MATRIX4 *);
    typedef double (*ACosFn)(float);
    typedef double (*AngleFn)(const Coord4 *, const Coord4 *);
    typedef void (*GenQuatFn)(const Coord4 *, Coord4 *);
    static const char *const kNames[] = { "BuildRotationQuat", "BuildRotationMat4", "VU0_GenerateMatrix4Unit",
                                          "VU0_GenerateMatrix4UnitAt", "ACosTurns", "MatrixFromDirection",
                                          "MatrixFromDirectionAt", "AngleTurns", "VU0_GenerateQuat" };
    for (int i = 0; i < 4500; i++) {
        int which = i % 9;
        Coord4 v[3] = { RandomVector(rng), RandomVector(rng), RandomVector(rng) };
        float turns = rng.Chance(80) ? rng.Range(-2.0f, 2.0f) : RandomFloat(rng);
        float cosine = rng.Chance(70) ? rng.Range(-1.2f, 1.2f) : RandomFloat(rng);
        bool ownRow = rng.Chance(70);
        uint32_t seed = rng.Next();
        struct Out {
            MATRIX4 matrix;
            Coord4 vector;
            double result;
        } out[2];
        for (int side = 0; side < 2; side++) {
            Rng fill = { seed };
            Fill(fill, &out[side], sizeof(Out));
            Out *o = &out[side];
            auto op = [&]() {
                Coord4 *row2 = reinterpret_cast<Coord4 *>(o->matrix.mtx[2]);
                switch (which) {
                case 0: At<QuatFn>(0x0007a1e0)(&o->vector, turns, &v[0]); break;
                case 1: At<Mat4Fn>(0x0007a280)(&o->matrix, turns, &v[0]); break;
                case 2:
                    if (ownRow)
                        *row2 = v[0];
                    At<UnitFn>(0x0007a2b0)(ownRow ? row2 : &v[0], &o->matrix);
                    break;
                case 3:
                    if (ownRow)
                        *row2 = v[0];
                    At<UnitAtFn>(0x0007a320)(ownRow ? row2 : &v[0], &v[1], &o->matrix);
                    break;
                case 4: o->result = At<ACosFn>(0x0007a3b0)(cosine); break;
                case 5: At<UnitFn>(0x0007a410)(&v[0], &o->matrix); break;
                case 6: At<UnitAtFn>(0x0007a440)(&v[0], &v[1], &o->matrix); break;
                case 7: o->result = At<AngleFn>(0x0007a470)(&v[0], &v[1]); break;
                case 8: At<GenQuatFn>(0x0007a4e0)(&v[0], &o->vector); break;
                }
            };
            if (!RunSide(side == 0, kSplineRange, &op)) {
                Fault(kNames[which], i);
                break;
            }
        }
        g_cases++;
        Compare(kNames[which], i, &out[0], &out[1], sizeof(Out));
    }
}

// ---- RCameraSpline

typedef RCameraSpline Spline;
typedef Spline::PointNode PointNode;
typedef Spline *(Spline::*SplineConstructFn)();
typedef void (Spline::*SplineVoidFn)();
typedef void (Spline::*EvalPointFn)(float, Coord4 *);
typedef void (Spline::*EvalFrameFn)(float, MATRIX4 *, bool);
typedef uint32_t (Spline::*SplineSizeFn)();
typedef void (Spline::*BuildExFn)(const Coord3 *, const Coord3 *, const Coord3 *, const Coord3 *);
typedef void (Spline::*BuildFn)(const Coord4 *, Coord4 *, const Coord4 *, Coord4 *, const Coord4 *, const Coord4 *,
                                float);
typedef void (Spline::*AddFn)(const Coord4 *);
typedef PointNode **(*FindFn)(PointNode **, Spline::PointList *, const Coord4 *);
typedef PointNode *(Spline::PointList::*BuyNodeFn)(PointNode *, PointNode *, const Coord4 *);
typedef PointNode *(Spline::PointList::*BuyHeadFn)();
typedef PointNode **(Spline::PointList::*EraseFn)(PointNode **, PointNode *, PointNode *);
typedef void (Spline::PointList::*IncreaseFn)(uint32_t);

PointNode *PointAt(Spline *spline, int index) {
    PointNode *node = spline->pointList.Begin();
    for (int i = 0; i < index && node != NULL; i++)
        node = node->next;
    return node;
}

int PointIndex(Spline *spline, PointNode *node) {
    if (node == spline->pointList.head)
        return -1;
    PointNode *walk = spline->pointList.Begin();
    for (int i = 0; walk != NULL && walk != spline->pointList.head; i++, walk = walk->next)
        if (walk == node)
            return i;
    return -2;
}

void CompareSplines(const char *what, int index, Spline *a, Spline *b) {
    Compare(what, index, a, b, offsetof(Spline, pointList));
    CompareValue(what, index, a->pointList.size, b->pointList.size);
    g_checks++;
    if ((a->pointList.head == NULL) != (b->pointList.head == NULL)) {
        g_diffs++;
        Detail("[camdata] %s #%d: one list has no head\n", what, index);
        return;
    }
    if (a->pointList.head == NULL)
        return;
    PointNode *x = a->pointList.Begin(), *y = b->pointList.Begin();
    for (uint32_t i = 0; i <= a->pointList.size && i <= b->pointList.size; i++) {
        bool xEnd = x == a->pointList.head, yEnd = y == b->pointList.head;
        if (xEnd != yEnd) {
            g_diffs++;
            Detail("[camdata] %s #%d: the lists end at different points\n", what, index);
            return;
        }
        if (xEnd)
            return;
        if (!Compare(what, index, &x->value, &y->value, sizeof(Coord4)))
            return;
        if (x->next->prev != x || y->next->prev != y) {
            g_diffs++;
            Detail("[camdata] %s #%d: a broken link\n", what, index);
            return;
        }
        x = x->next;
        y = y->next;
    }
}

struct SplineArgs {
    Coord4 a, b, c, d, rotationA, rotationB;
    Coord3 e, f, g, h;
    float t, tension;
    int index, index2;
    bool blend, rotations;
};

void SplineCases(Rng &rng) {
    static const char *const kNames[] = { "AddToSplinePtList", "BuildSpline", "BuildSplineEx", "EvaluateSpline",
                                          "EvaluateSpline (frame)", "EvaluateTangent", "GetPointListSize",
                                          "FindPointInList", "PointList::Erase", "PointList::BuyNode",
                                          "PointList::IncreaseSize", "ClearSplinePtList" };
    alignas(16) static uint8_t storage[2][sizeof(Spline)];
    Spline *splines[2] = { reinterpret_cast<Spline *>(storage[0]), reinterpret_cast<Spline *>(storage[1]) };
    int index = 0;
    for (int sequence = 0; sequence < 150; sequence++) {
        bool ok = true;
        for (int side = 0; side < 2 && ok; side++) {
            memset(splines[side], 0xcd, sizeof(Spline));
            Spline *s = splines[side];
            auto op = [&]() { (s->*At<SplineConstructFn>(0x0007acd0))(); };
            if (!RunSide(side == 0, kSplineRange, &op)) {
                Fault("RCameraSpline::Construct", sequence);
                ok = false;
            }
        }
        if (!ok)
            continue;
        g_cases++;
        uint8_t mask[sizeof(Spline)] = {};
        memset(mask + offsetof(Spline, pointList) + offsetof(Spline::PointList, head), 1, 4);
        Compare("RCameraSpline::Construct", sequence, splines[0], splines[1], sizeof(Spline), mask);

        Coord4 rotations[2] = { RandomVector(rng), RandomVector(rng) };
        for (int side = 0; side < 2; side++) {
            splines[side]->rotations[0] = rotations[0];
            splines[side]->rotations[1] = rotations[1];
        }
        int steps = 10 + int(rng.Below(40));
        for (int step = 0; step < steps && ok; step++, index++) {
            int which = int(rng.Below(12));
            if (which == 11 && rng.Chance(60))
                which = 0;   // fewer clears, longer lists
            SplineArgs args;
            args.a = RandomVector(rng);
            args.b = RandomVector(rng);
            args.c = RandomVector(rng);
            args.d = RandomVector(rng);
            args.rotationA = RandomVector(rng);
            args.rotationB = RandomVector(rng);
            args.e = { args.a.x, args.a.y, args.a.z };
            args.f = { args.b.x, args.b.y, args.b.z };
            args.g = { args.c.x, args.c.y, args.c.z };
            args.h = { args.d.x, args.d.y, args.d.z };
            args.t = rng.Chance(85) ? rng.Range(-0.1f, 1.1f) : RandomFloat(rng);
            args.tension = rng.Range(0.0f, 1.0f);
            int size = int(splines[0]->pointList.size);
            args.index = int(rng.Below(uint32_t(size + 1)));
            args.index2 = args.index + int(rng.Below(uint32_t(size + 1 - args.index)));
            args.blend = rng.Chance(50);
            args.rotations = rng.Chance(80);
            // points onto the list: the end point (c) of BuildSpline, FindPointInList's point (a)
            if ((which == 1 || which == 7) && size > 0 && rng.Chance(75)) {
                Coord4 onList = PointAt(splines[0], int(rng.Below(uint32_t(size))))->value;
                (which == 1 ? args.c : args.a) = onList;
            }
            if (which == 1 && rng.Chance(20))
                args.b.x = args.b.y = args.b.z = 0.0f;
            if (which == 1 && rng.Chance(20))
                args.d.x = args.d.y = args.d.z = 0.0f;

            struct Out {
                Coord4 point;
                MATRIX4 frame;
                SplineArgs args;
                uint32_t result;
                int node;
            } out[2];
            memset(out, 0, sizeof(out));
            for (int side = 0; side < 2 && ok; side++) {
                Spline *s = splines[side];
                Out *o = &out[side];
                o->args = args;
                SplineArgs *x = &o->args;
                auto op = [&]() {
                    switch (which) {
                    case 0: (s->*At<AddFn>(0x0007ae00))(&x->a); break;
                    case 1:
                        (s->*At<BuildFn>(0x0007aa20))(&x->a, &x->b, &x->c, &x->d,
                                                      x->rotations ? &x->rotationA : NULL,
                                                      x->rotations ? &x->rotationB : NULL, x->tension);
                        break;
                    case 2: (s->*At<BuildExFn>(0x0007a650))(&x->e, &x->f, &x->g, &x->h); break;
                    case 3: (s->*At<EvalPointFn>(0x0007a530))(x->t, &o->point); break;
                    case 4: (s->*At<EvalFrameFn>(0x0007a6d0))(x->t, &o->frame, x->blend); break;
                    case 5: (s->*At<EvalPointFn>(0x0007a5c0))(x->t, &o->point); break;
                    case 6: o->result = (s->*At<SplineSizeFn>(0x0007a640))(); break;
                    case 7: {
                        PointNode *found = NULL;
                        At<FindFn>(0x0007a8a0)(&found, &s->pointList, &x->a);
                        o->node = PointIndex(s, found);
                        break;
                    }
                    case 8: {
                        PointNode *result = NULL;
                        PointNode *first = PointAt(s, x->index);
                        PointNode *last = PointAt(s, x->index2);
                        (s->pointList.*At<EraseFn>(0x0007a900))(&result, first, last);
                        o->node = PointIndex(s, result);
                        break;
                    }
                    case 9: {
                        PointNode *node = (s->pointList.*At<BuyNodeFn>(0x0007a850))(
                            reinterpret_cast<PointNode *>(uintptr_t(0x1234)), reinterpret_cast<PointNode *>(uintptr_t(0x5678)), &x->b);
                        o->point = node->value;
                        o->result = uint32_t(uintptr_t(node->next)) ^ uint32_t(uintptr_t(node->prev));
                        UMemory::FastFree(node, sizeof(PointNode));
                        PointNode *head = (s->pointList.*At<BuyHeadFn>(0x0007a950))();
                        o->node = head->next == head && head->prev == head;
                        UMemory::FastFree(head, sizeof(PointNode));
                        break;
                    }
                    case 10:
                        (s->pointList.*At<IncreaseFn>(0x0007ad50))(uint32_t(x->index & 3));
                        o->result = s->pointList.size;
                        s->pointList.size -= uint32_t(x->index & 3);
                        break;
                    case 11: (s->*At<SplineVoidFn>(0x0007a9f0))(); break;
                    }
                };
                if (!RunSide(side == 0, kSplineRange, &op)) {
                    Fault(kNames[which], index);
                    ok = false;
                }
            }
            if (!ok)
                break;
            g_cases++;
            Compare(kNames[which], index, &out[0], &out[1], sizeof(Out));
            CompareSplines(kNames[which], index, splines[0], splines[1]);
        }
        for (int side = 0; side < 2; side++) {
            Spline *s = splines[side];
            auto op = [&]() { (s->*At<SplineVoidFn>(0x0007a9b0))(); };
            if (!RunSide(side == 0, kSplineRange, &op))
                Fault("RCameraSpline::Destruct", sequence);
        }
        Compare("RCameraSpline::Destruct", sequence, splines[0], splines[1], sizeof(Spline));
    }
}

// ---- the linker's strays among them

void StrayCases(Rng &rng) {
    typedef GameSymbolTable *(GameSymbolTable::*DeleteFn)(unsigned);
    typedef void (GameSymbolTable::*DestructFn)();
    typedef uint32_t (UDataRecord::*MatchFn)() const;
    typedef void *(*CallbackFn)(const char *, bool *);
    for (int i = 0; i < 40; i++) {
        uint32_t words[2][2];
        for (int side = 0; side < 2; side++) {
            GameSymbolTable *table = static_cast<GameSymbolTable *>(UMemory::FastAlloc(sizeof(GameSymbolTable), "USymbolTable"));
            table->Construct();
            table->vtable = reinterpret_cast<void *>(uintptr_t(0x00190e18));
            void *map = table->namespaces;
            bool deleting = (i & 1) != 0;
            auto op = [&]() {
                if (deleting)
                    (table->*At<DeleteFn>(0x0007ae40))(0);
                else
                    (table->*At<DestructFn>(0x0007ae60))();
            };
            if (!RunSide(side == 0, kSplineRange, &op))
                Fault("GameSymbolTable", i);
            words[side][0] = uint32_t(uintptr_t(table->vtable));
            words[side][1] = table->namespaces == map;
            UMemory::FastFree(table, sizeof(GameSymbolTable));
        }
        g_cases++;
        Compare("GameSymbolTable", i, words[0], words[1], sizeof(words[0]));
    }
    for (int i = 0; i < 500; i++) {
        UDataRecord record;
        record.tag = rng.Next();
        record.flags = rng.Next();
        record.count = 0;
        record.data = 0;
        uint32_t result[2];
        for (int side = 0; side < 2; side++) {
            auto op = [&]() { result[side] = (record.*At<MatchFn>(0x0007ae70))(); };
            if (!RunSide(side == 0, kSplineRange, &op))
                Fault("UDataRecord::MatchTag", i);
        }
        g_cases++;
        Compare("UDataRecord::MatchTag", i, &result[0], &result[1], 4);
    }
    struct Out {
        void *result;
        bool found;
    } out[2];
    for (int side = 0; side < 2; side++) {
        out[side].found = true;
        auto op = [&]() { out[side].result = At<CallbackFn>(0x0007ae90)("GAME::nothing", &out[side].found); };
        if (!RunSide(side == 0, kSplineRange, &op))
            Fault("NoSymbolCallback", 0);
    }
    g_cases++;
    CompareValue("NoSymbolCallback", 0, out[0].result, out[1].result);
    CompareValue("NoSymbolCallback", 0, out[0].found, out[1].found);
}

// ---- RCameraIniLoader

// FUN_00078590 at an address, with the list in EDI
__declspec(naked) bool CallCarListContains(const char *list, const char *name, uint32_t address) {
    __asm {
        push edi
        mov edi, [esp + 8]
        push dword ptr [esp + 12]
        call dword ptr [esp + 20]
        add esp, 4
        pop edi
        ret
    }
}

// The player's car, answering the loader's three questions as told
const char *g_carType;
int g_carClass;
const char *g_carSecondary;

const char *__fastcall FakeGetCarType(void *, int) {
    return g_carType;
}
int __fastcall FakeGetCarClass(void *, int) {
    return g_carClass;
}
const char *__fastcall FakeGetSecondaryType(void *, int) {
    return g_carSecondary;
}

#define ShadowOverrideCarType (*(const char **)0x001e7a8c)

// The player's car's vtable pointer
void **&PlayerCarVtable() {
    RigidVehicle *car = **reinterpret_cast<RigidVehicle ***>(uintptr_t(0x00234e40));
    return *reinterpret_cast<void ***>(car);
}

struct FakeCar {
    void **saved;
    const char *savedOverride;
    void *vtable[128];
    explicit FakeCar(const char *override) {
        saved = PlayerCarVtable();
        memcpy(vtable, saved, sizeof(vtable));
        vtable[0x3c / 4] = reinterpret_cast<void *>(&FakeGetCarType);
        vtable[0x48 / 4] = reinterpret_cast<void *>(&FakeGetCarClass);
        vtable[0x144 / 4] = reinterpret_cast<void *>(&FakeGetSecondaryType);
        PlayerCarVtable() = vtable;
        savedOverride = ShadowOverrideCarType;
        ShadowOverrideCarType = override;
    }
    ~FakeCar() {
        PlayerCarVtable() = saved;
        ShadowOverrideCarType = savedOverride;
    }
};

std::vector<std::string> g_carNames;   // every name in the file's "car" lists

std::string RandomCarList(Rng &rng) {
    static const char *const kExtra[] = { "", " ", ",", "car", "x", "AnyCar", "a" };
    std::string list;
    int count = int(rng.Below(4));
    for (int i = 0; i < count; i++) {
        if (i > 0)
            list += rng.Chance(50) ? "," : " ";
        std::string name = g_carNames.empty() || rng.Chance(20) ? kExtra[rng.Below(7)]
                                                                  : g_carNames[rng.Below(uint32_t(g_carNames.size()))];
        if (rng.Chance(20) && !name.empty())
            name = name.substr(0, rng.Below(uint32_t(name.size())) + 1);
        if (rng.Chance(20))
            for (char &c : name)
                c = char(toupper(c));
        list += name;
    }
    return list;
}

// A name from the file's lists (kept for the next 64 calls)
const char *RandomCarName(Rng &rng) {
    static std::string pool[64];
    static int next;
    std::string &name = pool[next++ % 64];
    name = g_carNames.empty() || rng.Chance(15) ? std::string("nocar") : g_carNames[rng.Below(uint32_t(g_carNames.size()))];
    return name.c_str();
}

void SmallIniCases(Rng &rng) {
    typedef float (*ClampFn)(float, float, float);
    typedef bool (*CheckFn)(char *);
    typedef void (*ResolveFn)(const char *, uint32_t *);
    for (int i = 0; i < 2000; i++) {
        float args[3] = { RandomFloat(rng), RandomFloat(rng), RandomFloat(rng) };
        if (rng.Chance(60)) {
            args[1] = -0.5f;
            args[2] = 0.5f;
        }
        float result[2];
        for (int side = 0; side < 2; side++) {
            auto op = [&]() { result[side] = At<ClampFn>(0x00078560)(args[0], args[1], args[2]); };
            if (!RunSide(side == 0, kIniRange, &op))
                Fault("ClampCameraValue", i);
        }
        g_cases++;
        Compare("ClampCameraValue", i, &result[0], &result[1], 4);
    }
    for (int i = 0; i < 1500; i++) {
        std::string list = RandomCarList(rng);
        std::string name = rng.Chance(30) ? RandomCarList(rng) : std::string(RandomCarName(rng));
        bool result[2];
        for (int side = 0; side < 2; side++) {
            auto op = [&]() { result[side] = CallCarListContains(list.c_str(), name.c_str(), 0x00078590); };
            if (!RunSide(side == 0, kIniRange, &op))
                Fault("FUN_00078590", i);
        }
        g_cases++;
        Compare("FUN_00078590", i, &result[0], &result[1], 1);
    }
    for (int i = 0; i < 1500; i++) {
        std::string list = RandomCarList(rng);
        bool none = rng.Chance(5);
        g_carType = RandomCarName(rng);
        g_carClass = int(rng.Below(3));
        g_carSecondary = rng.Chance(40) ? RandomCarName(rng) : NULL;
        const char *override = rng.Chance(30) ? RandomCarName(rng) : NULL;
        std::string text[2] = { list, list };
        bool result[2];
        for (int side = 0; side < 2; side++) {
            FakeCar car(override);
            char *cars = none ? NULL : &text[side][0];
            auto op = [&]() { result[side] = At<CheckFn>(0x00078670)(cars); };
            if (!RunSide(side == 0, kIniRange, &op))
                Fault("CheckCameraAgainstCar", i);
        }
        g_cases++;
        Compare("CheckCameraAgainstCar", i, &result[0], &result[1], 1);
        g_checks++;
        if (text[0] != text[1]) {
            g_diffs++;
            Detail("[camdata] CheckCameraAgainstCar #%d: the lists differ after\n", i);
        }
    }
    // weapon names from the weapon manager's slots
    struct SlotView {
        int32_t weapon;
        const char *name;
        uint8_t rest[0x4c];
    };
    uint8_t *manager = *reinterpret_cast<uint8_t **>(uintptr_t(0x0023923c));
    if (manager == NULL)
        return;
    SlotView *slots = *reinterpret_cast<SlotView **>(manager + 0x10);
    std::vector<std::string> weapons;
    for (int i = 0; i < 32; i++)
        if (slots[i].name != NULL && slots[i].name[0] != '\0')
            weapons.push_back(slots[i].name);
    for (int i = 0; i < 800; i++) {
        std::string names;
        int count = int(rng.Below(5));
        for (int k = 0; k < count && !weapons.empty(); k++)
            names += weapons[rng.Below(uint32_t(weapons.size()))] + (rng.Chance(50) ? " " : ",");
        if (rng.Chance(10))
            names += "junk";
        bool none = rng.Chance(5);
        uint32_t mask[2][2];
        for (int side = 0; side < 2; side++) {
            mask[side][0] = 0xdeadbeef;
            mask[side][1] = 0x12345678;
            auto op = [&]() { At<ResolveFn>(0x000785f0)(none ? NULL : names.c_str(), mask[side]); };
            if (!RunSide(side == 0, kIniRange, &op))
                Fault("ResolveWeaponNames", i);
        }
        g_cases++;
        Compare("ResolveWeaponNames", i, mask[0], mask[1], sizeof(mask[0]));
    }
}

// What LoadFile leaves: the globals, and the tables' contents
struct LoadedTables {
    CameraTables tables;
    CameraModeIndices indices;
    CameraConstants constants;
    int32_t shakePeriod, latency;
    std::vector<uint8_t> modes, bumpers, helis, heliArms, splines, ellipses, heights, fixeds, dashboards, autoDrive;
};

template <class T>
void Take(std::vector<uint8_t> *out, const T *table, int count) {
    if (table != NULL && count > 0)
        out->insert(out->end(), reinterpret_cast<const uint8_t *>(table), reinterpret_cast<const uint8_t *>(table + count));
}

void Snapshot(LoadedTables *out) {
    CameraTables &t = fgCameraTables;
    out->tables = t;
    out->indices = fgCameraModeIndices;
    out->constants = fgCameraConstants;
    out->shakePeriod = kExplosionShakePeriod;
    out->latency = kAutoDriveLatency;
    Take(&out->modes, t.modes, t.modeCount + 1);
    Take(&out->bumpers, t.bumpers, t.bumperCount);
    for (int i = 0; i < t.heliCount; i++) {
        HeliCamInfo heli = t.helis[i];
        Take(&out->heliArms, heli.arms, heli.armCount);
        heli.arms = NULL;
        memset(heli.unknown14, 0, sizeof(heli.unknown14));
        memset(heli.pad2A, 0, sizeof(heli.pad2A));
        Take(&out->helis, &heli, 1);
    }
    Take(&out->splines, t.splines, t.splineCount);
    for (int i = 0; i < t.ellipseCount; i++) {
        EllipseCamInfo ellipse = t.ellipses[i];
        Take(&out->heights, ellipse.heights, ellipse.heightCount);
        ellipse.heights = NULL;
        Take(&out->ellipses, &ellipse, 1);
    }
    Take(&out->fixeds, t.fixeds, t.fixedCount);
    Take(&out->dashboards, t.dashboards, t.dashboardCount);
    Take(&out->autoDrive, t.autoDriveArms, t.autoDriveArmCount);
    // the pointers compare as present or not
    out->tables.autoDriveArms = reinterpret_cast<AutoDriveArmInfo *>(uintptr_t(t.autoDriveArms != NULL));
    out->tables.bumpers = reinterpret_cast<BumperCamInfo *>(uintptr_t(t.bumpers != NULL));
    out->tables.dashboards = reinterpret_cast<DashboardCamInfo *>(uintptr_t(t.dashboards != NULL));
    out->tables.helis = reinterpret_cast<HeliCamInfo *>(uintptr_t(t.helis != NULL));
    out->tables.modes = reinterpret_cast<CameraModeInfo *>(uintptr_t(t.modes != NULL));
    out->tables.splines = reinterpret_cast<SplineCamInfo *>(uintptr_t(t.splines != NULL));
    out->tables.ellipses = reinterpret_cast<EllipseCamInfo *>(uintptr_t(t.ellipses != NULL));
    out->tables.fixeds = reinterpret_cast<FixedCamInfo *>(uintptr_t(t.fixeds != NULL));
}

void FreeLoaded() {
    CameraTables &t = fgCameraTables;
    for (int i = 0; i < t.heliCount; i++)
        OperatorDelete(t.helis[i].arms);
    for (int i = 0; i < t.ellipseCount; i++)
        OperatorDelete(t.ellipses[i].heights);
    OperatorDelete(t.autoDriveArms);
    OperatorDelete(t.bumpers);
    OperatorDelete(t.dashboards);
    OperatorDelete(t.helis);
    OperatorDelete(t.modes);
    OperatorDelete(t.splines);
    OperatorDelete(t.ellipses);
    OperatorDelete(t.fixeds);
}

void CompareTable(const char *what, int index, const std::vector<uint8_t> &a, const std::vector<uint8_t> &b,
                  size_t record = 0, const uint8_t *mask = NULL) {
    g_checks++;
    if (a.size() != b.size()) {
        g_diffs++;
        Detail("[camdata] LoadFile %s #%d: %u bytes, port %u\n", what, index, unsigned(a.size()), unsigned(b.size()));
        return;
    }
    if (record == 0) {
        Compare(what, index, a.data(), b.data(), a.size());
        return;
    }
    for (size_t at = 0; at + record <= a.size(); at += record)
        if (!Compare(what, index, a.data() + at, b.data() + at, record, mask))
            return;
}

#define ShadowArchiveName ((char *)0x002431d8)
#define ShadowArchiveOpen (*(uint8_t *)0x002434da)
#define ShadowArchiveLogRequests (*(uint8_t *)0x002434e0)

void LoadFileCases(Rng &rng) {
    typedef void (*LoadFn)();
    // the car names in the file
    IniFiles *ini = static_cast<IniFiles *>(OperatorNew(sizeof(IniFiles)));
    ini->Construct("data/render/camera.ini", false);
    if (ini->dafi == NULL || ini->text == NULL) {
        printf("[camdata] camera.ini could not be read: LoadFile not tested\n");
        ini->Delete(1);
        return;
    }
    int sections = DAFI_getsectioncount(ini->dafi);
    for (int i = 0; i < sections; i++) {
        const char *cars = ini->ReadString(DAFI_getsectionbyindex(ini->dafi, i), "car", NULL);
        if (cars == NULL)
            continue;
        std::string list(cars);
        for (char &c : list)
            if (c == ',')
                c = ' ';
        size_t at = 0;
        while (at < list.size()) {
            size_t end = list.find(' ', at);
            if (end == std::string::npos)
                end = list.size();
            if (end > at)
                g_carNames.push_back(list.substr(at, end - at));
            at = end + 1;
        }
    }
    ini->Delete(1);

    // the live tables, kept aside and put back after
    CameraTables liveTables = fgCameraTables;
    CameraModeIndices liveIndices = fgCameraModeIndices;
    CameraConstants liveConstants = fgCameraConstants;
    int32_t liveShake = kExplosionShakePeriod, liveLatency = kAutoDriveLatency;
    RigidVehicle *car = *(*reinterpret_cast<RigidVehicle ***>(uintptr_t(0x00234e40)));
    const char *realType = car->GetCarType();
    int realClass = car->GetCarClass();
    const char *realSecondary = reinterpret_cast<const char *(__fastcall *)(void *, int)>((*reinterpret_cast<void ***>(car))[0x144 / 4])(car, 0);

    for (int i = 0; i < 24; i++) {
        if (i == 0) {
            g_carType = realType;
            g_carClass = realClass;
            g_carSecondary = realSecondary;
        } else {
            g_carType = RandomCarName(rng);
            g_carClass = int(rng.Below(2));
            g_carSecondary = rng.Chance(40) ? RandomCarName(rng) : NULL;
        }
        const char *override = i > 0 && rng.Chance(25) ? RandomCarName(rng) : ShadowOverrideCarType;
        LoadedTables loaded[2];
        bool ok = true;
        for (int side = 0; side < 2 && ok; side++) {
            fgCameraTables = liveTables;
            fgCameraTables.modes = NULL;
            fgCameraModeIndices = liveIndices;
            fgCameraConstants = liveConstants;
            kExplosionShakePeriod = liveShake;
            kAutoDriveLatency = liveLatency;
            FakeCar fake(override);
            auto op = [&]() { At<LoadFn>(0x00078770)(); };
            if (!RunSide(side == 0, kIniRange, &op)) {
                Fault("LoadFile", i);
                ok = false;
                break;
            }
            Snapshot(&loaded[side]);
            FreeLoaded();
        }
        if (!ok)
            break;
        g_cases++;
        LoadedTables &a = loaded[0], &b = loaded[1];
        CompareValue("LoadFile tables", i, a.tables, b.tables);
        CompareValue("LoadFile indices", i, a.indices, b.indices);
        CompareValue("LoadFile constants", i, a.constants, b.constants);
        CompareValue("LoadFile kExplosionShakePeriod", i, a.shakePeriod, b.shakePeriod);
        CompareValue("LoadFile kAutoDriveLatency", i, a.latency, b.latency);
        CompareTable("modes", i, a.modes, b.modes);
        uint8_t bumperMask[sizeof(BumperCamInfo)] = {};
        memset(bumperMask + offsetof(BumperCamInfo, unknown38), 1, sizeof(BumperCamInfo::unknown38));
        CompareTable("bumpers", i, a.bumpers, b.bumpers, sizeof(BumperCamInfo), bumperMask);
        CompareTable("helis", i, a.helis, b.helis);
        uint8_t armMask[sizeof(HeliArmInfo)] = {};
        memset(armMask + offsetof(HeliArmInfo, unknown24), 1, sizeof(HeliArmInfo::unknown24));
        memset(armMask + offsetof(HeliArmInfo, unknown3C), 1, sizeof(HeliArmInfo::unknown3C));
        CompareTable("heli arms", i, a.heliArms, b.heliArms, sizeof(HeliArmInfo), armMask);
        CompareTable("splines", i, a.splines, b.splines);
        CompareTable("ellipses", i, a.ellipses, b.ellipses);
        CompareTable("ellipse heights", i, a.heights, b.heights);
        CompareTable("fixeds", i, a.fixeds, b.fixeds);
        CompareTable("dashboards", i, a.dashboards, b.dashboards);
        uint8_t autoMask[sizeof(AutoDriveArmInfo)] = {};
        memset(autoMask + offsetof(AutoDriveArmInfo, unknown3C), 1, sizeof(AutoDriveArmInfo::unknown3C));
        memset(autoMask + offsetof(AutoDriveArmInfo, pad55), 1, sizeof(AutoDriveArmInfo::pad55));
        // the bytes LoadFile does not write (the camera's run-time state among them)
        memset(autoMask + offsetof(AutoDriveArmInfo, unknown63), 1,
               offsetof(AutoDriveArmInfo, restInterpolFallScale) - offsetof(AutoDriveArmInfo, unknown63));
        memset(autoMask + offsetof(AutoDriveArmInfo, restStartStep), 1,
               offsetof(AutoDriveArmInfo, normCursorMoveSpeed) - offsetof(AutoDriveArmInfo, restStartStep));
        memset(autoMask + offsetof(AutoDriveArmInfo, restPitch), 1,
               sizeof(AutoDriveArmInfo) - offsetof(AutoDriveArmInfo, restPitch));
        CompareTable("auto-drive arms", i, a.autoDrive, b.autoDrive, sizeof(AutoDriveArmInfo), autoMask);
    }
    fgCameraTables = liveTables;
    fgCameraModeIndices = liveIndices;
    fgCameraConstants = liveConstants;
    kExplosionShakePeriod = liveShake;
    kAutoDriveLatency = liveLatency;
}

// ---- recording fakes for the camera's and the HUD's methods

struct Hook {
    uint32_t at;
    uint8_t saved[5];
    bool on;
};
Hook g_hooks[40];
int g_hookCount;

void HookOne(uint32_t at, const void *to) {
    if (g_hookCount == int(sizeof(g_hooks) / sizeof(g_hooks[0])))
        return;
    Hook &h = g_hooks[g_hookCount++];
    h.at = at;
    h.on = false;
    DWORD old;
    if (!VirtualProtect(reinterpret_cast<void *>(uintptr_t(at)), 5, PAGE_EXECUTE_READWRITE, &old))
        return;
    memcpy(h.saved, reinterpret_cast<void *>(uintptr_t(at)), 5);
    uint8_t jump[5];
    jump[0] = 0xe9;
    int32_t rel = int32_t(uint32_t(uintptr_t(to)) - (at + 5));
    memcpy(jump + 1, &rel, 4);
    memcpy(reinterpret_cast<void *>(uintptr_t(at)), jump, 5);
    VirtualProtect(reinterpret_cast<void *>(uintptr_t(at)), 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void *>(uintptr_t(at)), 5);
    h.on = true;
}

// An entry already ported holds a jump to the port, which ports call directly: both are hooked.
void HookInstall(uint32_t at, const void *to) {
    const uint8_t *entry = reinterpret_cast<const uint8_t *>(uintptr_t(at));
    uint32_t port = 0;
    if (entry[0] == 0xe9) {
        int32_t rel;
        memcpy(&rel, entry + 1, 4);
        port = at + 5 + uint32_t(rel);
    }
    HookOne(at, to);
    if (port != 0 && port != uint32_t(uintptr_t(to)))
        HookOne(port, to);
}

void HooksRemove() {
    for (int i = g_hookCount; i-- > 0;) {
        Hook &h = g_hooks[i];
        if (!h.on)
            continue;
        DWORD old;
        VirtualProtect(reinterpret_cast<void *>(uintptr_t(h.at)), 5, PAGE_EXECUTE_READWRITE, &old);
        memcpy(reinterpret_cast<void *>(uintptr_t(h.at)), h.saved, 5);
        VirtualProtect(reinterpret_cast<void *>(uintptr_t(h.at)), 5, old, &old);
        FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void *>(uintptr_t(h.at)), 5);
        h.on = false;
    }
    g_hookCount = 0;
}

std::vector<uint32_t> g_log;
uint32_t g_answers;        // the fakes' answers, shifted out a bit at a time; reset before each side

void Log(uint32_t type, std::initializer_list<uint32_t> words) {
    g_log.push_back(type);
    g_log.insert(g_log.end(), words.begin(), words.end());
}

uint32_t Bits(float f) {
    uint32_t bits;
    memcpy(&bits, &f, 4);
    return bits;
}

bool Answer() {
    bool bit = (g_answers & 1) != 0;
    g_answers = (g_answers >> 1) | (bit ? 0x80000000u : 0);
    return bit;
}

void LogData(uint32_t type, const RDirectorQueueData *data) {
    Log(type, { Bits(data->position.x), Bits(data->position.y), Bits(data->position.z), Bits(data->position.w),
                data->delay, data->cameraMode, data->unknown14, data->flags, data->data18 != NULL,
                data->data1C != NULL, uint32_t(uintptr_t(data->anchor)), data->unknown24 });
}

int __fastcall FakeGetAnchorResetAvailable(void *, int) {
    Log(1, {});
    return Answer();
}
int FakeGetMaxTumble() {
    Log(2, {});
    return 7;
}
void __fastcall FakeSetTumbleCam(void *, int, unsigned index) {
    Log(3, { index });
}
void __fastcall FakeSetCameraModeByIndex(void *, int, int mode, int a, int b, int c, int d, int e, int f) {
    Log(4, { uint32_t(mode), uint32_t(a), uint32_t(b), uint32_t(c), uint32_t(d), uint32_t(e), uint32_t(f) });
}
void __fastcall FakeDirectorSetAnchor(void *, int, RDirectorQueueData *data) {
    LogData(5, data);
}
void __fastcall FakeDirectorChangeCameraMode(void *, int, RDirectorQueueData *data) {
    LogData(6, data);
}
void __fastcall FakeNextCameraMode(void *, int, unsigned short player) {
    Log(7, { player });
}
void __fastcall FakePrevCameraMode(void *, int, unsigned short player) {
    Log(8, { player });
}
void __fastcall FakeSetCameraLookBack(void *, int, uint32_t lookBack) {
    Log(9, { lookBack & 0xff });
}
bool __fastcall FakeSetAutoDriveRotationX(void *, int, float rotation) {
    Log(10, { Bits(rotation) });
    return Answer();
}
bool __fastcall FakeSetAutoDriveRotationY(void *, int, float rotation) {
    Log(11, { Bits(rotation) });
    return Answer();
}
void __fastcall FakeSetAutoDriveZoom(void *, int, float zoom) {
    Log(12, { Bits(zoom) });
}
void __fastcall FakeToggleAutoDriveZoom(void *, int, uint32_t on) {
    Log(13, { on & 0xff });
}
void __fastcall FakeInitSpin(void *, int) {
    Log(14, {});
}
void __fastcall FakeInitWeaponChange(void *, int) {
    Log(15, {});
}
void __fastcall FakeInitAimZoom(void *, int) {
    Log(16, {});
}
void __fastcall FakeResetZoomSlope(void *, int) {
    Log(17, {});
}
void __fastcall FakeAimOn(void *, int) {
    Log(18, {});
}
void __fastcall FakeAimOff(void *, int) {
    Log(19, {});
}

void InstallFakes() {
    HookInstall(0x000974e0, reinterpret_cast<const void *>(&FakeGetAnchorResetAvailable));
    HookInstall(0x00081af0, reinterpret_cast<const void *>(&FakeGetMaxTumble));
    HookInstall(0x00088d10, reinterpret_cast<const void *>(&FakeSetTumbleCam));
    HookInstall(0x00083f50, reinterpret_cast<const void *>(&FakeSetCameraModeByIndex));
    HookInstall(0x00081340, reinterpret_cast<const void *>(&FakeDirectorSetAnchor));
    HookInstall(0x00084130, reinterpret_cast<const void *>(&FakeDirectorChangeCameraMode));
    HookInstall(0x00084030, reinterpret_cast<const void *>(&FakeNextCameraMode));
    HookInstall(0x000840b0, reinterpret_cast<const void *>(&FakePrevCameraMode));
    HookInstall(0x000814c0, reinterpret_cast<const void *>(&FakeSetCameraLookBack));
    HookInstall(0x000847f0, reinterpret_cast<const void *>(&FakeSetAutoDriveRotationX));
    HookInstall(0x00084820, reinterpret_cast<const void *>(&FakeSetAutoDriveRotationY));
    HookInstall(0x000816e0, reinterpret_cast<const void *>(&FakeSetAutoDriveZoom));
    HookInstall(0x00081700, reinterpret_cast<const void *>(&FakeToggleAutoDriveZoom));
    HookInstall(0x000819a0, reinterpret_cast<const void *>(&FakeInitSpin));
    HookInstall(0x00081a20, reinterpret_cast<const void *>(&FakeInitWeaponChange));
    HookInstall(0x00081a70, reinterpret_cast<const void *>(&FakeInitAimZoom));
    HookInstall(0x00081ad0, reinterpret_cast<const void *>(&FakeResetZoomSlope));
    HookInstall(0x000d9ba0, reinterpret_cast<const void *>(&FakeAimOn));
    HookInstall(0x000d9bd0, reinterpret_cast<const void *>(&FakeAimOff));
}

void CompareLogs(const char *what, int index, const std::vector<uint32_t> &a, const std::vector<uint32_t> &b) {
    g_checks++;
    if (a != b) {
        g_diffs++;
        Detail("[camdata] %s #%d: the calls differ (%u words, port %u)\n", what, index, unsigned(a.size()),
               unsigned(b.size()));
    }
}

// ---- RDirectorQueue

typedef RDirectorQueue Queue;
typedef Queue *(Queue::*QueueConstructFn)(RPlayerCamera *);
typedef void (Queue::*QueueVoidFn)();
typedef void (Queue::*AppendFn)(const RDirectorQueueData *);
typedef DirectorQueueNode **(Queue::Queue::*QueueEraseFn)(DirectorQueueNode **, DirectorQueueNode *, DirectorQueueNode *);
typedef void (Queue::Queue::*QueueListVoidFn)();
typedef RDirectorQueueData *(RDirectorQueueData::*DataConstructFn)(uint16_t, uint16_t, uint16_t, uint16_t, void *,
                                                                     PhysicsObject *, uint32_t);
typedef RDirectorQueueData *(RDirectorQueueData::*DataCopyFn)(const RDirectorQueueData *);
typedef void (RDirectorQueueData::*DataVoidFn)();

DirectorQueueNode *QueueAt(Queue *queue, int index) {
    DirectorQueueNode *node = queue->queue.Begin();
    for (int i = 0; i < index && node != NULL; i++)
        node = node->next;
    return node;
}

int QueueIndex(Queue *queue, DirectorQueueNode *node) {
    if (node == queue->queue.head)
        return -1;
    DirectorQueueNode *walk = queue->queue.Begin();
    for (int i = 0; walk != NULL && walk != queue->queue.head; i++, walk = walk->next)
        if (walk == node)
            return i;
    return -2;
}

// A change's fields, its two data pointers as present or not
void DataWords(const RDirectorQueueData *data, uint32_t *words) {
    memcpy(words, data, sizeof(RDirectorQueueData));
    words[6] = data->data18 != NULL;
    words[7] = data->data1C != NULL;
}

void CompareQueues(const char *what, int index, Queue *a, Queue *b) {
    CompareValue(what, index, a->queue.size, b->queue.size);
    Compare(what, index, &a->camera + 1, &b->camera + 1, 4);   // the flags and padding
    if (a->queue.head == NULL || b->queue.head == NULL) {
        g_checks++;
        if ((a->queue.head == NULL) != (b->queue.head == NULL)) {
            g_diffs++;
            Detail("[camdata] %s #%d: one queue has no head\n", what, index);
        }
        return;
    }
    DirectorQueueNode *x = a->queue.Begin(), *y = b->queue.Begin();
    for (uint32_t i = 0; i <= a->queue.size; i++) {
        bool xEnd = x == a->queue.head, yEnd = y == b->queue.head;
        g_checks++;
        if (xEnd != yEnd) {
            g_diffs++;
            Detail("[camdata] %s #%d: the queues end at different changes\n", what, index);
            return;
        }
        if (xEnd)
            return;
        uint32_t wx[10], wy[10];
        DataWords(&x->value, wx);
        DataWords(&y->value, wy);
        if (!Compare(what, index, wx, wy, sizeof(wx)))
            return;
        x = x->next;
        y = y->next;
    }
}

RDirectorQueueData RandomChange(Rng &rng, int modeCount) {
    RDirectorQueueData data;
    data.position = RandomVector(rng);
    data.delay = uint16_t(rng.Chance(60) ? 0 : rng.Below(3));
    data.cameraMode = uint16_t(rng.Below(uint32_t(modeCount + 1)));
    data.unknown14 = uint16_t(rng.Next());
    static const uint16_t kFlags[] = { 0, RDirectorQueueData::kKeep, RDirectorQueueData::kOwnsData18,
                                       RDirectorQueueData::kOwnsData1C, 0x2c, 0x01 };
    data.flags = kFlags[rng.Below(6)];
    data.data18 = rng.Chance(50) ? reinterpret_cast<CARP::Instance *>(uintptr_t(1)) : NULL;   // made real per side
    data.data1C = rng.Chance(50) ? reinterpret_cast<void *>(uintptr_t(1)) : NULL;
    data.anchor = rng.Chance(40) ? reinterpret_cast<PhysicsObject *>(uintptr_t(rng.Next() | 1)) : NULL;
    data.unknown24 = rng.Next();
    return data;
}

// The data pointers: blocks the queue may delete when it owns them
void MakeBlocks(RDirectorQueueData *data) {
    if (data->data18 != NULL)
        data->data18 = (data->flags & RDirectorQueueData::kOwnsData18)
                           ? static_cast<CARP::Instance *>(OperatorNew(8))
                           : reinterpret_cast<CARP::Instance *>(uintptr_t(0x18181818));
    if (data->data1C != NULL)
        data->data1C = (data->flags & RDirectorQueueData::kOwnsData1C) ? OperatorNew(8)
                                                                       : reinterpret_cast<void *>(uintptr_t(0x1c1c1c1c));
}

void QueueCases(Rng &rng) {
    static const char *const kNames[] = { "AppendData", "ProcessDirectorLogic", "RestartDirectorQueue",
                                          "Queue::PopFront", "Queue::Erase", "RDirectorQueueData" };
    CameraTables &tables = fgCameraTables;
    if (tables.modes == NULL || tables.modeCount <= 0) {
        printf("[camdata] no camera modes: the director queue not tested\n");
        return;
    }
    alignas(16) static uint8_t cameraStorage[2][sizeof(RPlayerCamera)];
    alignas(16) static uint8_t queueStorage[2][sizeof(Queue)];
    RPlayerCamera *cameras[2] = { reinterpret_cast<RPlayerCamera *>(cameraStorage[0]),
                                  reinterpret_cast<RPlayerCamera *>(cameraStorage[1]) };
    Queue *queues[2] = { reinterpret_cast<Queue *>(queueStorage[0]), reinterpret_cast<Queue *>(queueStorage[1]) };
    int index = 0;
    for (int sequence = 0; sequence < 80; sequence++) {
        bool ok = true;
        uint32_t seed = rng.Next();
        for (int side = 0; side < 2; side++) {
            Rng fill = { seed };
            Fill(fill, cameraStorage[side], sizeof(RPlayerCamera));
            memset(queueStorage[side], 0xcd, sizeof(Queue));
            Queue *q = queues[side];
            RPlayerCamera *camera = cameras[side];
            auto op = [&]() { (q->*At<QueueConstructFn>(0x0007c380))(camera); };
            if (!RunSide(side == 0, kQueueRange, &op)) {
                Fault("RDirectorQueue::Construct", sequence);
                ok = false;
            }
        }
        if (!ok)
            continue;
        g_cases++;
        CompareQueues("RDirectorQueue::Construct", sequence, queues[0], queues[1]);
        int steps = 10 + int(rng.Below(30));
        for (int step = 0; step < steps && ok; step++, index++) {
            int which = int(rng.Below(6));
            if (which >= 3 && which <= 4 && rng.Chance(50))
                which = 0;
            RDirectorQueueData change = RandomChange(rng, tables.modeCount);
            int size = int(queues[0]->queue.size);
            int first = int(rng.Below(uint32_t(size + 1)));
            int last = first + int(rng.Below(uint32_t(size + 1 - first)));
            uint8_t flags = uint8_t(rng.Below(4));
            int mode = int(rng.Below(uint32_t(tables.modeCount + 1)));
            int previous = int(rng.Below(uint32_t(tables.modeCount + 1)));
            int tumbleIndex = int(rng.Below(4)) - 1;
            uint32_t modeChangeFlags = rng.Next() & 3;
            uint32_t answers = rng.Next();
            struct Out {
                std::vector<uint32_t> log;
                uint32_t words[3][10];
                int node;
            } out[2];
            for (int side = 0; side < 2 && ok; side++) {
                Queue *q = queues[side];
                RPlayerCamera *camera = cameras[side];
                Out *o = &out[side];
                memset(o->words, 0, sizeof(o->words));
                o->node = 0;
                RDirectorQueueData data = change;
                MakeBlocks(&data);
                if (which == 1 || which == 2) {
                    q->flags = flags;
                    camera->cameraMode = mode;
                    camera->previousCameraMode = previous;
                    camera->tumbleCamIndex = unsigned(tumbleIndex);
                    camera->modeChangeFlags = modeChangeFlags;
                }
                g_log.clear();
                g_answers = answers;
                auto op = [&]() {
                    switch (which) {
                    case 0: (q->*At<AppendFn>(0x0007c6a0))(&data); break;
                    case 1: (q->*At<QueueVoidFn>(0x0007c3f0))(); break;
                    case 2: (q->*At<QueueVoidFn>(0x0007c5e0))(); break;
                    case 3: (q->queue.*At<QueueListVoidFn>(0x0007c340))(); break;
                    case 4: {
                        DirectorQueueNode *result = NULL;
                        (q->queue.*At<QueueEraseFn>(0x0007c2e0))(&result, QueueAt(q, first), QueueAt(q, last));
                        o->node = QueueIndex(q, result);
                        break;
                    }
                    case 5: {
                        RDirectorQueueData made, copy;
                        memset(&made, 0xcd, sizeof(made));
                        memset(&copy, 0xcd, sizeof(copy));
                        (made.*At<DataConstructFn>(0x0007c160))(data.delay, data.cameraMode, data.unknown14,
                                                                 data.flags, data.data18, data.anchor, data.unknown24);
                        DataWords(&made, o->words[0]);
                        (copy.*At<DataCopyFn>(0x0007c1c0))(&data);
                        DataWords(&copy, o->words[1]);
                        (copy.*At<DataVoidFn>(0x0007c220))();
                        DataWords(&copy, o->words[2]);
                        break;
                    }
                    }
                };
                if (!RunSide(side == 0, kQueueRange, &op)) {
                    Fault(kNames[which], index);
                    ok = false;
                }
                o->log = g_log;
            }
            if (!ok)
                break;
            g_cases++;
            CompareLogs(kNames[which], index, out[0].log, out[1].log);
            Compare(kNames[which], index, out[0].words, out[1].words, sizeof(out[0].words));
            CompareValue(kNames[which], index, out[0].node, out[1].node);
            Compare(kNames[which], index, cameras[0], cameras[1], sizeof(RPlayerCamera));
            CompareQueues(kNames[which], index, queues[0], queues[1]);
        }
        for (int side = 0; side < 2; side++) {
            Queue *q = queues[side];
            auto op = [&]() { (q->*At<QueueVoidFn>(0x0007c3b0))(); };
            if (!RunSide(side == 0, kQueueRange, &op))
                Fault("RDirectorQueue::Destruct", sequence);
        }
        uint8_t mask[sizeof(Queue)] = {};
        memset(mask + offsetof(Queue, camera), 1, sizeof(RPlayerCamera *));
        Compare("RDirectorQueue::Destruct", sequence, queues[0], queues[1], sizeof(Queue), mask);
    }
}

// ---- RPlayerCamState

typedef RPlayerCamState State;
typedef State *(State::*StateConstructFn)(RPlayerCamera *);
typedef void (State::*StateVoidFn)();
typedef void (State::*StateInputFn)(int, float);

struct CarAudioFlagsView {
    uint8_t unknown000[0xc2];
    uint8_t flags[3];
};

#define ShadowSimState I32_AT(0x00234e24)
#define ShadowSimStepCount I32_AT(0x00234e34)
#define ShadowMission (*(uint8_t **)0x00239220)
#define ShadowMissionName ((char *)(0x00243b90 + 0x4f4))
#define ShadowUnknown2445d4 I32_AT(0x002445d4)

void StateCases(Rng &rng) {
    static const char *const kNames[] = { "Construct", "ResetState", "ResetStateForAnimation", "DriveCamInputHandler",
                                          "AutoDriveCamInputHandler", "AimZoom", "AimRelease" };
    static const int kInputs[] = { 1, 27, 28, 33, 34, 35, 38, 39, 40, 41, 42, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53,
                                   54, 56, 58, 0, 2, 36, 43, 55, 57, 60 };
    RigidVehicle *car = *(*reinterpret_cast<RigidVehicle ***>(uintptr_t(0x00234e40)));
    typedef CarAudioFlagsView *(RigidVehicle::*AudioFn)();
    CarAudioFlagsView *audio = (car->*XbeVirtual<AudioFn>(car, 0x1c / 4))();
    uint8_t *mission = ShadowMission;
    if (audio == NULL || mission == NULL) {
        printf("[camdata] no car audio or mission manager: RPlayerCamState not tested\n");
        return;
    }
    // everything a handler reads or writes outside the state, kept aside
    uint8_t savedAudio[3];
    memcpy(savedAudio, audio->flags, 3);
    int32_t savedSimState = ShadowSimState, savedSteps = ShadowSimStepCount;
    int32_t savedMission[3] = { *reinterpret_cast<int32_t *>(mission + 0x474), *reinterpret_cast<int32_t *>(mission + 0x47c),
                                *reinterpret_cast<int32_t *>(mission + 0x4f0) };
    uint8_t savedMission710 = mission[0x710];
    char savedName[16];
    memcpy(savedName, ShadowMissionName, 16);
    uint8_t savedLockOn = CameraLockOnFlag;
    int32_t savedUnknown = ShadowUnknown2445d4;

    alignas(16) static uint8_t storage[2][sizeof(State)];
    alignas(16) static uint8_t cameraStorage[sizeof(RPlayerCamera)];
    RPlayerCamera *camera = reinterpret_cast<RPlayerCamera *>(cameraStorage);
    for (int i = 0; i < 6000; i++) {
        int which = i < 20 ? int(i % 3) : 3 + int(rng.Below(4));
        if (which == 5 || which == 6)
            which = rng.Chance(30) ? which : 4;
        // the state, as the handlers leave it: bytes 0 or 1, floats
        State start;
        memset(&start, 0, sizeof(start));
        start.camera = camera;
        uint8_t *bytes = reinterpret_cast<uint8_t *>(&start);
        for (int b = 4; b < 0x10; b++)
            bytes[b] = rng.Chance(25);
        for (int k = 0; k < 2; k++) {
            start.rotationY[k] = rng.Chance(40) ? 0.0f : RandomFloat(rng);
            start.rotationX[k] = rng.Chance(40) ? 0.0f : RandomFloat(rng);
        }
        start.unknown20 = rng.Chance(50);
        start.unknown24 = RandomFloat(rng);
        start.lookingBack = rng.Chance(50);
        start.unknown28 = RandomFloat(rng);
        start.unknown2C = rng.Chance(50);
        start.unknown30 = int32_t(rng.Below(200));
        int input = kInputs[rng.Below(sizeof(kInputs) / sizeof(kInputs[0]))];
        float value = rng.Chance(30) ? 0.0f : RandomFloat(rng);
        int32_t simState = rng.Chance(30) ? 3 : int32_t(rng.Below(3));
        int32_t steps = rng.Chance(30) ? int32_t(rng.Below(5)) : savedSteps;
        int32_t missionWords[3] = { rng.Chance(15) ? 4 : savedMission[0], rng.Chance(25) ? 1 : 0,
                                    rng.Chance(25) ? 1 : 0 };
        uint8_t mission710 = rng.Chance(50);
        bool spinTrack = rng.Chance(50);
        uint8_t lockOn = rng.Chance(50);
        int32_t unknown = rng.Chance(50) ? 1 : 0;
        uint8_t audioStart[3] = { uint8_t(rng.Chance(50)), uint8_t(rng.Chance(50)), uint8_t(rng.Chance(50)) };
        uint32_t answers = rng.Next();
        struct Out {
            State state;
            uint8_t audio[3];
            uint8_t lockOn;
            std::vector<uint32_t> log;
        } out[2];
        bool ok = true;
        for (int side = 0; side < 2 && ok; side++) {
            State *s = reinterpret_cast<State *>(storage[side]);
            *s = start;
            if (which == 0)
                memset(s, 0xcd, sizeof(State));
            ShadowSimState = simState;
            ShadowSimStepCount = steps;
            *reinterpret_cast<int32_t *>(mission + 0x474) = missionWords[0];
            *reinterpret_cast<int32_t *>(mission + 0x47c) = missionWords[1];
            *reinterpret_cast<int32_t *>(mission + 0x4f0) = missionWords[2];
            mission[0x710] = mission710;
            if (spinTrack)
                strcpy(ShadowMissionName, "SNOW1a_mis3");
            CameraLockOnFlag = lockOn;
            ShadowUnknown2445d4 = unknown;
            memcpy(audio->flags, audioStart, 3);
            g_log.clear();
            g_answers = answers;
            auto op = [&]() {
                switch (which) {
                case 0: (s->*At<StateConstructFn>(0x00089df0))(camera); break;
                case 1: (s->*At<StateVoidFn>(0x00089b70))(); break;
                case 2: (s->*At<StateVoidFn>(0x00089bd0))(); break;
                case 3: (s->*At<StateInputFn>(0x00089be0))(input, value); break;
                case 4: (s->*At<StateInputFn>(0x00089e10))(input, value); break;
                case 5: (s->*At<StateVoidFn>(0x0008a310))(); break;
                case 6: (s->*At<StateVoidFn>(0x0008a390))(); break;
                }
            };
            if (!RunSide(side == 0, kStateRange, &op)) {
                Fault(kNames[which], i);
                ok = false;
            }
            Out *o = &out[side];
            o->state = *s;
            memcpy(o->audio, audio->flags, 3);
            o->lockOn = CameraLockOnFlag;
            o->log = g_log;
            memcpy(ShadowMissionName, savedName, 16);
        }
        if (!ok)
            break;
        g_cases++;
        Compare(kNames[which], i, &out[0].state, &out[1].state, sizeof(State));
        Compare(kNames[which], i, out[0].audio, out[1].audio, 3);
        CompareValue(kNames[which], i, out[0].lockOn, out[1].lockOn);
        CompareLogs(kNames[which], i, out[0].log, out[1].log);
    }
    memcpy(audio->flags, savedAudio, 3);
    ShadowSimState = savedSimState;
    ShadowSimStepCount = savedSteps;
    *reinterpret_cast<int32_t *>(mission + 0x474) = savedMission[0];
    *reinterpret_cast<int32_t *>(mission + 0x47c) = savedMission[1];
    *reinterpret_cast<int32_t *>(mission + 0x4f0) = savedMission[2];
    mission[0x710] = savedMission710;
    memcpy(ShadowMissionName, savedName, 16);
    CameraLockOnFlag = savedLockOn;
    ShadowUnknown2445d4 = savedUnknown;
}

// ---- the renderer's strays

void RendererCases(Rng &rng) {
    typedef void (*FillFn)(ReverseDrawEntry *, ReverseDrawEntry *, const ReverseDrawEntry *);
    typedef ReverseDrawEntry **(*CopyBackFn)(ReverseDrawEntry **, ReverseDrawEntry *, ReverseDrawEntry *,
                                             ReverseDrawEntry *);
    typedef ReverseDrawEntry *(*UcopyFn)(ReverseDrawEntry *, ReverseDrawEntry *, ReverseDrawEntry *);
    typedef void (*UfillFn)(ReverseDrawEntry *, uint32_t, const ReverseDrawEntry *);
    typedef uint32_t (ReverseDrawList::*SizeFn)();
    typedef ReverseDrawEntry *(ReverseDrawList::*MemberCopyFn)(ReverseDrawEntry *, ReverseDrawEntry *,
                                                               ReverseDrawEntry *);
    typedef ReverseDrawEntry *(ReverseDrawList::*MemberFillFn)(ReverseDrawEntry *, uint32_t, const ReverseDrawEntry *);
    typedef void (ReverseDrawList::*DeallocateFn)(ReverseDrawEntry *, uint32_t);
    typedef void (ReverseDrawList::*TidyFn)();
    typedef TextureDofState *(TextureDofState::*DofConstructFn)();
    typedef void (TextureDofState::*DofDestructFn)();
    static const char *const kNames[] = { "FillReverseDrawEntries", "CopyBackwardReverseDrawEntries",
                                          "CopyBackwardReverseDrawEntriesThunk", "UninitializedCopyReverseDrawEntries",
                                          "UninitializedFillReverseDrawEntries", "ReverseDrawList::Ucopy",
                                          "ReverseDrawList::Ufill", "ReverseDrawList::Size",
                                          "ReverseDrawList::Deallocate", "ReverseDrawList::Tidy" };
    const int kEntries = 24;
    for (int i = 0; i < 1500; i++) {
        int which = i % 10;
        static ReverseDrawEntry arrays[2][2][24];
        uint32_t seed = rng.Next();
        int a = int(rng.Below(kEntries + 1)), b = a + int(rng.Below(uint32_t(kEntries + 1 - a)));
        int dest = int(rng.Below(kEntries + 1));
        if (dest + (b - a) > kEntries)
            dest = kEntries - (b - a);
        int count = int(rng.Below(uint32_t(kEntries + 1 - dest)));
        int blockCount = count % 12 + 1;
        // A NULL destination only for at most one entry: the helpers skip a NULL slot (placement new) but step on
        // from it, so a second entry would be written just above address 0 - in the game as here.
        bool nullDest = rng.Chance(10);
        if ((which == 3 && b - a > 1) || (which == 4 && count > 1))
            nullDest = false;
        ReverseDrawEntry value;
        Fill(rng, &value, sizeof(value));
        struct Out {
            int32_t result;
            uint32_t list[4];
        } out[2];
        bool faulted[2];
        for (int side = 0; side < 2; side++) {
            Rng fill = { seed };
            Fill(fill, arrays[side], sizeof(arrays[side]));
            ReverseDrawEntry *base = arrays[side][0], *other = arrays[side][1];
            Out *o = &out[side];
            memset(o, 0, sizeof(Out));
            ReverseDrawList list = {};
            auto op = [&]() {
                switch (which) {
                case 0: At<FillFn>(0x0007c820)(base + a, base + b, &value); break;
                case 1:
                case 2: {
                    // backwards into the same array, overlapping, the destination's end at or after the source's
                    ReverseDrawEntry *result = NULL;
                    int end = b + int(uint32_t(dest) % uint32_t(kEntries + 1 - b));
                    At<CopyBackFn>(which == 1 ? 0x0007c850 : 0x0007c890)(&result, base + a, base + b, base + end);
                    o->result = int32_t(result - base);
                    break;
                }
                case 3:
                    o->result = int32_t(uintptr_t(At<UcopyFn>(0x0007c8d0)(base + a, base + b,
                                                                           nullDest ? NULL : other + dest)) -
                                        (nullDest ? 0 : uintptr_t(other)));
                    break;
                case 4: At<UfillFn>(0x0007c900)(nullDest ? NULL : other + dest, count, &value); break;
                case 5: o->result = int32_t(((list.*At<MemberCopyFn>(0x0007c930))(base + a, base + b, other + dest)) - other); break;
                case 6: o->result = int32_t(((list.*At<MemberFillFn>(0x0007c960))(other + dest, count, &value)) - other); break;
                case 7:
                    list.first = nullDest ? NULL : base + a;
                    list.last = base + b;
                    o->result = int32_t((list.*At<SizeFn>(0x0007c7d0))());
                    break;
                case 8: {
                    ReverseDrawEntry *block = nullDest ? NULL : static_cast<ReverseDrawEntry *>(
                        UMemory::FastAlloc(blockCount * sizeof(ReverseDrawEntry), "STL"));
                    (list.*At<DeallocateFn>(0x0007c800))(block, blockCount);
                    break;
                }
                case 9: {
                    list.first = static_cast<ReverseDrawEntry *>(UMemory::FastAlloc(blockCount * sizeof(ReverseDrawEntry), "STL"));
                    list.last = list.first + blockCount / 2;
                    list.end = list.first + blockCount;
                    (list.*At<TidyFn>(0x0007c990))();
                    memcpy(o->list, &list, sizeof(o->list));
                    break;
                }
                }
            };
            faulted[side] = !RunSide(side == 0, kQueueRange, &op);
        }
        g_cases++;
        if (faulted[0] || faulted[1]) {
            // both faulting alike is agreement
            g_checks++;
            if (faulted[0] != faulted[1])
                Fault(kNames[which], i);
            continue;
        }
        Compare(kNames[which], i, &out[0], &out[1], sizeof(Out));
        Compare(kNames[which], i, arrays[0], arrays[1], sizeof(arrays[0]));
    }
    // TextureDofState: only while the TextureDOF method has its shaders (a child then copies its packets)
    EAGL::RenderMethod *parent = reinterpret_cast<EAGL::RenderMethod *>(uintptr_t(0x00241460));
    if (parent->vertexShaders == NULL) {
        printf("[camdata] the TextureDOF method has no shaders yet: TextureDofState not tested\n");
        return;
    }
    struct Out {
        TextureDofState state;
        EAGL::RenderMethod method;
    } out[2];
    for (int side = 0; side < 2; side++) {
        TextureDofState state;
        memset(&state, 0xcd, sizeof(state));
        auto make = [&]() { (state.*At<DofConstructFn>(0x0007c6e0))(); };
        if (!RunSide(side == 0, kQueueRange, &make)) {
            Fault("TextureDofState::Construct", 0);
            return;
        }
        out[side].state = state;
        out[side].method = *state.method;
        out[side].state.method = NULL;
        out[side].method.packets = NULL;
        auto destroy = [&]() { (state.*At<DofDestructFn>(0x0007c7b0))(); };
        if (!RunSide(side == 0, kQueueRange, &destroy))
            Fault("TextureDofState::Destruct", 0);
    }
    g_cases++;
    Compare("TextureDofState", 0, &out[0], &out[1], sizeof(Out));
}

} // namespace

void CamDataShadow_Run(void) {
    const char *on = getenv("NIGHTFIRE_CAMDATASHADOW");
    if (on == NULL || atoi(on) == 0)
        return;
    Rng rng = { 0x5eed1234 };
    MathCases(rng);
    SplineCases(rng);
    StrayCases(rng);

    // camera.ini is in the mission archive, closed before the main loop: opened again for these, closed after
    bool reopened = false;
    uint8_t logRequests = ShadowArchiveLogRequests;
    if (ShadowArchiveOpen == 0 && ShadowArchiveName[0] != 0) {
        char name[256];
        strcpy(name, ShadowArchiveName);
        reopened = UFileLoader::StartUsingBigFile("driving", name, false);
    }
    LoadFileCases(rng);
    if (reopened)
        UFileLoader::StopUsingBigFile();
    ShadowArchiveLogRequests = logRequests;
    SmallIniCases(rng);

    InstallFakes();
    QueueCases(rng);
    StateCases(rng);
    HooksRemove();
    RendererCases(rng);

    printf("[camdata] RCameraMath, RCameraSpline, RCameraIniLoader, RDirectorQueue, RPlayerCamState: %d cases, %d "
           "checks, %d differ (%d faulted)\n", g_cases, g_checks, g_diffs, g_faults);
    fflush(stdout);
}
