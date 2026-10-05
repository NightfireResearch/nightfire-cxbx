#pragma fp_contract(off)
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "GeomShadow.h"
#include "FpControl.h"

#include "../engine/OBB.h"
#include "../engine/PhysicsUtil.h"
#include "../engine/UMemory.hpp"
#include "../world/WorldMath.h"
#include "../world/WorldPos.h"
#include "../platform/RealMath.h"
#include "../../common/xbeOriginal.h"
#include "../../helpers.h"

#include <windows.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// NIGHTFIRE_GEOMSHADOW=1. The ORIGINAL (its entry bytes swapped back for the call, common/xbeOriginal.h - the
// whole package at once, so an original's calls into the package reach originals too) and the PORT of each
// function, on identical inputs, compared bit for bit: every output float, the inputs (in case either writes
// them), untouched output words (filled with a pattern first), the return value (doubles at full precision;
// returned pointers relative to the buffer).
//
// GeomShadow_Run, at injection time: the pure functions, on random inputs in four styles per function - small
// integers (which make parallel, collinear, zero-length and exactly-touching cases common), moderate randoms,
// floats of any size with raw bit patterns (NaN, infinities, denormals, huge), and structured cases (unit
// directions, rotation matrices, boxes that nearly touch, rays aimed at spheres). Util_GaussRandom and the two
// functions that call it run with the C runtime's rand jumped to a resettable fake (both sides start from the same
// generator state and the same kept second number), and the state after is compared too. WWorldPos::HeightAtPoint
// and the constructor are pure enough to run here (the face normal at 0x0005d3f0 is plain maths).
//
// GeomShadow_RunWorld, at the first simulation tick: MakeFaceAtPoint and the three FindClosestFace searches over the
// loaded track, at points scattered about the player's car (or the origin), with real instance lists from
// WCollisionMgr::GetInstanceList; the collision manager's 0x34 bytes are put back before each side and compared
// after, with the WWorldPos and the answers. HeightAtPoint on the faces found.
//
// A difference where both sides are NaN is counted apart (the x87 and SSE pick NaN payloads differently).
// Not covered: BarrierList_Deallocate (four lines, a pool free). Mutations this catches, by reading: IntersectCircle
// computing b * b from the unrounded b alone changes the discriminant's low bits on most random cases;
// Util_GenerateCarTensor adding the unrounded x squared in its third term changes z.
// ---------------------------------------------------------------------------------------------------------------

