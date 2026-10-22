// Shadow tests for the collision code (engine/Collide.cpp, the vecutil_ functions of math.cpp) and the rigid bodies
// (game/obj/RigidBody.cpp): each function, the original and ours, on the same made-up world - two collision trees
// (boxes, triangles, vertex blocks and materials in the map's format), cels joined by portals, objects with their
// own tests and geometry, joint boxes, the hit heap, the triangle heap - and random queries, with zeros of both
// signs, NaNs and exact boundary values among them. What needs a level (the cel search, linking to a room, objects
// made, sounds, gas, wind, the heap, the joint boxes) goes to a logging fake; the collision code's other functions,
// ours, run for real underneath both. Run at start with MenuShadowTests=on (MenuProbe.cpp).
//
// The register-argument originals are called with their registers set (CallRegs), and so are our adaptors. A bool
// is compared on AL (tools/narrow_returns.py checks no caller reads more), a double on all its bits.

#include "CollidePortShadow.h"
#include "AllOriginal.h"
#include "ShadowHarness.h"

#include "../../common/xbeOriginal.h"
#include "../actionhelpers.h"
#include "../game.h"
#include "../engine/Anim.h"
#include "../engine/Collide.h"
#include "../engine/celglist.h"
#include "../engine/viewer.h"
#include "../game/mp/multiplayer.h"
#include "../game/obj/RigidBody.h"
#include "../game/obj/Light.h"
#include "../game/obj/bullet.h"
#include "../game/obj/control.h"
#include "../game/obj/object.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <map>
#include <string>
#include <vector>

