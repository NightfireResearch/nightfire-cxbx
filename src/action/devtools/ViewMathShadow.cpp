// Shadow tests for parcel 13 and the maths made exact to the x87 with it: each function, ours and the original, on
// the same inputs and state, compared bit for bit. The maths group takes random values and the edge cases
// MathExactShadow uses (zeros of both signs, NaNs of several payloads, infinities, FLT_MAX, denormals, FSIN's limit
// and huge angles, cancelling sums), and counts apart the cases where the two differ only in which NaN came out
// where two NaNs met (plain double arithmetic on SSE2 keeps the first operand, the x87 the larger). The camera,
// vision and view groups run on a made-up world of views, cels, portals and objects, with logging fakes for what
// would need the game running (the heap, the draw calls, lights, sounds, sprites). Run at start with
// MenuShadowTests=on (MenuProbe.cpp); NIGHTFIRE_SHADOW_ONLY=viewmath runs this suite alone.
//
// The register-argument originals (View_AddCels, View_AddObjects, View_DrawCelGlist, View_CaptureScene) are called
// with their registers set, and so are our adaptors.

#include "ViewMathShadow.h"
#include "AllOriginal.h"
#include "ShadowHarness.h"

#include "../../common/xbeOriginal.h"
#include "../actionhelpers.h"
#include "../game.h"
#include "../math/math.h"
#include "../math/X87Ops.h"
#include "../util/Random.h"
#include "../engine/Anim.h"
#include "../engine/Camera.h"
#include "../engine/Collide.h"
#include "../engine/Vision.h"
#include "../engine/viewer.h"
#include "../game/view.h"
#include "../game/mp/multiplayer.h"
#include "../game/obj/Effect.h"
#include "../game/obj/Player.h"
#include "../game/obj/build.h"
#include "../game/obj/object.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <string>