namespace {

// ---- the package's code, swapped back as a whole for the originals

const uint32_t kRanges[][2] = {
    { 0x00037a70, 0x00037a80 }, { 0x000ac870, 0x000acde0 }, { 0x000bce10, 0x000bd750 },
    { 0x000d1060, 0x000d11a0 }, { 0x000d2860, 0x000d32f0 },
};
int g_restoredCount;

struct OriginalsWindow {
    OriginalsWindow() {
        int n = 0;
        for (const auto &r : kRanges)
            n += XbeOriginal_RestoreRange(r[0], r[1], true);
        g_restoredCount = n;
    }
    ~OriginalsWindow() {
        for (const auto &r : kRanges)
            XbeOriginal_RestoreRange(r[0], r[1], false);
    }
};

// ---- the originals' types (thiscall through __fastcall with a dummy EDX)

typedef double (*DoubleFFF)(float, float, float);
typedef float (*FloatFFF)(float, float, float);
typedef MATRIX4 *(*GenerateMatrixFn)(MATRIX4 *, const Coord3 *);
typedef Coord3 *(*TensorFn)(Coord3 *, float, float, float, float);
typedef double (*DoubleVoid)(void);
typedef Coord3 *(*PerturbAnglesFn)(Coord3 *, float, float, const Coord4 *);
typedef Coord3 *(*PerturbRandomFn)(Coord3 *, const Coord4 *, float);
typedef void (*PoleFn)(const Coord3 *, float *, float *);
typedef uint8_t (*CharMapFn)(float, float, const char *, int, int);
typedef void (*SegmentDirectionFn)(const Coord4 *, const Coord4 *, float *, Coord4 *);
typedef double (*DoubleFF)(float, float);
typedef void (*NearestOnSegmentFn)(const Coord4 *, const Coord4 *, float, const Coord4 *, Coord4 *);
typedef uint8_t (*RaySphereFn)(const Coord4 *, const Coord4 *, const Coord4 *, float, Coord3 *);
typedef uint8_t (*RayStretchedFn)(const Coord4 *, const Coord4 *, const Coord4 *, float, float, Coord3 *);
typedef OBB *(__fastcall *ObbConstructFn)(OBB *, int);
typedef OBB *(__fastcall *ObbConstructFromFn)(OBB *, int, const MATRIX4 *, const Coord4 *, const Coord4 *);
typedef void (__fastcall *ObbResetFn)(OBB *, int, const MATRIX4 *, const Coord4 *, const Coord4 *);
typedef uint8_t (__fastcall *ObbCheckFn)(OBB *, int, const OBB *);
typedef uint8_t (*SegmentsFn)(const Coord4 *, const Coord4 *, Coord4 *);
typedef uint8_t (*CircleFn)(float, float, float, float, float, float, float, float *, float *);
typedef void (*NearestLineFn)(const Coord4 *, const Coord4 *, Coord4 *);
typedef double (*PlaneYFn)(const Coord3 *, const Coord3 *, const Coord3 *);
typedef uint8_t (*SegPlaneFn)(const Coord4 *, const Coord4 *, const Coord3 *, const Coord4 *, Coord4 *, float *);
typedef uint8_t (*SegSpaceFn)(const Coord4 *, const Coord4 *, MATRIX4 *);
typedef WWorldPos *(__fastcall *PosConstructFn)(WWorldPos *, int);
typedef void (__fastcall *PosMakeFaceFn)(WWorldPos *, int, const Coord3 *);
typedef double (__fastcall *PosHeightFn)(WWorldPos *, int, const Coord3 *, bool);
typedef uint8_t (__fastcall *PosFindListFn)(WWorldPos *, int, const InstanceList *, const Coord3 *);
typedef uint8_t (__fastcall *PosFindSegmentFn)(WWorldPos *, int, const InstanceList *, const Coord4 *,
                                               const Coord4 *);
typedef uint8_t (__fastcall *PosFindFn)(WWorldPos *, int, const Coord3 *, bool);
typedef void (__fastcall *GetInstanceListFn)(void *, int, InstanceList *, const Coord3 *, int, int, int);

// The original at its address in the game's image
template <class F>
F Original(uint32_t address) {
    return reinterpret_cast<F>(uintptr_t(address));
}

// ---- counters and random numbers

int g_cases, g_checks, g_differ, g_nanOnly, g_faults, g_details;
uint32_t g_seed = 0x6e47f00d;

uint32_t Next() {
    g_seed ^= g_seed << 13;
    g_seed ^= g_seed >> 17;
    g_seed ^= g_seed << 5;
    return g_seed;
}
float Bits(uint32_t u) {
    float f;
    memcpy(&f, &u, 4);
    return f;
}
uint32_t U(float f) {
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}
bool IsNaN(uint32_t u) {
    return (u & 0x7f800000) == 0x7f800000 && (u & 0x007fffff) != 0;
}
float Uniform(float lo, float hi) {
    return lo + (hi - lo) * float(Next() & 0xffffff) / 16777216.0f;
}

float AnyFloat() {
    static const uint32_t specials[] = { 0x00000000, 0x80000000, 0x00000001, 0x80000001, 0x00800000, 0x3f800000,
                                         0xbf800000, 0x7f7fffff, 0xff7fffff, 0x7f800000, 0xff800000, 0x7fc00000,
                                         0xffc00000, 0x7fa00000, 0x3f000000, 0x38d1b717, 0xb8d1b717, 0x40800000,
                                         0x40a00000, 0x3f8ccccd, 0x4cbebc20, 0x7e967699 };
    uint32_t r = Next() % 100;
    if (r < 40) {
        float m = float(Next() & 0xffffff) / 16777216.0f;
        int e = int(Next() % 25) - 12;
        float v = m * powf(10.0f, float(e));
        return (Next() & 1) ? -v : v;
    }
    if (r < 60)
        return Uniform(-1.0f, 1.0f);
    if (r < 75)
        return Bits(specials[Next() % (sizeof(specials) / 4)]);
    if (r < 85)
        return Bits(Next());
    return float(int(Next() % 21) - 10);
}

// A coordinate in one of the styles: 0 small integers, 1 moderate, 2 anything, 3 large but finite
float Coordinate(int style) {
    switch (style) {
    case 0: return float(int(Next() % 7) - 3);
    case 1: return Uniform(-10.0f, 10.0f);
    case 2: return AnyFloat();
    default: return Uniform(-1.0f, 1.0f) * powf(10.0f, float(Next() % 30));
    }
}

void UnitVector(float *v) {
    float x = Uniform(-1, 1), y = Uniform(-1, 1), z = Uniform(-1, 1);
    double n = sqrt(double(x) * x + double(y) * y + double(z) * z);
    if (n < 1e-3)
        x = 0.0f, y = 1.0f, z = 0.0f, n = 1.0;
    v[0] = float(x / n);
    v[1] = float(y / n);
    v[2] = float(z / n);
}

void RotationMatrix(float *m) {
    float q[4];
    UnitVector(q);
    float angle = Uniform(-3.14159f, 3.14159f);
    double s = sin(angle * 0.5), c = cos(angle * 0.5);
    double x = q[0] * s, y = q[1] * s, z = q[2] * s, w = c;
    float r[16] = { float(1 - 2 * (y * y + z * z)), float(2 * (x * y + w * z)), float(2 * (x * z - w * y)), 0,
                    float(2 * (x * y - w * z)), float(1 - 2 * (x * x + z * z)), float(2 * (y * z + w * x)), 0,
                    float(2 * (x * z + w * y)), float(2 * (y * z - w * x)), float(1 - 2 * (x * x + y * y)), 0,
                    0, 0, 0, 1 };
    memcpy(m, r, sizeof(r));
}

// ---- the test bed: one buffer per side, inputs and outputs in it, compared word for word

struct Work {
    alignas(16) float f[128];
    uint64_t ret;
    uint32_t mask[4];   // words (bit per word, f[0..127]) not compared
};

const uint64_t kFaulted = 0xfa17fa17fa17fa17ull;

typedef void (*Generator)(Work *w, int style);
typedef void (*Body)(Work *w, bool original);

void Fill(Work *w) {
    for (int i = 0; i < 128; i++)
        w->f[i] = Bits(0x5a5a0000u | uint32_t(i));
    w->ret = 0x0123456789abcdefull;
    memset(w->mask, 0, sizeof(w->mask));
}

void Mask(Work *w, int word) {
    w->mask[word >> 5] |= 1u << (word & 31);
}

uint64_t D(double d) {
    uint64_t u;
    memcpy(&u, &d, 8);
    return u;
}

uint64_t Offset(const void *p, const Work *w) {
    return uint64_t(uintptr_t(p) - uintptr_t(w));
}

void Guarded(Body body, Work *w, bool original) {
#ifdef _MSC_VER
    __try {
        body(w, original);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        w->ret = kFaulted;
        g_faults++;
    }
#else
    body(w, original);
#endif
}

void Detail(const char *line) {
    if (g_details++ < 16)
        printf("[geomshadow] %s\n", line);
}

void Compare(const char *name, int index, const Work &o, const Work &p) {
    g_checks++;
    bool differ = false, nanOnly = true;
    int firstWord = -1;
    for (int i = 0; i < 128; i++) {
        if (o.mask[i >> 5] & (1u << (i & 31)))
            continue;
        uint32_t a = U(o.f[i]), b = U(p.f[i]);
        if (a == b)
            continue;
        differ = true;
        if (firstWord < 0)
            firstWord = i;
        if (!(IsNaN(a) && IsNaN(b)))
            nanOnly = false;
    }
    if (o.ret != p.ret) {
        differ = true;
        double a, b;
        memcpy(&a, &o.ret, 8);
        memcpy(&b, &p.ret, 8);
        if (!(a != a && b != b))
            nanOnly = false;
    }
    if (!differ)
        return;
    if (nanOnly) {
        g_nanOnly++;
        return;
    }
    g_differ++;
    char line[256];
    if (firstWord >= 0)
        snprintf(line, sizeof(line), "%s case %d: f[%d] %08x / %08x, ret %016llx / %016llx", name, index, firstWord,
                 U(o.f[firstWord]), U(p.f[firstWord]), (unsigned long long)o.ret, (unsigned long long)p.ret);
    else
        snprintf(line, sizeof(line), "%s case %d: ret %016llx / %016llx", name, index, (unsigned long long)o.ret,
                 (unsigned long long)p.ret);
    Detail(line);
}

void Test(const char *name, int cases, Generator gen, Body body) {
    static Work o, p;
    for (int i = 0; i < cases; i++) {
        Fill(&o);
        gen(&o, i & 3);
        p = o;
        {
            OriginalsWindow window;
            Guarded(body, &o, true);
        }
        Guarded(body, &p, false);
        Compare(name, i, o, p);
        g_cases++;
    }
}

// ---- the C runtime's rand jumped to a fake with a state we set (the real one is per-thread CRT data)

struct Hook {
    uint32_t at;
    uint8_t saved[5];
    bool on;
};
Hook g_randHook;
uint32_t g_rand;

int FakeRand() {
    g_rand = g_rand * 0x343fd + 0x269ec3;
    return int((g_rand >> 16) & 0x7fff);
}

bool HookInstall(Hook *h, uint32_t at, const void *to) {
    h->at = at;
    h->on = false;
    DWORD old;
    if (!VirtualProtect((void *)uintptr_t(at), 5, PAGE_EXECUTE_READWRITE, &old))
        return false;
    memcpy(h->saved, (void *)uintptr_t(at), 5);
    uint8_t jump[5] = { 0xe9 };
    int32_t rel = int32_t(uint32_t(uintptr_t(to)) - (at + 5));
    memcpy(jump + 1, &rel, 4);
    memcpy((void *)uintptr_t(at), jump, 5);
    VirtualProtect((void *)uintptr_t(at), 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)uintptr_t(at), 5);
    h->on = true;
    return true;
}