namespace {

#define RandWords (GameSpan<uint32_t>{0x0018cdf8})
#define CollideGlobals (GameSpan<uint8_t>{0x001dc9a0})    // ColBoxs to CelList
#define COLLIDE_GLOBALS_SIZE (0x001dee38 - 0x001dc9a0)
#define ForcedListInfo ((uint8_t *)0x001df41c)
#define FrameRates (GameSpan<uint8_t>{0x0017c100})       // FRAME_RATE_MUL, REC_FRAME_RATE

#define ORIGINAL(at, type) ((type)(uintptr_t)(at))
#ifndef COUNT
#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))
#endif

const int NODES = 400;
const int OBJS = 8;
const int CELS = 6;
const int PORTALS = 6;
const int BOXES = 7;
const int TRIS = 16;
const int BLOCKS = 4;
const int JOINTS = 4;
const int TRIHEAP = 640;
const int HEAP = 0x10000;

struct Geometry {
    COLLBOX_tag boxes[BOXES];
    collDataB tris[TRIS];
    uchar materials[1 + TRIS];      // [0]: the byte before the table, which Intersect_CylTriGeom reads
    collDataC blocks[BLOCKS];
    COLLDATA_tag coll;
    char pad[2];
    celglist_tag glist;
};

struct World {
    HITDATA_tag nodes[NODES];
    Geometry geom[2];               // [0] the cels', [1] the objects'
    celglist_tag noColl;            // geometry without collision data
    cel_tag cels[CELS];
    portal_tag portals[PORTALS];
    obj_tag objs[OBJS];
    uint8_t extra[OBJS][0x400];     // BLData, Drone_tag, BU_tag or RIGIDBODY, as far as the code reads them
    uint8_t weapons[2][0x40];       // weapon_definition_tag::damageClass at 0x10
    HITTEST_tag objTests[OBJS];
    HITTEST_tag test;
    HITDATA_tag *list;
    obj_tag *objList[8];
    ushort objCount;
    COLLJOINT_tag joints[JOINTS];
    ushort jointCount;
    _VECTOR triHeap[TRIHEAP];
    uint8_t heap[HEAP];
    uint32_t heapUsed;
    _VECTOR wind;
    int findCel;                    // the cel build_FindCel answers, -1 for none
    _VECTOR v[8];
    plane_equ_tag plane;
    _MATRIX mtx;
    ushort code;
    uint32_t count;
    light_tag light;
    uint8_t placement[0x40];
    quaternion_tag quat;
    float inertia[3];
    uint32_t stream[64];
    uint32_t streamPos;
};
World world;

ShadowRegions regions;
int cases, differ;

// The cases' random numbers: each run of a case starts from the same point, so the original and ours are given the
// same arguments, and the fakes the same answers
uint32_t gen, caseSeed;

uint32_t R(uint32_t n) {
    gen = gen * 1664525u + 1013904223u;
    uint32_t v = gen ^ (gen >> 16);
    return n ? v % n : v;
}

float RF(float lo, float hi) {
    return lo + (hi - lo) * (float)R(0x10000) / 65535.0f;
}

uint32_t Bits(float f) {
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

float FromBits(uint32_t u) {
    float f;
    memcpy(&f, &u, 4);
    return f;
}

// A pointer as the logs and the results show it: an offset in the world, the stack as one value, else its address
uint32_t W(const void *p) {
    if (p == NULL)
        return 0xffffffffu;
    uintptr_t at = (uintptr_t)p, base = (uintptr_t)&world;
    if (at >= base && at < base + sizeof(world))
        return (uint32_t)(at - base);
    char here;
    ptrdiff_t distance = (const char *)p - &here;
    if (distance > -0x10000 && distance < 0x10000)
        return 0x7fffffffu;
    return 0x80000000u | (uint32_t)at;
}

uint32_t Fold(double d) {
    uint64_t bits;
    memcpy(&bits, &d, 8);
    if (d != d)
        bits = 0x7ff8000000000000ull;
    return (uint32_t)bits ^ (uint32_t)(bits >> 32);
}

void LogVector(const char *name, const _VECTOR *v) {
    if (v == NULL)
        ShadowLog("%s=null", name);
    else
        ShadowLog("%s=%08x,%08x,%08x", name, Bits(v->x), Bits(v->y), Bits(v->z));
}

// ---- values

// Mostly in range, now and then an edge: zeros of both signs, a NaN, an infinity, a huge or tiny value
float Value(float lo, float hi) {
    switch (R(40)) {
    case 0: return 0.0f;
    case 1: return -0.0f;
    case 2: return NAN;
    case 3: return R(2) ? INFINITY : -INFINITY;
    case 4: return R(2) ? 1e30f : -1e-30f;
    case 5: return (float)(int)RF(lo, hi);
    default: return RF(lo, hi);
    }
}

// Mostly in range, without the edges
float Plain(float lo, float hi) {
    return RF(lo, hi);
}

void RandomVector(_VECTOR *v, float range) {
    v->x = Value(-range, range);
    v->y = Value(-range, range);
    v->z = Value(-range, range);
}

void PlainVector(_VECTOR *v, float range) {
    v->x = Plain(-range, range);
    v->y = Plain(-range, range);
    v->z = Plain(-range, range);
}

// Often exactly one of the values given, else a value near or between them
float Around(float a, float b) {
    switch (R(6)) {
    case 0: return a;
    case 1: return b;
    case 2: return Value(-8.0f, 8.0f);
    default: return Plain(a < b ? a : b, a < b ? b : a) + Plain(-0.5f, 0.5f);
    }
}

// ---- the fakes

COLLJOINT_tag *__cdecl FakeCollData(obj_tag *obj, ushort *count) {
    ShadowLog("colldata(%x)", W(obj));
    *count = world.jointCount;
    return world.joints;
}

cel_tag *__cdecl FakeFindCel(_VECTOR *pos, world_tag *w) {
    LogVector("findcel", pos);
    return world.findCel < 0 ? NULL : &world.cels[world.findCel];
}

bool __cdecl FakeLinkToRoom(obj_tag *obj, char flags, world_tag *w) {
    ShadowLog("link(%x,%d)", W(obj), flags);
    return true;
}

obj_tag *__cdecl FakeCreate(int size, _VECTOR *pos, _VECTOR *rot, quaternion_tag *quat) {
    ShadowLog("create(%d)", size);
    LogVector("pos", pos);
    LogVector("rot", rot);
    if (R(16) == 0)
        return NULL;
    obj_tag *obj = &world.objs[OBJS - 1];
    memset(obj, 0, sizeof(obj_tag));
    memset(world.extra[OBJS - 1], 0, sizeof(world.extra[0]));
    if (pos != NULL)
        obj->position = *pos;
    if (rot != NULL)
        obj->rotation = *rot;
    RotTransMatrix(&obj->rotation, &obj->position, &obj->transformMatrix);
    obj->extraObjectData = world.extra[OBJS - 1];
    return obj;
}

DYNAMICSOUNDS *__cdecl FakeSound3D(Action_SFX sfx, _VECTOR *pos, float volume, float outer, float inner, short pitch,
                                   int p7, int limited) {
    ShadowLog("sound3d(%x,%x,%08x,%08x,%08x,%d,%d,%d)", (uint)sfx, W(pos), Bits(volume), Bits(outer), Bits(inner), pitch,
              p7, limited);
    return NULL;
}

obj_tag *__cdecl FakeGas(cel_tag *cel, _VECTOR *pos, _VECTOR *a, _VECTOR *b, float f5, float f6, HASHCODE hc, uint p8,
                         uint p9, uint p10, ushort p11) {
    ShadowLog("gas(%x,%x,%x,%x,%08x,%08x,%x,%x,%x,%x,%x)", W(cel), W(pos), W(a), W(b), Bits(f5), Bits(f6), (uint)hc,
              p8, p9, p10, p11);
    return NULL;
}

void __cdecl FakeWind(cel_tag *cel, _VECTOR *pos, _VECTOR *out) {
    ShadowLog("wind(%x,%x)", W(cel), W(pos));
    *out = world.wind;
}

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

void InstallFakes(void) {
    ShadowHook_Install(0x000136b0, (const void *)&FakeCollData);
    ShadowHook_Install(0x00020c30, (const void *)&FakeFindCel);
    ShadowHook_Install(0x00021110, (const void *)&FakeLinkToRoom);
    ShadowHook_Install(0x0002dc10, (const void *)&FakeCreate);
    ShadowHook_Install(0x000cb8c0, (const void *)&FakeSound3D);
    ShadowHook_Install(0x000cd140, (const void *)&FakeGas);
    ShadowHook_Install(0x00068cf0, (const void *)&FakeWind);
    ShadowHook_Install(0x00070ae0, (const void *)&FakeMalloc);
    ShadowHook_Install(0x000705f0, (const void *)&FakeFree);
}

// ---- the world

void Put(short *slot, double v) {
    if (v > 32767.0)
        v = 32767.0;
    if (v < -32768.0)
        v = -32768.0;
    *slot = (short)(v < 0 ? v - 0.5 : v + 0.5);
}

_VECTOR Vertex(const COLLBOX_tag *box, const short *block, int at) {
    _VECTOR v;
    v.x = (float)((double)block[at] * box->quant.scale + box->quant.offset.x);
    v.y = (float)((double)block[at + 1] * box->quant.scale + box->quant.offset.y);
    v.z = (float)((double)block[at + 2] * box->quant.scale + box->quant.offset.z);
    return v;
}

void Grow(COLLBOX_tag *box, const _VECTOR &v) {
    if (v.x < box->boundMin.x) box->boundMin.x = v.x;
    if (v.y < box->boundMin.y) box->boundMin.y = v.y;
    if (v.z < box->boundMin.z) box->boundMin.z = v.z;
    if (v.x > box->boundMax.x) box->boundMax.x = v.x;
    if (v.y > box->boundMax.y) box->boundMax.y = v.y;
    if (v.z > box->boundMax.z) box->boundMax.z = v.z;
}

const uchar kMaterials[] = {0, 1, 13, 14, 16, 0x40, 0x80, 0xc0, 0x4d, 0x8e, 0x50, 5};

// A tree of seven boxes, a leaf's triangles from its own vertex block of four vertices and four planes
void BuildGeometry(Geometry &g) {
    memset(&g, 0, sizeof(g));
    float scale = Plain(0.002f, 0.012f);
    _VECTOR offset;
    PlainVector(&offset, 2.0f);
    for (int b = 0; b < BLOCKS; b++) {
        short *block = (short *)g.blocks[b].raw;
        for (int i = 0; i < 12; i++)
            block[i] = (short)((int)R(1200) - 600);
        if (R(10) == 0)
            memcpy(&block[3], &block[0], 6);        // a degenerate triangle
    }
    int tri = 0;
    for (int leaf = 3; leaf < BOXES; leaf++) {
        COLLBOX_tag *box = &g.boxes[leaf];
        box->childA = -1;
        box->vertexBlock = (ushort)(leaf - 3);
        box->quant.scale = scale * (R(4) ? 1.0f : Plain(0.5f, 2.0f));
        box->quant.offset = offset;
        if (R(4) == 0)
            PlainVector(&box->quant.offset, 2.0f);
        short *block = (short *)g.blocks[box->vertexBlock].raw;
        box->boundMin.x = box->boundMin.y = box->boundMin.z = 1e30f;
        box->boundMax.x = box->boundMax.y = box->boundMax.z = -1e30f;
        for (int i = 0; i < 4; i++)
            Grow(box, Vertex(box, block, i * 3));
        float margin = R(3) ? 0.0f : Plain(-0.2f, 0.4f);
        box->boundMin.x -= margin; box->boundMin.y -= margin; box->boundMin.z -= margin;
        box->boundMax.x += margin; box->boundMax.y += margin; box->boundMax.z += margin;
        int count = R(8) == 0 ? 0 : 1 + R(4);
        box->childB = (short)tri;
        for (int t = 0; t < count; t++, tri++) {
            collDataB *triangle = &g.tris[tri];
            static const ushort corners[4][3] = {{0, 3, 6}, {0, 6, 9}, {3, 9, 6}, {9, 3, 0}};
            const ushort *c = corners[R(4)];
            for (int k = 0; k < 3; k++)
                triangle->v[k] = c[k];
            triangle->normal = (ushort)(12 + 5 * t);
            short *plane = block + triangle->normal;
            _VECTOR a = Vertex(box, block, c[0]), b2 = Vertex(box, block, c[1]), c2 = Vertex(box, block, c[2]);
            plane_equ_tag eq;
            Plane_PlaneEq(&eq, &a, &b2, &c2);
            if (R(8) == 0) {
                eq.a = -eq.a; eq.b = -eq.b; eq.c = -eq.c; eq.d = -eq.d;
            }
            if (R(16) == 0) {
                eq.a = Plain(-1.0f, 1.0f); eq.b = Plain(-1.0f, 1.0f); eq.c = Plain(-1.0f, 1.0f);
            }
            Put(&plane[0], eq.a * 16384.0);
            Put(&plane[1], eq.b * 16384.0);
            Put(&plane[2], eq.c * 16384.0);
            float d = R(16) ? eq.d : Value(-4.0f, 4.0f);
            memcpy(&plane[3], &d, 4);
            g.materials[1 + tri] = R(3) ? 0 : kMaterials[R(COUNT(kMaterials))];
        }
        box->triEnd = (short)tri;
    }
    g.materials[0] = R(3) ? 0 : 0xc0;
    // The tree: 0 holds 1 and 2, 1 holds 3 and 4, 2 holds 5 and 6; sometimes the root alone is a leaf
    static const short children[3][2] = {{1, 2}, {3, 4}, {5, 6}};
    for (int inner = 2; inner >= 0; inner--) {
        COLLBOX_tag *box = &g.boxes[inner];
        box->childA = children[inner][0];
        box->childB = children[inner][1];
        box->boundMin = g.boxes[box->childA].boundMin;
        box->boundMax = g.boxes[box->childA].boundMax;
        Grow(box, g.boxes[box->childB].boundMin);
        Grow(box, g.boxes[box->childB].boundMax);
    }
    if (R(10) == 0)
        g.boxes[0] = g.boxes[3 + R(4)];
    g.coll.collBoxes = g.boxes;
    g.coll.dataStartB = g.tris;
    g.coll.dataStartD = &g.materials[1];
    g.coll.dataStartC = g.blocks;
    g.coll.countB = TRIS;
    g.coll.numCollBoxes = BOXES;
    g.glist.colldata = &g.coll;
    g.glist.boundSphere.r = Plain(0.2f, 3.0f);
    PlainVector(&g.glist.extentMin, 2.0f);
    g.glist.extentMax = g.glist.extentMin;
    g.glist.extentMax.x += Value(0.0f, 3.0f);
    g.glist.extentMax.y += Value(0.0f, 3.0f);
    g.glist.extentMax.z += Value(0.0f, 3.0f);
}

void RandomMatrix(_MATRIX *m) {
    _VECTOR rot, pos;
    PlainVector(&rot, 3.2f);
    PlainVector(&pos, 3.0f);
    if (R(4) == 0)
        rot.x = rot.y = rot.z = 0.0f;
    RotTransMatrix(&rot, &pos, m);
}

void RandomTest(HITTEST_tag *test) {
    static const ushort types[] = {0x100, 0x101, 0x200, 0x201, 0x400, 0x800, 0x1000, 0x2000, 0x101, 0x201, 0x800, 0};
    memset(test, 0, sizeof(*test));
    test->type = types[R(COUNT(types))];
    if (R(30) == 0)
        test->type = (ushort)R(0x10000);
    PlainVector(&test->rayStart, 6.0f);
    if (R(2))
        PlainVector(&test->rayEnd, 6.0f);
    else
        RandomVector(&test->rayEnd, 6.0f);
    if (R(6) == 0)
        test->rayEnd = test->rayStart;
    test->dir.x = test->rayEnd.x - test->rayStart.x;
    test->dir.y = test->rayEnd.y - test->rayStart.y;
    test->dir.z = test->rayEnd.z - test->rayStart.z;
    if (R(5) == 0)
        test->dir.x = 0.0f;
    if (R(5) == 0)
        test->dir.y = 0.0f;
    if (R(5) == 0)
        test->dir.z = 0.0f;
    PlainVector(&test->pos, 6.0f);
    if (R(8) == 0)
        RandomVector(&test->pos, 6.0f);
    test->sphere = test->pos;
    test->radius = R(10) ? Plain(0.05f, 3.0f) : Value(-1.0f, 4.0f);
    test->sphereRadius = test->radius;
    test->damage = Plain(0.0f, 50.0f);
    static const ushort masks[] = {0, 1, 2, 4, 8, 0x10, 0x20, 0x40, 0x80, 0x100, 0x200, 0x400, 0x800};
    test->mask = (ushort)(R(3) ? 0 : masks[R(COUNT(masks))] | masks[R(COUNT(masks))]);
    static const ushort flags[] = {0, 0, 1, 4, 8, 0x10, 0x20, 0x11, 0x18};
    test->flags = flags[R(COUNT(flags))];
    test->found = (ushort)(R(4) == 0);
    static const int rooms[] = {0, 1, 2, 4};
    test->cel = R(8) ? &world.cels[rooms[R(4)]] : NULL;
    test->tester = &world.objs[R(OBJS)];
    test->ignoreA = R(3) ? NULL : &world.objs[R(OBJS)];
    test->ignore = R(3) ? NULL : &world.objs[R(OBJS)];
    test->glist = &world.geom[R(2)].glist;
    test->matrix = &world.mtx;
    test->plane.a = Plain(-1.0f, 1.0f);
    if (R(2))
        RandomVector(&test->push, 0.5f);
}

// A cel with collision geometry, as Collide_Pick finds them
cel_tag *CollidingCel(void) {
    for (int tries = 0; tries < 8; tries++) {
        cel_tag *cel = &world.cels[R(CELS)];
        if (cel->glist != NULL && cel->glist->colldata != NULL)
            return cel;
    }
    return &world.cels[0];
}

// A list of n hits from the heap's nodes, with the distances, objects and materials that matter
HITDATA_tag *RandomList(int n) {
    HITDATA_tag *head = NULL;
    for (int i = 0; i < n; i++) {
        HITDATA_tag *hit = (HITDATA_tag *)LList_Cut(&*(LLISTINFO_tag *)XbeOurs(0x001ddc60));
        if (hit == NULL)
            break;
        memset(hit, 0, sizeof(*hit));
        hit->dist = R(5) == 0 ? HIT_NONE : Value(-2.0f, 30.0f);
        if (R(8) == 0)
            hit->dist = 1.5f;
        hit->dmgAmt = Value(0.0f, 40.0f);
        hit->materialType = (char)(R(2) ? 0 : kMaterials[R(COUNT(kMaterials))]);
        hit->flags = (char)R(8);
        hit->hitBoneIdx = (short)R(70);
        RandomVector(&hit->hitPosition, 6.0f);
        RandomVector(&hit->surfaceNormal.normal, 1.0f);
        RandomVector(&hit->maybehitDirection, 1.0f);
        int which = R(4);
        if (which == 0)
            hit->hitCel = CollidingCel();
        else if (which == 1 || which == 2)
            hit->hitObj = &world.objs[R(OBJS)];
        hit->next = head;
        if (head != NULL)
            head->prev = hit;
        head = hit;
    }
    return head;
}

void RandomObject(int i) {
    obj_tag *obj = &world.objs[i];
    memset(obj, 0, sizeof(*obj));
    memset(world.extra[i], 0, sizeof(world.extra[i]));
    static const char types[] = {OBJECTTYPE_DRONE, OBJECTTYPE_PLAYER, OBJECTTYPE_BULLET, OBJECTTYPE_DEAD_DRONE,
                                 OBJECTTYPE_DEAD_PLAYER, OBJECTTYPE_RIGIDBODY, 1, 9};
    obj->objectType = types[R(COUNT(types))];
    PlainVector(&obj->centrePoint, 5.0f);
    obj->radius = Plain(0.1f, 2.5f);
    RandomMatrix(&obj->transformMatrix);
    obj->position = *Mat_Position(obj->transformMatrix);
    obj->lastPosition = obj->position;
    if (R(2))
        obj->lastPosition.x += Plain(-1.0f, 1.0f);
    switch (R(4)) {
    case 0: obj->objGraphics = NULL; break;
    case 1: obj->objGraphics = &world.noColl; break;
    default: obj->objGraphics = &world.geom[1].glist; break;
    }
    // Geometry without collision data only on a skinned object (Collide_Pick passes the others by)
    if (R(3) == 0 || obj->objGraphics == &world.noColl)
        obj->transformFlags |= TRANSFORM_ANIMATED;
    obj->unknown_0xd9 = (char)R(8);
    static const int effects[] = {0, 0, 0, 0x10, 0x20, 0x40, 0x1000, 0x10000000, 0x20000000};
    obj->effectFlags = effects[R(COUNT(effects))];
    HITTEST_tag *test = &world.objTests[i];
    RandomTest(test);
    if (R(3))
        test->type = HITTEST_CYLINDER;
    obj->maybeCollision = R(4) ? test : NULL;
    obj->extraObjectData = world.extra[i];
    float *movement = (float *)(world.extra[i] + 0xc);         // BLData::movement
    movement[0] = Plain(-1.0f, 1.0f);
    movement[1] = Plain(-1.0f, 1.0f);
    movement[2] = Plain(-1.0f, 1.0f);
    *(float *)(world.extra[i] + 0x38c) = Plain(0.0f, 1.0f);     // Drone_tag::moved
    *(void **)(world.extra[i] + 0x38) = world.weapons[R(2)];   // BU_tag::wpnDef
    obj->nextObject = i + 1 < OBJS - 1 ? &world.objs[i + 1] : NULL;
}

void RandomCel(int i) {
    cel_tag *cel = &world.cels[i];
    memset(cel, 0, sizeof(*cel));
    switch (R(5)) {
    case 0: cel->glist = NULL; break;
    case 1: cel->glist = (i == 3 || i == 5) ? &world.noColl : NULL; break;
    default: cel->glist = &world.geom[0].glist; break;
    }
    static const uint flags[] = {0, 0, 0, CEL_NO_COLLISION, CEL_NO_CONTACT, CEL_MASKABLE, 0x8};
    cel->flags84 = flags[R(COUNT(flags))];
    cel->bitsFromPlacementTag = R(3) == 0 ? CEL_WORLD_SPACE : 0;
    PlainVector(&cel->someMin, 4.0f);
    cel->someMax = cel->someMin;
    cel->someMax.x += Plain(0.0f, 6.0f);
    cel->someMax.y += Plain(0.0f, 6.0f);
    cel->someMax.z += Plain(0.0f, 6.0f);
    PlainVector(&cel->sphere, 4.0f);
    cel->sphereRadius = Plain(0.5f, 5.0f);
    PlainVector(&cel->position, 2.0f);
    PlainVector(&cel->rotation, 3.2f);
    if (R(3) == 0)
        cel->rotation.x = cel->rotation.y = cel->rotation.z = 0.0f;
    cel->shearHeight = Plain(-3.0f, 3.0f);
}

void RandomPortal(portal_tag *p, cel_tag *to, portal_tag *partner) {
    memset(p, 0, sizeof(*p));
    _VECTOR centre;
    PlainVector(&centre, 3.0f);
    _VECTOR u, w;
    PlainVector(&u, 2.0f);
    PlainVector(&w, 2.0f);
    for (int k = 0; k < 4; k++) {
        float s = (k == 0 || k == 3) ? -1.0f : 1.0f, t = (k < 2) ? -1.0f : 1.0f;
        p->corners[k].x = centre.x + s * u.x + t * w.x;
        p->corners[k].y = centre.y + s * u.y + t * w.y;
        p->corners[k].z = centre.z + s * u.z + t * w.z;
    }
    Plane_PlaneEq(&p->plane, &p->corners[0], &p->corners[1], &p->corners[2]);
    p->centre = centre;
    p->boxMin = p->boxMax = p->corners[0];
    for (int k = 1; k < 4; k++) {
        const _VECTOR &c = p->corners[k];
        if (c.x < p->boxMin.x) p->boxMin.x = c.x;
        if (c.y < p->boxMin.y) p->boxMin.y = c.y;
        if (c.z < p->boxMin.z) p->boxMin.z = c.z;
        if (c.x > p->boxMax.x) p->boxMax.x = c.x;
        if (c.y > p->boxMax.y) p->boxMax.y = c.y;
        if (c.z > p->boxMax.z) p->boxMax.z = c.z;
    }
    p->cel = to;
    p->partner = partner;
    p->stamp = R(6) == 0 ? GameState.collisionStamp + 1 : 0;
}

void RandomWorld(void) {
    memset(&world, 0, sizeof(world));
    BuildGeometry(world.geom[0]);
    BuildGeometry(world.geom[1]);
    world.noColl = world.geom[1].glist;
    world.noColl.colldata = NULL;
    for (int i = 0; i < CELS; i++)
        RandomCel(i);
    // The rooms' own geometry has collision data
    world.cels[0].glist = &world.geom[0].glist;
    // Two rooms, each with two cels chained from it
    world.cels[0].childCel = &world.cels[2];
    world.cels[2].childCel = R(2) ? &world.cels[3] : NULL;
    world.cels[1].childCel = &world.cels[4];
    world.cels[4].childCel = &world.cels[5];
    // Portals: 0 and 1 between the rooms, 2 and 3 from room 0 to cel 4 and back, 4 and 5 from room 1 to cel 2
    RandomPortal(&world.portals[0], &world.cels[1], &world.portals[1]);
    RandomPortal(&world.portals[1], &world.cels[0], &world.portals[0]);
    RandomPortal(&world.portals[2], &world.cels[4], &world.portals[3]);
    RandomPortal(&world.portals[3], &world.cels[0], &world.portals[2]);
    RandomPortal(&world.portals[4], &world.cels[2], &world.portals[5]);
    RandomPortal(&world.portals[5], &world.cels[1], &world.portals[4]);
    if (R(3) == 0)
        world.portals[1] = world.portals[0];
    world.cels[0].portals = &world.portals[0];
    world.portals[0].next = R(2) ? &world.portals[2] : NULL;
    world.cels[1].portals = &world.portals[1];
    world.portals[1].next = &world.portals[4];
    world.cels[4].portals = R(2) ? &world.portals[3] : NULL;
    world.cels[2].portals = &world.portals[5];
    for (int i = 0; i < OBJS; i++)
        RandomObject(i);
    // The cels' objects, and the forced list
    world.cels[0].firstObject = &world.objs[0];
    world.objs[0].nextInCel = &world.objs[1];
    world.objs[1].nextInCel = R(2) ? &world.objs[2] : NULL;
    world.cels[1].firstObject = &world.objs[3];
    world.objs[3].nextInCel = &world.objs[4];
    LLISTINFO_tag *forced = (LLISTINFO_tag *)ForcedListInfo;
    memset(forced, 0, sizeof(*forced));
    if (R(2)) {
        forced->head = (LLNODE_tag *)&world.objs[5];
        world.objs[5].llNext = R(2) ? (LLNODE_tag *)&world.objs[6] : (LLNODE_tag *)&world.objs[5];
        world.objs[6].llNext = NULL;
    }
    // The hit heap: all the nodes
    LLISTINFO_tag *heap = (LLISTINFO_tag *)XbeOurs(0x001ddc60);
    memset(heap, 0, sizeof(*heap));
    heap->elementSize = sizeof(HITDATA_tag);
    for (int i = 0; i < NODES; i++)
        LList_Add(heap, (LLNODE_tag *)&world.nodes[i]);
    // The box list: entries past any count are earlier searches' leaves
    BOXENTRY_tag *boxes = (BOXENTRY_tag *)XbeOurs(0x001dc9a0);
    for (int i = 0; i < MAX_COLBOXES; i++) {
        boxes[i].box = &world.geom[R(2)].boxes[3 + R(4)];
        boxes[i].t = 0.0f;
    }
    U32_AT(XbeOurs(0x001ddc6c)) = 0;
    // The triangle heap
    U32_AT(XbeOurs(0x001dec28)) = 3 * (1 + R(8));
    if (R(10) == 0)
        U32_AT(XbeOurs(0x001dec28)) = 501 + R(2) * 99;
    U32_AT(XbeOurs(0x001dec2c)) = R(3) ? 0 : 3 * R(4);
    *(_VECTOR **)XbeOurs(0x001dec30) = world.triHeap;
    U8_AT(XbeOurs(0x001dec1c)) = (uint8_t)R(2);
    DeltaP.x = Plain(-1.0f, 1.0f);
    // Joints
    world.jointCount = (ushort)R(JOINTS + 1);
    for (int j = 0; j < JOINTS; j++) {
        RandomMatrix(&world.joints[j].m);
        PlainVector(&world.joints[j].half, 1.0f);
        world.joints[j].half.x = fabsf(world.joints[j].half.x);
        world.joints[j].half.y = fabsf(world.joints[j].half.y);
        world.joints[j].half.z = fabsf(world.joints[j].half.z);
        world.joints[j].joint = (short)R(70);
    }
    *(ushort *)(world.weapons[0] + 0x10) = (ushort)R(0x10000);
    *(ushort *)(world.weapons[1] + 0x10) = (ushort)(1 << R(16));
    PlainVector(&world.wind, 3.0f);
    static const int rooms[] = {0, 1, 2, 4};
    world.findCel = R(5) ? rooms[R(4)] : -1;
    for (int i = 0; i < 8; i++)
        PlainVector(&world.v[i], 6.0f);
    RandomMatrix(&world.mtx);
    for (int i = 0; i < 64; i++)
        world.stream[i] = R(0);
    MPSettings.isMultiplayer = R(3) == 0;
    FLOAT_AT(XbeOurs(0x0017c100)) = R(4) ? 1.0f : Plain(0.5f, 2.0f);
    FLOAT_AT(XbeOurs(0x0017c104)) = R(4) ? 1.0f / 60.0f : Plain(0.0f, 0.1f);
    GameState.NumFramesUnpaused = R(64);
    RandWords[0] = R(0) | 1;
    RandWords[1] = R(0);
    RandomTest(&world.test);
}

// A tester left pointing at the stack (Collide_CylinderIntersect's) shows as one value
void Settle(void) {
    char here;
    for (int i = 0; i < OBJS; i++) {
        ptrdiff_t d = (char *)world.objTests[i].tester - &here;
        if (d > -0x10000 && d < 0x10000)
            world.objTests[i].tester = (obj_tag *)(uintptr_t)0x5ca1ab1e;
        RIGIDBODY *rb = (RIGIDBODY *)world.extra[i];
        d = (char *)rb->test.tester - &here;
        if (d > -0x10000 && d < 0x10000)
            rb->test.tester = (obj_tag *)(uintptr_t)0x5ca1ab1e;
    }
    ptrdiff_t d = (char *)world.test.tester - &here;
    if (d > -0x10000 && d < 0x10000)
        world.test.tester = (obj_tag *)(uintptr_t)0x5ca1ab1e;
}

// ---- calling

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

uint32_t P(const void *p) { return (uint32_t)(uintptr_t)p; }

typedef uint32_t (*Runner)(bool original);
struct Case {
    const char *name;
    Runner run;
};
const Case *current;

// The stack below, as zeros, the same for both
__declspec(noinline) void ZeroStack(void) {
    volatile uint32_t below[0x1000];
    for (int i = 0; i < 0x1000; i++)
        below[i] = 0;
}

uint32_t RunTheirs() {
    gen = caseSeed;
    ZeroStack();
    uint32_t result = current->run(true);
    Settle();
    return result;
}

uint32_t RunOurs() {
    gen = caseSeed;
    ZeroStack();
    uint32_t result = current->run(false);
    Settle();
    return result;
}

uint32_t Regs6(bool original, unsigned at, const void *ours, Regs r, int argc, const uint32_t *args) {
    if (original) {
        XbeOriginalScope o(at);
        ZeroStack();
        return CallRegs(at, &r, argc, args);
    }
    return CallRegs((unsigned)(uintptr_t)ours, &r, argc, args);
}

#define CDECL_RUN(at, ours, type, ...)                                                                                \
    (original ? ([&]() { XbeOriginalScope o(at); ZeroStack(); return ORIGINAL(at, type)(__VA_ARGS__); }()) \
              : ours(__VA_ARGS__))

obj_tag *AnyObj() { return &world.objs[R(OBJS)]; }
// The cels a test can start in (the others may have geometry without collision data)
cel_tag *AnyCel() {
    static const int rooms[] = {0, 1, 2, 4};
    return &world.cels[rooms[R(4)]];
}

// ---- the cases: hits and lists

uint32_t RunAddHit(bool original) {
    world.list = RandomList(R(3));
    float dist = Value(-1.0f, 20.0f);
    cel_tag *cel = R(2) ? AnyCel() : NULL;
    obj_tag *obj = R(2) ? AnyObj() : NULL;
    return W(CDECL_RUN(0x00029560, Coll_AddHitToList, HITDATA_tag *(__cdecl *)(HITDATA_tag **, float, cel_tag *, obj_tag *),
                       &world.list, dist, cel, obj));
}

uint32_t RunSort(bool original) {
    world.list = RandomList(R(12));
    Regs r = {0, P(&world.list), 0, 0, 0, 0};
    Regs6(original, 0x000295a0, (const void *)&Collide_Sort, r, 0, NULL);
    return 0;
}

uint32_t RunFlags(bool original) {
    uchar material = R(2) ? kMaterials[R(COUNT(kMaterials))] : (uchar)R(256);
    uchar hitFlags = (uchar)R(8);
    ushort mask = (ushort)R(0x1000);
    if (R(2))
        mask &= 0x80b;
    world.v[0].x = R(3) ? HIT_NONE : Value(0.0f, 10.0f);
    return CDECL_RUN(0x00028a50, Intersect_CheckIfFlagsMatch, bool (__cdecl *)(uchar, uchar, ushort, float *), material,
                     hitFlags, mask, &world.v[0].x);
}

uint32_t RunFilterBullets(bool original) {
    world.list = RandomList(R(8));
    ushort flags = (ushort)R(0x10000);
    CDECL_RUN(0x0002b3b0, Collide_FilterBullets, void (__cdecl *)(HITDATA_tag **, ushort), &world.list, flags);
    return 0;
}

uint32_t RunDamage(bool original) {
    world.list = RandomList(R(10));
    obj_tag **list = R(3) ? world.objList : NULL;
    ushort *count = R(4) ? &world.objCount : NULL;
    ushort max = (ushort)R(9);
    return Fold(CDECL_RUN(0x0002b350, Collide_GetDamageNObjects,
                          double (__cdecl *)(HITDATA_tag *, obj_tag **, ushort *, ushort), world.list, list, count, max));
}

// ---- boxes, spheres and triangles

void BoxArgs(_VECTOR *c, _VECTOR *mn, _VECTOR *mx) {
    PlainVector(mn, 4.0f);
    *mx = *mn;
    mx->x += Value(0.0f, 4.0f);
    mx->y += Value(0.0f, 4.0f);
    mx->z += Value(0.0f, 4.0f);
    c->x = Around(mn->x, mx->x);
    c->y = Around(mn->y, mx->y);
    c->z = Around(mn->z, mx->z);
}

uint32_t RunSphereBox(bool original) {
    BoxArgs(&world.v[0], &world.v[1], &world.v[2]);
    float r = R(4) ? Plain(0.0f, 2.0f) : Value(-1.0f, 3.0f);
    if (R(4) == 0) {
        r = 1.0f;
        world.v[0].x = world.v[2].x + 1.0f;
    }
    return CDECL_RUN(0x00028ae0, Intersect_SphereBox, bool (__cdecl *)(_VECTOR *, float, _VECTOR *, _VECTOR *),
                     &world.v[0], r, &world.v[1], &world.v[2]);
}

uint32_t RunBoxBox(bool original) {
    BoxArgs(&world.v[4], &world.v[0], &world.v[1]);
    BoxArgs(&world.v[4], &world.v[2], &world.v[3]);
    if (R(3) == 0)
        world.v[2].y = world.v[1].y;
    return CDECL_RUN(0x00028b60, Intersect_BoxBox, bool (__cdecl *)(_VECTOR *, _VECTOR *, _VECTOR *, _VECTOR *),
                     &world.v[0], &world.v[1], &world.v[2], &world.v[3]);
}

uint32_t RunPointOOBox(bool original) {
    RandomMatrix(&world.mtx);
    PlainVector(&world.v[0], 2.0f);
    RandomVector(&world.v[1], 6.0f);
    float r = Value(0.0f, 2.0f);
    return CDECL_RUN(0x00028bd0, Intersect_PointOOBox, bool (__cdecl *)(_MATRIX *, _VECTOR *, _VECTOR *, float),
                     &world.mtx, &world.v[0], &world.v[1], r);
}

uint32_t RunRayBox(bool original) {
    BoxArgs(&world.v[3], &world.v[0], &world.v[1]);
    HITTEST_tag *test = &world.test;
    test->rayStart = world.v[3];
    if (R(3) == 0)
        test->rayStart.x = world.v[0].x - Plain(0.0f, 3.0f);
    RandomVector(&test->dir, 4.0f);
    if (R(3) == 0)
        test->dir.x = 0.0f;
    if (R(3) == 0)
        test->dir.y = 0.0f;
    if (R(3) == 0)
        test->dir.z = 0.0f;
    test->radius = Value(0.0f, 3.0f);
    world.v[4].x = Value(-1.0f, 1.0f);
    return CDECL_RUN(0x00028cf0, Intersect_RayBox, bool (__cdecl *)(HITTEST_tag *, _VECTOR *, _VECTOR *, float *), test,
                     &world.v[0], &world.v[1], &world.v[4].x);
}

uint32_t RunSwept(bool original) {
    for (int i = 0; i < 3; i++)
        RandomVector(&world.v[i], 4.0f);
    BoxArgs(&world.v[5], &world.v[3], &world.v[4]);
    if (R(3) == 0)
        world.v[1].x = world.v[3].x;
    Regs r = {0, P(&world.v[4]), P(&world.v[1]), P(&world.v[0]), P(&world.v[2]), P(&world.v[3])};
    return Regs6(original, 0x00029020, (const void *)&Intersect_AABB_SweptVolume, r, 0, NULL) & 0xff;
}

uint32_t RunSpheres(bool original) {
    HITTEST_tag *test = &world.test;
    PlainVector(&test->sphere, 3.0f);
    test->sphereRadius = Value(0.0f, 2.0f);
    obj_tag *obj = AnyObj();
    if (R(3) == 0)
        obj->centrePoint = test->sphere;
    float dist = 0.0f;
    uint32_t args[1] = {P(&dist)};
    uint32_t result;
    switch (R(3)) {
    case 0: {
        Regs r = {0, P(&test->sphere), 0, 0, P(&world.v[0]), P(&obj->centrePoint)};
        result = Regs6(original, 0x00029370, (const void *)&Intersect_SphereSphere, r, 1, args);
        break;
    }
    case 1: {
        Regs r = {0, 0, 0, 0, P(test), 0};
        result = Regs6(original, 0x000293b0, (const void *)&Intersect_RaySphere, r, 1, args);
        break;
    }
    default: {
        Regs r = {0, 0, 0, 0, P(test), 0};
        result = Regs6(original, 0x00029410, (const void *)&Intersect_CylSphere, r, 1, args);
        break;
    }
    }
    return (result & 0xff) ^ (Bits(dist) << 8);
}

uint32_t RunRecurse(bool original) {
    Geometry &g = world.geom[R(2)];
    COLLBOX_tag *box = &g.boxes[R(BOXES)];
    U32_AT(XbeOurs(0x001ddc6c)) = R(4);
    switch (R(3)) {
    case 0:
        CDECL_RUN(0x000291c0, RecurseBoxesSph, void (__cdecl *)(COLLDATA_tag *, COLLBOX_tag *, _VECTOR *, float),
                  &g.coll, box, &world.test.sphere, world.test.sphereRadius);
        break;
    case 1:
        CDECL_RUN(0x000292e0, ASH_RecurseBoxesRay, void (__cdecl *)(COLLDATA_tag *, COLLBOX_tag *, HITTEST_tag *),
                  &g.coll, box, &world.test);
        break;
    default:
        BoxArgs(&world.v[2], &world.v[0], &world.v[1]);
        CDECL_RUN(0x00029250, ASH_RecurseBoxesBox, void (__cdecl *)(COLLDATA_tag *, COLLBOX_tag *, _VECTOR *, _VECTOR *),
                  &g.coll, box, &world.v[0], &world.v[1]);
        break;
    }
    return 0;
}

uint32_t RunUnpack(bool original) {
    Geometry &g = world.geom[R(2)];
    uchar *block = g.blocks[R(BLOCKS)].raw;
    if (R(2)) {
        for (int i = 0; i < 32; i++)
            ((short *)block)[i] = (short)R(0x10000);
    }
    if (R(2))
        return CDECL_RUN(0x000288e0, Intersect_UnpackTrianglePlaneEq, bool (__cdecl *)(uchar *, ushort, plane_equ_tag *),
                         block, (ushort)R(27), &world.plane);
    COLLBOX_tag *box = &g.boxes[3 + R(4)];
    if (R(4) == 0)
        box->quant.scale = Value(-1.0f, 1.0f);
    CDECL_RUN(0x00028960, Intersect_UnpackTriangleVerticesToWorld,
              void (__cdecl *)(uchar *, ushort, ushort, ushort, _VECTOR *, COLLQUANT_tag *), block,
              (ushort)R(30), (ushort)R(30), (ushort)R(30), &world.v[0], &box->quant);
    return 0;
}

// ---- vecutil

uint32_t RunClosest(bool original) {
    RandomVector(&world.v[1], 5.0f);
    world.v[2] = world.v[1];
    switch (R(4)) {
    case 0: break;
    case 1: world.v[2].x += 1e-12f; break;
    default: RandomVector(&world.v[2], 5.0f); break;
    }
    RandomVector(&world.v[3], 5.0f);
    if (R(5) == 0)
        world.v[3] = R(2) ? world.v[1] : world.v[2];
    return Fold(CDECL_RUN(0x000d7d00, vecutil_calculate_closest_point_on_line,
                          double (__cdecl *)(_VECTOR *, _VECTOR *, _VECTOR *, _VECTOR *), &world.v[0], &world.v[1],
                          &world.v[2], &world.v[3]));
}

uint32_t RunDist2Tri(bool original) {
    for (int i = 1; i < 4; i++)
        PlainVector(&world.v[i], 4.0f);
    if (R(8) == 0)
        world.v[2] = world.v[1];
    Plane_PlaneEq(&world.plane, &world.v[1], &world.v[2], &world.v[3]);
    if (R(6) == 0)
        world.plane.a = -world.plane.a;
    RandomVector(&world.v[0], 6.0f);
    _VECTOR *closest = R(4) ? &world.v[4] : NULL;
    if (R(2))
        return Fold(CDECL_RUN(0x000d82d0, vecutil_Dist2Tri,
                              double (__cdecl *)(_VECTOR *, _VECTOR *, _VECTOR *, _VECTOR *, plane_equ_tag *, ushort *, _VECTOR *),
                              &world.v[0], &world.v[1], &world.v[2], &world.v[3], &world.plane, &world.code, closest));
    float planeDist = (float)DistancePointToPlane(&world.v[0], &world.plane);
    if (R(5) == 0)
        planeDist = Value(-3.0f, 3.0f);
    return Fold(CDECL_RUN(0x000d80f0, vecutil_Dist2Tri_PointGeom,
                          double (__cdecl *)(_VECTOR *, _VECTOR *, _VECTOR *, _VECTOR *, plane_equ_tag *, float, _VECTOR *),
                          &world.v[0], &world.v[1], &world.v[2], &world.v[3], &world.plane, planeDist, closest));
}

uint32_t RunLineLine(bool original) {
    for (int i = 0; i < 4; i++)
        RandomVector(&world.v[i], 3.0f);
    switch (R(6)) {
    case 0: world.v[3] = world.v[2]; break;
    case 1: world.v[2].x = world.v[0].x; world.v[2].z = world.v[0].z; break;
    case 2: // along each other
        world.v[2].x = world.v[0].x + (world.v[1].x - world.v[0].x) * 0.25f;
        world.v[2].z = world.v[0].z + (world.v[1].z - world.v[0].z) * 0.25f;
        world.v[3].x = world.v[0].x + (world.v[1].x - world.v[0].x) * 0.75f;
        world.v[3].z = world.v[0].z + (world.v[1].z - world.v[0].z) * 0.75f;
        break;
    default: break;
    }
    return CDECL_RUN(0x000d60a0, vecutil_intersect_line_line_2d,
                     int (__cdecl *)(_VECTOR *, _VECTOR *, _VECTOR *, _VECTOR *, _VECTOR *), &world.v[0], &world.v[1],
                     &world.v[2], &world.v[3], &world.v[4]);
}

// ---- the geometry tests

HITDATA_tag *AnyHit(void) {
    HITDATA_tag *hit = RandomList(1);
    hit->flags = (char)(R(2) ? 0 : R(8));
    return hit;
}

uint32_t RunPointGeom(bool original) {
    HITDATA_tag *hit = AnyHit();
    CDECL_RUN(0x00029630, Intersect_PointGeom, void (__cdecl *)(HITTEST_tag *, HITDATA_tag *), &world.test, hit);
    return 0;
}

uint32_t RunRayGeom(bool original) {
    HITDATA_tag *hit = AnyHit();
    CDECL_RUN(0x00029fa0, Intersect_RayGeom, void (__cdecl *)(HITTEST_tag *, HITDATA_tag *), &world.test, hit);
    return 0;
}

uint32_t RunCylGeom(bool original) {
    HITDATA_tag *hit = AnyHit();
    CDECL_RUN(0x0002a2c0, Intersect_CylGeom, void (__cdecl *)(HITTEST_tag *, HITDATA_tag *), &world.test, hit);
    return 0;
}

uint32_t RunCylTriGeom(bool original) {
    HITDATA_tag *hit = AnyHit();
    CDECL_RUN(0x00029a30, Intersect_CylTriGeom, void (__cdecl *)(HITTEST_tag *, HITDATA_tag *), &world.test, hit);
    return 0;
}

// ---- the broad phase

uint32_t RunStraddle(bool original) {
    return W(CDECL_RUN(0x0002b910, Collide_StraddleCels, cel_tag **(__cdecl *)(HITTEST_tag *, ushort *), &world.test,
                       &world.code));
}

uint32_t RunPortal(bool original) {
    portal_tag *portal = &world.portals[R(PORTALS)];
    if (R(3) == 0)
        world.test.pos = portal->corners[R(4)];
    return CDECL_RUN(0x0002b5c0, Intersect_Portal, bool (__cdecl *)(HITTEST_tag *, portal_tag *), &world.test, portal);
}

uint32_t RunPick(bool original) {
    CDECL_RUN(0x0002ba00, Collide_Pick, void (__cdecl *)(HITTEST_tag *), &world.test);
    return 0;
}

uint32_t RunPickObj(bool original) {
    obj_tag *obj = AnyObj();
    uint32_t args[2] = {P(obj), P(R(6) ? world.test.tester : obj)};
    Regs r = {P(&world.test), 0, 0, 0, 0, 0};
    Regs6(original, 0x0002aa80, (const void *)&Collide_PickObj, r, 2, args);
    return 0;
}

// ---- the narrow phase and the queries

uint32_t RunJointy(bool original) {
    HITDATA_tag *hit = AnyHit();
    hit->hitObj = AnyObj();
    uint32_t args[1] = {P(&world.test)};
    Regs r = {P(hit), 0, 0, 0, 0, 0};
    return Regs6(original, 0x0002acd0, (const void *)&Collide_Jointy, r, 1, args) & 0xff;
}

uint32_t RunIntersect(bool original) {
    HITDATA_tag *hit = AnyHit();
    if (R(2)) {
        hit->hitCel = CollidingCel();
        hit->hitObj = NULL;
    } else {
        hit->hitCel = NULL;
        hit->hitObj = R(10) ? AnyObj() : NULL;
    }
    uint32_t args[2] = {P(&world.test), (uint32_t)R(0x40)};
    Regs r = {P(hit), 0, 0, 0, 0, 0};
    return Regs6(original, 0x0002be10, (const void *)&Collide_Intersect, r, 2, args) & 0xff;
}

uint32_t RunTestHits(bool original) {
    world.test.tester = AnyObj();
    world.test.tester->hitList = RandomList(R(3));
    world.test.tester->maybeCollision = &world.test;
    uint32_t args[1] = {R(2)};
    Regs r = {R(0x40), 0, 0, 0, 0, P(&world.test)};
    Regs6(original, 0x0002c440, (const void *)&Collide_TestHits, r, 1, args);
    return 0;
}

uint32_t RunRay(bool original) {
    cel_tag *cel = R(10) ? AnyCel() : NULL;
    return CDECL_RUN(0x0002c4e0, Collide_RayIntersect,
                     bool (__cdecl *)(_VECTOR *, _VECTOR *, cel_tag *, obj_tag *, obj_tag *, HITDATA_tag **, char, ushort, ushort),
                     &world.v[0], &world.v[1], cel, R(2) ? AnyObj() : NULL, R(2) ? AnyObj() : NULL,
                     &world.list, (char)R(2), world.test.mask, (ushort)(R(3) ? 0 : 0x10));
}

uint32_t RunSphere(bool original) {
    cel_tag *cel = R(10) ? AnyCel() : NULL;
    return CDECL_RUN(0x0002c610, Collide_SphereIntersect,
                     bool (__cdecl *)(_VECTOR *, float, cel_tag *, obj_tag *, obj_tag *, HITDATA_tag **, char, char, ushort, ushort),
                     &world.v[0], Plain(0.05f, 3.0f), cel, R(2) ? AnyObj() : NULL,
                     R(2) ? AnyObj() : NULL, &world.list, (char)R(2), (char)R(2),
                     world.test.mask, world.test.flags);
}

uint32_t RunCylinder(bool original) {
    return CDECL_RUN(0x0002c6f0, Collide_CylinderIntersect, bool (__cdecl *)(HITTEST_tag *, HITDATA_tag **, char, ushort),
                     &world.test, &world.list, (char)R(2), world.test.flags);
}

uint32_t RunCylinderTris(bool original) {
    cel_tag *cel = R(4) ? AnyCel() : NULL;
    return W(CDECL_RUN(0x0002c760, Collide_CylinderTris,
                       _VECTOR *(__cdecl *)(_VECTOR *, _VECTOR *, float, cel_tag *, uint *, ushort, ushort), &world.v[0],
                       &world.v[1], Plain(0.05f, 3.0f), cel, &world.count, world.test.mask,
                       (ushort)(R(2) | (R(3) ? 0 : 0x10))));
}

uint32_t RunPlaceObject(bool original) {
    RandomMatrix(&world.mtx);
    return CDECL_RUN(0x0002c8e0, Collide_PlaceObject, bool (__cdecl *)(_VECTOR *, _MATRIX *, float, float, obj_tag *),
                     &world.v[0], &world.mtx, Plain(0.05f, 2.0f), Plain(0.0f, 2.0f), AnyObj());
}

// The objects' hit lists, and types with no collision handler
char quietTypes[64];
int quietCount;

uint32_t RunUpdate(bool original) {
    for (int i = 0; i < OBJS - 1; i++) {
        world.objs[i].objectType = quietTypes[R(quietCount)];
        if (R(3) == 0)
            world.objs[i].hitList = RandomList(R(3));
    }
    CDECL_RUN(0x0002cca0, Collide_Update, void (__cdecl *)(obj_tag *), R(10) ? &world.objs[0] : NULL);
    return 0;
}

// ---- the rigid bodies

RIGIDBODY *RandomBody(obj_tag *obj, int i) {
    RIGIDBODY *rb = (RIGIDBODY *)world.extra[i];
    memset(rb, 0, sizeof(*rb));
    obj->extraObjectData = rb;
    obj->objectType = OBJECTTYPE_RIGIDBODY;
    obj->inCel = R(6) ? AnyCel() : NULL;
    RandomVector(&rb->vel, 3.0f);
    RandomVector(&rb->omega, 3.0f);
    RandomVector(&rb->accel, 3.0f);
    RandomVector(&rb->angAccel, 3.0f);
    RandomVector(&rb->impulse, 3.0f);
    RandomVector(&rb->angImpulse, 3.0f);
    if (R(4) == 0)
        rb->omega.x = rb->omega.y = rb->omega.z = 0.0f;
    if (R(4) == 0)
        rb->angImpulse.x = rb->angImpulse.y = rb->angImpulse.z = 0.0f;
    if (R(6) == 0)
        rb->angImpulse.x = 0.0001f;
    rb->gravity = R(2) ? &GRAVITY_VECTOR : &world.v[7];
    rb->quat.q[0] = Plain(-1.0f, 1.0f);
    rb->quat.q[1] = Plain(-1.0f, 1.0f);
    rb->quat.q[2] = Plain(-1.0f, 1.0f);
    rb->quat.q[3] = Plain(-1.0f, 1.0f);
    rb->matrix = &obj->transformMatrix;
    rb->mass = Value(0.5f, 20.0f);
    rb->invMass = 1.0f / rb->mass;
    for (int k = 0; k < 3; k++) {
        rb->inertia[k] = Value(0.1f, 5.0f);
        rb->invInertia[k] = Value(0.1f, 5.0f);
    }
    rb->restitution = R(2) ? -0.5f : Value(-1.0f, 0.0f);
    rb->halfHeight = Plain(0.0f, 2.0f);
    rb->linDamp = R(3) ? Value(0.0f, 2.0f) : 0.0f;
    rb->angDamp = R(3) ? Value(0.0f, 40.0f) : 0.0f;
    rb->kind = (ushort)R(6);
    static const ushort tests[] = {0, HITTEST_SPHERE, HITTEST_POINT};
    rb->testType = tests[R(3)];
    RandomTest(&rb->test);
    rb->test.cel = AnyCel();
    rb->test.mask = 0;
    rb->light = R(3) ? NULL : &world.light;
    rb->carriesLight = (uchar)R(2);
    rb->sound = R(0x200);
    obj->maybeCollision = &rb->test;
    return rb;
}

uint32_t RunBodySimple(bool original) {
    obj_tag *obj = &world.objs[0];
    RIGIDBODY *rb = RandomBody(obj, 0);
    float dt = R(5) ? Plain(0.0f, 0.1f) : Value(-1.0f, 1.0f);
    switch (R(9)) {
    case 0:
        CDECL_RUN(0x000c0020, RB_Init, void (__cdecl *)(RIGIDBODY *, ushort), rb, (ushort)R(6));
        return 0;
    case 1:
        for (int k = 0; k < 3; k++)
            world.inertia[k] = R(5) ? Plain(0.0f, 5.0f) : Value(-1.0f, 5.0f);
        if (R(3) == 0)
            world.inertia[2] = world.inertia[0] + world.inertia[1];
        return CDECL_RUN(0x000c00a0, RB_Build, bool (__cdecl *)(RIGIDBODY *, _VECTOR *, float *, float), rb, &world.v[0],
                         world.inertia, Value(0.1f, 10.0f));
    case 2:
        CDECL_RUN(0x000c0170, RB_AddImpulsiveForceAtPoint, void (__cdecl *)(RIGIDBODY *, _VECTOR *, _VECTOR *), rb,
                  &world.v[0], &world.v[1]);
        return 0;
    case 3:
        CDECL_RUN(0x000c0fb0, RB_CuboidMomentsOfInertia, void (__cdecl *)(float *, float, float, float, float),
                  world.inertia, Value(0.1f, 10.0f), Value(0.0f, 4.0f), Value(0.0f, 4.0f), Value(0.0f, 4.0f));
        return 0;
    case 4:
        CDECL_RUN(0x000c1010, RB_ToWorld, void (__cdecl *)(_VECTOR *, _VECTOR *, _MATRIX *), &world.v[0], &world.v[1],
                  &world.mtx);
        CDECL_RUN(0x000c1070, RB_ToBody, void (__cdecl *)(_VECTOR *, _VECTOR *, _MATRIX *), &world.v[2], &world.v[1],
                  &world.mtx);
        return 0;
    case 5:
        world.quat = rb->quat;
        CDECL_RUN(0x000c10d0, RB_apply_omega, void (__cdecl *)(quaternion_tag *, _VECTOR *, float), &world.quat,
                  &rb->omega, dt);
        return 0;
    case 6:
        CDECL_RUN(0x000c1150, RB_VecSpecial, void (__cdecl *)(_VECTOR *, float, _VECTOR *, _VECTOR *, float), &world.v[0],
                  Value(-2.0f, 2.0f), &world.v[1], &world.v[2], Value(-2.0f, 2.0f));
        return 0;
    case 7:
        CDECL_RUN(0x000c01d0, RB_Drag, void (__cdecl *)(RIGIDBODY *, _VECTOR *, _VECTOR *, float), rb, &rb->accel,
                  &rb->angAccel, dt);
        return 0;
    default:
        CDECL_RUN(0x000c02d0, RB_Move, void (__cdecl *)(RIGIDBODY *, float), rb, dt);
        return 0;
    }
}

uint32_t RunBodyStep(bool original) {
    obj_tag *obj = &world.objs[0];
    RIGIDBODY *rb = RandomBody(obj, 0);
    float dt = R(5) ? Plain(0.0f, 0.1f) : Value(-1.0f, 1.0f);
    switch (R(4)) {
    case 0:
        CDECL_RUN(0x000c0330, RB_ApplyImpulses, void (__cdecl *)(RIGIDBODY *), rb);
        break;
    case 1:
        CDECL_RUN(0x000c0890, RB_EulerEvolve, void (__cdecl *)(RIGIDBODY *, float, int), rb, dt, (int)R(2));
        break;
    case 2:
        CDECL_RUN(0x000c0b80, RB_ApplyWind, void (__cdecl *)(RIGIDBODY *, obj_tag *), rb, obj);
        break;
    default:
        CDECL_RUN(0x000c0da0, RB_Update, void (__cdecl *)(obj_tag *), R(10) ? obj : NULL);
        break;
    }
    return 0;
}

uint32_t RunBodyContact(bool original) {
    obj_tag *obj = &world.objs[0];
    RIGIDBODY *rb = RandomBody(obj, 0);
    HITDATA_tag *hits = RandomList(R(4));
    for (HITDATA_tag *h = hits; h != NULL; h = h->next) {
        if (h->hitObj == obj)
            h->hitObj = &world.objs[1];
    }
    obj->hitList = hits;
    if (R(3) == 0)
        rb->test.push.x = rb->test.push.y = rb->test.push.z = 0.0f;
    switch (R(4)) {
    case 0:
        CDECL_RUN(0x000c0c30, RB_RestingContact, void (__cdecl *)(obj_tag *, RIGIDBODY *, HITDATA_tag *), obj, rb, hits);
        break;
    case 1:
        CDECL_RUN(0x000c0470, RB_RestingContactRot, void (__cdecl *)(obj_tag *, RIGIDBODY *, HITDATA_tag *), obj, rb,
                  hits);
        break;
    case 2:
        if (hits != NULL)
            CDECL_RUN(0x000c06e0, RB_CalcTorque, void (__cdecl *)(RIGIDBODY *, HITDATA_tag *), rb, hits);
        break;
    default:
        CDECL_RUN(0x000c0f60, RB_CollisionHandler, void (__cdecl *)(obj_tag *), R(10) ? obj : NULL);
        break;
    }
    return 0;
}

uint32_t RunBodyCreate(bool original) {
    uint32_t *keys = (uint32_t *)world.placement;
    keys[0x2c / 4] = R(3) ? 0 : (R(4) ? R(50) : 0x80000000u + R(1000));
    *(ushort *)(world.placement + 0x34) = (ushort)R(6);
    world.placement[0x38] = (uint8_t)R(2);
    keys[0x3c / 4] = R(2) ? 0 : R(0x300);
    celglist_tag *glist = &world.geom[R(2)].glist;
    return W(CDECL_RUN(0x000c0990, RB_Create, obj_tag *(__cdecl *)(_VECTOR *, _VECTOR *, celglist_tag *, level_tag *),
                       &world.v[0], &world.v[1], glist, (level_tag *)world.placement));
}

// The regions, as ShadowRegions holds them, for comparing word by word
struct Area {
    uint8_t *at;
    size_t size;
};
Area areas[8];
int areaCount;
int nanChoices;

void AddArea(void *at, size_t size) {
    regions.Add(at, size);
    areas[areaCount].at = (uint8_t *)at;
    areas[areaCount++].size = size;
}

bool IsNaNWord(const uint8_t *p) {
    uint32_t u;
    memcpy(&u, p, 4);
    return (u & 0x7f800000u) == 0x7f800000u && (u & 0x007fffffu) != 0;
}

// As ShadowCompare, but two NaNs are the same: when an operation meets two NaNs, the x87 keeps the one with the larger
// significand (the positive one between equals) and SSE2 the first, which only a NaN's sign or payload shows
bool CompareCase(const char *what, int trial) {
    static std::vector<uint8_t> theirState, ourState;
    regions.Save();
    ShadowLog_Clear();
    uint32_t theirResult = RunTheirs();
    regions.Capture(theirState);
    std::string theirLog = ShadowLog_Text();
    regions.Restore();
    ShadowLog_Clear();
    uint32_t ourResult = RunOurs();
    regions.Capture(ourState);
    const std::string &ourLog = ShadowLog_Text();

    static std::map<std::string, int> reports;
    int &reported = reports[what];
    bool same = true;
    if (theirResult != ourResult) {
        same = false;
        if (reported++ < 3)
            printf("[collideport] %s case %d: returns 0x%x, the original 0x%x\n", what, trial, ourResult, theirResult);
    }
    size_t at = 0;
    bool nans = false;
    bool memorySame = true;
    for (int a = 0; a < areaCount && memorySame; a++) {
        for (size_t j = 0; j < areas[a].size; j++) {
            if (theirState[at + j] == ourState[at + j])
                continue;
            // A float the differing byte is in, at any alignment, that is a NaN in both
            size_t window = j >= 3 ? j - 3 : 0;
            for (; window <= j && window + 4 <= areas[a].size; window++) {
                if (IsNaNWord(&theirState[at + window]) && IsNaNWord(&ourState[at + window]))
                    break;
            }
            if (window <= j && window + 4 <= areas[a].size) {
                nans = true;
                j = window + 3;
                continue;
            }
            size_t word = j & ~(size_t)3;
            memorySame = false;
            if (reported++ < 3) {
                uint32_t theirs = 0, ours = 0;
                if (word + 4 <= areas[a].size) {
                    memcpy(&theirs, &theirState[at + word], 4);
                    memcpy(&ours, &ourState[at + word], 4);
                }
                printf("[collideport] %s case %d: memory differs in region %d at +0x%x: 0x%08x, the original 0x%08x\n",
                       what, trial, a, (unsigned)word, ours, theirs);
            }
            break;
        }
        at += areas[a].size;
    }
    same = same && memorySame;
    if (theirLog != ourLog) {
        same = false;
        if (reported++ < 3)
            printf("[collideport] %s case %d: calls differ\n  ours:     %s\n  original: %s\n", what, trial,
                   ourLog.c_str(), theirLog.c_str());
    }
    if (same && nans)
        nanChoices++;
    return same;
}

void Test(const char *group, const Case *list, int count, int trials) {
    int groupCases = cases, groupDiffer = differ, groupNaNs = nanChoices;
    for (int c = 0; c < count; c++) {
        current = &list[c];
        for (int trial = 0; trial < trials; trial++) {
            gen = ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
            RandomWorld();
            caseSeed = gen;
            cases++;
            if (!CompareCase(current->name, trial))
                differ++;
        }
    }
    printf("[collideport] %s: %d cases, %d differ (%d with NaNs chosen differently)\n", group, cases - groupCases,
           differ - groupDiffer, nanChoices - groupNaNs);
}

#define C(name, run) {name, run}

const Case kHits[] = {
    C("Coll_AddHitToList", RunAddHit),
    C("Collide_Sort", RunSort),
    C("Intersect_CheckIfFlagsMatch", RunFlags),
    C("Collide_FilterBullets", RunFilterBullets),
    C("Collide_GetDamageNObjects", RunDamage),
};

const Case kBoxes[] = {
    C("Intersect_SphereBox", RunSphereBox),
    C("Intersect_BoxBox", RunBoxBox),
    C("Intersect_PointOOBox", RunPointOOBox),
    C("Intersect_RayBox", RunRayBox),
    C("Intersect_AABB_SweptVolume", RunSwept),
    C("Intersect_SphereSphere, RaySphere, CylSphere", RunSpheres),
    C("RecurseBoxesSph, ASH_RecurseBoxesRay, ASH_RecurseBoxesBox", RunRecurse),
    C("Intersect_UnpackTriangle*", RunUnpack),
};

const Case kVecutil[] = {
    C("vecutil_calculate_closest_point_on_line", RunClosest),
    C("vecutil_Dist2Tri(_PointGeom)", RunDist2Tri),
    C("vecutil_intersect_line_line_2d", RunLineLine),
};

const Case kGeometry[] = {
    C("Intersect_PointGeom", RunPointGeom),
    C("Intersect_RayGeom", RunRayGeom),
    C("Intersect_CylGeom", RunCylGeom),
    C("Intersect_CylTriGeom", RunCylTriGeom),
};

const Case kBroad[] = {
    C("Collide_StraddleCels", RunStraddle),
    C("Intersect_Portal", RunPortal),
    C("Collide_Pick", RunPick),
    C("Collide_PickObj", RunPickObj),
};

const Case kQueries[] = {
    C("Collide_Jointy", RunJointy),
    C("Collide_Intersect", RunIntersect),
    C("Collide_TestHits", RunTestHits),
    C("Collide_RayIntersect", RunRay),
    C("Collide_SphereIntersect", RunSphere),
    C("Collide_CylinderIntersect", RunCylinder),
    C("Collide_CylinderTris", RunCylinderTris),
    C("Collide_PlaceObject", RunPlaceObject),
    C("Collide_Update", RunUpdate),
};

const Case kBodies[] = {
    C("RB_Init, Build, forces, inertia, ToWorld, ToBody, apply_omega, VecSpecial, Drag, Move", RunBodySimple),
    C("RB_ApplyImpulses, EulerEvolve, ApplyWind, Update", RunBodyStep),
    C("RB_RestingContact(Rot), CalcTorque, CollisionHandler", RunBodyContact),
    C("RB_Create", RunBodyCreate),
};

} // namespace

void CollidePortShadow_Run(void) {
    // Everything the cases change, kept to put back
    static uint8_t gameState[sizeof(GameState_t)], mpSettings[sizeof(MPSettings_t)], globals[COLLIDE_GLOBALS_SIZE];
    static uint8_t forced[12], frameRates[8];
    static uint32_t rand[2];
    memcpy(gameState, &GameState, sizeof(gameState));
    memcpy(mpSettings, &MPSettings, sizeof(mpSettings));
    XbeOursRead(globals, CollideGlobals.at, sizeof(globals));
    memcpy(forced, ForcedListInfo, sizeof(forced));
    XbeOursRead(frameRates, FrameRates.at, sizeof(frameRates));
    XbeOursRead(rand, RandWords.at, sizeof(rand));

    quietCount = 0;
    for (int t = 0; t < 0x55 && quietCount < COUNT(quietTypes); t++) {
        if (control_funcs[t].collideFunc == NULL)
            quietTypes[quietCount++] = (char)t;
    }

    regions.Clear();
    areaCount = 0;
    AddArea(&world, sizeof(world));
    AddArea(&GameState, sizeof(GameState_t));
    AddArea(&MPSettings, sizeof(MPSettings_t));
    AddArea(CollideGlobals.game(), COLLIDE_GLOBALS_SIZE);
    AddArea(ForcedListInfo, 12);
    AddArea(FrameRates.game(), 8);
    AddArea(RandWords.game(), 8);
    NfWarnMuted++;
    cases = differ = nanChoices = 0;

    InstallFakes();
    Test("hits and lists", kHits, COUNT(kHits), 3000);
    Test("boxes, spheres and triangles", kBoxes, COUNT(kBoxes), 4000);
    Test("vecutil", kVecutil, COUNT(kVecutil), 6000);
    Test("the geometry", kGeometry, COUNT(kGeometry), 6000);
    Test("the broad phase", kBroad, COUNT(kBroad), 4000);
    Test("the narrow phase and the queries", kQueries, COUNT(kQueries), 3000);
    Test("the rigid bodies", kBodies, COUNT(kBodies), 6000);
    ShadowHook_RemoveAll();

    NfWarnMuted--;
    memcpy(&GameState, gameState, sizeof(gameState));
    memcpy(&MPSettings, mpSettings, sizeof(mpSettings));
    XbeOursWrite(CollideGlobals.at, globals, sizeof(globals));
    memcpy(ForcedListInfo, forced, sizeof(forced));
    XbeOursWrite(FrameRates.at, frameRates, sizeof(frameRates));
    XbeOursWrite(RandWords.at, rand, sizeof(rand));
}