namespace {

#define ORIGINAL(at, fn) ((decltype(&fn))(uintptr_t)(at))
// Calls the original at 'at' (inside its scope) or ours
#define CALL(original, at, fn, ...)                     \
    do {                                                \
        if (original) {                                 \
            XbeOriginalScope scope_(at);                \
            ORIGINAL(at, fn)(__VA_ARGS__);              \
        } else {                                        \
            fn(__VA_ARGS__);                            \
        }                                               \
    } while (0)
#define CALLR(result, original, at, fn, ...)            \
    do {                                                \
        if (original) {                                 \
            XbeOriginalScope scope_(at);                \
            result = ORIGINAL(at, fn)(__VA_ARGS__);     \
        } else {                                        \
            result = fn(__VA_ARGS__);                   \
        }                                               \
    } while (0)

const uint32_t kUntouched = 0x7fa5a5a5;   // a NaN no operation produces, in every word a function should not write

// Zeros, NaNs of both signs (quiet and signalling, several payloads), infinities, the largest and smallest
// floats, FSIN's limit 2^63 and its neighbours, multiples of pi, and ordinary values
const uint32_t kEdges[] = {
    0x00000000, 0x80000000, 0x7fc00000, 0xffc00000, 0x7f800001, 0xff800001, 0x7fbfffff, 0x7fc12345, 0xffffffff,
    0x7f800000, 0xff800000, 0x7f7fffff, 0xff7fffff, 0x00000001, 0x80000001, 0x007fffff, 0x807fffff, 0x00800000,
    0x5f000000, 0xdf000000, 0x5effffff, 0xdeffffff, 0x5f000001, 0x5f800000, 0x60ad78ec, 0x40490fdb, 0xc0490fdb,
    0x40c90fdb, 0x3fc90fdb, 0x40490fda, 0x43b18000, 0x47cb1d00, 0x4996b438, 0x3f800000, 0xbf800000, 0x0da24260,
    0x3f000000, 0x4cbebc20, 0x501502f9, 0x3f7fbe77, 0x38d1b717, 0x42c80000,
};
const int kEdgeCount = sizeof(kEdges) / sizeof(kEdges[0]);

uint32_t s_lcg = 0x2545f491u;
uint32_t NextWord(void) {
    s_lcg = s_lcg * 1664525u + 1013904223u;
    return s_lcg ^ (s_lcg >> 15);
}
uint32_t R(uint32_t n) {
    return n ? NextWord() % n : 0;
}

float FromBits(uint32_t bits) {
    float f;
    memcpy(&f, &bits, sizeof(f));
    return f;
}
uint32_t Bits(float f) {
    uint32_t b;
    memcpy(&b, &f, sizeof(b));
    return b;
}

float Uniform(float range) {
    return ((float)(NextWord() >> 8) * (1.0f / 8388608.0f) - 1.0f) * range;
}

float Positive(float range) {
    return (float)(NextWord() >> 8) * (1.0f / 16777216.0f) * range;
}

float AnyMagnitude(void) {
    uint32_t w = NextWord();
    uint32_t exponent = (w >> 23) % 255;
    return FromBits((w & 0x807fffff) | (exponent << 23));
}

float Edge(void) {
    return FromBits(kEdges[R(kEdgeCount)]);
}

float Angle(void) {
    switch (R(10)) {
    case 0: case 1: case 2: return Uniform(7.0f);
    case 3: return Uniform(1e4f);
    case 4: return Uniform(1e7f);
    case 5: return Uniform(1e20f);
    case 6: return AnyMagnitude();
    case 7: return FromBits(NextWord());
    default: return Edge();
    }
}

float Coordinate(void) {
    switch (R(16)) {
    case 0: return AnyMagnitude();
    case 1: return Edge();
    case 2: return Uniform(1e30f);
    case 3: return Uniform(1e-30f);
    default: return Uniform(100.0f);
    }
}

// A unit-ish value, the kind the rotations hold, or anything
float Unitish(void) {
    return R(6) ? Uniform(1.0f) : Coordinate();
}

// The same float or one ulp either way
float Nudge(float f) {
    return FromBits(Bits(f) + R(3) - 1);
}

// ---- results

struct Suite {
    const char *name;
    int cases;
    int differ;
    int nanChoices;
};

bool IsNaN32(const uint8_t *p) {
    float f;
    memcpy(&f, p, 4);
    return f != f;
}
bool IsNaN64(const uint8_t *p) {
    double d;
    memcpy(&d, p, 8);
    return d != d;
}

// Two NaNs that differ only in the quiet bit are a signalling NaN let through where the x87 quietens it: not a choice
bool QuietBitOnly32(const uint8_t *a, const uint8_t *b) {
    uint32_t x, y;
    memcpy(&x, a, 4);
    memcpy(&y, b, 4);
    return (x ^ y) == 0x00400000u;
}
bool QuietBitOnly64(const uint8_t *a, const uint8_t *b) {
    uint64_t x, y;
    memcpy(&x, a, 8);
    memcpy(&y, b, 8);
    return (x ^ y) == 0x0008000000000000ull;
}

// Bytes made of floatBytes of floats, then doubleBytes of doubles, then the rest compared as bytes. Where every
// difference is a NaN against a NaN, the two only chose differently where two NaNs met.
bool OnlyNaNChoices(const uint8_t *a, const uint8_t *b, size_t size, size_t floatBytes, size_t doubleBytes) {
    size_t i = 0;
    for (; i + 4 <= floatBytes; i += 4)
        if (memcmp(a + i, b + i, 4) != 0 && !(IsNaN32(a + i) && IsNaN32(b + i) && !QuietBitOnly32(a + i, b + i)))
            return false;
    for (; i + 8 <= floatBytes + doubleBytes; i += 8)
        if (memcmp(a + i, b + i, 8) != 0 && !(IsNaN64(a + i) && IsNaN64(b + i) && !QuietBitOnly64(a + i, b + i)))
            return false;
    for (; i < size; i++)
        if (a[i] != b[i])
            return false;
    return true;
}

void Note(Suite &suite, bool nanChoice, const char *what, unsigned offset, uint32_t ours, uint32_t theirs,
          const char *inputs) {
    int shown = nanChoice ? suite.nanChoices++ : suite.differ++;
    if (shown < (nanChoice ? 2 : 4))
        printf("[viewmath] %s case %d: %s%s at byte %u: ours %08x, the original's %08x%s%s\n", suite.name,
               suite.cases - 1, nanChoice ? "NaN chosen in the " : "", what, offset, ours, theirs,
               inputs[0] ? "; inputs" : "", inputs);
}

void Compare(Suite &suite, const void *ours, const void *theirs, size_t size, size_t floatBytes, size_t doubleBytes,
             const char *what, const char *inputs) {
    suite.cases++;
    if (memcmp(ours, theirs, size) == 0)
        return;
    const uint8_t *a = (const uint8_t *)ours, *b = (const uint8_t *)theirs;
    size_t word = 0;
    while (word + 4 <= size && memcmp(a + word, b + word, 4) == 0)
        word += 4;
    uint32_t x = 0, y = 0;
    memcpy(&x, a + word, size - word < 4 ? size - word : 4);
    memcpy(&y, b + word, size - word < 4 ? size - word : 4);
    Note(suite, OnlyNaNChoices(a, b, size, floatBytes, doubleBytes), what, (unsigned)word, x, y, inputs);
}

void Summary(const Suite &suite) {
    if (suite.nanChoices != 0)
        printf("[viewmath] %s: %d cases, %d differ (and %d only in the NaN chosen where two NaNs meet)\n", suite.name,
               suite.cases, suite.differ, suite.nanChoices);
    else
        printf("[viewmath] %s: %d cases, %d differ\n", suite.name, suite.cases, suite.differ);
}

// ---- the maths: a block of floats in and out, and two doubles for the unrounded returns

struct Buf {
    float f[64];
    double d[2];
};

typedef void (*MakeFn)(Buf &b);
typedef void (*RunFn)(Buf &b, bool original);

struct MathCase {
    const char *name;
    MakeFn make;
    RunFn run;
    int inputs;     // how many of the leading floats to print with a difference
};

const int kTrials = 20000;

void Fill(void *p, size_t size) {
    for (size_t i = 0; i + 4 <= size; i += 4)
        XbeOursWrite((unsigned)(size_t)p + (unsigned)i, &kUntouched, 4);
}

void PutVector(Buf &b, int at, float (*component)(void) = Coordinate) {
    for (int i = 0; i < 3; i++)
        b.f[at + i] = component();
}

// A rotation with an offset, a rotation from Euler angles, edge values or any words
void PutMatrix(Buf &b, int at) {
    float *m = &b.f[at];
    switch (R(8)) {
    case 0: case 1:
        for (int i = 0; i < 12; i++)
            m[i] = Uniform(1.0f);
        for (int i = 12; i < 15; i++)
            m[i] = Uniform(1000.0f);
        break;
    case 2: {
        _VECTOR angles = {Uniform(7.0f), Uniform(7.0f), Uniform(7.0f)};
        RotMatrix(&angles, (_MATRIX *)m);
        for (int i = 12; i < 15; i++)
            m[i] = Uniform(100.0f);
        break;
    }
    case 3:
        for (int i = 0; i < 15; i++)
            m[i] = Edge();
        break;
    default:
        for (int i = 0; i < 15; i++)
            m[i] = Coordinate();
        break;
    }
    for (int i = 3; i < 12; i += 4)
        m[i] = R(2) ? Edge() : FromBits(NextWord());
}

// A unit quaternion, one of zeros, edge values or any
void PutQuat(Buf &b, int at) {
    float *q = &b.f[at];
    switch (R(6)) {
    case 0: {
        double sum = 0.0;
        for (int i = 0; i < 4; i++) {
            q[i] = Uniform(1.0f);
            sum += (double)q[i] * q[i];
        }
        if (sum > 0.0)
            for (int i = 0; i < 4; i++)
                q[i] = (float)(q[i] / sqrt(sum));
        break;
    }
    case 1:
        for (int i = 0; i < 4; i++)
            q[i] = R(2) ? 0.0f : -0.0f;
        if (R(3) == 0)
            q[R(4)] = Edge();
        break;
    case 2:
        for (int i = 0; i < 4; i++)
            q[i] = Edge();
        break;
    default:
        for (int i = 0; i < 4; i++)
            q[i] = Unitish();
        break;
    }
}

// Large products that cancel in a matrix row against a vector, so the order of the sums shows
void Cancel(float *m, float *v) {
    v[0] = R(2) ? Uniform(1e19f) : AnyMagnitude();
    v[1] = Nudge(R(2) ? v[0] : -v[0]);
    v[2] = R(2) ? Nudge(v[0]) : Uniform(1.0f);
    for (int r = 0; r < 3; r++) {
        if (R(4) == 0)
            continue;
        m[r] = R(2) ? Uniform(1e19f) : AnyMagnitude();
        m[4 + r] = Nudge(R(2) ? m[r] : -m[r]);
        m[8 + r] = Nudge(R(2) ? m[r] : -m[r]);
        if (R(2))
            m[12 + r] = Nudge((float)-((double)m[r] * v[0]));
    }
}

_VECTOR *V(Buf &b, int at) { return (_VECTOR *)&b.f[at]; }
_MATRIX *M(Buf &b, int at) { return (_MATRIX *)&b.f[at]; }
quaternion_tag *Q(Buf &b, int at) { return (quaternion_tag *)&b.f[at]; }

// Where the output goes: apart (at 'apart'), or over the input at 'in'
int Out(int in, int apart) {
    return R(4) == 0 ? in : apart;
}
int s_out;   // the output position the make chose, for the run

// Ang_RotateToBearing
void MakeBearing(Buf &b) {
    b.f[0] = Angle();
    b.f[1] = R(2) ? b.f[0] + Uniform(0.5f) : Angle();
    b.f[2] = R(4) ? Uniform(1.2f) : Coordinate();
    b.f[3] = R(4) ? Uniform(4.0f) : Edge();
}
void RunBearing(Buf &b, bool o) {
    float r;
    CALLR(r, o, 0x000d6380, Ang_RotateToBearing, b.f[0], b.f[1], b.f[2], b.f[3]);
    b.f[4] = r;
}

void MakeAvg3(Buf &b) {
    PutVector(b, 0);
    PutVector(b, 3);
    PutVector(b, 6);
    s_out = Out(0, 9);
}
void RunAvg3(Buf &b, bool o) { CALL(o, 0x000d4dd0, Vec_Avg3, V(b, 0), V(b, 3), V(b, 6), V(b, s_out)); }

void MakeTwo(Buf &b) {
    PutVector(b, 0);
    PutVector(b, 3);
    if (R(3) == 0) {   // close in plan: cancellation
        b.f[3] = Nudge(b.f[0]);
        b.f[5] = Nudge(b.f[2]);
    }
}
void RunSqDist2D(Buf &b, bool o) { CALLR(b.d[0], o, 0x000d4e50, Vec_SqDist2D, V(b, 0), V(b, 3)); }
void RunDist2D(Buf &b, bool o) { CALLR(b.d[0], o, 0x000d4e20, Vec_Dist2D, V(b, 0), V(b, 3)); }
void RunSqMagnitude(Buf &b, bool o) { CALLR(b.d[0], o, 0x000d4eb0, Vec_SqMagnitude, V(b, 0)); }

void MakeUnit(Buf &b) {
    PutVector(b, 0);
    PutVector(b, 3);
    s_out = Out(0, 6);
}
void RunUnitVector(Buf &b, bool o) { CALL(o, 0x000d8a90, Vec_UnitVector, V(b, s_out), V(b, 0), V(b, 3)); }

void MakeOne(Buf &b) {
    PutVector(b, 0, R(2) ? Unitish : Coordinate);
    s_out = Out(0, 3);
}
void RunNormToRot(Buf &b, bool o) { CALL(o, 0x000d4ee0, Vec_NormToRot, V(b, 0), V(b, s_out)); }
void RunDirToRot(Buf &b, bool o) { CALL(o, 0x000d4f20, Vec_DirToRot, V(b, 0), V(b, s_out)); }

void MakeMul(Buf &b) {
    PutVector(b, 0);
    PutVector(b, 3);
    s_out = Out(0, 3);
}
void RunMul(Buf &b, bool o) { CALL(o, 0x000d6e90, Vec_Mul, V(b, 0), V(b, s_out)); }

void MakeBasis(Buf &b) {
    PutVector(b, 0, R(2) ? Unitish : Coordinate);
    if (R(4) == 0)   // |x| and |y| equal, or one ulp apart
        b.f[1] = Nudge(R(2) ? b.f[0] : -b.f[0]);
}
void RunBasis(Buf &b, bool o) { CALL(o, 0x000d6ec0, Vec_MakeOrthonormalBasis, V(b, 3), V(b, 6), V(b, 0)); }

void MakeMatrixVector(Buf &b) {
    PutMatrix(b, 0);
    PutVector(b, 15);
    s_out = Out(15, 18);
}
void RunGetUp(Buf &b, bool o) { CALL(o, 0x000d5050, Mat_GetUp, V(b, 18), M(b, 0)); }
void RunGetNorm(Buf &b, bool o) { CALL(o, 0x000d5070, Mat_GetNorm, V(b, 18), M(b, 0)); }
void RunSetDir(Buf &b, bool o) { CALL(o, 0x000d50b0, Mat_SetDir, V(b, 15), M(b, 0)); }
void RunSetUp(Buf &b, bool o) { CALL(o, 0x000d50d0, Mat_SetUp, V(b, 15), M(b, 0)); }

void MakeMatrix(Buf &b) {
    PutMatrix(b, 0);
    s_out = Out(0, 16);
    b.f[32] = R(2) ? Uniform(3.0f) : Coordinate();
    b.f[33] = R(2) ? Uniform(3.0f) : Coordinate();
    b.f[34] = R(2) ? Uniform(3.0f) : Coordinate();
}
void RunInverseSelf(Buf &b, bool o) { CALL(o, 0x000d5190, Mat_InverseSelf, M(b, 0)); }
void RunInverse(Buf &b, bool o) { CALL(o, 0x000d51e0, Mat_Inverse, M(b, 0), M(b, s_out)); }
void RunMatrixScale(Buf &b, bool o) { CALL(o, 0x000d6500, MatrixScale, M(b, 0), b.f[32]); }

void MakeTwoMatrices(Buf &b) {
    PutMatrix(b, 0);
    PutMatrix(b, 16);
    if (R(4) == 0)
        Cancel(&b.f[0], &b.f[16]);
    s_out = R(4) == 0 ? 0 : 32;
}
void RunMulMatrix0(Buf &b, bool o) { CALL(o, 0x000d6a40, MulMatrix0, M(b, 0), M(b, 16), M(b, 32)); }
void RunMulMatrix2(Buf &b, bool o) { CALL(o, 0x000d6920, MulMatrix2, M(b, 0), M(b, s_out == 0 ? 0 : 16)); }
void RunConcat(Buf &b, bool o) { CALL(o, 0x000d6d20, Concat, M(b, 0), M(b, s_out == 0 ? 0 : 16)); }

void MakeAngles(Buf &b) {
    b.f[0] = Angle();
    b.f[1] = Angle();
}
void RunRotX(Buf &b, bool o) { CALL(o, 0x000d67b0, RotMatrixX, b.f[0], M(b, 2)); }
void RunRotY(Buf &b, bool o) { CALL(o, 0x000d67f0, RotMatrixY, b.f[0], M(b, 2)); }
void RunRotZ(Buf &b, bool o) { CALL(o, 0x000d6850, RotMatrixZ, b.f[0], M(b, 2)); }
void RunRotXY(Buf &b, bool o) { CALL(o, 0x000d6890, RotMatrixXY, b.f[0], b.f[1], M(b, 2)); }

void MakeTransform(Buf &b) {
    PutMatrix(b, 0);
    PutVector(b, 15);
    if (R(4) == 0)
        Cancel(&b.f[0], &b.f[15]);
    s_out = Out(15, 18);
}
void RunPreTrans(Buf &b, bool o) { CALL(o, 0x000d6cc0, RotPreTransVec, M(b, 0), V(b, 15), V(b, s_out)); }
void RunApplyLVI(Buf &b, bool o) { CALL(o, 0x000d6b30, ApplyMatrixLVI, M(b, 0), V(b, 15)); }

void MakeQuatVector(Buf &b) {
    PutQuat(b, 0);
    PutVector(b, 4);
    s_out = Out(4, 7);
}
void RunRotVec(Buf &b, bool o) { CALL(o, 0x000d56a0, Quat_Rot_Vec, Q(b, 0), V(b, 4), V(b, s_out)); }

void MakeEuler(Buf &b) {
    PutVector(b, 0, Angle);
    b.f[3] = Angle();
}
void RunEuler(Buf &b, bool o) { CALL(o, 0x000d5740, Quat_EulerToQuat, V(b, 0), Q(b, 4)); }
void RunAxisAngle(Buf &b, bool o) { CALL(o, 0x000d59b0, Quat_AxisAngleToQuat, &b.f[0], Q(b, 4)); }

void MakeQuat(Buf &b) {
    PutQuat(b, 0);
}
void RunQuatNormalise(Buf &b, bool o) { CALL(o, 0x000d5da0, Quat_Normalise, Q(b, 0)); }
void RunQuatToMatrix(Buf &b, bool o) { CALL(o, 0x000d58e0, Quat_QuaternionToMatrix, M(b, 4), Q(b, 0)); }

// A rotation, or one with each diagonal element largest in turn, or anything
void MakeRotation(Buf &b) {
    PutMatrix(b, 0);
    if (R(3) == 0) {
        int big = R(3);
        for (int i = 0; i < 3; i++)
            b.f[i * 5] = i == big ? Uniform(1.0f) * 0.5f + 0.6f : -Uniform(1.0f);
    }
}
void RunMatrixToQuat(Buf &b, bool o) { CALL(o, 0x000d5c20, Quat_MatrixToQuaternion, Q(b, 16), M(b, 0)); }

void MakeLookAt(Buf &b) {
    PutVector(b, 0);
    PutVector(b, 3);
    PutVector(b, 6, Unitish);
    PutVector(b, 9, Unitish);
    if (R(3) == 0) {   // along up, or nearly
        b.f[3] = b.f[0] + b.f[6] * 3.0f;
        b.f[4] = Nudge(b.f[1] + b.f[7] * 3.0f);
        b.f[5] = b.f[2] + b.f[8] * 3.0f;
    }
}
void RunLookAt(Buf &b, bool o) { CALL(o, 0x000d77f0, Mat_LookAt, M(b, 12), V(b, 0), V(b, 3), V(b, 6), V(b, 9)); }

void MakeSpherical(Buf &b) {
    PutVector(b, 0, R(2) ? Unitish : Coordinate);
}
void RunCartesian(Buf &b, bool o) { CALL(o, 0x000d8530, Vec_Cartesian_2_Spherical, V(b, 3), b.f[0], b.f[1], b.f[2]); }

const MathCase kMathCases[] = {
    {"Ang_RotateToBearing", MakeBearing, RunBearing, 4},
    {"Vec_Avg3", MakeAvg3, RunAvg3, 9},
    {"Vec_SqDist2D", MakeTwo, RunSqDist2D, 6},
    {"Vec_Dist2D", MakeTwo, RunDist2D, 6},
    {"Vec_SqMagnitude", MakeTwo, RunSqMagnitude, 3},
    {"Vec_UnitVector", MakeUnit, RunUnitVector, 6},
    {"Vec_NormToRot", MakeOne, RunNormToRot, 3},
    {"Vec_DirToRot", MakeOne, RunDirToRot, 3},
    {"Vec_Mul", MakeMul, RunMul, 6},
    {"Vec_MakeOrthonormalBasis", MakeBasis, RunBasis, 3},
    {"Mat_GetUp", MakeMatrixVector, RunGetUp, 0},
    {"Mat_GetNorm", MakeMatrixVector, RunGetNorm, 0},
    {"Mat_SetDir", MakeMatrixVector, RunSetDir, 0},
    {"Mat_SetUp", MakeMatrixVector, RunSetUp, 0},
    {"Mat_InverseSelf", MakeMatrix, RunInverseSelf, 0},
    {"Mat_Inverse", MakeMatrix, RunInverse, 0},
    {"MatrixScale", MakeMatrix, RunMatrixScale, 0},
    {"MulMatrix0", MakeTwoMatrices, RunMulMatrix0, 0},
    {"MulMatrix2", MakeTwoMatrices, RunMulMatrix2, 0},
    {"Concat", MakeTwoMatrices, RunConcat, 0},
    {"RotMatrixX", MakeAngles, RunRotX, 1},
    {"RotMatrixY", MakeAngles, RunRotY, 1},
    {"RotMatrixZ", MakeAngles, RunRotZ, 1},
    {"RotMatrixXY", MakeAngles, RunRotXY, 2},
    {"RotPreTransVec", MakeTransform, RunPreTrans, 0},
    {"ApplyMatrixLVI", MakeTransform, RunApplyLVI, 0},
    {"Quat_Rot_Vec", MakeQuatVector, RunRotVec, 7},
    {"Quat_EulerToQuat", MakeEuler, RunEuler, 3},
    {"Quat_AxisAngleToQuat", MakeEuler, RunAxisAngle, 4},
    {"Quat_Normalise", MakeQuat, RunQuatNormalise, 4},
    {"Quat_QuaternionToMatrix", MakeQuat, RunQuatToMatrix, 4},
    {"Quat_MatrixToQuaternion", MakeRotation, RunMatrixToQuat, 0},
    {"Mat_LookAt", MakeLookAt, RunLookAt, 12},
    {"Vec_Cartesian_2_Spherical", MakeSpherical, RunCartesian, 3},
};

void Inputs(const Buf &b, int count, char *out, size_t size) {
    out[0] = 0;
    size_t at = 0;
    for (int i = 0; i < count && at + 10 < size; i++)
        at += snprintf(out + at, size - at, " %08x", Bits(b.f[i]));
}

void RunMath(void) {
    for (const MathCase &c : kMathCases) {
        Suite suite = {c.name, 0, 0, 0};
        for (int t = 0; t < kTrials; t++) {
            Buf start;
            Fill(&start, sizeof(start));
            c.make(start);
            Buf a = start, b = start;
            c.run(a, false);
            c.run(b, true);
            char inputs[160];
            Inputs(start, c.inputs, inputs, sizeof(inputs));
            Compare(suite, &a, &b, sizeof(Buf), sizeof(a.f), sizeof(a.d), "result", inputs);
        }
        Summary(suite);
    }
}

// ---- the rest of math.cpp and the view's matrices: the ports Part 2 made exact, and the ones it checked

typedef double (*DoubleOfTwo)(_VECTOR *, _VECTOR *);
typedef double (*DoubleOfOne)(_VECTOR *);
typedef double (*DoubleNormLen)(_VECTOR *, _VECTOR *);
typedef double (*DoubleAngles)(float, float);

void MakeQuatPair(Buf &b) {
    PutQuat(b, 0);
    PutQuat(b, 4);
    b.f[8] = R(2) ? Positive(0.1f) : Edge();
    s_out = R(4) == 0 ? 0 : 12;
}
void RunQuatCopy(Buf &b, bool o) { CALL(o, 0x000d55f0, Quat_Copy, Q(b, 12), Q(b, 0)); }
void RunQuatIsEqual(Buf &b, bool o) {
    bool r;
    CALLR(r, o, 0x000d5560, Quat_IsEqual, Q(b, 0), Q(b, R(5) ? 4 : 0), b.f[8]);
    b.f[16] = FromBits((uint8_t)r);
}
void RunQuatAlt(Buf &b, bool o) { CALL(o, 0x000d7470, MultiplyQuaternionAltOrder, Q(b, 0), Q(b, 4), Q(b, s_out)); }
void RunQuatMultiply(Buf &b, bool o) { CALL(o, 0x000d5610, Quat_QuaternionMultiply, Q(b, 0), Q(b, 4), Q(b, s_out)); }
void RunQuatTransToMat(Buf &b, bool o) { CALL(o, 0x000d59e0, Quat_QuatTransToMat, Q(b, 0), &b.f[4], M(b, 16)); }
void RunQuatToMat(Buf &b, bool o) { CALL(o, 0x000d5800, Quat_QuatToMat, Q(b, 0), M(b, 16)); }
void RunMatToQuat(Buf &b, bool o) { CALL(o, 0x000d5ad0, Quat_MatToQuat, Q(b, 16), M(b, 0)); }
void RunQuatSlerp(Buf &b, bool o) { CALL(o, 0x000d5e40, Quat_Slerp, b.f[8], Q(b, 0), Q(b, 4), Q(b, 12)); }

void MakeSlerp(Buf &b) {
    PutQuat(b, 0);
    PutQuat(b, 4);
    if (R(3) == 0)   // nearly the same, or opposite
        for (int i = 0; i < 4; i++)
            b.f[4 + i] = Nudge(R(2) ? b.f[i] : -b.f[i]);
    b.f[8] = R(4) ? Positive(1.0f) : Coordinate();
}
void RunSlerpAcc(Buf &b, bool o) { CALL(o, 0x000d5e90, Quat_Slerp_Acc, b.f[8], &b.f[0], &b.f[4], &b.f[12]); }

void MakeTrack(Buf &b) {
    PutVector(b, 0);
    PutVector(b, 3);
    PutMatrix(b, 8);
    b.f[6] = R(4) ? Positive(1.0f) : Coordinate();
}
void RunTrack(Buf &b, bool o) { CALL(o, 0x000d8950, Vec_Track, b.f[6], &b.f[0], &b.f[3], M(b, 8), Q(b, 24)); }

void RunWorld2View(Buf &b, bool o) { CALL(o, 0x000d4f60, Mat_World2ViewMat, M(b, 0), M(b, s_out)); }
void RunScale3f(Buf &b, bool o) {
    CALL(o, 0x000d5240, Mat_Scale3f, M(b, s_out), M(b, 0), b.f[32], b.f[33], b.f[34]);
}
void RunGetDir(Buf &b, bool o) { CALL(o, 0x000d5030, Mat_GetDir, V(b, 18), M(b, 0)); }
void RunMatCopy(Buf &b, bool o) { CALL(o, 0x000d4ff0, Mat_Copy, M(b, 0), M(b, 16)); }
void RunMatCopyRot(Buf &b, bool o) { CALL(o, 0x000d5010, Mat_CopyRot, M(b, 0), M(b, 16)); }
void RunIdentityT(Buf &b, bool o) { CALL(o, 0x000d5160, Mat_IdentityT, M(b, 0)); }
void RunIdentity(Buf &b, bool o) { CALL(o, 0x000d5130, Mat_Identity, M(b, 0)); }
void RunRotTransMat(Buf &b, bool o) { CALL(o, 0x000d6b80, RotTransMat, M(b, 0), M(b, s_out == 0 ? 0 : 16)); }
void RunSetTrans(Buf &b, bool o) { CALL(o, 0x000d5110, Matrix_SetTrans, V(b, 15), M(b, 0)); }
void RunNormalize(Buf &b, bool o) { CALL(o, 0x000d7be0, Mat_Normalize, M(b, 0)); }

void MakeAlign(Buf &b) {
    PutMatrix(b, 0);
    PutVector(b, 15, R(2) ? Unitish : Coordinate);
    PutVector(b, 18, R(2) ? Unitish : Coordinate);
    PutVector(b, 21, Unitish);
    if (R(3) == 0)   // direction along the up hint
        for (int i = 0; i < 3; i++)
            b.f[15 + i] = Nudge(b.f[18 + i] * 2.0f);
}
void RunAlign2Up(Buf &b, bool o) { CALL(o, 0x000d7b00, Mat_Align2Up, &b.f[0], &b.f[15], &b.f[18]); }
void RunAlign2Dir(Buf &b, bool o) { CALL(o, 0x000d7990, Mat_Align2Dir, M(b, 0), V(b, 15), V(b, 18), V(b, 21)); }

void RunNormalise(Buf &b, bool o) { CALL(o, 0x000d76d0, Vec_Normalise, V(b, s_out), V(b, 0)); }
void RunNormaliseLen(Buf &b, bool o) {
    if (o) {
        XbeOriginalScope scope(0x000d7670);
        b.d[0] = ((DoubleNormLen)(uintptr_t)0x000d7670)(V(b, s_out), V(b, 0));
    } else {
        b.d[0] = Vec_NormaliseLen(V(b, s_out), V(b, 0));
    }
}
void RunMagnitude(Buf &b, bool o) {
    if (o) {
        XbeOriginalScope scope(0x000d4e80);
        b.d[0] = ((DoubleOfOne)(uintptr_t)0x000d4e80)(V(b, 0));
    } else {
        b.d[0] = Vec_Magnitude(V(b, 0));
    }
}
void RunDist3D(Buf &b, bool o) {
    if (o) {
        XbeOriginalScope scope(0x000d7600);
        b.d[0] = ((DoubleOfTwo)(uintptr_t)0x000d7600)(V(b, 0), V(b, 3));
    } else {
        b.d[0] = Vec_Dist3D(V(b, 0), V(b, 3));
    }
}
void RunSqDist3D(Buf &b, bool o) {
    if (o) {
        XbeOriginalScope scope(0x000d7640);
        b.d[0] = ((DoubleOfTwo)(uintptr_t)0x000d7640)(V(b, 0), V(b, 3));
    } else {
        b.d[0] = Vec_SqDist3D(V(b, 0), V(b, 3));
    }
}

void MakeVectors(Buf &b) {
    PutVector(b, 0, R(2) ? Unitish : Coordinate);
    PutVector(b, 3, R(2) ? Unitish : Coordinate);
    PutVector(b, 6, R(2) ? Unitish : Coordinate);
    if (R(4) == 0)
        for (int i = 0; i < 3; i++)
            b.f[3 + i] = Nudge(b.f[i]);
    b.f[12] = R(3) ? Uniform(3.0f) : Coordinate();
    s_out = R(4) == 0 ? 0 : 9;
}
void RunSubtract(Buf &b, bool o) { CALL(o, 0x000d7370, Vec_Subtract, V(b, 0), V(b, 3), V(b, s_out)); }
void RunZero(Buf &b, bool o) { CALL(o, 0x000d4dc0, Vec_Zero, V(b, 0)); }
void RunNegate(Buf &b, bool o) { CALL(o, 0x000d7280, Vec_Negate, V(b, 0), V(b, s_out)); }
void RunCopy(Buf &b, bool o) { CALL(o, 0x000d50f0, Vec_Copy, V(b, 0), V(b, 9)); }
void RunMulR32(Buf &b, bool o) { CALL(o, 0x000d72a0, Vec_MulR32, V(b, s_out), V(b, 0), b.f[12]); }
void RunCross(Buf &b, bool o) { CALL(o, 0x000d73a0, Vec_Cross, V(b, 0), V(b, 3), V(b, 9)); }
void RunCopy2(Buf &b, bool o) { CALL(o, 0x000d4d90, Vec_Copy2, V(b, 0), V(b, 9), V(b, 13)); }
void RunIsEqual(Buf &b, bool o) {
    bool r;
    CALLR(r, o, 0x000d8a00, Vec_IsEqual, V(b, 0), V(b, 3), b.f[12]);
    b.f[16] = FromBits((uint8_t)r);
}
void RunCrossNormalise(Buf &b, bool o) { CALL(o, 0x000d7730, Vec_CrossNormalise, V(b, 0), V(b, 3), V(b, 9)); }
void RunMax(Buf &b, bool o) { CALL(o, 0x000d6fa0, Vec_Max, V(b, 0), V(b, 3), V(b, s_out)); }
void RunMin(Buf &b, bool o) { CALL(o, 0x000d7000, Vec_Min, V(b, 0), V(b, 3), V(b, s_out)); }
void RunMinMax(Buf &b, bool o) { CALL(o, 0x000d7060, Vec_MinMax, V(b, 0), V(b, 3), V(b, 9), V(b, 13)); }
void RunMinMax3(Buf &b, bool o) { CALL(o, 0x000d7100, Vec_MinMax3, V(b, 0), V(b, 3), V(b, 6), V(b, 9), V(b, 13)); }
void RunAddMul(Buf &b, bool o) { CALL(o, 0x000d7430, auxVec_AddMulR32, V(b, 0), V(b, 3), b.f[12], V(b, s_out)); }
void RunSwap(Buf &b, bool o) { CALL(o, 0x000d75c0, Vec_Swap, V(b, 0), V(b, 3)); }
typedef double (*DoubleOfThree)(_VECTOR *, _VECTOR *, _VECTOR *);
void RunTriple(Buf &b, bool o) {
    if (o) {
        XbeOriginalScope scope(0x000d7780);
        b.d[0] = ((DoubleOfThree)(uintptr_t)0x000d7780)(V(b, 0), V(b, 3), V(b, 6));
    } else {
        b.d[0] = Vec_ScalarTripleProduct(V(b, 0), V(b, 3), V(b, 6));
    }
}
void RunAdd2(Buf &b, bool o) { CALL(o, 0x000d7340, Vec_Add2, V(b, 0), V(b, 3), V(b, s_out)); }
void RunCartesianAcc(Buf &b, bool o) {
    CALL(o, 0x000d5520, vecutil_cartesian_to_spherical_acc, V(b, 9), b.f[0], b.f[1], b.f[2]);
}

// Finite angles only: the XBE's fmod raises its error for the others, which needs the game's thread
float FiniteAngle(void) {
    float a;
    do
        a = Angle();
    while (!(a - a == 0.0f));
    return a;
}
void MakeAnglePair(Buf &b) {
    b.f[0] = FiniteAngle();
    b.f[1] = R(3) ? b.f[0] + Uniform(4.0f) : FiniteAngle();
}
void RunAngleDifference(Buf &b, bool o) {
    if (o) {
        XbeOriginalScope scope(0x000d4cb0);
        b.d[0] = ((DoubleAngles)(uintptr_t)0x000d4cb0)(b.f[0], b.f[1]);
    } else {
        b.d[0] = Vec_AngleDifference(b.f[0], b.f[1]);
    }
}
typedef float (*FloatOfThree)(float, float, float);
typedef float (*FloatOfOne)(float);
void MakeTowards(Buf &b) {
    b.f[0] = Coordinate();
    b.f[1] = R(4) ? Coordinate() : b.f[0];
    b.f[2] = R(3) ? Uniform(1.0f) : (R(2) ? 1.0f : 2.0f);
}
void RunTowards(Buf &b, bool o) {
    if (o) {
        XbeOriginalScope scope(0x00024000);
        b.f[3] = ((FloatOfThree)(uintptr_t)0x00024000)(b.f[0], b.f[1], b.f[2]);
    } else {
        b.f[3] = moveTowardsValue(b.f[0], b.f[1], b.f[2]);   // the adaptor, and the core under it
    }
}
void MakeWrap(Buf &b) {
    static const float scales[] = {1.0f, 3.0f, 100.0f, 0.5f};
    static const float edges[] = {3.1415925f, 3.1415927f, -3.1415927f, 3.141593f, 3.14f, 3.1416f, 6.2831855f,
                                  -6.2831855f, 9.424778f, -9.424778f, 0.0f, -0.0f};
    b.f[0] = R(3) ? FiniteAngle() * scales[R(4)] : edges[R(12)];
}
void RunWrap(Buf &b, bool o) {
    if (o) {
        XbeOriginalScope scope(0x00024050);
        b.d[0] = ((double (*)(float))(uintptr_t)0x00024050)(b.f[0]);
    } else {
        b.d[0] = normaliseToPlusMinusPi(b.f[0]);
    }
}
void MakeAtan(Buf &b) {
    b.f[0] = Coordinate();
    b.f[1] = R(4) ? Coordinate() : R(2) ? b.f[0] : -b.f[0];
}
void RunAtan(Buf &b, bool o) {
    if (o) {
        XbeOriginalScope scope(0x000d6440);
        b.d[0] = ((DoubleAngles)(uintptr_t)0x000d6440)(b.f[0], b.f[1]);
    } else {
        b.d[0] = maybeAtan2(b.f[0], b.f[1]);
    }
}

void MakeRotTrans(Buf &b) {
    PutVector(b, 0, Angle);
    PutVector(b, 3);
    PutVector(b, 6, R(2) ? Unitish : Coordinate);
}
void RunViewRotTrans(Buf &b, bool o) { CALL(o, 0x000d92e0, View_RotTransMatrix, V(b, 0), V(b, 3), M(b, 12)); }
void RunViewRotTransScale(Buf &b, bool o) {
    CALL(o, 0x000d9210, View_RotTransScaleMatrix, V(b, 0), V(b, 3), V(b, 6), M(b, 12));
}

const MathCase kExactCases[] = {
    {"Quat_Copy", MakeQuatPair, RunQuatCopy, 0},
    {"Quat_IsEqual", MakeQuatPair, RunQuatIsEqual, 9},
    {"MultiplyQuaternionAltOrder", MakeQuatPair, RunQuatAlt, 8},
    {"Quat_QuaternionMultiply", MakeQuatPair, RunQuatMultiply, 8},
    {"Quat_QuatTransToMat", MakeQuatPair, RunQuatTransToMat, 8},
    {"Quat_QuatToMat", MakeQuatPair, RunQuatToMat, 4},
    {"Quat_MatToQuat", MakeRotation, RunMatToQuat, 0},
    {"Quat_Slerp", MakeQuatPair, RunQuatSlerp, 9},
    {"Quat_Slerp_Acc", MakeSlerp, RunSlerpAcc, 9},
    {"Vec_Track", MakeTrack, RunTrack, 7},
    {"Mat_World2ViewMat", MakeMatrix, RunWorld2View, 0},
    {"Mat_Scale3f", MakeMatrix, RunScale3f, 0},
    {"Mat_GetDir", MakeMatrixVector, RunGetDir, 0},
    {"Mat_Copy", MakeMatrix, RunMatCopy, 0},
    {"Mat_CopyRot", MakeMatrix, RunMatCopyRot, 0},
    {"Mat_IdentityT", MakeMatrix, RunIdentityT, 0},
    {"Mat_Identity", MakeMatrix, RunIdentity, 0},
    {"RotTransMat", MakeTwoMatrices, RunRotTransMat, 0},
    {"Matrix_SetTrans", MakeMatrixVector, RunSetTrans, 0},
    {"Mat_Normalize", MakeMatrix, RunNormalize, 0},
    {"Mat_Align2Up", MakeAlign, RunAlign2Up, 0},
    {"Mat_Align2Dir", MakeAlign, RunAlign2Dir, 0},
    {"Vec_Normalise", MakeOne, RunNormalise, 3},
    {"Vec_NormaliseLen", MakeOne, RunNormaliseLen, 3},
    {"Vec_Magnitude", MakeOne, RunMagnitude, 3},
    {"Vec_Dist3D", MakeTwo, RunDist3D, 6},
    {"Vec_SqDist3D", MakeTwo, RunSqDist3D, 6},
    {"Vec_Subtract", MakeVectors, RunSubtract, 6},
    {"Vec_Zero", MakeVectors, RunZero, 0},
    {"Vec_Negate", MakeVectors, RunNegate, 3},
    {"Vec_Copy", MakeVectors, RunCopy, 3},
    {"Vec_MulR32", MakeVectors, RunMulR32, 3},
    {"Vec_Cross", MakeVectors, RunCross, 6},
    {"Vec_Copy2", MakeVectors, RunCopy2, 3},
    {"Vec_IsEqual", MakeVectors, RunIsEqual, 6},
    {"Vec_CrossNormalise", MakeVectors, RunCrossNormalise, 6},
    {"Vec_Max", MakeVectors, RunMax, 6},
    {"Vec_Min", MakeVectors, RunMin, 6},
    {"Vec_MinMax", MakeVectors, RunMinMax, 6},
    {"Vec_MinMax3", MakeVectors, RunMinMax3, 9},
    {"auxVec_AddMulR32", MakeVectors, RunAddMul, 6},
    {"Vec_Swap", MakeVectors, RunSwap, 6},
    {"Vec_ScalarTripleProduct", MakeVectors, RunTriple, 9},
    {"Vec_Add2", MakeVectors, RunAdd2, 6},
    {"vecutil_cartesian_to_spherical_acc", MakeVectors, RunCartesianAcc, 3},
    {"Vec_AngleDifference", MakeAnglePair, RunAngleDifference, 2},
    {"maybeAtan2", MakeAtan, RunAtan, 2},
    {"moveTowardsValue", MakeTowards, RunTowards, 3},
    {"normaliseToPlusMinusPi", MakeWrap, RunWrap, 1},
    {"View_RotTransMatrix", MakeRotTrans, RunViewRotTrans, 6},
    {"View_RotTransScaleMatrix", MakeRotTrans, RunViewRotTransScale, 9},
};

void RunExact(void) {
    for (const MathCase &c : kExactCases) {
        Suite suite = {c.name, 0, 0, 0};
        for (int t = 0; t < kTrials; t++) {
            Buf start;
            Fill(&start, sizeof(start));
            c.make(start);
            Buf a = start, b = start;
            uint32_t lcg = s_lcg;   // a run may draw (Quat_IsEqual's alias); both draw the same
            c.run(a, false);
            s_lcg = lcg;
            c.run(b, true);
            char inputs[160];
            Inputs(start, c.inputs, inputs, sizeof(inputs));
            Compare(suite, &a, &b, sizeof(Buf), sizeof(a.f), sizeof(a.d), "result", inputs);
        }
        Summary(suite);
    }
}


// ---- the random numbers

#define RandState (GameSpan<uint8_t>{0x0018cdf8})   // the two states and the two multipliers

void RunRandom(void) {
    uint8_t saved[16];
    XbeOursRead(saved, RandState.at, sizeof(saved));
    Suite suite = {"Rand_Random, Rand_Rand", 0, 0, 0};
    for (int t = 0; t < kTrials; t++) {
        uint32_t state[4];
        memcpy(state, saved, sizeof(state));
        state[0] = R(4) ? NextWord() : R(2) ? 0 : 0xffffffffu;
        state[1] = R(4) ? NextWord() : R(2) ? 0 : 0xffffffffu;
        if (R(8) == 0) {
            state[2] = NextWord();
            state[3] = NextWord();
        }
        uint32_t max = R(4) ? R(0x10001) : NextWord();
        bool random = R(2);
        struct Result {
            uint32_t state[4];
            uint32_t value;
        } a, b;
        XbeOursWrite(RandState.at, state, sizeof(state));
        if (random)
            a.value = Rand_Random();
        else
            a.value = Rand_Rand(max);
        XbeOursRead(a.state, RandState.at, sizeof(a.state));
        XbeOursWrite(RandState.at, state, sizeof(state));
        if (random)
            CALLR(b.value, true, 0x000d52c0, Rand_Random);
        else
            CALLR(b.value, true, 0x000d5310, Rand_Rand, max);
        XbeOursRead(b.state, RandState.at, sizeof(b.state));
        Compare(suite, &a, &b, sizeof(a), 0, 0, random ? "Rand_Random" : "Rand_Rand", "");
    }
    XbeOursWrite(RandState.at, saved, sizeof(saved));
    Summary(suite);
}

// ---- the world: views, cels and portals, objects, and the game's state they read

const int VIEWS = 11;
const int CELS = 8;
const int PORTALS = 16;
const int OBJS = 12;
const int GLISTS = 4;
const int MARKS = 4;
const int ENTRIES = 0x50;   // each view's draw lists: 0x20, 0x20 and 0x10 entries
const int HEAP = 0x4000;

struct World {
    viewer_tag views[VIEWS];
    DrawEntry entries[VIEWS][ENTRIES];
    world_tag world;
    cel_tag cels[CELS];
    portal_tag portals[PORTALS];
    obj_tag objs[OBJS];
    AnimState anims[4];
    uint8_t extra[OBJS][0x20];
    BLData bloke;
    celglist_tag glists[GLISTS];
    ATTACH_tag marks[MARKS];
    VisPortal polygons[3];      // ClipPortalWithPlane's in and out, and room for an out that overruns
    plane_equ_tag plane;
    _VECTOR v[4];
    uint8_t heap[HEAP];
    uint32_t heapUsed;
    int findCel;                // what build_FindCel answers (CELS: NULL)
    obj_tag *first;             // control_first_object's
    undefined4 spriteStart;     // what Sprite_BuildList answers
    uint spriteLast;
    uint spriteCount;
    uint8_t big[4][0x100];      // extra data larger than extra's: the searchlight's, the made objects'
    int createNext;
};
World world;

uint32_t W(const void *p) {
    uintptr_t a = (uintptr_t)p, base = (uintptr_t)&world;
    if (a >= base && a < base + sizeof(world))
        return 0xa0000000u | (uint32_t)(a - base);
    return (uint32_t)a;
}

// Game state the functions read or write, kept apart from the world
struct Area {
    unsigned at;
    unsigned size;
};
const Area kAreas[] = {
    {XbeOurs(0x001f661c), 11 * 4},       // glb_viewer
    {XbeOurs(0x001f6674), 4},            // glb_world
    {0x00260018, 4},            // MPSettings.isMultiplayer
    {0x0018cdf8, 16},           // the random states
    {XbeOurs(0x001f6580), 0x58},         // GameState
    {0x0029e828, 0x2a0a74 - 0x29e828},   // the vision's points, polygons, planes and cel list
    {XbeOurs(0x0029d79c), 4},            // GfxList
    {XbeOurs(0x0029dbf0), 12},           // CamPos
    {0x0029e800, 0x24},         // Tots, object_display_mask, SprCnt, OnlyScaleZ, FlipScale, the lightning, SkyTurn
    {XbeOurs(0x0029d7a0), 21 * 0x34},    // the sky's models
    {0x001f6668, 12},           // the sun's position
    {0x001f667c, 4},            // GlobalBlur
    {XbeOurs(0x001dec38), 0x80 * 4},     // Collide_StraddleCels' list
    {0x001fe6dc, 4},            // FlashOption
    {XbeOurs(0x0027911c), 4},            // FlashMod
    {0x001d7824, 4},            // bCharacterLight, bShadowCharacter
    {0x001f6654, 16},           // glb_players
    {XbeOurs(0x001df138), 256},          // switch_channels
    {XbeOurs(0x001f6610), 4},            // the widescreen flag
};

std::string s_saved;

void SaveAll(std::string &out) {
    out.assign((const char *)&world, sizeof(world));
    for (const Area &a : kAreas)
        {
        std::string part(a.size, 0);
        XbeOursRead(&part[0], a.at, a.size);
        out += part;
    }
}

void RestoreAll(const std::string &in) {
    memcpy(&world, in.data(), sizeof(world));
    size_t at = sizeof(world);
    for (const Area &a : kAreas) {
        XbeOursWrite(a.at, in.data() + at, a.size);
        at += a.size;
    }
}

// The world's state, then the areas, compared word by word: a NaN against a NaN is the choice where two NaNs met
bool OnlyNaNWords(const std::string &a, const std::string &b) {
    for (size_t i = 0; i < a.size(); i += 4) {
        size_t n = a.size() - i < 4 ? a.size() - i : 4;
        if (memcmp(a.data() + i, b.data() + i, n) == 0)
            continue;
        const uint8_t *x = (const uint8_t *)a.data() + i, *y = (const uint8_t *)b.data() + i;
        if (n < 4 || !(IsNaN32(x) && IsNaN32(y) && !QuietBitOnly32(x, y)))
            return false;
    }
    return true;
}

typedef uint32_t (*WorldRun)(bool original);

// Runs the original then ours from the same world: the state they leave, the calls they make and what they
// return must agree. A float return is passed as its bits.
void WorldCase(Suite &suite, const char *what, WorldRun run, bool floatResult = false) {
    std::string start, theirs, ours;
    SaveAll(start);
    ShadowLog_Clear();
    uint32_t theirResult = run(true);
    SaveAll(theirs);
    std::string theirLog = ShadowLog_Text();
    RestoreAll(start);
    ShadowLog_Clear();
    uint32_t ourResult = run(false);
    SaveAll(ours);
    std::string ourLog = ShadowLog_Text();
    suite.cases++;

    bool resultSame = theirResult == ourResult;
    bool resultNaN = floatResult && FromBits(theirResult) != FromBits(theirResult) &&
                     FromBits(ourResult) != FromBits(ourResult);
    bool stateSame = theirs == ours;
    if (resultSame && stateSame && theirLog == ourLog)
        return;
    bool nanChoice = theirLog == ourLog && (resultSame || resultNaN) && (stateSame || OnlyNaNWords(ours, theirs));
    int shown = nanChoice ? suite.nanChoices++ : suite.differ++;
    if (shown >= (nanChoice ? 2 : 4))
        return;
    if (!resultSame)
        printf("[viewmath] %s case %d: %s%s returns %08x, the original %08x\n", suite.name, suite.cases - 1,
               nanChoice ? "NaN chosen: " : "", what, ourResult, theirResult);
    if (!stateSame) {
        size_t i = 0;
        while (i < ours.size() && ours[i] == theirs[i])
            i++;
        i &= ~(size_t)3;
        uint32_t x = 0, y = 0;
        memcpy(&x, ours.data() + i, 4);
        memcpy(&y, theirs.data() + i, 4);
        if (i < sizeof(world))
            printf("[viewmath] %s case %d: %s%s world +0x%x: ours %08x, the original's %08x\n", suite.name,
                   suite.cases - 1, nanChoice ? "NaN chosen: " : "", what, (unsigned)i, x, y);
        else
            printf("[viewmath] %s case %d: %s%s state +0x%x past the world: ours %08x, the original's %08x\n",
                   suite.name, suite.cases - 1, nanChoice ? "NaN chosen: " : "", what, (unsigned)(i - sizeof(world)),
                   x, y);
    }
    if (theirLog != ourLog)
        printf("[viewmath] %s case %d: %s calls differ\n  ours:     %.600s\n  original: %.600s\n", suite.name,
               suite.cases - 1, what, ourLog.c_str(), theirLog.c_str());
}

// ---- calling the register-argument originals and our adaptors

struct Regs {
    uint32_t eax, ebx, ecx, edx, esi, edi;
};

__declspec(naked) uint32_t __cdecl CallRegs(unsigned at, const Regs *r, int argc, const uint32_t *args) {
    _asm {
        push ebp
        mov ebp, esp
        push ebx
        push esi
        push edi
        mov ecx, [ebp + 16]
        mov edx, [ebp + 20]
    pushing:
        test ecx, ecx
        jz pushed
        push dword ptr [edx + ecx * 4 - 4]
        dec ecx
        jmp pushing
    pushed:
        mov eax, [ebp + 12]
        mov ebx, [eax + 4]
        mov ecx, [eax + 8]
        mov edx, [eax + 12]
        mov esi, [eax + 16]
        mov edi, [eax + 20]
        mov eax, [eax]
        call dword ptr [ebp + 8]
        mov ecx, [ebp + 16]
        lea esp, [esp + ecx * 4]
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret
    }
}

// The original at 'at', or our adaptor, with these registers and stack arguments
uint32_t CallWith(bool original, unsigned at, const void *ours, const Regs &r, int argc, const uint32_t *args) {
    if (original) {
        XbeOriginalScope scope(at);
        return CallRegs(at, &r, argc, args);
    }
    return CallRegs((unsigned)(uintptr_t)ours, &r, argc, args);
}

uint32_t P(const void *p) {
    return (uint32_t)(uintptr_t)p;
}

// ---- the fakes

void *__cdecl FakeMalloc(size_t size, MallocFlags flags, uint32_t align) {
    ShadowLog("malloc(%x,%x,%x)", (uint)size, (uint)(ushort)flags, align);
    if (world.heapUsed + size + 16 > HEAP)
        return NULL;
    void *p = world.heap + world.heapUsed;
    world.heapUsed = (world.heapUsed + (uint32_t)size + 15) & ~15u;
    return p;
}
void __cdecl FakeFree(void **p) {
    ShadowLog("free(%x)", W(*p));
    *p = NULL;
}
void __cdecl FakeHeadPos(_VECTOR *head, obj_tag *player, _MATRIX *mtx) {
    ShadowLog("head(%x,%x)", W(player), W(mtx));   // head is often the caller's stack
    head->x = player->position.x;
    head->y = player->position.y + 1.5f;
    head->z = player->position.z;
}
cel_tag *__cdecl FakeFindCel(_VECTOR *pos, world_tag *in) {
    ShadowLog("findcel(%08x,%08x,%08x,%x)", Bits(pos->x), Bits(pos->y), Bits(pos->z), W(in));
    return world.findCel < CELS ? &world.cels[world.findCel] : NULL;
}
bool __cdecl FakeDoorClosed(obj_tag *door) {
    ShadowLog("door(%x)", W(door));
    return door != NULL && (door->curState & 1);
}
void **__cdecl FakeClosestLights(_VECTOR *sphere, uint mask, ushort *count) {
    ShadowLog("lights(%08x,%08x,%08x,%08x,%x)", Bits(sphere->x), Bits(sphere->y), Bits(sphere->z),
              Bits(((float *)sphere)[3]), mask & 0xffff);
    *count = (ushort)(mask & 3);
    return (void **)(uintptr_t)(0x1230 + (mask & 0xff));
}
void __cdecl FakeSetLights(void **lights, ushort count, float r, float g, float b) {
    ShadowLog("setlights(%x,%d,%08x,%08x,%08x)", W(lights), count, Bits(r), Bits(g), Bits(b));
}
void __cdecl FakeTweak(uint8_t a, uint8_t r, uint8_t g, uint8_t b) {
    ShadowLog("tweak(%02x,%02x,%02x,%02x)", a, r, g, b);
}
void __cdecl FakeDrawMatrix(celglist_tag *glist, _MATRIX *m) {
    char text[200];
    int at = snprintf(text, sizeof(text), "draw(%x", W(glist));
    for (int i = 0; i < 15; i++)
        if (i % 4 != 3)
            at += snprintf(text + at, sizeof(text) - at, ",%08x", Bits(m->m[i]));
    ShadowLog("%s)", text);
}
void __cdecl FakeFog(char enable, float nearDist, float farDist, uint8_t r, uint8_t g, uint8_t b) {
    ShadowLog("fog(%d,%08x,%08x,%x,%x,%x)", enable, Bits(nearDist), Bits(farDist), r, g, b);
}
void __cdecl FakeBlend(int opaque) {
    ShadowLog("blend(%d)", opaque);
}
void __cdecl FakeUseCamera(void *viewer) {
    ShadowLog("camera(%x)", W(viewer));
}
void __cdecl FakeBlur(uint8_t amount) {
    ShadowLog("blur(%x)", amount);
}
void __cdecl FakePreDraw(void) { ShadowLog("predraw"); }
void __cdecl FakePostDraw(void) { ShadowLog("postdraw"); }
void __cdecl FakeClearZ(void) { ShadowLog("clearz"); }
void __cdecl FakeShardStates(void) { ShadowLog("shardstates"); }
void __cdecl FakeShards(void) { ShadowLog("shards"); }
void __cdecl FakeDrops(void) { ShadowLog("drops"); }
void __cdecl FakeFade(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    ShadowLog("fade(%x,%x,%x,%x)", r, g, b, a);
}
sprite **__cdecl FakeBuildList(ushort viewer, undefined4 *start, uint *last, uint *count) {
    ShadowLog("sprites(%d)", viewer);
    *start = world.spriteStart + viewer;
    *last = viewer & 1 ? 0 : world.spriteLast;
    *count = viewer & 2 ? 0 : world.spriteCount;
    return (sprite **)(uintptr_t)(0x5000 + viewer);
}
void __cdecl FakeDrawSprites(viewer_tag *viewer, sprite **list, uint first, uint last) {
    ShadowLog("drawsprites(%x,%x,%x,%x)", W(viewer), W(list), first, last);
}
void __cdecl FakeAnimDraw(obj_tag *obj, viewer_tag *viewer) {
    ShadowLog("animdraw(%x,%x)", W(obj), W(viewer));
}
void __cdecl FakeEmitterDraw(obj_tag *obj, viewer_tag *viewer) {
    ShadowLog("emitter(%x,%x)", W(obj), W(viewer));
}
void *__cdecl FakeSound(uint sfx, float volume, ushort pitch, uint p4) {
    ShadowLog("sound(%x,%08x,%x,%x)", sfx, Bits(volume), pitch, p4);
    return NULL;
}
obj_tag *__cdecl FakeFirstObject(void) {
    return world.first;
}

// Game_Draw's own callees in this parcel
void __cdecl LogCapture(viewer_tag *viewer) {
    ShadowLog("capture(%x)", W(viewer));
}
__declspec(naked) void FakeCapture(void) {
    _asm {
        push eax
        call LogCapture
        add esp, 4
        ret
    }
}
void __cdecl FakeDrawSky(viewer_tag *viewer, char pass) {
    ShadowLog("sky(%x,%d)", W(viewer), pass);
}
void __cdecl FakeDrawList(viewer_tag *viewer, ushort list) {
    ShadowLog("list(%x,%d)", W(viewer), list);
}

void InstallFakes(void) {
    ShadowHook_Install(0x00070ae0, (const void *)&FakeMalloc);
    ShadowHook_Install(0x000705f0, (const void *)&FakeFree);
    ShadowHook_Install(0x000a89a0, (const void *)&FakeHeadPos);
    ShadowHook_Install(0x00020c30, (const void *)&FakeFindCel);
    ShadowHook_Install(0x0002f520, (const void *)&FakeDoorClosed);
    ShadowHook_Install(0x0006d8f0, (const void *)&FakeClosestLights);
    ShadowHook_Install(0x000de610, (const void *)&FakeSetLights);
    ShadowHook_Install(0x000ddf00, (const void *)&FakeTweak);
    ShadowHook_Install(0x000dde50, (const void *)&FakeDrawMatrix);
    ShadowHook_Install(0x000ddb00, (const void *)&FakeFog);
    ShadowHook_Install(0x000dd930, (const void *)&FakeBlend);
    ShadowHook_Install(0x000dd950, (const void *)&FakeUseCamera);
    ShadowHook_Install(0x000ddaf0, (const void *)&FakeBlur);
    ShadowHook_Install(0x000dd150, (const void *)&FakePreDraw);
    ShadowHook_Install(0x000dd8e0, (const void *)&FakePostDraw);
    ShadowHook_Install(0x000dfb30, (const void *)&FakeClearZ);
    ShadowHook_Install(0x000dfac0, (const void *)&FakeShardStates);
    ShadowHook_Install(0x0001fe50, (const void *)&FakeShards);
    ShadowHook_Install(0x000a3560, (const void *)&FakeDrops);
    ShadowHook_Install(0x000de560, (const void *)&FakeFade);
    ShadowHook_Install(0x000ced20, (const void *)&FakeBuildList);
    ShadowHook_Install(0x000d9410, (const void *)&FakeDrawSprites);
    ShadowHook_Install(0x00016c20, (const void *)&FakeAnimDraw);
    ShadowHook_Install(0x00068930, (const void *)&FakeEmitterDraw);
    ShadowHook_Install(0x000cb940, (const void *)&FakeSound);
    ShadowHook_Install(0x0002cfc0, (const void *)&FakeFirstObject);
}

void InstallDrawFakes(void) {
    ShadowHook_Install(0x000dabb0, (const void *)&FakeCapture);
    ShadowHook_Install(0x000d9d60, (const void *)&FakeDrawSky);
    ShadowHook_Install(0x000da890, (const void *)&FakeDrawList);
}

// ---- making the world

// Mostly ordinary, now and then an edge
float Value(float range) {
    return R(12) ? Uniform(range) : Coordinate();
}

void RandomMatrix(_MATRIX *m) {
    _VECTOR angles = {Uniform(7.0f), Uniform(7.0f), Uniform(7.0f)};
    RotMatrix(&angles, m);
    for (int i = 12; i < 15; i++)
        m->m[i] = Value(50.0f);
    for (int i = 3; i < 12; i += 4)
        m->m[i] = FromBits(NextWord());
    if (R(16) == 0)
        for (int i = 0; i < 15; i++)
            m->m[i] = Coordinate();
}

_VECTOR RandomPoint(float range) {
    _VECTOR v = {Value(range), Value(range), Value(range)};
    return v;
}

cel_tag *RandomCel(void) {
    int i = R(CELS + 2);
    return i < CELS ? &world.cels[i] : NULL;
}
obj_tag *RandomObj(void) {
    int i = R(OBJS + 2);
    return i < OBJS ? &world.objs[i] : NULL;
}
celglist_tag *RandomGlist(void) {
    int i = R(GLISTS + 1);
    return i < GLISTS ? &world.glists[i] : NULL;
}

uint RandomBits(const uint *bits, int count) {
    uint value = 0;
    for (int i = 0; i < count; i++)
        if (R(3) == 0)
            value |= bits[i];
    return value;
}

void MakeViewer(int i) {
    viewer_tag &v = world.views[i];
    memset(&v, 0, sizeof(v));
    v.world = &world.world;
    v.someCel = R(6) ? &world.cels[R(4)] : RandomCel();
    v.draw.sizes[0] = 0x20;
    v.draw.sizes[1] = 0x20;
    v.draw.sizes[2] = 0x10;
    if (R(8)) {
        v.draw.lists[0] = &world.entries[i][0];
        v.draw.lists[1] = &world.entries[i][0x20];
        v.draw.lists[2] = &world.entries[i][0x40];
    }
    for (int k = 0; k < 3; k++)
        v.draw.counts[k] = (ushort)(R(3) ? R(v.draw.sizes[k]) : 0);
    v.draw.skyVisible = (uchar)R(2);
    v.camMode = R(3);
    v.idx = (undefined1)(R(4) ? i : R(8));
    v.enabled = (undefined1)(R(4) != 0);
    v.spritesShown = (undefined1)R(2);
    v.cameraUpdated = (undefined1)R(2);
    v.pos = RandomPoint(40.0f);
    v.prevPos = R(3) ? v.pos : RandomPoint(40.0f);
    if (R(3) == 0)
        v.prevPos.x = v.pos.x + Uniform(3.0f);
    v.someVec = RandomPoint(7.0f);
    v.zoom = R(6) ? 1.0f + Positive(3.0f) : Coordinate();
    v.AspectRatio = R(6) ? 1.3333334f : Coordinate();
    v.fovRadians = R(6) ? 0.3f + Positive(2.0f) : Angle();
    v.width = R(6) ? 160.0f + Positive(1800.0f) : Coordinate();
    v.height = R(6) ? 120.0f + Positive(1000.0f) : Coordinate();
    RandomMatrix(&v.viewMatrix);
    RandomMatrix(&v.worldMatrix);
    RandomMatrix(&v.mtx);
    v.apocalypseEffect = Value(5.0f);
    v.swingPhase = R(4) ? Uniform(7.0f) : Angle();
    v.player = R(2) ? &world.objs[R(OBJS)] : NULL;
    v.fade = R(4) ? Uniform(1.2f) : Coordinate();
    v.fadeRate = R(4) ? Uniform(0.3f) : Coordinate();
    for (int k = 0; k < 4; k++)
        v.fadeColour[k] = (uchar)NextWord();
    v.fadeHold = (undefined1)R(2);
    v.fadeState = (undefined1)R(4);
    v.nightVisionRelated = (undefined2)R(3);
    for (int k = 0; k < 4; k++) {
        _VECTOR p[3] = {RandomPoint(30.0f), RandomPoint(30.0f), RandomPoint(30.0f)};
        Plane_PlaneEq(&v.frustumPlanes[k], &p[0], &p[1], &p[2]);
    }
}

void MakeCel(int i) {
    cel_tag &c = world.cels[i];
    memset(&c, 0, sizeof(c));
    c.someMin = RandomPoint(30.0f);
    c.someMax = c.someMin;
    c.someMax.x += Positive(40.0f);
    c.someMax.y += Positive(40.0f);
    c.someMax.z += Positive(40.0f);
    if (R(8) == 0)
        c.someMin.y = Coordinate();
    c.glist = R(4) ? &world.glists[R(GLISTS)] : NULL;
    static const uint kPlacement[] = {0x10000000, 0x08000000, 0x00008018, 0x0000c023};
    c.bitsFromPlacementTag = RandomBits(kPlacement, 4);
    if ((c.bitsFromPlacementTag & 0x8018) == 0x8018 && (c.bitsFromPlacementTag & 0xc023) == 0xc023)
        c.bitsFromPlacementTag &= ~0x4000u;
    static const uint kFlags[] = {1, 0x2000, 0x40000, 8, 4, 0x20, 0x100};
    c.flags84 = RandomBits(kFlags, 7);
    c.sphere = RandomPoint(30.0f);
    c.sphereRadius = R(8) ? Positive(20.0f) : Coordinate();
    c.position = RandomPoint(30.0f);
    c.rotation = RandomPoint(7.0f);
    c.blink = (uchar)NextWord();
    c.addedToDraw = (undefined)R(2);
    c.attachments = R(3) ? NULL : &world.marks[R(MARKS)];
}

void MakePortal(int i) {
    portal_tag &p = world.portals[i];
    memset(&p, 0, sizeof(p));
    p.partner = &world.portals[R(PORTALS)];
    p.cel = &world.cels[R(CELS)];
    p.door = R(3) ? NULL : &world.objs[R(OBJS)];
    _VECTOR centre = RandomPoint(20.0f);
    float w = Positive(8.0f) + 0.01f, h = Positive(8.0f) + 0.01f;
    int axis = R(3);
    for (int k = 0; k < 4; k++) {
        _VECTOR q = centre;
        float a = (k == 1 || k == 2) ? w : -w;
        float b = k >= 2 ? h : -h;
        if (axis == 0) { q.y += a; q.z += b; }
        else if (axis == 1) { q.x += a; q.z += b; }
        else { q.x += a; q.y += b; }
        p.corners[k] = q;
    }
    if (R(10) == 0)
        p.corners[R(4)] = RandomPoint(1e30f);
    if (R(2)) {
        _VECTOR swap = p.corners[1];
        p.corners[1] = p.corners[3];
        p.corners[3] = swap;
    }
    Plane_PlaneEq(&p.plane, &p.corners[0], &p.corners[1], &p.corners[2]);
    p.centre = centre;
    p.boxMin = p.corners[0];
    p.boxMax = p.corners[0];
    for (int k = 1; k < 4; k++) {
        const float *q = &p.corners[k].x;
        float *lo = &p.boxMin.x, *hi = &p.boxMax.x;
        for (int j = 0; j < 3; j++) {
            if (q[j] < lo[j])
                lo[j] = q[j];
            if (q[j] > hi[j])
                hi[j] = q[j];
        }
    }
    p.open = (uchar)(R(5) != 0);
    p.seen = (uchar)R(2);
}

void MakeObj(int i) {
    obj_tag &o = world.objs[i];
    memset(&o, 0, sizeof(o));
    static const uint kTransform[] = {0x10, 0x100, 8, 1, 2};
    o.transformFlags = (ushort)RandomBits(kTransform, 5);
    o.displayMask = (ushort)(R(4) ? 0x1f : NextWord() & 0x1f);
    static const uint kEffect[] = {0x8000, 0x10, 0x400, 1, 0x2000, 8, 4, 0x20000, 0x80000, 0x800};
    o.effectFlags = (int)RandomBits(kEffect, 10);
    o.centrePoint = RandomPoint(30.0f);
    o.radius = R(6) ? Positive(3.0f) : Coordinate();
    o.objGraphics = R(5) ? &world.glists[R(GLISTS)] : NULL;
    o.animState = R(3) ? &world.anims[R(4)] : NULL;
    o.tweakR = (uchar)NextWord();
    o.tweakG = (uchar)NextWord();
    o.tweakB = (uchar)NextWord();
    o.tweakA = (uchar)NextWord();
    o.ambientR = (uchar)NextWord();
    o.ambientG = (uchar)NextWord();
    o.ambientB = (uchar)NextWord();
    o.flags = (char)NextWord();
    static const char kTypes[] = {2, 2, OBJECTTYPE_CLOUD, 5, 1};
    o.objectType = kTypes[R(5)];
    o.inCel = RandomCel();
    o.position = RandomPoint(30.0f);
    o.rotation = RandomPoint(7.0f);
    if (R(3) == 0)
        o.rotation.z = R(2) ? 0.0f : -0.0f;
    o.scale = R(3) ? 1.0f : Value(2.0f);
    RandomMatrix(&o.transformMatrix);
    o.attachments = R(3) ? NULL : &world.marks[R(MARKS)];
    o.extraObjectData = i == 0 ? (void *)&world.bloke : (void *)world.extra[i];
    o.creationTimeFrames = (int)R(1000);
    o.curState = (ushort)NextWord();
}

void MakeWorld(void) {
    memset(&world, 0, sizeof(world));
    for (int i = 0; i < VIEWS; i++)
        MakeViewer(i);
    for (int i = 0; i < CELS; i++)
        MakeCel(i);
    for (int i = 0; i < PORTALS; i++)
        MakePortal(i);
    for (int i = 0; i < OBJS; i++)
        MakeObj(i);
    for (int i = 0; i < GLISTS; i++) {
        world.glists[i].geom_idx = i + 1;
        static const uint kGlist[] = {0x10, 0x80000, 0x400, 8, 2};
        world.glists[i].applyFlagsToObject = RandomBits(kGlist, 5);
    }
    for (int i = 0; i < MARKS; i++) {
        ATTACH_tag &a = world.marks[i];
        a.next = i + 1 < MARKS && R(2) ? &world.marks[i + 1] : NULL;
        a.glist = RandomGlist();
        a.parent = R(2) ? &world.objs[R(OBJS)] : NULL;
        RandomMatrix(&a.matrix);
    }
    for (int i = 0; i < OBJS; i++)
        for (int k = 0; k < 0x20; k += 4)
            *(int32_t *)&world.extra[i][k] = R(4) ? (int32_t)R(40) - 20 : (int32_t)NextWord();
    world.bloke.pitchFromHorizontal = R(6) ? Uniform(1.0f) : Coordinate();

    // The world's cels in a list; cels 0-3 hold 4-7 in chains of inside cels
    world.world.firstCel = &world.cels[0];
    for (int i = 0; i + 1 < CELS; i++)
        world.cels[i].nextCel = &world.cels[i + 1];
    for (int i = 0; i < 4; i++)
        world.cels[i].childCel = R(3) ? &world.cels[4 + R(4)] : NULL;
    for (int i = 4; i < CELS; i++)
        world.cels[i].childCel = i + 1 < CELS && R(2) ? &world.cels[i + 1] : NULL;
    // Each cel's portals in a chain, and its objects
    for (int i = 0; i < CELS; i++)
        world.cels[i].portals = R(5) ? &world.portals[2 * i] : NULL;
    for (int i = 0; i < PORTALS; i++)
        world.portals[i].next = i % 2 == 0 && R(3) ? &world.portals[i + 1] : NULL;
    for (int i = 0; i < OBJS; i++)
        world.objs[i].nextInCel = NULL;
    for (int i = 0; i < OBJS; i++) {
        cel_tag &c = world.cels[R(CELS)];
        world.objs[i].nextInCel = c.firstObject;
        c.firstObject = &world.objs[i];
    }
    world.first = R(6) ? &world.objs[R(OBJS)] : NULL;
    for (int i = 0; i + 1 < OBJS; i++)
        world.objs[i].nextObject = R(5) ? &world.objs[i + 1] : NULL;
    // The frame's visible cels
    cel_tag *last = NULL;
    GfxList = NULL;
    for (int i = 0; i < CELS; i++) {
        if (R(2)) {
            if (last == NULL)
                GfxList = &world.cels[i];
            else
                last->nextVisible = &world.cels[i];
            last = &world.cels[i];
        }
    }
    world.findCel = R(CELS + 2);
    world.spriteStart = R(3) ? 0 : R(20);
    world.spriteLast = R(3) ? 0 : R(20);
    world.spriteCount = R(3) ? 0 : R(20);

    // The game's state
    for (int i = 0; i < VIEWS; i++)
        glb_viewer[i] = R(8) ? &world.views[i] : NULL;
    glb_world = &world.world;
    MPSettings.isMultiplayer = R(2);
    GameState.NumFramesUnpaused = NextWord();
    GameState.SomeAlternatePauseState = (undefined)(R(4) == 0);
    GameState.CurrentLevelHashcode = (HASHCODE)(R(3) ? 0x0700001b : 0x07000002);
    GameState.maybeLoadingBlobs = NextWord();
    U32_AT(XbeOurs(0x0029e80c)) = 1u << R(5);    // object_display_mask
    *(_VECTOR *)XbeOurs(0x0029dbf0) = RandomPoint(30.0f);   // CamPos
    U8_AT(0x001fe6dc) = (uint8_t)R(2);  // FlashOption
    FLOAT_AT(XbeOurs(0x0027911c)) = R(6) ? 0.5f + Positive(0.5f) : Coordinate();   // FlashMod
    U8_AT(XbeOurs(0x001f667c)) = (uint8_t)NextWord();   // GlobalBlur
    U32_AT(XbeOurs(0x0029e808)) = R(2);          // Tots[2]
    U8_AT(XbeOurs(0x0029e81b)) = (uint8_t)R(2);  // ThunderNear
    U32_AT(XbeOurs(0x0029e820)) = R(4) ? R(2000) : NextWord();   // SkyTurn
    for (int i = 0; i < 21; i++) {
        uint8_t *sky = (uint8_t *)(uintptr_t)(XbeOurs(0x0029d7a0) + i * 0x34);
        memset(sky, 0, 0x34);
        *(celglist_tag **)(sky + 0x00) = R(3) ? NULL : RandomGlist();
        *(celglist_tag **)(sky + 0x04) = R(3) ? NULL : RandomGlist();
        *(obj_tag **)(sky + 0x08) = R(3) ? NULL : RandomObj();
        *(obj_tag **)(sky + 0x0c) = R(3) ? NULL : RandomObj();
        *(_VECTOR *)(sky + 0x10) = RandomPoint(30.0f);
        *(_VECTOR *)(sky + 0x1c) = RandomPoint(7.0f);
        *(ushort *)(sky + 0x28) = (ushort)R(200);
        *(ushort *)(sky + 0x2a) = (ushort)R(10);
        *(short *)(sky + 0x2c) = (short)(R(4) ? R(5) : NextWord());
        *(short *)(sky + 0x2e) = (short)(R(4) ? R(5) : NextWord());
        *(short *)(sky + 0x30) = (short)(R(4) ? R(5) : NextWord());
        *(sky + 0x32) = (uint8_t)R(2);
    }
    // The vision's state: a frustum for depth 0 and a few cels found
    VisPortal *polygon = &world.polygons[0];
    polygon->count = (char)(R(4) + 1);
    U32_AT(XbeOurs(0x0029ea58)) = R(4) ? R(10) : R(110);   // VisionRecurseCount
    U16_AT(XbeOurs(0x002a0a70)) = (ushort)(R(3) ? R(10) : R(65));   // VisCelCount
    for (int i = 0; i < 64; i++) {
        *(cel_tag **)(uintptr_t)(0x002a0870 + i * 8) = &world.cels[R(CELS)];
        *(ushort *)(uintptr_t)(0x002a0874 + i * 8) = (ushort)R(8);
        *(ushort *)(uintptr_t)(0x002a0876 + i * 8) = 0;
    }
}

// The view's own frustum set up as Vision_Calculate_View_Fustrum would (ours, checked apart)
void SetFrustum(viewer_tag *viewer) {
    Vision_Calculate_View_Fustrum(viewer);
}

// ---- the camera

int s_view;
float s_float;
uint s_word;
char s_flag;

viewer_tag *s_viewer;
_VECTOR *s_vecA, *s_vecB;
obj_tag *s_obj;
cel_tag *s_cel;

uint32_t RunCameraSet(bool o) { CALL(o, 0x000240c0, Camera_Set, s_viewer, s_vecA, s_vecB); return 0; }
uint32_t RunPlaceAround(bool o) { CALL(o, 0x000245a0, Camera_PlaceAround, s_vecA, s_viewer, s_float); return 0; }
uint32_t RunPlaceAbove(bool o) { CALL(o, 0x00024610, Camera_PlaceAbove, s_vecA, s_viewer, s_float); return 0; }
uint32_t RunPlaceOver(bool o) {
    float posy = FLOAT_AT(XbeOurs(0x001fec50));   // debug_cam_posy
    FLOAT_AT(XbeOurs(0x001fec50)) = s_vecB->z;
    CALL(o, 0x000246a0, Camera_PlaceOver, s_obj, s_viewer, s_float, s_vecB->y);
    FLOAT_AT(XbeOurs(0x001fec50)) = posy;
    return 0;
}
uint32_t RunLookAtObject(bool o) { CALL(o, 0x00024d40, Camera_LookAtObject, s_obj, s_viewer); return 0; }
uint32_t RunPlaceBehind(bool o) {
    CALL(o, 0x00024710, Camera_PlaceBehind, s_vecA, s_vecB, s_viewer, s_float, s_vecB->x);
    return 0;
}
uint32_t RunCameraEnable(bool o) {
    CALL(o, 0x00024150, Camera_Enable, (ushort)s_view, s_flag, (undefined1)s_word, s_vecA);
    return 0;
}
uint32_t RunSetFade(bool o) {
    bool r;
    CALLR(r, o, 0x000243b0, Camera_SetFade, (ushort)s_view, s_float, s_word, (undefined1)s_flag);
    return (uint8_t)r;
}
uint32_t RunUpdateGlbVars(bool o) { CALL(o, 0x00024fd0, Camera_UpdateGlbVars, (ushort)s_view); return 0; }
uint32_t RunApplySwing(bool o) { CALL(o, 0x00024490, Camera_ApplySwing, s_viewer, s_float); return 0; }
uint32_t RunSetToPlayer(bool o) { CALL(o, 0x00024530, Camera_SetToPlayer, s_obj, s_viewer); return 0; }
uint32_t RunShake(bool o) { CALL(o, 0x00025330, Camera_Shake, s_vecA, s_float); return 0; }
uint32_t RunCheckLocation(bool o) { CALL(o, 0x000250a0, Camera_CheckLocation, (ushort)s_view); return 0; }
uint32_t RunViewerInit(bool o) { CALL(o, 0x00024290, Viewer_Init, s_viewer); return 0; }
uint32_t RunAllocViewer(bool o) {
    viewer_tag *v;
    CALLR(v, o, 0x00020610, build_alloc_viewer, (unsigned char)s_word);
    return W(v);
}
uint32_t RunInBox(bool o) {
    bool r;
    CALLR(r, o, 0x00020b70, build_point_in_bounding_box, s_vecA, s_cel);
    return (uint8_t)r;
}
uint32_t RunLeftCel(bool o) {
    cel_tag *r;
    CALLR(r, o, 0x000d8810, Cel_ObjectLeftCel, s_cel, s_vecA, s_vecB);
    return W(r);
}

const int kWorldTrials = 6000;

void RunCamera(void) {
    struct {
        const char *name;
        WorldRun run;
    } cases[] = {
        {"Camera_Set", RunCameraSet},
        {"Camera_PlaceAround", RunPlaceAround},
        {"Camera_PlaceAbove", RunPlaceAbove},
        {"Camera_PlaceOver", RunPlaceOver},
        {"Camera_PlaceBehind", RunPlaceBehind},
        {"Camera_LookAtObject", RunLookAtObject},
        {"Camera_Enable", RunCameraEnable},
        {"Camera_SetFade", RunSetFade},
        {"Camera_UpdateGlbVars", RunUpdateGlbVars},
        {"Camera_ApplySwing", RunApplySwing},
        {"Camera_SetToPlayer", RunSetToPlayer},
        {"Camera_Shake", RunShake},
        {"Camera_CheckLocation", RunCheckLocation},
        {"Viewer_Init", RunViewerInit},
        {"build_alloc_viewer", RunAllocViewer},
        {"build_point_in_bounding_box", RunInBox},
        {"Cel_ObjectLeftCel", RunLeftCel},
    };
    for (auto &c : cases) {
        Suite suite = {c.name, 0, 0, 0};
        for (int t = 0; t < kWorldTrials; t++) {
            MakeWorld();
            s_view = R(13) < 11 ? R(11) : R(4);
            s_viewer = R(10) ? &world.views[R(VIEWS)] : NULL;
            world.v[0] = RandomPoint(30.0f);
            world.v[1] = RandomPoint(7.0f);
            s_vecA = R(10) ? &world.v[0] : NULL;
            s_vecB = R(10) ? &world.v[1] : NULL;
            s_obj = R(10) ? &world.objs[0] : NULL;
            s_cel = RandomCel();
            s_flag = (char)R(2);
            s_word = R(3) ? (NextWord() | 0xff) : NextWord();
            s_float = R(3) ? Uniform(60.0f) : Coordinate();
            if (c.run == RunShake) {
                s_vecA = &world.v[0];
                s_float = R(3) ? Positive(30.0f) : Coordinate();
            }
            if (c.run == RunViewerInit)
                s_viewer = &world.views[R(VIEWS)];
            if (c.run == RunPlaceAround || c.run == RunPlaceAbove || c.run == RunPlaceOver || c.run == RunPlaceBehind ||
                c.run == RunLookAtObject) {
                s_vecA = &world.v[0];
                s_vecB = &world.v[1];
                s_obj = &world.objs[0];
                s_viewer = &world.views[R(VIEWS)];
                s_float = R(3) ? Positive(1.0f) : Coordinate();
            }
            if (c.run == RunApplySwing)
                s_float = R(3) ? Uniform(0.2f) : Coordinate();
            if (c.run == RunInBox) {
                s_vecA = &world.v[0];
                if (s_cel != NULL && R(2)) {   // inside, or on a face
                    world.v[0].x = s_cel->someMin.x + Positive(s_cel->someMax.x - s_cel->someMin.x);
                    world.v[0].y = R(4) ? s_cel->someMin.y + Positive(s_cel->someMax.y - s_cel->someMin.y) : s_cel->someMax.y;
                    world.v[0].z = R(4) ? s_cel->someMin.z + Positive(s_cel->someMax.z - s_cel->someMin.z) : s_cel->someMin.z;
                }
            }
            if (c.run == RunLeftCel || c.run == RunCheckLocation) {
                // A segment through one of the cel's portals, now and then
                cel_tag *cel = c.run == RunLeftCel ? s_cel : glb_viewer[s_view] ? glb_viewer[s_view]->someCel : NULL;
                _VECTOR *from = c.run == RunLeftCel ? &world.v[0] : glb_viewer[s_view] ? &glb_viewer[s_view]->prevPos : NULL;
                _VECTOR *to = c.run == RunLeftCel ? &world.v[1] : glb_viewer[s_view] ? &glb_viewer[s_view]->pos : NULL;
                if (c.run == RunLeftCel) {
                    s_vecA = from;
                    s_vecB = to;
                }
                if (cel != NULL && cel->portals != NULL && from != NULL && R(3)) {
                    portal_tag *p = R(2) || !cel->portals->next ? cel->portals : cel->portals->next;
                    float u = Positive(1.0f), w = Positive(1.0f);
                    _VECTOR at;
                    at.x = p->corners[0].x + (p->corners[1].x - p->corners[0].x) * u + (p->corners[3].x - p->corners[0].x) * w;
                    at.y = p->corners[0].y + (p->corners[1].y - p->corners[0].y) * u + (p->corners[3].y - p->corners[0].y) * w;
                    at.z = p->corners[0].z + (p->corners[1].z - p->corners[0].z) * u + (p->corners[3].z - p->corners[0].z) * w;
                    float back = Positive(4.0f), ahead = R(3) ? Positive(4.0f) : -Positive(0.5f);
                    from->x = at.x - p->plane.a * back;
                    from->y = at.y - p->plane.b * back;
                    from->z = at.z - p->plane.c * back;
                    to->x = at.x + p->plane.a * ahead;
                    to->y = at.y + p->plane.b * ahead;
                    to->z = at.z + p->plane.c * ahead;
                }
            }
            WorldCase(suite, c.name, c.run);
        }
        Summary(suite);
    }
}

// ---- the vision

int s_depth;
plane_equ_tag *s_plane;

uint32_t RunFrustum(bool o) { CALL(o, 0x000db5c0, Vision_Calculate_View_Fustrum, s_viewer); return 0; }
uint32_t RunInitPortal(bool o) { CALL(o, 0x000db940, Vision_Init_Portal_Recurse, s_viewer); return 0; }
uint32_t RunClip(bool o) {
    bool r;
    CALLR(r, o, 0x000db7c0, ClipPortalWithPlane, s_plane, &world.polygons[0], &world.polygons[1]);
    return (uint8_t)r;
}
uint32_t RunRecurse(bool o) { CALL(o, 0x000db9f0, Vision_Recurse, s_cel, s_depth); return 0; }
uint32_t RunPortalRecurse(bool o) { CALL(o, 0x000dbc40, Vision_Portal_Recurse, s_viewer); return 0; }
uint32_t RunAddCel(bool o) { CALL(o, 0x000db540, Vision_AddCelToDraw, s_cel, (ushort)s_word); return 0; }
uint32_t RunDisplayList(bool o) { CALL(o, 0x000db4b0, vision_generate_display_list, s_viewer); return 0; }
uint32_t RunCamPos(bool o) { CALL(o, 0x000db5a0, vision_GetCamPos, &world.v[2]); return 0; }

#define VisionPlanes ((plane_equ_tag *)0x0029ea60)
#define VisionPlaneCounts (GameSpan<int>{0x0029e998})

void RunVision(void) {
    struct {
        const char *name;
        WorldRun run;
    } cases[] = {
        {"Vision_Calculate_View_Fustrum", RunFrustum},
        {"Vision_Init_Portal_Recurse", RunInitPortal},
        {"ClipPortalWithPlane", RunClip},
        {"Vision_Recurse", RunRecurse},
        {"Vision_Portal_Recurse", RunPortalRecurse},
        {"Vision_AddCelToDraw", RunAddCel},
        {"vision_generate_display_list", RunDisplayList},
        {"vision_GetCamPos", RunCamPos},
    };
    for (auto &c : cases) {
        Suite suite = {c.name, 0, 0, 0};
        for (int t = 0; t < kWorldTrials; t++) {
            MakeWorld();
            s_viewer = &world.views[R(VIEWS)];
            s_cel = &world.cels[R(CELS)];
            s_word = R(4) ? R(5) : NextWord();
            // A frustum at depth 0 from a view, and the same planes or another view's deeper down
            SetFrustum(&world.views[R(VIEWS)]);
            s_depth = R(3) ? 0 : R(4) ? R(5) : R(48);
            if (s_depth > 0 && s_depth < 48) {
                int count = R(11);
                VisionPlaneCounts[s_depth] = count;
                for (int k = 0; k < count; k++)
                    VisionPlanes[s_depth * 10 + k] = VisionPlanes[R(5)];
            }
            // A polygon: a portal's corners, or up to twelve points about a plane, or anything
            VisPortal &in = world.polygons[0];
            int count = R(4) ? 3 + R(4) : R(13);
            in.count = (char)count;
            _VECTOR centre = RandomPoint(20.0f);
            for (int k = 0; k < 12; k++) {
                float a = 6.2831855f * k / (count ? count : 1);
                in.v[k].x = centre.x + 5.0f * (float)cos(a);
                in.v[k].y = centre.y + 5.0f * (float)sin(a);
                in.v[k].z = centre.z + Uniform(1.0f);
                if (R(12) == 0)
                    in.v[k] = RandomPoint(1e30f);
            }
            world.plane.normal = RandomPoint(1.0f);
            world.plane.d = Value(20.0f);
            if (R(3) == 0) {   // through the centre, or a corner
                world.plane.d = -(world.plane.a * centre.x + world.plane.b * centre.y + world.plane.c * centre.z);
                if (R(2) && count > 0)
                    world.plane.d = -(world.plane.a * in.v[0].x + world.plane.b * in.v[0].y + world.plane.c * in.v[0].z);
            }
            s_plane = R(4) ? &world.plane : &VisionPlanes[R(5)];
            WorldCase(suite, c.name, c.run);
        }
        Summary(suite);
    }
}

// ---- the view's lists and drawing

uint32_t RunAddObjects(bool o) {
    Regs r = {P(s_cel), 0, 0x11111111, 0x22222222, 0x33333333, 0x44444444};
    uint32_t args[2] = {P(s_viewer), 0};
    CallWith(o, 0x000da490, (const void *)&View_AddObjects, r, 2, args);
    return 0;
}
uint32_t RunAddCels(bool o) {
    Regs r = {0, 0, 0x11111111, 0x22222222, P(s_viewer), 0x44444444};
    CallWith(o, 0x000daa00, (const void *)&View_AddCels, r, 0, NULL);
    return 0;
}
uint32_t RunForced(bool o) { CALL(o, 0x000da250, View_AddForcedObjects, s_viewer); return 0; }
uint32_t RunCapture(bool o) {
    Regs r = {P(s_viewer), 0, 0x11111111, 0x22222222, 0x33333333, 0x44444444};
    CallWith(o, 0x000dabb0, (const void *)&View_CaptureScene, r, 0, NULL);
    return 0;
}
uint32_t RunTweak(bool o) { CALL(o, 0x000d9900, View_SetupRenderModes_Tweak, s_viewer, s_obj); return 0; }
uint32_t RunRenderModes(bool o) { CALL(o, 0x000d9af0, View_SetupRenderModes, s_viewer, s_obj); return 0; }
uint32_t RunRenderModes2(bool o) { CALL(o, 0x000d9b40, View_SetupRenderModes2, s_viewer, s_obj); return 0; }
uint32_t RunCelGlist(bool o) {
    Regs r = {P(s_viewer), 0, 0x11111111, 0x22222222, P(s_cel), 0x44444444};
    uint32_t args[2] = {P(s_cel), P(s_viewer)};
    CallWith(o, 0x000da670, (const void *)&View_DrawCelGlist, r, 2, args);
    return 0;
}
uint32_t RunObjectGlist(bool o) { CALL(o, 0x000da750, View_DrawObjectGlist, s_obj, s_viewer); return 0; }
uint32_t RunObjList(bool o) { CALL(o, 0x000da890, View_DrawObjList, s_viewer, (ushort)s_word); return 0; }
uint32_t RunSky(bool o) { CALL(o, 0x000d9d60, View_DrawSky, s_viewer, s_flag); return 0; }
uint32_t RunGameDraw(bool o) { CALL(o, 0x000dac20, Game_Draw); return 0; }

void FillEntries(void) {
    for (int i = 0; i < VIEWS; i++) {
        for (int k = 0; k < ENTRIES; k++) {
            DrawEntry &e = world.entries[i][k];
            e.key = Value(1000.0f);
            if (R(3)) {
                e.obj = &world.objs[R(OBJS)];
                e.cel = NULL;
            } else {
                e.obj = NULL;
                e.cel = &world.cels[R(CELS)];
            }
        }
    }
}

void RunView(bool draw) {
    struct {
        const char *name;
        WorldRun run;
        bool draw;
    } cases[] = {
        {"View_AddObjects", RunAddObjects, false},
        {"View_AddCels", RunAddCels, false},
        {"View_AddForcedObjects", RunForced, false},
        {"View_CaptureScene", RunCapture, false},
        {"View_SetupRenderModes_Tweak", RunTweak, false},
        {"View_SetupRenderModes", RunRenderModes, false},
        {"View_SetupRenderModes2", RunRenderModes2, false},
        {"View_DrawCelGlist", RunCelGlist, false},
        {"View_DrawObjectGlist", RunObjectGlist, false},
        {"View_DrawObjList", RunObjList, false},
        {"View_DrawSky", RunSky, false},
        {"Game_Draw", RunGameDraw, true},
    };
    for (auto &c : cases) {
        if (c.draw != draw)
            continue;
        Suite suite = {c.name, 0, 0, 0};
        for (int t = 0; t < kWorldTrials; t++) {
            MakeWorld();
            FillEntries();
            s_viewer = &world.views[R(VIEWS)];
            if (c.run == RunAddCels || c.run == RunCapture) {
                // The view's cel is one of the rooms, and the rooms' kinds what the inside test looks for
                s_viewer->someCel = &world.cels[R(4)];
            }
            if (c.run == RunAddCels || c.run == RunAddObjects || c.run == RunForced || c.run == RunCapture) {
                SetFrustum(s_viewer);
                for (int k = 0; k < 3; k++)
                    s_viewer->draw.counts[k] = (ushort)R(s_viewer->draw.sizes[k]);
                if (s_viewer->draw.lists[0] == NULL) {
                    s_viewer->draw.lists[0] = &world.entries[0][0];
                    s_viewer->draw.lists[1] = &world.entries[0][0x20];
                    s_viewer->draw.lists[2] = &world.entries[0][0x40];
                }
            }
            s_obj = R(12) ? &world.objs[R(OBJS)] : NULL;
            if (c.run == RunObjectGlist || c.run == RunTweak)
                s_obj = &world.objs[R(OBJS)];
            s_cel = &world.cels[R(CELS)];
            s_word = R(3);
            s_flag = (char)R(2);
            if (c.run == RunObjList && s_viewer->draw.lists[0] == NULL) {
                s_viewer->draw.lists[0] = &world.entries[0][0];
                s_viewer->draw.lists[1] = &world.entries[0][0x20];
                s_viewer->draw.lists[2] = &world.entries[0][0x40];
            }
            if (c.run == RunTweak || c.run == RunRenderModes)   // near enough to flash, now and then
                if (s_obj != NULL && R(2))
                    s_obj->centrePoint = s_viewer->pos;
            WorldCase(suite, c.name, c.run);
        }
        Summary(suite);
    }
}

// ---- game functions Part 2 made exact: the view's projection, the sensor's cone, the searchlight

// A made-up object the fake control_create_object hands out, with extra data behind it
obj_tag *__cdecl FakeCreate(int size, _VECTOR *pos, _VECTOR *rot, quaternion_tag *quat) {
    ShadowLog("create(%x,%x,%x)", size, W(pos), W(rot));
    int at = world.createNext++ % 2;
    obj_tag *obj = &world.objs[8 + at];
    memset(obj, 0, sizeof(obj_tag));
    obj->extraObjectData = world.big[2 + at];
    memset(world.big[2 + at], 0, sizeof(world.big[0]));
    obj->transformMatrix = world.objs[at].transformMatrix;
    return obj;
}
void __cdecl FakeEntityGfx(obj_tag *obj, HASHCODE hash) {
    ShadowLog("gfx(%x,%x)", W(obj), hash);
}
bool __cdecl FakeLinkToRoom(obj_tag *obj, char flags, world_tag *in) {
    ShadowLog("link(%x,%d,%x)", W(obj), flags, W(in));
    return true;
}
bool __cdecl FakeLListAdd(void *list, void *node) {
    ShadowLog("ladd(%x,%x)", W(list), W(node));
    return true;
}
HITDATA_tag s_hit;
bool s_rayHits, s_rayList;
void __cdecl FakeGasInit(cel_tag *cel, _VECTOR *pos, _VECTOR *vel, void *a, float b, float c, uint hash, int d, uint e,
                         int f, int g) {
    ShadowLog("gas(%x,%08x,%08x,%08x,%x)", W(cel), Bits(vel->x), Bits(vel->y), Bits(vel->z), hash);
}
void *__cdecl FakeSound3D(uint sfx, _VECTOR *pos, float volume, float outer, float inner, ushort pitch, uint a, int b) {
    ShadowLog("sound3d(%x,%x)", sfx, W(pos));
    return NULL;
}
bool __cdecl FakeRayIntersect(_VECTOR *from, _VECTOR *to, cel_tag *cel, obj_tag *ignore, obj_tag *b, HITDATA_tag **list,
                              int c, int mask, int d) {
    ShadowLog("ray(%08x,%08x,%08x,%08x,%08x,%08x,%x)", Bits(from->x), Bits(from->y), Bits(from->z), Bits(to->x),
              Bits(to->y), Bits(to->z), mask);
    *list = s_rayHits && s_rayList ? &s_hit : NULL;
    return s_rayHits;
}
void __cdecl FakeFreeHits(HITDATA_tag **list) {
    ShadowLog("freehits(%x)", W(*list));
    *list = NULL;
}
bool __cdecl FakeLineOfSight(_VECTOR *from, _VECTOR *to, cel_tag *cel, obj_tag *a, obj_tag *b, int mask) {
    ShadowLog("sight(%08x,%08x,%08x)", Bits(to->x), Bits(to->y), Bits(to->z));
    return (Bits(to->x) & 1) != 0;
}

uint8_t s_params[0x50];
celglist_tag *s_glist;

uint32_t RunCalcViewAngles(bool o) { CALL(o, 0x00025190, Camera_CalcViewAngles, (ushort)s_view, s_float); return 0; }
uint32_t RunSensorCreate(bool o) {
    obj_tag *r;
    CALLR(r, o, 0x000d0630, Sensor_Create, &world.v[0], &world.v[1], (level_tag *)s_params, s_glist);
    return W(r);
}

uint32_t RunSearchlight(bool o) { CALL(o, 0x0006dcb0, Searchlight_Update, s_obj); return 0; }

void RunGameMaths(void) {
    ShadowHook_Install(0x0002dc10, (const void *)&FakeCreate);
    ShadowHook_Install(0x0006b5e0, (const void *)&FakeEntityGfx);
    ShadowHook_Install(0x00021110, (const void *)&FakeLinkToRoom);
    ShadowHook_Install(0x0006e4d0, (const void *)&FakeLListAdd);
    ShadowHook_Install(0x0002c4e0, (const void *)&FakeRayIntersect);
    ShadowHook_Install(0x000294d0, (const void *)&FakeFreeHits);
    ShadowHook_Install(0x0002c5b0, (const void *)&FakeLineOfSight);
    ShadowHook_Install(0x000cd140, (const void *)&FakeGasInit);
    ShadowHook_Install(0x000cb8c0, (const void *)&FakeSound3D);

    {
        Suite suite = {"Camera_CalcViewAngles", 0, 0, 0};
        uint32_t widescreen = U32_AT(XbeOurs(0x001f6610)), layout = U32_AT(XbeOurs(0x001f660c));
        for (int t = 0; t < kWorldTrials; t++) {
            MakeWorld();
            s_view = R(13) < 11 ? R(11) : R(4);
            s_float = R(4) ? 0.3f + Positive(2.5f) : Angle();
            if (glb_viewer[s_view] != NULL && R(8) == 0)
                glb_viewer[s_view]->width = R(2) ? 0.0f : -0.0f;
            U32_AT(XbeOurs(0x001f6610)) = R(2);
            U32_AT(XbeOurs(0x001f660c)) = R(3);
            WorldCase(suite, "Camera_CalcViewAngles", RunCalcViewAngles);
        }
        U32_AT(XbeOurs(0x001f6610)) = widescreen;
        U32_AT(XbeOurs(0x001f660c)) = layout;
        Summary(suite);
    }
    {
        Suite suite = {"Sensor_Create", 0, 0, 0};
        for (int t = 0; t < kWorldTrials; t++) {
            MakeWorld();
            for (int i = 0; i < 0x50; i += 4)
                *(uint32_t *)&s_params[i] = R(3) ? R(200) : NextWord();
            *(ushort *)&s_params[2] = (ushort)(R(4) ? R(10) : NextWord());
            *(ushort *)&s_params[4] = (ushort)(R(4) ? R(600) : NextWord());
            *(uint32_t *)&s_params[0x2c] = R(2) ? 0x05001234 : 0;
            *(uint32_t *)&s_params[0x48] = R(2) ? 0x05004321 : 0;
            *(uint32_t *)&s_params[0x30] = R(4) ? R(360) : NextWord();
            s_params[0x40] = (uint8_t)R(2);
            world.v[0] = RandomPoint(30.0f);
            world.v[1] = RandomPoint(7.0f);
            s_glist = &world.glists[R(GLISTS)];
            WorldCase(suite, "Sensor_Create", RunSensorCreate);
        }
        Summary(suite);
    }
    {
        Suite suite = {"Searchlight_Update", 0, 0, 0};
        obj_tag *player0 = glb_players[0];
        for (int t = 0; t < kWorldTrials; t++) {
            MakeWorld();
            // The light (objs[2]) and its parts, and the player (objs[1])
            obj_tag *light = &world.objs[2];
            ObjData_Spotlight *data = (ObjData_Spotlight *)world.big[0];
            ObjData_Spotlight *spot = (ObjData_Spotlight *)world.big[1];
            memset(data, 0, sizeof(ObjData_Spotlight));
            memset(spot, 0, sizeof(ObjData_Spotlight));
            light->extraObjectData = data;
            light->hitList = NULL;
            light->curState = (ushort)(R(5) ? R(2) : R(4));
            data->searchlightType = R(8) ? 1 : 0;
            data->phase = R(2) ? Uniform(1e3f) : Angle();
            data->rotationAngles = RandomPoint(7.0f);
            data->sweepHalfRange = R(4) ? Positive(1.5f) : Coordinate();
            data->sweepCenter = R(4) ? Uniform(1.0f) : Coordinate();
            data->alertSwitchChannel = (char)R(4);
            data->standObject = R(2) ? &world.objs[5] : NULL;
            data->projectionSpot = R(4) ? &world.objs[3] : NULL;
            data->volumetricCone = R(2) ? &world.objs[6] : NULL;
            data->lensFlare = R(2) ? &world.objs[7] : NULL;
            world.objs[3].extraObjectData = spot;
            for (int i = 3; i < 8; i++)   // not animated: View_SetDrawInViews would follow their animations
                world.objs[i].transformFlags &= ~TRANSFORM_ANIMATED;
            spot->phase = R(2) ? Positive(1200.0f) : Edge();
            light->rotation = RandomPoint(R(4) ? 2.0f : 7.0f);
            light->drawnCountdown = (char)R(2);
            obj_tag *player = &world.objs[1];
            glb_players[0] = player;
            if (R(2))
                player->position = light->position;
            player->position.x += Uniform(20.0f);
            player->position.z += Uniform(20.0f);
            if (R(6) == 0)
                player->position = world.objs[3].position;
            s_hit.hitPosition = RandomPoint(30.0f);
            s_hit.surfaceNormal.normal = RandomPoint(1.0f);
            if (R(3) == 0)
                s_hit.hitPosition = player->position;
            s_rayHits = R(3) != 0;
            s_rayList = R(4) != 0;
            s_obj = light;
            WorldCase(suite, "Searchlight_Update", RunSearchlight);
        }
        glb_players[0] = player0;
        Summary(suite);
    }
}

} // namespace

void ViewMathShadow_Run(void) {
    RunMath();
    RunExact();
    RunRandom();

    std::string game;
    SaveAll(game);
    NfWarnMuted++;
    InstallFakes();
    RunCamera();
    RunVision();
    RunView(false);
    InstallDrawFakes();
    RunView(true);
    RunGameMaths();
    ShadowHook_RemoveAll();
    NfWarnMuted--;
    RestoreAll(game);
}