void HookRemove(Hook *h) {
    if (!h->on)
        return;
    DWORD old;
    VirtualProtect((void *)uintptr_t(h->at), 5, PAGE_EXECUTE_READWRITE, &old);
    memcpy((void *)uintptr_t(h->at), h->saved, 5);
    VirtualProtect((void *)uintptr_t(h->at), 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)uintptr_t(h->at), 5);
    h->on = false;
}

// Util_GaussRandom's kept number: f[100] the generator state, f[101] the kept value, f[102] its flag; after the
// call the same words hold the state after.
#define GaussSpareValue FLOAT_AT(0x00239a60)
#define GaussSpareFlag U8_AT(0x00239a64)

void RandomState(Work *w) {
    w->f[100] = Bits(Next());
    w->f[101] = Next() % 3 == 0 ? AnyFloat() : Uniform(-3, 3);
    w->f[102] = Bits(Next() % 3 == 0 ? 1u : 0u);
}
void SetRandom(const Work *w) {
    g_rand = U(w->f[100]);
    GaussSpareValue = w->f[101];
    GaussSpareFlag = uint8_t(U(w->f[102]));
}
void TakeRandom(Work *w) {
    w->f[100] = Bits(g_rand);
    w->f[101] = GaussSpareValue;
    w->f[102] = Bits(GaussSpareFlag);
}

// ---- the register-argument region fit: the original and our adapter called the same way. Answers 0 when EBX,
// ESI and EDI came back as they went in.

__declspec(naked) uint32_t CallRegionFit(uint32_t, uint32_t, const Coord4 *, Coord4 *, float *, float,
                                         const Coord4 *) {
    __asm {
        push ebp
        mov ebp, esp
        push ebx
        push esi
        push edi
        push 0x7e7e7e7e
        push dword ptr [ebp + 32]
        push dword ptr [ebp + 28]
        mov eax, dword ptr [ebp + 12]
        mov esi, dword ptr [ebp + 16]
        mov edi, dword ptr [ebp + 20]
        mov ebx, dword ptr [ebp + 24]
        call dword ptr [ebp + 8]
        add esp, 12
        mov eax, ebx
        xor eax, dword ptr [ebp + 24]
        mov ecx, esi
        xor ecx, dword ptr [ebp + 16]
        or eax, ecx
        mov ecx, edi
        xor ecx, dword ptr [ebp + 20]
        or eax, ecx
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret
    }
}

// ---- generators and bodies

void GenScalars(Work *w, int style) {
    for (int i = 0; i < 8; i++)
        w->f[i] = Coordinate(style);
}

void BodyStep(Work *w, bool original) {
    w->ret = D((original ? Original<DoubleFFF>(0x000bce10) : Util_Step)(w->f[0], w->f[1], w->f[2]));
}

void BodyBound(Work *w, bool original) {
    w->ret = U((original ? Original<FloatFFF>(0x000bce50) : Util_Bound)(w->f[0], w->f[1], w->f[2]));
}

void GenDirection(Work *w, int style) {
    if (style == 3 || style == 0)
        UnitVector(w->f);
    else
        GenScalars(w, style);
    if (Next() % 8 == 0)
        w->f[0] = w->f[2] = 0.0f;     // straight up or down: the cross product with up vanishes
}

void BodyGenerateMatrix(Work *w, bool original) {
    MATRIX4 *m = reinterpret_cast<MATRIX4 *>(&w->f[16]);
    const Coord3 *d = reinterpret_cast<const Coord3 *>(&w->f[0]);
    w->ret = Offset((original ? Original<GenerateMatrixFn>(0x000bce90) : Util_GenerateMatrix)(m, d), w);
    Mask(w, 16 + 3);    // rows 0 and 1's w: the original's stack leftovers
    Mask(w, 16 + 7);
}

void BodyTensor(Work *w, bool original) {
    Coord3 *out = reinterpret_cast<Coord3 *>(&w->f[8]);
    w->ret = Offset((original ? Original<TensorFn>(0x000bcf40) : Util_GenerateTensor)(out, w->f[0], w->f[1], w->f[2], w->f[3]),
                    w);
}

void BodyCarTensor(Work *w, bool original) {
    Coord3 *out = reinterpret_cast<Coord3 *>(&w->f[8]);
    w->ret = Offset((original ? Original<TensorFn>(0x000bd030) : Util_GenerateCarTensor)(out, w->f[0], w->f[1], w->f[2],
                                                                                w->f[3]), w);
}

void GenRandomState(Work *w, int style) {
    GenScalars(w, style);
    if (style == 0 || style == 3)
        UnitVector(&w->f[4]);
    else
        for (int i = 4; i < 8; i++)
            w->f[i] = Coordinate(style);
    RandomState(w);
}

void BodyGauss(Work *w, bool original) {
    SetRandom(w);
    DoubleVoid gauss = original ? Original<DoubleVoid>(0x000bd130) : Util_GaussRandom;
    double first = gauss();
    double second = gauss();
    memcpy(&w->f[110], &first, 8);
    w->ret = D(second);
    TakeRandom(w);
}

void BodyPerturbRandom(Work *w, bool original) {
    SetRandom(w);
    Coord3 *out = reinterpret_cast<Coord3 *>(&w->f[8]);
    const Coord4 *d = reinterpret_cast<const Coord4 *>(&w->f[4]);
    w->ret = Offset((original ? Original<PerturbRandomFn>(0x000bd250) : (PerturbRandomFn)Util_PerturbVector)(out, d, w->f[0]),
                    w);
    TakeRandom(w);
}

void BodyApplyVariance(Work *w, bool original) {
    SetRandom(w);
    w->ret = D((original ? Original<DoubleFF>(0x000bd450) : Util_ApplyVariance)(w->f[0], w->f[1]));
    TakeRandom(w);
}

