// Shadow test for the maths ports that follow the original's x87 code exactly (math.cpp's rotations, the plane and
// triangle tests, Collide_RayTriangle, Vision_InView, Searchlight_Update's sweep, MatrixMultiplyVector and
// ApplyMatrixLV): ours and the original on the same inputs, random and edge cases (zeros of both signs, NaNs,
// infinities, huge angles, denormals), compared bit for bit. FSIN and FCOS run on this CPU for both, so even their
// results must agree. Run at start with MenuShadowTests=on (MenuProbe.cpp).

#include "MathExactShadow.h"
#include "AllOriginal.h"

#include "../../common/xbeOriginal.h"
#include "../actionhelpers.h"
#include "../math/math.h"
#include "../engine/Collide.h"
#include "../engine/Vision.h"
#include "../engine/viewer.h"
#include "../game/obj/Searchlight.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace {

const unsigned kRotMatrix = 0x000d6560;
const unsigned kRotTransMatrix = 0x000d6630;
const unsigned kRotMatrixZYX = 0x000d6710;
const unsigned kVec_Spherical_2_Cartesian = 0x000d54e0;
const unsigned kPlane_PlaneEq = 0x000d7e40;
const unsigned kDistancePointToPlane = 0x000d7410;
const unsigned kvecutil_point_on_poly = 0x000d7f80;
const unsigned kCollide_RayTriangle = 0x0002b1b0;
const unsigned kVision_InView = 0x000dbc80;
const unsigned kSearchlight_Update = 0x0006dcb0;
const unsigned kMatrixMultiplyVector = 0x000d7500;
const unsigned kApplyMatrixLV = 0x000d7560;
const unsigned kMat_GetPosition = 0x000d5090;
const unsigned kVec_Scalef = 0x000d73e0;
const unsigned kVec_Mul_F3 = 0x000d72d0;
const unsigned kVec4_MulAdd = 0x000d7300;
const unsigned kVec_LineSphereIntersect = 0x000d8590;
const unsigned kVec_SegmentSphereIntersect = 0x000d8690;

typedef void(__cdecl *RotationFn)(_VECTOR *, _MATRIX *);
typedef void(__cdecl *RotTransFn)(_VECTOR *, _VECTOR *, _MATRIX *);
typedef void(__cdecl *SphericalFn)(float *, float, float, float);
typedef uint8_t(__cdecl *PlaneEqFn)(plane_equ_tag *, _VECTOR *, _VECTOR *, _VECTOR *);
typedef double(__cdecl *DistanceFn)(_VECTOR *, plane_equ_tag *);
typedef uint8_t(__cdecl *PointOnPolyFn)(_VECTOR *, _VECTOR *, _VECTOR *, _VECTOR *, plane_equ_tag *);
typedef uint8_t(__cdecl *RayTriangleFn)(_VECTOR *, _VECTOR *, _VECTOR *, _VECTOR *, _VECTOR *, float *);
typedef uint8_t(__cdecl *InViewFn)(_VECTOR *, float, viewer_tag *);
typedef void(__cdecl *UpdateFn)(obj_tag *);
typedef void(__cdecl *TransformFn)(_MATRIX *, _VECTOR *, _VECTOR *);

const int kTrials = 40000;
const uint32_t kUntouched = 0x7fa5a5a5;   // a NaN no operation produces, in every word a function should not write

// Zeros, NaNs of both signs (quiet and signalling, several payloads), infinities, the largest and smallest
// floats, FSIN's limit 2^63 and its neighbours, multiples of pi, and ordinary values
const uint32_t kEdges[] = {
    0x00000000, 0x80000000, 0x7fc00000, 0xffc00000, 0x7f800001, 0xff800001, 0x7fbfffff, 0x7fc12345, 0xffffffff,
    0x7f800000, 0xff800000, 0x7f7fffff, 0xff7fffff, 0x00000001, 0x80000001, 0x007fffff, 0x807fffff, 0x00800000,
    0x5f000000, 0xdf000000, 0x5effffff, 0xdeffffff, 0x5f000001, 0x5f800000, 0x60ad78ec, 0x40490fdb, 0xc0490fdb,
    0x40c90fdb, 0x3fc90fdb, 0x40490fda, 0x43b18000, 0x47cb1d00, 0x4996b438, 0x3f800000, 0xbf800000, 0x0da24260,
    0x3f000000, 0x4cbebc20, 0x501502f9,
};
const int kEdgeCount = sizeof(kEdges) / sizeof(kEdges[0]);