void BodyPerturbAngles(Work *w, bool original) {
    Coord3 *out = reinterpret_cast<Coord3 *>(&w->f[8]);
    const Coord4 *d = reinterpret_cast<const Coord4 *>(&w->f[4]);
    PerturbAnglesFn fn = original ? Original<PerturbAnglesFn>(0x000bd1e0) : (PerturbAnglesFn)Util_PerturbVector;
    w->ret = Offset(fn(out, w->f[0], w->f[1], d), w);
}

void BodyPole(Work *w, bool original) {
    (original ? Original<PoleFn>(0x000bd2e0) : Util_VecToPoleDepthAndLong)(reinterpret_cast<const Coord3 *>(&w->f[0]),
                                                                 &w->f[4], &w->f[5]);
}

char g_charMap[0x10000];

void GenCharMap(Work *w, int style) {
    w->f[0] = style == 2 ? Uniform(-0.25f, 1.25f) : Uniform(0.0f, 1.0f);
    w->f[1] = style == 2 ? Uniform(-0.25f, 1.25f) : Uniform(0.0f, 1.0f);
    if (style == 0) {
        w->f[0] = float(Next() % 5) * 0.25f;   // exact halves round to even
        w->f[1] = float(Next() % 5) * 0.25f;
    }
    w->f[2] = Bits(1 + Next() % 64);
    w->f[3] = Bits(1 + Next() % 64);
}

void BodyCharMap(Work *w, bool original) {
    const char *map = g_charMap + 0x8000;
    int width = int(U(w->f[2])), height = int(U(w->f[3]));
    w->ret = original ? (Original<CharMapFn>(0x000bd390))(w->f[0], w->f[1], map, width, height)
                      : uint8_t(Util_CharMapLookup(w->f[0], w->f[1], map, width, height));
}

void GenVectors(Work *w, int style) {
    for (int i = 0; i < 24; i++)
        w->f[i] = Coordinate(style);
    if (Next() % 6 == 0)
        memcpy(&w->f[4], &w->f[0], 16);    // zero length
}

void BodySegmentDirection(Work *w, bool original) {
    const Coord4 *from = reinterpret_cast<const Coord4 *>(&w->f[0]);
    const Coord4 *to = reinterpret_cast<const Coord4 *>(&w->f[4]);
    Coord4 *direction = reinterpret_cast<Coord4 *>(&w->f[12]);
    (original ? Original<SegmentDirectionFn>(0x000bd3e0) : Util_SegmentDirection)(from, to, &w->f[8], direction);
}

void GenNearestOnSegment(Work *w, int style) {
    GenVectors(w, style);
    if (style != 2) {
        double l2 = double(w->f[12]) * w->f[12] + double(w->f[13]) * w->f[13] + double(w->f[14]) * w->f[14];
        w->f[8] = l2 == 0 ? 0.0f : float(1.0 / l2);
    }
}

void BodyNearestOnSegment(Work *w, bool original) {
    const Coord4 *v = reinterpret_cast<const Coord4 *>(w->f);
    Coord4 *out = reinterpret_cast<Coord4 *>(&w->f[16]);
    (original ? Original<NearestOnSegmentFn>(0x000bd470) : Util_NearestPointOnSegment)(&v[0], &v[1], w->f[8], &v[3], out);
}

// origin f0, direction f4, centre f8, radius f12, stretch f13, hit f16
void GenRay(Work *w, int style) {
    GenVectors(w, style);
    if (style == 3 || style == 1) {
        for (int i = 0; i < 3; i++)
            w->f[8 + i] = Uniform(-20, 20);
        w->f[12] = Uniform(0.5f, 8.0f);
        w->f[13] = Uniform(0.25f, 4.0f);
        // aimed at the centre, give or take
        for (int i = 0; i < 3; i++)
            w->f[4 + i] = w->f[8 + i] - w->f[i] + Uniform(-3, 3);
    }
}

void BodyRaySphere(Work *w, bool original) {
    const Coord4 *v = reinterpret_cast<const Coord4 *>(w->f);
    Coord3 *hit = reinterpret_cast<Coord3 *>(&w->f[16]);
    w->ret = original ? (Original<RaySphereFn>(0x000bd500))(&v[0], &v[1], &v[2], w->f[12], hit)
                      : Util_CollideRayWithSphere(&v[0], &v[1], &v[2], w->f[12], hit);
}

void BodyRayStretched(Work *w, bool original) {
    const Coord4 *v = reinterpret_cast<const Coord4 *>(w->f);
    Coord3 *hit = reinterpret_cast<Coord3 *>(&w->f[16]);
    w->ret = original ? (Original<RayStretchedFn>(0x000bd5d0))(&v[0], &v[1], &v[2], w->f[12], w->f[13], hit)
                      : Util_CollideRayWithStretchedSphere(&v[0], &v[1], &v[2], w->f[12], w->f[13], hit);
}

// moved f0 (raw word: only its low byte counts), position f4, centre out f8, radius out f12, radius f13, previous f16
void GenRegion(Work *w, int style) {
    GenVectors(w, style);
    w->f[0] = Bits(Next() & (Next() % 2 ? 0xffffff00u : 0xffffffffu));
}

void BodyRegion(Work *w, bool original) {
    uint32_t target = uint32_t(uintptr_t(original ? Original<void (*)()>(0x000bd690) : &FUN_000bd690));
    const Coord4 *v = reinterpret_cast<const Coord4 *>(w->f);
    uint32_t registers = CallRegionFit(target, U(w->f[0]), &v[1], reinterpret_cast<Coord4 *>(&w->f[8]), &w->f[12],
                                       w->f[13], &v[4]);
    w->ret = registers;
}

// ---- OBB: box A at f[0..43], box B at f[44..87], a matrix at f[88..103], a position f[104], half extents f[108]

OBB *BoxA(Work *w) { return reinterpret_cast<OBB *>(&w->f[0]); }
OBB *BoxB(Work *w) { return reinterpret_cast<OBB *>(&w->f[44]); }
MATRIX4 *BoxMatrix(Work *w) { return reinterpret_cast<MATRIX4 *>(&w->f[88]); }

void BoxInputs(float *m, float *position, float *extents, int style) {
    if (style == 2) {
        for (int i = 0; i < 16; i++)
            m[i] = AnyFloat();
        for (int i = 0; i < 4; i++)
            position[i] = AnyFloat(), extents[i] = AnyFloat();
        return;
    }
    if (style == 0)
        for (int i = 0; i < 16; i++)
            m[i] = (i % 5 == 0) ? 1.0f : 0.0f;    // axis-aligned: faces exactly touching happen
    else
        RotationMatrix(m);
    for (int i = 0; i < 3; i++) {
        position[i] = style == 0 ? float(int(Next() % 9) - 4) : Uniform(-6, 6);
        extents[i] = style == 0 ? float(1 + Next() % 3) : Uniform(0.2f, 4.0f);
    }
    position[3] = 1.0f;
    extents[3] = 0.0f;
}

void GenBoxInputs(Work *w, int style) {
    for (int i = 0; i < 88; i++)
        w->f[i] = Next() % 4 == 0 ? AnyFloat() : Bits(0x5a5a0000u | uint32_t(i));
    BoxInputs(&w->f[88], &w->f[104], &w->f[108], style);
}

void GenBoxes(Work *w, int style) {
    float m[16], p[4], e[4];
    for (int box = 0; box < 2; box++) {
        BoxInputs(m, p, e, style);
        OBB *b = box == 0 ? BoxA(w) : BoxB(w);
        b->Reset(reinterpret_cast<const MATRIX4 *>(m), reinterpret_cast<const Coord4 *>(p),
                 reinterpret_cast<const Coord4 *>(e));
        if (Next() % 3 == 0)
            b->penetration = Uniform(-10, 1);
        b->secondPass = uint8_t(Next());
    }
}

void BodyObbConstruct(Work *w, bool original) {
    w->ret = Offset(original ? (Original<ObbConstructFn>(0x00037a70))(BoxA(w), 0) : BoxA(w)->Construct(), w);
}

void BodyObbConstructFrom(Work *w, bool original) {
    const Coord4 *p = reinterpret_cast<const Coord4 *>(&w->f[104]), *e = reinterpret_cast<const Coord4 *>(&w->f[108]);
    OBB *r = original ? (Original<ObbConstructFromFn>(0x000ac870))(BoxA(w), 0, BoxMatrix(w), p, e)
                      : BoxA(w)->Construct(BoxMatrix(w), p, e);
    w->ret = Offset(r, w);
}

void BodyObbReset(Work *w, bool original) {
    const Coord4 *p = reinterpret_cast<const Coord4 *>(&w->f[104]), *e = reinterpret_cast<const Coord4 *>(&w->f[108]);
    if (original)
        (Original<ObbResetFn>(0x000ac970))(BoxA(w), 0, BoxMatrix(w), p, e);
    else
        BoxA(w)->Reset(BoxMatrix(w), p, e);
}

void BodyObbOverlap(Work *w, bool original) {
    w->ret = original ? (Original<ObbCheckFn>(0x000aca70))(BoxA(w), 0, BoxB(w)) : BoxA(w)->CheckOBBOverlap(BoxB(w));
}

void BodyObbIntersection(Work *w, bool original) {
    w->ret = original ? (Original<ObbCheckFn>(0x000acb90))(BoxA(w), 0, BoxB(w))
                      : BoxA(w)->CheckOBBOverlapAndFindIntersection(BoxB(w));
}

// ---- WWorldMath: segments a f[0..7], b f[8..15], hit f[16..19], f[20] bit 0 = hit NULL

void GenSegments(Work *w, int style) {
    for (int i = 0; i < 16; i++)
        w->f[i] = Coordinate(style);
    uint32_t r = Next() % 8;
    if (r == 0) {           // parallel: b is a shifted
        float dx = Coordinate(style), dz = Coordinate(style);
        for (int i = 0; i < 8; i += 4)
            w->f[8 + i] = w->f[i] + dx, w->f[10 + i] = w->f[2 + i] + dz;
    } else if (r == 1) {    // zero length
        memcpy(&w->f[4], &w->f[0], 16);
    } else if (r == 2) {    // sharing an end
        memcpy(&w->f[8], &w->f[4], 16);
    }
    w->f[20] = Bits(Next() % 5 == 0);
}

void BodyFasterSegments(Work *w, bool original) {
    const Coord4 *v = reinterpret_cast<const Coord4 *>(w->f);
    Coord4 *hit = U(w->f[20]) ? NULL : reinterpret_cast<Coord4 *>(&w->f[16]);
    w->ret = original ? (Original<SegmentsFn>(0x000d1060))(&v[0], &v[2], hit) : FasterSegmentIntersect(&v[0], &v[2], hit);
}

void BodySegments(Work *w, bool original) {
    const Coord4 *v = reinterpret_cast<const Coord4 *>(w->f);
    Coord4 *hit = U(w->f[20]) ? NULL : reinterpret_cast<Coord4 *>(&w->f[16]);
    w->ret = original ? (Original<SegmentsFn>(0x000d2860))(&v[0], &v[2], hit) : WWorldMath::SegmentIntersect(&v[0], &v[2], hit);
}

// f0..f6 the segment, centre and radius; t1 f8, t2 f9 (or both f8 when f10's bit is set)
void GenCircle(Work *w, int style) {
    for (int i = 0; i < 7; i++)
        w->f[i] = Coordinate(style);
    if (style != 2)
        w->f[6] = fabsf(w->f[6]);
    if (Next() % 6 == 0) {      // a segment inside the circle
        w->f[2] = w->f[0];
        w->f[3] = w->f[1];
        w->f[4] = w->f[0];
        w->f[5] = w->f[1];
        w->f[6] = 1.0f;
    }
    w->f[10] = Bits(Next() % 8 == 0);
}

void BodyCircle(Work *w, bool original) {
    float *t1 = &w->f[8], *t2 = U(w->f[10]) ? &w->f[8] : &w->f[9];
    if (original)
        w->ret = (Original<CircleFn>(0x000d29e0))(w->f[0], w->f[1], w->f[2], w->f[3], w->f[4], w->f[5], w->f[6], t1, t2);
    else
        w->ret = WWorldMath::IntersectCircle(w->f[0], w->f[1], w->f[2], w->f[3], w->f[4], w->f[5], w->f[6], t1, t2);
}

void BodyNearest2D(Work *w, bool original) {
    const Coord4 *v = reinterpret_cast<const Coord4 *>(w->f);
    (original ? Original<NearestLineFn>(0x000d2b70) : WWorldMath::NearestPointLine2D)(&v[0], &v[1],
                                                                            reinterpret_cast<Coord4 *>(&w->f[12]));
}

void BodyNearest3D(Work *w, bool original) {
    const Coord4 *v = reinterpret_cast<const Coord4 *>(w->f);
    (original ? Original<NearestLineFn>(0x000d2cf0) : WWorldMath::NearestPointLine3D)(&v[0], &v[1],
                                                                            reinterpret_cast<Coord4 *>(&w->f[12]));
}