uint32_t s_lcg = 0x9e3779b9u;
uint32_t NextWord(void) {
    s_lcg = s_lcg * 1664525u + 1013904223u;
    return s_lcg ^ (s_lcg >> 15);
}

float FromBits(uint32_t bits) {
    float f;
    memcpy(&f, &bits, sizeof(f));
    return f;
}

float Uniform(float range) {
    return ((float)(NextWord() >> 8) * (1.0f / 8388608.0f) - 1.0f) * range;
}

// Any sign and binary exponent, a random significand
float AnyMagnitude(void) {
    uint32_t w = NextWord();
    uint32_t exponent = (w >> 23) % 255;
    return FromBits((w & 0x807fffff) | (exponent << 23));
}

float Edge(void) {
    return FromBits(kEdges[NextWord() % kEdgeCount]);
}

float Angle(void) {
    switch (NextWord() % 10) {
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
    switch (NextWord() % 16) {
    case 0: return AnyMagnitude();
    case 1: return Edge();
    case 2: return Uniform(1e30f);
    case 3: return Uniform(1e-30f);
    default: return Uniform(100.0f);
    }
}

_VECTOR Vector(float (*component)(void)) {
    _VECTOR v;
    v.x = component();
    v.y = component();
    v.z = component();
    return v;
}

void Fill(void *p, size_t size) {
    for (size_t i = 0; i + 4 <= size; i += 4)
        XbeOursWrite((unsigned)(size_t)p + (unsigned)i, &kUntouched, 4);
}

struct Suite {
    const char *name;
    int cases;
    int differ;
    int nanChoices;
};

bool IsNaN(const uint8_t *p, size_t wordSize) {
    if (wordSize == 8) {
        double d;
        memcpy(&d, p, 8);
        return d != d;
    }
    float f;
    memcpy(&f, p, 4);
    return f != f;
}

// The leading nanBytes of a result are floats (wordSize 4) or doubles (8). Where every difference is a NaN against
// a NaN in them, the two only chose differently between NaNs that met in one operation: the x87 takes the one
// with the larger significand (the positive one if only the signs differ), SSE2 the first operand. The ports in
// plain double make SSE2's choice; these are counted apart.
bool OnlyNaNChoices(const uint8_t *ours, const uint8_t *theirs, size_t size, size_t nanBytes, size_t wordSize) {
    for (size_t i = 0; i < size; i += (i < nanBytes ? wordSize : 1)) {
        if (i < nanBytes) {
            bool same = memcmp(ours + i, theirs + i, wordSize) == 0;
            if (!same && !(IsNaN(ours + i, wordSize) && IsNaN(theirs + i, wordSize)))
                return false;
        } else if (ours[i] != theirs[i]) {
            return false;
        }
    }
    return true;
}

void Check(Suite &suite, const void *ours, const void *theirs, size_t size, size_t nanBytes, size_t wordSize,
           const char *what, const float *inputs, int inputCount) {
    suite.cases++;
    if (memcmp(ours, theirs, size) == 0)
        return;
    bool nanChoice = OnlyNaNChoices((const uint8_t *)ours, (const uint8_t *)theirs, size, nanBytes, wordSize);
    int shown = nanChoice ? suite.nanChoices++ : suite.differ++;
    if (shown < (nanChoice ? 2 : 4)) {
        size_t word = 0;
        while (word + 4 <= size && memcmp((const uint8_t *)ours + word, (const uint8_t *)theirs + word, 4) == 0)
            word += 4;
        uint32_t a = 0, b = 0;
        memcpy(&a, (const uint8_t *)ours + word, size - word < 4 ? size - word : 4);
        memcpy(&b, (const uint8_t *)theirs + word, size - word < 4 ? size - word : 4);
        printf("[mathexact] %s case %d: %s%s at byte %u: ours %08x, the original's %08x; inputs", suite.name,
               suite.cases - 1, nanChoice ? "NaN chosen in the " : "", what, (unsigned)word, a, b);
        for (int i = 0; i < inputCount; i++) {
            uint32_t bits;
            memcpy(&bits, &inputs[i], 4);
            printf(" %08x", bits);
        }
        printf("\n");
    }
}

void Summary(const Suite &suite) {
    if (suite.nanChoices != 0)
        printf("[mathexact] %s: %d cases, %d differ (and %d only in the NaN chosen where two NaNs meet)\n", suite.name,
               suite.cases, suite.differ, suite.nanChoices);
    else
        printf("[mathexact] %s: %d cases, %d differ\n", suite.name, suite.cases, suite.differ);
}

void CheckRotation(const char *name, unsigned address, void (*ours)(_VECTOR *, _MATRIX *)) {
    Suite suite = {name, 0, 0, 0};
    for (int t = 0; t < kTrials; t++) {
        _VECTOR angles = Vector(Angle);
        _MATRIX a, b;
        Fill(&a, sizeof(a));
        Fill(&b, sizeof(b));
        ours(&angles, &a);
        {
            XbeOriginalScope original(address);
            ((RotationFn)address)(&angles, &b);
        }
        Check(suite, &a, &b, sizeof(a), sizeof(a), 4, "matrix", &angles.x, 3);
    }
    Summary(suite);
}

void CheckRotTransMatrix(void) {
    Suite suite = {"RotTransMatrix", 0, 0, 0};
    for (int t = 0; t < kTrials; t++) {
        float inputs[6];
        _VECTOR angles = Vector(Angle);
        _VECTOR trans = Vector(Coordinate);
        memcpy(inputs, &angles, 12);
        memcpy(inputs + 3, &trans, 12);
        _MATRIX a, b;
        Fill(&a, sizeof(a));
        Fill(&b, sizeof(b));
        RotTransMatrix(&angles, &trans, &a);
        {
            XbeOriginalScope original(kRotTransMatrix);
            ((RotTransFn)kRotTransMatrix)(&angles, &trans, &b);
        }
        Check(suite, &a, &b, sizeof(a), sizeof(a), 4, "matrix", inputs, 6);
    }
    Summary(suite);
}

void CheckSpherical(void) {
    Suite suite = {"Vec_Spherical_2_Cartesian", 0, 0, 0};
    for (int t = 0; t < kTrials; t++) {
        float inputs[3] = {Coordinate(), Angle(), Angle()};
        float a[4], b[4];
        Fill(a, sizeof(a));
        Fill(b, sizeof(b));
        Vec_Spherical_2_Cartesian(a, inputs[0], inputs[1], inputs[2]);
        {
            XbeOriginalScope original(kVec_Spherical_2_Cartesian);
            ((SphericalFn)kVec_Spherical_2_Cartesian)(b, inputs[0], inputs[1], inputs[2]);
        }
        Check(suite, a, b, sizeof(a), sizeof(a), 4, "vector", inputs, 3);
    }
    Summary(suite);
}

// Three vertices: random, collinear, coincident or sharing a coordinate plane
void Triangle(_VECTOR *v) {
    v[0] = Vector(Coordinate);
    v[1] = Vector(Coordinate);
    v[2] = Vector(Coordinate);
    switch (NextWord() % 8) {
    case 0: {   // collinear
        float k = Uniform(3.0f);
        v[2].x = v[0].x + (v[1].x - v[0].x) * k;
        v[2].y = v[0].y + (v[1].y - v[0].y) * k;
        v[2].z = v[0].z + (v[1].z - v[0].z) * k;
        break;
    }
    case 1:
        v[2] = v[NextWord() % 2];
        break;
    case 2:
        v[0].y = v[1].y = v[2].y;
        break;
    default:
        break;
    }
}

struct Inputs {
    float f[16];
    int count;
    void Add(const _VECTOR &v) { f[count++] = v.x; f[count++] = v.y; f[count++] = v.z; }
    void Add(float x) { f[count++] = x; }
};

void CheckPlaneEq(void) {
    Suite suite = {"Plane_PlaneEq", 0, 0, 0};
    for (int t = 0; t < kTrials; t++) {
        _VECTOR v[3];
        Triangle(v);
        Inputs in = {};
        in.Add(v[0]); in.Add(v[1]); in.Add(v[2]);
        struct Result {
            plane_equ_tag plane;
            uint8_t returned;
        } a, b;
        Fill(&a, sizeof(a));
        Fill(&b, sizeof(b));
        a.returned = Plane_PlaneEq(&a.plane, &v[0], &v[1], &v[2]);
        {
            XbeOriginalScope original(kPlane_PlaneEq);
            b.returned = ((PlaneEqFn)kPlane_PlaneEq)(&b.plane, &v[0], &v[1], &v[2]);
        }
        Check(suite, &a, &b, sizeof(a), sizeof(a.plane), 4, "plane and result", in.f, in.count);
    }
    Summary(suite);
}

// A plane through the triangle (ours, already checked) or any plane
plane_equ_tag Plane(_VECTOR *v) {
    plane_equ_tag plane;
    if (NextWord() % 4 != 0) {
        Plane_PlaneEq(&plane, &v[0], &v[1], &v[2]);
    } else {
        plane.normal = Vector(Coordinate);
        plane.d = Coordinate();
    }
    return plane;
}

// A point inside the triangle, outside it, on a vertex or anywhere
_VECTOR Point(_VECTOR *v) {
    switch (NextWord() % 4) {
    case 0: {
        float u = Uniform(1.2f), w = Uniform(1.2f);
        _VECTOR p;
        p.x = v[0].x + (v[1].x - v[0].x) * u + (v[2].x - v[0].x) * w;
        p.y = v[0].y + (v[1].y - v[0].y) * u + (v[2].y - v[0].y) * w;
        p.z = v[0].z + (v[1].z - v[0].z) * u + (v[2].z - v[0].z) * w;
        return p;
    }
    case 1:
        return v[NextWord() % 3];
    default:
        return Vector(Coordinate);
    }
}

void CheckDistance(void) {
    Suite suite = {"DistancePointToPlane", 0, 0, 0};
    for (int t = 0; t < kTrials; t++) {
        _VECTOR v[3];
        Triangle(v);
        plane_equ_tag plane = Plane(v);
        _VECTOR point = Point(v);
        Inputs in = {};
        in.Add(point); in.Add(plane.a); in.Add(plane.b); in.Add(plane.c); in.Add(plane.d);
        double a = DistancePointToPlane(&point, &plane);
        double b;
        {
            XbeOriginalScope original(kDistancePointToPlane);
            b = ((DistanceFn)kDistancePointToPlane)(&point, &plane);
        }
        Check(suite, &a, &b, sizeof(a), sizeof(a), 8, "distance", in.f, in.count);
    }
    Summary(suite);
}

void CheckPointOnPoly(void) {
    Suite suite = {"vecutil_point_on_poly", 0, 0, 0};
    for (int t = 0; t < kTrials; t++) {
        _VECTOR v[3];
        Triangle(v);
        plane_equ_tag plane = Plane(v);
        _VECTOR point = Point(v);
        Inputs in = {};
        in.Add(point); in.Add(v[0]); in.Add(v[1]); in.Add(v[2]);
        uint8_t a = vecutil_point_on_poly(&point, &v[0], &v[1], &v[2], &plane);
        uint8_t b;
        {
            XbeOriginalScope original(kvecutil_point_on_poly);
            b = ((PointOnPolyFn)kvecutil_point_on_poly)(&point, &v[0], &v[1], &v[2], &plane);
        }
        Check(suite, &a, &b, sizeof(a), 0, 4, "result", in.f, in.count);
    }
    Summary(suite);
}

// The original calls our Plane_PlaneEq, DistancePointToPlane and vecutil_point_on_poly too: this checks its own body
void CheckRayTriangle(void) {
    Suite suite = {"Collide_RayTriangle", 0, 0, 0};
    for (int t = 0; t < kTrials; t++) {
        _VECTOR v[3];
        Triangle(v);
        _VECTOR start = Point(v);
        _VECTOR end = NextWord() % 2 ? Point(v) : Vector(Coordinate);
        if (NextWord() % 8 == 0) {   // parallel to the triangle
            end.x = v[1].x - v[0].x;
            end.y = v[1].y - v[0].y;
            end.z = v[1].z - v[0].z;
        }
        Inputs in = {};
        in.Add(start); in.Add(end); in.Add(v[0]); in.Add(v[1]); in.Add(v[2]);
        struct Result {
            float distance;
            uint8_t returned;
        } a, b;
        Fill(&a, sizeof(a));
        Fill(&b, sizeof(b));
        a.returned = Collide_RayTriangle(&start, &end, &v[0], &v[1], &v[2], &a.distance);
        {
            XbeOriginalScope original(kCollide_RayTriangle);
            b.returned = ((RayTriangleFn)kCollide_RayTriangle)(&start, &end, &v[0], &v[1], &v[2], &b.distance);
        }
        Check(suite, &a, &b, sizeof(a), sizeof(a.distance), 4, "distance and result", in.f, in.count);
    }
    Summary(suite);
}

viewer_tag s_viewer;

void CheckInView(void) {
    Suite suite = {"Vision_InView", 0, 0, 0};
    for (int t = 0; t < kTrials; t++) {
        Inputs in = {};
        for (int i = 0; i < 4; i++) {
            _VECTOR v[3];
            Triangle(v);
            s_viewer.frustumPlanes[i] = Plane(v);
        }
        _VECTOR point = Vector(Coordinate);
        float radius = NextWord() % 4 ? Uniform(10.0f) : Coordinate();
        if (NextWord() % 2)   // the distance to the first plane, as a float: the comparison is on the unrounded one
            radius = (float)-DistancePointToPlane(&point, &s_viewer.frustumPlanes[0]);
        in.Add(point); in.Add(radius);
        uint8_t a = Vision_InView(&point, radius, &s_viewer);
        uint8_t b;
        {
            XbeOriginalScope original(kVision_InView);
            b = ((InViewFn)kVision_InView)(&point, radius, &s_viewer);
        }
        Check(suite, &a, &b, sizeof(a), 0, 4, "result", in.f, in.count);
    }
    Summary(suite);
}

// The sweep only: no stand, projection spot, cone, flare or hits, so nothing else is reached
struct Searchlight {
    obj_tag obj;
    ObjData_Spotlight data;
};
Searchlight s_searchlights[2];

void CheckSweep(void) {
    Suite suite = {"Searchlight_Update (sweep)", 0, 0, 0};
    for (int t = 0; t < kTrials; t++) {
        Searchlight start;
        memset(&start, 0, sizeof(start));
        start.obj.rotation = Vector(Coordinate);
        start.data.searchlightType = 1;
        start.data.phase = NextWord() % 2 ? Uniform(1e5f) : Angle();
        start.data.rotationAngles = Vector(Coordinate);
        start.data.sweepHalfRange = NextWord() % 2 ? Uniform(2.0f) : Coordinate();
        start.data.sweepCenter = NextWord() % 2 ? Uniform(2.0f) : Coordinate();
        float inputs[4] = {start.data.phase, start.data.rotationAngles.y, start.data.sweepHalfRange,
                           start.data.sweepCenter};
        Searchlight &ours = s_searchlights[0], &theirs = s_searchlights[1];
        memcpy(&ours, &start, sizeof(start));
        memcpy(&theirs, &start, sizeof(start));
        ours.obj.extraObjectData = &ours.data;
        theirs.obj.extraObjectData = &theirs.data;
        Searchlight_Update(&ours.obj);
        {
            XbeOriginalScope original(kSearchlight_Update);
            ((UpdateFn)kSearchlight_Update)(&theirs.obj);
        }
        theirs.obj.extraObjectData = &ours.data;
        Check(suite, &ours, &theirs, sizeof(Searchlight), 0, 4, "searchlight", inputs, 4);
    }
    Summary(suite);
}

// The same float or one ulp either way
float Nudge(float f) {
    uint32_t bits;
    memcpy(&bits, &f, sizeof(bits));
    bits = bits + NextWord() % 3 - 1;
    return FromBits(bits);
}

// A rotation with an offset, edge values or any words. m[3], m[7] and m[11] are never read and get anything.
_MATRIX Matrix(void) {
    _MATRIX m;
    switch (NextWord() % 8) {
    case 0: case 1:
        for (int i = 0; i < 12; i++)
            m.m[i] = Uniform(1.0f);
        for (int i = 12; i < 15; i++)
            m.m[i] = Uniform(1000.0f);
        break;
    case 2:
        for (int i = 0; i < 15; i++)
            m.m[i] = Edge();
        break;
    default:
        for (int i = 0; i < 15; i++)
            m.m[i] = Coordinate();
        break;
    }
    for (int i = 3; i < 12; i += 4)
        m.m[i] = NextWord() % 2 ? Edge() : FromBits(NextWord());
    return m;
}

// Large products in a row that cancel, within an ulp or exactly: m[r] * x against m[4 + r] * y and m[8 + r] * z,
// and the translation against the first, so the order of the sums shows in the result
void Cancel(_MATRIX *m, _VECTOR *v) {
    v->x = NextWord() % 2 ? Uniform(1e19f) : AnyMagnitude();
    v->y = Nudge(NextWord() % 2 ? v->x : -v->x);
    v->z = NextWord() % 2 ? Nudge(v->x) : Uniform(1.0f);
    for (int r = 0; r < 3; r++) {
        if (NextWord() % 4 == 0)
            continue;
        m->m[r] = NextWord() % 2 ? Uniform(1e19f) : AnyMagnitude();
        m->m[4 + r] = Nudge(NextWord() % 2 ? m->m[r] : -m->m[r]);
        m->m[8 + r] = Nudge(NextWord() % 2 ? m->m[r] : -m->m[r]);
        if (NextWord() % 2)
            m->m[12 + r] = Nudge((float)-((double)m->m[r] * v->x));
    }
}

// Where vIn lies against vOut. The original stores each component before reading the next, so an overlap shows.
enum Placement { kApart, kSame, kOutAfterIn, kOutBeforeIn };
const char *const kPlacementNames[] = {"vector (vIn apart)", "vector (vIn == vOut)", "vector (vOut one word after vIn)",
                                       "vector (vOut one word before vIn)"};

// vOut at words[2], or words[3] when it follows vIn; two guard words either side of the four that may be written
struct Placed {
    float words[8];
    _VECTOR apart;
    _VECTOR *in;
    _VECTOR *out;

    void Place(int placement, const _VECTOR &v) {
        Fill(words, sizeof(words));
        apart = v;
        in = &apart;
        out = (_VECTOR *)&words[2];
        if (placement == kSame) {
            in = out;
        } else if (placement == kOutAfterIn) {
            in = (_VECTOR *)&words[2];
            out = (_VECTOR *)&words[3];
        } else if (placement == kOutBeforeIn) {
            in = (_VECTOR *)&words[3];
        }
        if (in != &apart)
            *in = v;
    }
};

// The four words between the guards as floats, then the guards apart so a NaN written there is never taken for a
// choice between NaNs
struct TransformResult {
    float out[4];
    uint32_t guards[4];
};

TransformResult Collect(const Placed &placed) {
    TransformResult result;
    memcpy(result.out, &placed.words[2], sizeof(result.out));
    memcpy(&result.guards[0], &placed.words[0], 8);
    memcpy(&result.guards[2], &placed.words[6], 8);
    return result;
}

void CheckTransform(const char *name, unsigned address, void (*ours)(_MATRIX *, _VECTOR *, _VECTOR *),
                    bool translation) {
    Suite suite = {name, 0, 0, 0};
    for (int t = 0; t < kTrials; t++) {
        _MATRIX m = Matrix();
        _VECTOR v = Vector(Coordinate);
        if (NextWord() % 4 == 0)
            Cancel(&m, &v);
        int placement = NextWord() % 4;
        Inputs in = {};
        in.Add(v);
        for (int i = 0; i < 15; i++)
            if (i % 4 != 3 && (i < 12 || translation))
                in.Add(m.m[i]);
        Placed placed;
        placed.Place(placement, v);
        ours(&m, placed.in, placed.out);
        TransformResult a = Collect(placed);
        placed.Place(placement, v);
        {
            XbeOriginalScope original(address);
            ((TransformFn)address)(&m, placed.in, placed.out);
        }
        TransformResult b = Collect(placed);
        Check(suite, &a, &b, sizeof(a), sizeof(a.out), 4, kPlacementNames[placement], in.f, in.count);
    }
    Summary(suite);
}

// ---- the vector helpers

typedef void(__cdecl *GetPositionFn)(_VECTOR *, _MATRIX *);
typedef void(__cdecl *ScalefFn)(_VECTOR *, float, _VECTOR *);
typedef void(__cdecl *MulF3Fn)(_VECTOR *, _VECTOR *, float, float, float);
typedef void(__cdecl *MulAddFn)(float, float *, float *);
typedef int(__cdecl *SphereFn)(_VECTOR *, _VECTOR *, _VECTOR *, float, _VECTOR *);

void CheckVectorHelpers(void) {
    Suite position = {"Mat_GetPosition", 0, 0, 0}, scalef = {"Vec_Scalef", 0, 0, 0},
          mulF3 = {"Vec_Mul_F3", 0, 0, 0}, mulAdd = {"Vec4_MulAdd", 0, 0, 0};
    for (int t = 0; t < kTrials; t++) {
        _MATRIX m;
        for (int i = 0; i < 15; i++)
            m.m[i] = NextWord() % 4 ? Coordinate() : FromBits(NextWord());
        _VECTOR a[2], b[2];
        Fill(a, sizeof(a));
        Fill(b, sizeof(b));
        Mat_GetPosition(&a[0], &m);
        {
            XbeOriginalScope original(kMat_GetPosition);
            ((GetPositionFn)kMat_GetPosition)(&b[0], &m);
        }
        Check(position, a, b, sizeof(a), 0, 4, "vector", m.m + 12, 3);

        // Apart, or in place
        bool inPlace = NextWord() % 3 == 0;
        float inputs[6];
        _VECTOR v = Vector(Coordinate);
        float scale = NextWord() % 3 ? Uniform(10.0f) : Coordinate();
        memcpy(inputs, &v, 12);
        inputs[3] = scale;
        Fill(a, sizeof(a));
        Fill(b, sizeof(b));
        a[0] = b[0] = v;
        Vec_Scalef(&a[0], scale, inPlace ? &a[0] : &a[1]);
        {
            XbeOriginalScope original(kVec_Scalef);
            ((ScalefFn)kVec_Scalef)(&b[0], scale, inPlace ? &b[0] : &b[1]);
        }
        Check(scalef, a, b, sizeof(a), sizeof(a), 4, "vector", inputs, 4);

        inputs[4] = Coordinate();
        inputs[5] = Coordinate();
        Fill(a, sizeof(a));
        Fill(b, sizeof(b));
        a[0] = b[0] = v;
        Vec_Mul_F3(inPlace ? &a[0] : &a[1], &a[0], scale, inputs[4], inputs[5]);
        {
            XbeOriginalScope original(kVec_Mul_F3);
            ((MulF3Fn)kVec_Mul_F3)(inPlace ? &b[0] : &b[1], &b[0], scale, inputs[4], inputs[5]);
        }
        Check(mulF3, a, b, sizeof(a), sizeof(a), 4, "vector", inputs, 6);

        // Four lanes added into, sometimes into the vector scaled
        float x[8], y[8];
        for (int i = 0; i < 8; i++)
            x[i] = y[i] = Coordinate();
        Vec4_MulAdd(scale, x, inPlace ? x : x + 4);
        {
            XbeOriginalScope original(kVec4_MulAdd);
            ((MulAddFn)kVec4_MulAdd)(scale, y, inPlace ? y : y + 4);
        }
        Check(mulAdd, x, y, sizeof(x), sizeof(x), 4, "lanes", inputs, 4);
    }
    Summary(position);
    Summary(scalef);
    Summary(mulF3);
    Summary(mulAdd);
}

// A line or segment and a sphere: mostly crossing it, inside it or near it
void CheckSphere(const char *name, unsigned address, int (*ours)(_VECTOR *, _VECTOR *, _VECTOR *, float, _VECTOR *)) {
    Suite suite = {name, 0, 0, 0};
    for (int t = 0; t < kTrials; t++) {
        float inputs[10];
        _VECTOR p, q, centre;
        float radius;
        if (NextWord() % 5 == 0) {
            p = Vector(Coordinate);
            q = Vector(Coordinate);
            centre = Vector(Coordinate);
            radius = Coordinate();
        } else {
            centre = Vector([]() { return Uniform(50.0f); });
            radius = NextWord() % 8 ? 0.1f + (float)(NextWord() % 2000) * 0.01f : Uniform(5.0f);
            p = Vector([]() { return Uniform(40.0f); });
            q = Vector([]() { return Uniform(40.0f); });
            if (NextWord() % 2) {
                p.x += centre.x;
                p.y += centre.y;
                p.z += centre.z;
            }
            if (NextWord() % 6 == 0)
                q = p;
        }
        memcpy(inputs, &p, 12);
        memcpy(inputs + 3, &q, 12);
        memcpy(inputs + 6, &centre, 12);
        inputs[9] = radius;
        struct Result {
            _VECTOR out;
            int hit;
        } a, b;
        Fill(&a, sizeof(a));
        Fill(&b, sizeof(b));
        a.hit = ours(&p, &q, &centre, radius, &a.out);
        {
            XbeOriginalScope original(address);
            b.hit = ((SphereFn)address)(&p, &q, &centre, radius, &b.out);
        }
        Check(suite, &a, &b, sizeof(a), sizeof(a.out), 4, "point", inputs, 10);
    }
    Summary(suite);
}

} // namespace

void MathExactShadow_Run(void) {
    CheckVectorHelpers();
    CheckSphere("Vec_LineSphereIntersect", kVec_LineSphereIntersect, Vec_LineSphereIntersect);
    CheckSphere("Vec_SegmentSphereIntersect", kVec_SegmentSphereIntersect, Vec_SegmentSphereIntersect);
    CheckRotation("RotMatrix", kRotMatrix, RotMatrix);
    CheckRotation("RotMatrixZYX", kRotMatrixZYX, RotMatrixZYX);
    CheckRotTransMatrix();
    CheckSpherical();
    CheckPlaneEq();
    CheckDistance();
    CheckPointOnPoly();
    CheckRayTriangle();
    CheckInView();
    CheckSweep();
    CheckTransform("MatrixMultiplyVector", kMatrixMultiplyVector, MatrixMultiplyVector, true);
    CheckTransform("ApplyMatrixLV", kApplyMatrixLV, ApplyMatrixLV, false);
}