void GenPlane(Work *w, int style) {
    GenVectors(w, style);
    if (style == 3 || style == 1)
        UnitVector(&w->f[12]);
    if (Next() % 6 == 0)
        w->f[13] = 0.0f;   // a vertical plane: horizontal normal
    if (Next() % 6 == 0)
        w->f[1] = 0.0f;
}

void BodyPlaneY(Work *w, bool original) {
    const Coord3 *n = reinterpret_cast<const Coord3 *>(&w->f[0]), *p = reinterpret_cast<const Coord3 *>(&w->f[4]),
                 *q = reinterpret_cast<const Coord3 *>(&w->f[8]);
    w->ret = D((original ? Original<PlaneYFn>(0x000d2be0) : WWorldMath::GetPlaneY)(n, p, q));
}

void BodySegPlane(Work *w, bool original) {
    const Coord4 *from = reinterpret_cast<const Coord4 *>(&w->f[0]), *to = reinterpret_cast<const Coord4 *>(&w->f[4]),
                 *normal = reinterpret_cast<const Coord4 *>(&w->f[12]);
    const Coord3 *point = reinterpret_cast<const Coord3 *>(&w->f[8]);
    Coord4 *hit = reinterpret_cast<Coord4 *>(&w->f[16]);
    w->ret = original ? (Original<SegPlaneFn>(0x000d2c20))(from, to, point, normal, hit, &w->f[20])
                      : WWorldMath::IntersectSegPlane(from, to, point, normal, hit, &w->f[20]);
}

void GenSegSpace(Work *w, int style) {
    GenVectors(w, style);
    if (Next() % 5 == 0)
        for (int i = 4; i < 7; i++)
            w->f[i] = w->f[i - 4] + Uniform(-0.0002f, 0.0002f);   // within the tolerance, or just outside
    for (int i = 16; i < 32; i++)
        w->f[i] = Next() % 2 ? AnyFloat() : Bits(0x5a5a0000u | uint32_t(i));
}

void BodySegSpace(Work *w, bool original) {
    const Coord4 *from = reinterpret_cast<const Coord4 *>(&w->f[0]), *to = reinterpret_cast<const Coord4 *>(&w->f[4]);
    MATRIX4 *m = reinterpret_cast<MATRIX4 *>(&w->f[16]);
    w->ret = original ? (Original<SegSpaceFn>(0x000d2d70))(from, to, m) : WWorldMath::MakeSegSpaceMatrix(from, to, m);
}

// ---- WWorldPos's pure members: the object at f[0..15], a point at f[16]

WWorldPos *Pos(Work *w) { return reinterpret_cast<WWorldPos *>(&w->f[0]); }

void GenPos(Work *w, int style) {
    for (int i = 0; i < 16; i++)
        w->f[i] = Bits(Next());
    for (int i = 0; i < 12; i++)
        if (i % 4 != 3)
            w->f[i] = Coordinate(style);
    Pos(w)->valid = uint8_t(Next() % 4 == 0 ? Next() : Next() % 2);
    for (int i = 16; i < 20; i++)
        w->f[i] = Coordinate(style);
    w->f[20] = Bits(Next() & 1);
}

void BodyPosConstruct(Work *w, bool original) {
    w->ret = Offset(original ? (Original<PosConstructFn>(0x000d2fd0))(Pos(w), 0) : Pos(w)->Construct(), w);
}

void BodyPosHeight(Work *w, bool original) {
    const Coord3 *point = reinterpret_cast<const Coord3 *>(&w->f[16]);
    bool flag = U(w->f[20]) != 0;
    w->ret = D(original ? (Original<PosHeightFn>(0x000d2f90))(Pos(w), 0, point, flag) : Pos(w)->HeightAtPoint(point, flag));
}

bool Enabled() {
    const char *value = getenv("NIGHTFIRE_GEOMSHADOW");
    return value != NULL && value[0] != 0 && strcmp(value, "0") != 0;
}

void Summary(const char *what) {
    printf("[geomshadow] %s: %d cases, %d checks, %d differ (%d NaN-only, %d faults; %d entry points swapped)\n",
           what, g_cases, g_checks, g_differ, g_nanOnly, g_faults, g_restoredCount);
    fflush(stdout);
}

// ---- the world: a WWorldPos run by both sides from the same state, with the collision manager's bytes put back

const int kManagerBytes = 0x34;
#define CollisionManagerObject (*(uint8_t **)0x00239a70)
#define WCollisionMgr_GetInstanceList ((void (__fastcall *)(void *, int, InstanceList *, const Coord3 *, int, int, int))0x000c4510)

struct WorldSide {
    alignas(16) WWorldPos pos;
    uint8_t manager[kManagerBytes];
    uint64_t ret;
};

int g_worldFaces;

bool GuardedWorld(void (*body)(void *), void *context) {
#ifdef _MSC_VER
    __try {
        body(context);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_faults++;
        return false;
    }
#else
    body(context);
#endif
    return true;
}

struct WorldCase {
    int kind;                   // 0 MakeFaceAtPoint, 1 FindClosestFace(point, keep), 2 (list, point), 3 (list, from, to),
                                // 4 HeightAtPoint
    bool original;
    Coord4 point, from, to;
    bool keep;
    InstanceList *list;
    WorldSide *side;
};

void RunWorldCase(void *context) {
    WorldCase *c = static_cast<WorldCase *>(context);
    WWorldPos *pos = &c->side->pos;
    const Coord3 *point = reinterpret_cast<const Coord3 *>(&c->point);
    const Coord4 *from = &c->from, *to = &c->to;
    switch (c->kind) {
    case 0:
        if (c->original)
            (Original<PosMakeFaceFn>(0x000d2f10))(pos, 0, point);
        else
            pos->MakeFaceAtPoint(point);
        break;
    case 1:
        c->side->ret = c->original ? (Original<PosFindFn>(0x000d31f0))(pos, 0, point, c->keep) : pos->FindClosestFace(point, c->keep);
        break;
    case 2:
        c->side->ret = c->original ? (Original<PosFindListFn>(0x000d3050))(pos, 0, c->list, point)
                                   : pos->FindClosestFace(c->list, point);
        break;
    case 3:
        c->side->ret = c->original ? (Original<PosFindSegmentFn>(0x000d3110))(pos, 0, c->list, from, to)
                                   : pos->FindClosestFace(c->list, from, to);
        break;
    default:
        c->side->ret = D(c->original ? (Original<PosHeightFn>(0x000d2f90))(pos, 0, point, false)
                                     : pos->HeightAtPoint(point, false));
        break;
    }
}

const char *const kWorldNames[] = { "WWorldPos::MakeFaceAtPoint", "WWorldPos::FindClosestFace(point)",
                                    "WWorldPos::FindClosestFace(list, point)",
                                    "WWorldPos::FindClosestFace(list, segment)", "WWorldPos::HeightAtPoint" };

// Both sides from `start`; the manager's bytes restored before each and compared after.
//
// The manager's query stamp (+0x04) is the exception. GetInstanceList increments it and stamps every instance it
// visits with the new value, skipping instances that already carry it - and the instances are not restored. Put
// back to the same value, the second side's list would skip everything the first side's visited (and find no
// face). So the second side starts one stamp later, which no instance carries yet, and the stamps are compared by
// how far each side advanced them.
//
// The face's +0x0c word (corner[0]'s fourth word) is left out: FindFaceInCInst never writes it (MakeStripFace's
// VU0 transforms keep the destination's w), so both FindClosestFace searches copy it from their uninitialised
// candidate slot - a leftover stack word, different in every caller's frame. Nothing reads it: the face normal, the
// plane height, IntersectSegPlane and the point-in-triangle test read x, y and z (or x and z), and the game's own
// readers of a WWorldPos (GetGroundCollision, CheckHitWorld, GetWorldNormal, RigidBody::TempGetHeightInformation,
// RScorchWorld) read only the tag at +0x2c, the strip flags at +0x1c and the vertices' x, y and z.
const int kQueryStamp = 0x04;

void WorldCompare(WorldCase *c, const WWorldPos *start, int index) {
    static WorldSide sides[2];
    uint8_t *manager = CollisionManagerObject;
    uint8_t saved[kManagerBytes];
    memcpy(saved, manager, kManagerBytes);
    uint32_t savedStamp;
    memcpy(&savedStamp, saved + kQueryStamp, 4);
    for (int s = 0; s < 2; s++) {
        WorldSide *side = &sides[s];
        side->pos = *start;
        side->ret = 0;
        memcpy(manager, saved, kManagerBytes);
        const uint32_t startStamp = savedStamp + uint32_t(s);
        memcpy(manager + kQueryStamp, &startStamp, 4);
        c->original = s == 0;
        c->side = side;
        bool ok;
        if (c->original) {
            OriginalsWindow window;
            ok = GuardedWorld(RunWorldCase, c);
        } else {
            ok = GuardedWorld(RunWorldCase, c);
        }
        if (!ok)
            side->ret = kFaulted;
        memcpy(side->manager, manager, kManagerBytes);
        uint32_t advanced;
        memcpy(&advanced, side->manager + kQueryStamp, 4);
        advanced -= startStamp;
        memcpy(side->manager + kQueryStamp, &advanced, 4);
    }
    // the game goes on from the furthest stamp either side reached
    memcpy(manager, saved, kManagerBytes);
    uint32_t furthest;
    memcpy(&furthest, sides[1].manager + kQueryStamp, 4);
    furthest += savedStamp + 1;
    memcpy(manager + kQueryStamp, &furthest, 4);
    g_cases++;
    g_checks++;
    WWorldPos a = sides[0].pos, b = sides[1].pos;
    a.face.corner[0].flags = b.face.corner[0].flags = 0;
    bool samePos = memcmp(&a, &b, sizeof(a)) == 0;
    bool sameManager = memcmp(sides[0].manager, sides[1].manager, kManagerBytes) == 0;
    bool sameRet = sides[0].ret == sides[1].ret;
    if (samePos && sameManager && sameRet)
        return;
    if (c->kind == 4 && samePos && sameManager) {
        double x, y;
        memcpy(&x, &sides[0].ret, 8);
        memcpy(&y, &sides[1].ret, 8);
        if (x != x && y != y) {
            g_nanOnly++;
            return;
        }
    }
    g_differ++;
    char line[256];
    snprintf(line, sizeof(line), "%s case %d at (%.3f, %.3f, %.3f): ret %016llx / %016llx%s%s", kWorldNames[c->kind],
             index, c->point.x, c->point.y, c->point.z, (unsigned long long)sides[0].ret,
             (unsigned long long)sides[1].ret, samePos ? "" : ", position differs",
             sameManager ? "" : ", manager differs");
    Detail(line);
    // the first differing words, offset and both values
    const uint32_t *wa = reinterpret_cast<const uint32_t *>(&a), *wb = reinterpret_cast<const uint32_t *>(&b);
    int shown = 0;
    for (size_t k = 0; k < sizeof(a) / 4 && shown < 4; k++) {
        if (wa[k] == wb[k])
            continue;
        snprintf(line, sizeof(line), "  position +0x%02x: %08x / %08x", unsigned(k * 4), wa[k], wb[k]);
        Detail(line);
        shown++;
    }
    const uint32_t *ma = reinterpret_cast<const uint32_t *>(sides[0].manager);
    const uint32_t *mb = reinterpret_cast<const uint32_t *>(sides[1].manager);
    for (int k = 0; k < kManagerBytes / 4 && shown < 6; k++) {
        if (ma[k] == mb[k])
            continue;
        snprintf(line, sizeof(line), "  manager +0x%02x: %08x / %08x", unsigned(k * 4), ma[k], mb[k]);
        Detail(line);
        shown++;
    }
}

bool PlayerPosition(float *out) {
    uint8_t **handle = *(uint8_t ***)0x00234e40;
    uint8_t *car = handle != NULL ? *handle : NULL;
    if (car == NULL)
        return false;
    typedef uint8_t *(__fastcall *GetRigidBodyFn)(void *, int, int);
    uint8_t *body = (Original<GetRigidBodyFn>(0x000b2700))((void *)0x00233ff0, 0, *(int16_t *)(car + 0x4a));
    if (body == NULL)
        return false;
    memcpy(out, body + 0x10, 12);
    return true;
}

struct PlayerQuery {
    float position[3];
    bool found;
};

void QueryPlayer(void *context) {
    PlayerQuery *q = static_cast<PlayerQuery *>(context);
    q->found = PlayerPosition(q->position);
}

} // namespace

void GeomShadow_Run(void) {
    if (!Enabled())
        return;
    unsigned int x87, sse;
    FpControlGet(&x87, &sse);
    printf("[geomshadow] x87 control word %04x, MXCSR %08x\n", x87, sse);
    float spareValue = GaussSpareValue;
    uint8_t spareFlag = GaussSpareFlag;
    const int N = 4000;

    Test("Util_Step", N, GenScalars, BodyStep);
    Test("Util_Bound", N, GenScalars, BodyBound);
    Test("Util_GenerateMatrix", N, GenDirection, BodyGenerateMatrix);
    Test("Util_GenerateTensor", N, GenScalars, BodyTensor);
    Test("Util_GenerateCarTensor", N, GenScalars, BodyCarTensor);
    Test("Util_PerturbVector(angles)", N, GenRandomState, BodyPerturbAngles);
    Test("Util_VecToPoleDepthAndLong", N, GenDirection, BodyPole);
    Test("Util_CharMapLookup", N, GenCharMap, BodyCharMap);
    Test("Util_SegmentDirection", N, GenVectors, BodySegmentDirection);
    Test("Util_NearestPointOnSegment", N, GenNearestOnSegment, BodyNearestOnSegment);
    Test("Util_CollideRayWithSphere", N, GenRay, BodyRaySphere);
    Test("Util_CollideRayWithStretchedSphere", N, GenRay, BodyRayStretched);
    Test("FUN_000bd690", N, GenRegion, BodyRegion);
    if (HookInstall(&g_randHook, 0x00133ee0, (const void *)FakeRand)) {
        Test("Util_GaussRandom", N, GenRandomState, BodyGauss);
        Test("Util_PerturbVector(random)", N, GenRandomState, BodyPerturbRandom);
        Test("Util_ApplyVariance", N, GenRandomState, BodyApplyVariance);
        HookRemove(&g_randHook);
    } else {
        Detail("could not jump the C runtime's rand to the fake: Util_GaussRandom and its callers not tested");
    }
    GaussSpareValue = spareValue;
    GaussSpareFlag = spareFlag;

    Test("OBB::OBB()", 16, GenBoxInputs, BodyObbConstruct);
    Test("OBB::OBB(matrix, position, extents)", N, GenBoxInputs, BodyObbConstructFrom);
    Test("OBB::Reset", N, GenBoxInputs, BodyObbReset);
    Test("OBB::CheckOBBOverlap", N, GenBoxes, BodyObbOverlap);
    Test("OBB::CheckOBBOverlapAndFindIntersection", N, GenBoxes, BodyObbIntersection);

    Test("FasterSegmentIntersect", N, GenSegments, BodyFasterSegments);
    Test("WWorldMath::SegmentIntersect", N, GenSegments, BodySegments);
    Test("WWorldMath::IntersectCircle", N, GenCircle, BodyCircle);
    Test("WWorldMath::NearestPointLine2D", N, GenVectors, BodyNearest2D);
    Test("WWorldMath::GetPlaneY", N, GenPlane, BodyPlaneY);
    Test("WWorldMath::IntersectSegPlane", N, GenPlane, BodySegPlane);
    Test("WWorldMath::NearestPointLine3D", N, GenVectors, BodyNearest3D);
    Test("WWorldMath::MakeSegSpaceMatrix", N, GenSegSpace, BodySegSpace);
    Test("WWorldPos::WWorldPos", 64, GenPos, BodyPosConstruct);
    Test("WWorldPos::HeightAtPoint", N, GenPos, BodyPosHeight);

    FpControlSetX87(x87);
    FpControlSetSse(sse);
    Summary("pure geometry");
}

void GeomShadow_RunWorld(void) {
    if (!Enabled())
        return;
    if (CollisionManagerObject == NULL) {
        printf("[geomshadow] world: no collision manager yet - not tested\n");
        fflush(stdout);
        return;
    }
    g_cases = g_checks = g_differ = g_nanOnly = g_faults = g_details = 0;
    PlayerQuery player = {};
    GuardedWorld(QueryPlayer, &player);
    float centre[3] = { 0.0f, 0.0f, 0.0f };
    if (player.found)
        memcpy(centre, player.position, sizeof(centre));
    printf("[geomshadow] world: points about (%.1f, %.1f, %.1f)%s\n", centre[0], centre[1], centre[2],
           player.found ? ", the player's car" : ", the origin (no car)");

    const int kPoints = 300;
    WorldCase c = {};
    alignas(16) WWorldPos fresh, found;
    fresh.Construct();
    for (int i = 0; i < kPoints; i++) {
        float spread = i % 3 == 0 ? 8.0f : (i % 3 == 1 ? 60.0f : 400.0f);
        c.point.x = centre[0] + Uniform(-spread, spread);
        c.point.y = centre[1] + Uniform(-10.0f, 30.0f);
        c.point.z = centre[2] + Uniform(-spread, spread);
        c.point.w = 1.0f;

        // MakeFaceAtPoint over a scrambled object
        WWorldPos scrambled;
        for (size_t k = 0; k < sizeof(scrambled) / 4; k++)
            reinterpret_cast<uint32_t *>(&scrambled)[k] = Next();
        c.kind = 0;
        WorldCompare(&c, &scrambled, i);

        // FindClosestFace(point, keep): from a fresh position, and from one holding a face found nearby (whose
        // instance stamp is sometimes made stale)
        found = fresh;
        Coord4 nearby = c.point;
        nearby.x += Uniform(-1.0f, 1.0f);
        nearby.z += Uniform(-1.0f, 1.0f);
        found.FindClosestFace(reinterpret_cast<const Coord3 *>(&nearby), false);
        if (found.valid)
            g_worldFaces++;
        if (Next() % 4 == 0)
            found.article = reinterpret_cast<CollisionArticle *>(uintptr_t(found.article) ^ 1);
        c.kind = 1;
        for (int keep = 0; keep < 2; keep++) {
            c.keep = keep != 0;
            WorldCompare(&c, &fresh, i);
            WorldCompare(&c, &found, i);
        }

        // the list searches, over the instances GetInstanceList finds here
        InstanceList list = {};
        WCollisionMgr_GetInstanceList(CollisionManagerObject, 0, &list, reinterpret_cast<const Coord3 *>(&c.point), 0,
                                      0, 1);
        c.list = &list;
        c.kind = 2;
        WorldCompare(&c, &found, i);
        c.kind = 3;
        c.from = c.point;
        c.from.y += 5.0f;
        c.to = c.point;
        if (i % 2 == 0) {
            c.to.y -= Uniform(10.0f, 60.0f);
        } else {
            c.to.x += Uniform(-30.0f, 30.0f);
            c.to.y += Uniform(-30.0f, 10.0f);
            c.to.z += Uniform(-30.0f, 30.0f);
        }
        WorldCompare(&c, &found, i);
        if (list.first != NULL)
            UMemory::FastFree(list.first, unsigned(list.end - list.first) * sizeof(InstanceListEntry));
        c.list = NULL;

        // the height under points about the face found
        if (found.valid) {
            for (int k = 0; k < 3; k++) {
                c.kind = 4;
                Coord4 keepPoint = c.point;
                c.point.x += Uniform(-2.0f, 2.0f);
                c.point.z += Uniform(-2.0f, 2.0f);
                WorldCompare(&c, &found, i);
                c.point = keepPoint;
            }
        }
    }
    printf("[geomshadow] world: %d of %d points had a face nearby\n", g_worldFaces, kPoints);
    Summary("world positions");
}
