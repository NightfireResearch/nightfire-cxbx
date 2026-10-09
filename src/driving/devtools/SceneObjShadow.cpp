#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "SceneObjShadow.h"
#include "FpControl.h"

#include "../eagl/View.h"                 // EAGL::ViewPort
#include "../engine/UMemory.hpp"          // OperatorNew, OperatorDelete
#include "../render/RSceneObj.hpp"
#include "../render/RenderHigh.h"
#include "../render/Renderer.h"
#include "../render/WorldCulling.h"
#include "../world/Render.h"              // CachedDrawInfo
#include "../world/Tree.h"
#include "../world/Trigger.h"             // gEventDynamicData
#include "../world/World.h"
#include "../../common/xbeOriginal.h"
#include "../../helpers.h"

#include <windows.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// NIGHTFIRE_SCENEOBJSHADOW=1, from the first simulation tick on the open track: render/WorldCulling.cpp,
// render/RSceneObj.cpp and RRenderHigh::RearrangeSplitScreens against the originals (0x0001aa70-0x0001ab20,
// 0x0001b180-0x0001b1b0, 0x0008bb10 and 0x0008d180-0x00090b90 swapped back in for each original run,
// common/xbeOriginal.h), on identical inputs, compared byte for byte. Whatever a call writes is put back between
// the two runs.
//
//   - RRenderWorldCulling on objects of our own (copies of the live one): Setup2dFrustrum, InitializePlaneInfo and
//     the constructor for random positions, frames, fields of view and ranges (repeated ones too, which skip the
//     plane set-up; NaN among them), every byte compared; then IsInFrustum2d and QuadtreeFrustrumCheck2d for
//     random circles and squares around each frustum's eye, centre and edges, with and without the far test.
//   - Every scene object of the live tree: the size and skeleton queries (every bone's name and others), the view
//     draw lists, GetVelocity, GetViewDistance, the system ids of every instance and past the last,
//     ConvertInstanceToLocal for the model's instances and others, LookupFXHandle for every locator; for the
//     objects of the two base classes (their virtual calls do nothing) also GetInstancePosition and the two
//     transforms with random inputs, and LocateFX. The results gathered in one record, compared.
//   - The culling walk (PrepareSceneObjsForCulling, GetNextSceneObjCullInfo to the end, SetSceneObjectCull keeping
//     or culling by a rule of our own) on WRender's visible list and on lists of random live nodes with repeats;
//     the outputs and the draw lists' statics (0x001f1cf0-0x001f2594) compared.
//   - On copies of base-class objects: GetCollisionInfo and AddCollisionInfo with no sums, this tick's (and none
//     counted) and a stale one; SetEventDynamicData (the event record compared and put back); SetNowVisible up to
//     the lists' limit; SetMapNode in random sequences over nodes of our own.
//   - CARP::Instance::SizeY on every world instance and random packings, the renderer's random sequence (the seed
//     put back), the CARP file tree's Min for every node, the handle and CARP file getters.
//   - RearrangeSplitScreens for one to four views (and none), letterboxed and not, on views of our own copied
//     from the live one, with viewports of our own; AspectScale put back.
//
// Construction, destruction, loading, UpdatePosition, the effects (they reach the gallery's effects) and the
// drawing are tested by lockstep runs.
//
// One mutation this catches: RearrangeSplitScreens with its row and column tables swapped gives the two-view
// case side-by-side extents instead of stacked ones; every two-view case differs.
// ---------------------------------------------------------------------------------------------------------------

namespace {

void OriginalWindow(bool original) {
    XbeOriginal_RestoreRange(0x0001aa70, 0x0001ab20, original);
    XbeOriginal_RestoreRange(0x0001b180, 0x0001b1b0, original);
    XbeOriginal_RestoreRange(0x0008bb10, 0x0008bb20, original);
    XbeOriginal_RestoreRange(0x0008d180, 0x00090b90, original);
}

// ---- the originals (thiscall through __fastcall with a dummy EDX)

typedef RRenderWorldCulling RWC;
#define Orig_Setup2dFrustrum ((void (__fastcall *)(RWC *, int, const Coord4 *, const MATRIX4 *, float, float))0x0008d440)
#define Orig_InitializePlaneInfo ((void (__fastcall *)(RWC *, int, float, float))0x0008d370)
#define Orig_CullingConstruct ((RWC *(__fastcall *)(RWC *, int, float, float))0x0008d420)
#define Orig_IsInFrustum2d ((bool (__fastcall *)(RWC *, int, const Coord4 *, float, bool, float, float *))0x0008d180)
#define Orig_QuadtreeFrustrumCheck2d ((int (__fastcall *)(RWC *, int, const Coord4 *, float))0x0008d250)
#define Orig_GetTransform ((void (__fastcall *)(RSceneObj *, int, MATRIX4 *))0x0008dc60)
#define Orig_GetPosition ((void (__fastcall *)(RSceneObj *, int, Coord3 *))0x0008dc90)
#define Orig_GetRenderOffset ((float (__fastcall *)(RSceneObj *, int))0x0008dce0)
#define Orig_GetBoundingDimensions ((void (__fastcall *)(RSceneObj *, int, Coord4 *))0x0008dfd0)
#define Orig_ComputeBoundingRadius ((float (__fastcall *)(RSceneObj *, int))0x0008e010)
#define Orig_GetBoundingRadius ((float (__fastcall *)(RSceneObj *, int))0x0008f500)
#define Orig_GetCollisionGeometry ((void *(__fastcall *)(RSceneObj *, int, uint32_t *))0x0008e060)
#define Orig_GetNumBones ((uint32_t (__fastcall *)(RSceneObj *, int))0x0008e080)
#define Orig_GetBoneIndex ((int (__fastcall *)(RSceneObj *, int, const char *))0x0008e0c0)
#define Orig_GetViewDrawList ((void *(__fastcall *)(RSceneObj *, int, uint32_t, uint32_t *))0x0008db50)
#define Orig_GetVelocity ((Coord3 *(__fastcall *)(RSceneObj *, int))0x0008e1a0)
#define Orig_GetViewDistance ((double (__fastcall *)(RSceneObj *, int))0x0008ed50)
#define Orig_GetInstanceSystemID ((uint32_t (__fastcall *)(RSceneObj *, int, uint32_t))0x0008dcf0)
#define Orig_ConvertInstanceToLocal ((CARP::Instance *(__fastcall *)(RSceneObj *, int, CARP::Instance *))0x0008edd0)
#define Orig_LookupFXHandle ((ArticleEffect *(__fastcall *)(RSceneObj *, int, const char *, int))0x0008ec20)
#define Orig_GetInstancePosition ((uint32_t (__fastcall *)(RSceneObj *, int, uint32_t, MATRIX4 *, bool))0x0008dd20)
#define Orig_TransformPoint ((uint32_t (__fastcall *)(RSceneObj *, int, uint32_t, Coord4 *, bool))0x0008ddf0)
#define Orig_TransformMatrix ((uint32_t (__fastcall *)(RSceneObj *, int, uint32_t, MATRIX4 *, bool))0x0008dee0)
#define Orig_LocateFX ((void (__fastcall *)(RSceneObj *, int, ArticleEffect *, MATRIX4 *, bool))0x0008eca0)
#define Orig_PrepareSceneObjsForCulling ((void (*)(CachedDrawInfo *))0x0008d830)
#define Orig_GetNextSceneObjCullInfo ((int (*)(Coord4 *, float *, bool *, float *))0x0008d860)
#define Orig_SetSceneObjectCull ((void (*)(int, bool, float))0x0008f000)
#define Orig_SetNowVisible ((void (__fastcall *)(RSceneObj *, int))0x0008dbc0)
#define Orig_GetCollisionInfo ((void (__fastcall *)(RSceneObj *, int, Coord4 *, Coord4 *))0x0008e2d0)
#define Orig_AddCollisionInfo ((void (__fastcall *)(RSceneObj *, int, const Coord3 *, const Coord3 *))0x0008e1f0)
#define Orig_SetEventDynamicData ((void (__fastcall *)(RSceneObj *, int))0x0008dae0)
#define Orig_SetMapNode ((void (__fastcall *)(RSceneObj *, int, WMapNode *))0x0008dc00)
#define Orig_SizeY ((double (__fastcall *)(const CARP::Instance *, int))0x0008d640)
#define Orig_RandomShort ((uint32_t (*)(void))0x0001aab0)
#define Orig_RandomScaled ((double (*)(float))0x0001aae0)
#define Orig_TreeMin ((RefCounterNode *(*)(RefCounterNode *))0x0008eea0)
#define Orig_GetInstanceCount ((uint32_t (__fastcall *)(Handle *, int))0x0001aa90)
#define Orig_GetInstances ((CARP::Instance *(__fastcall *)(Handle *, int))0x0001aaa0)
#define Orig_SetEffectOn ((void (__fastcall *)(Handle *, int, uint32_t))0x0008d610)
#define Orig_GetRoot ((UGroup *(__fastcall *)(RCARPFile *, int))0x0001aa80)
#define Orig_RearrangeSplitScreens ((void (__fastcall *)(RRenderHigh *, int))0x0008bb10)

// ---- the game's state
#define GameTick U32_AT(0x001f2a4c)
#define VisibleList (*(CachedDrawInfo *)0x0023b3c0)
#define SceneObjStatics ((uint8_t *)0x001f1cf0)
const uint32_t kSceneObjStaticsBytes = 0x001f2594 - 0x001f1cf0;
#define RandomSeed U32_AT(0x001c45c4)
#define AspectScale U32_AT(0x001c47a0)
#define RendererUnknown48 (*(int32_t *)(*(uint8_t **)0x001ebff4 + 0x48))   // RRenderer::unknown48
#define CarpFileRefsHead (*(RefCounterNode **)0x001f25f8)
#define CarpFileRefsGuard U32_AT(0x001f2600)

const uint32_t kSceneObjVtable = 0x00191be0;
const uint32_t kAutonomousObjVtable = 0x0018a398;

// ---- results

int g_cases = 0, g_checks = 0, g_differ = 0, g_details = 0, g_faults = 0;
unsigned int g_x87 = 0, g_sse = 0;

void Differ(const char *what, int index, const char *detail) {
    g_differ++;
    if (g_details++ < 10)
        printf("[sceneobj]   %s #%d: %s\n", what, index, detail);
}

void CheckBytes(const char *what, int index, const void *a, const void *b, size_t bytes) {
    g_checks++;
    if (memcmp(a, b, bytes) == 0)
        return;
    const uint8_t *x = static_cast<const uint8_t *>(a), *y = static_cast<const uint8_t *>(b);
    size_t at = 0;
    while (x[at] == y[at])
        at++;
    char detail[96];
    snprintf(detail, sizeof(detail), "byte %u of %u: original %02x, port %02x", unsigned(at), unsigned(bytes), x[at],
             y[at]);
    Differ(what, index, detail);
}

void ResetFpu() {
    _fpreset();
    FpControlSetX87(g_x87);
    FpControlSetSse(g_sse);
}

typedef void (*CaseFn)(void *context, bool original);

// Runs one side; a fault is counted, not fatal. The original runs inside the window.
bool Guarded(CaseFn run, void *context, bool original) {
    if (original)
        OriginalWindow(true);
    bool ok = true;
#ifdef _MSC_VER
    __try {
        run(context, original);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ResetFpu();
        g_faults++;
        ok = false;
    }
#else
    run(context, original);
#endif
    if (original)
        OriginalWindow(false);
    return ok;
}

// ---- random inputs (a generator of our own: the game's is not touched)

uint32_t g_seed = 0x5ce2e0b1;

uint32_t Next() {
    g_seed = g_seed * 1664525u + 1013904223u;
    return g_seed >> 8;
}

float Uniform(float lo, float hi) {
    return lo + (hi - lo) * float(Next() & 0xffff) / 65535.0f;
}

uint32_t Pick(uint32_t n) {
    return n != 0 ? Next() % n : 0;
}

void RandomFrame(MATRIX4 *frame) {
    float yaw = Uniform(-3.2f, 3.2f), pitch = Uniform(-1.4f, 1.4f), roll = Uniform(-0.3f, 0.3f);
    memset(frame, 0, sizeof(*frame));
    frame->mtx[0][0] = cosf(yaw) * cosf(roll);
    frame->mtx[0][1] = sinf(roll);
    frame->mtx[0][2] = -sinf(yaw);
    frame->mtx[1][1] = cosf(pitch);
    frame->mtx[2][0] = sinf(yaw) * cosf(pitch);
    frame->mtx[2][1] = sinf(pitch);
    frame->mtx[2][2] = cosf(yaw) * cosf(pitch);
    frame->mtx[3][0] = Uniform(-500.0f, 500.0f);
    frame->mtx[3][1] = Uniform(-20.0f, 60.0f);
    frame->mtx[3][2] = Uniform(-500.0f, 500.0f);
    frame->mtx[3][3] = 1.0f;
}

// =============================================================================================================
// RRenderWorldCulling

struct CullSetup {
    RWC object;
    int mode;                   // 0 Setup2dFrustrum, 1 InitializePlaneInfo, 2 the constructor
    Coord4 position;
    MATRIX4 frame;
    float fov, range;
};

CullSetup g_cullSetup[2];

void RunCullSetup(void *context, bool original) {
    CullSetup *c = static_cast<CullSetup *>(context);
    if (c->mode == 0) {
        if (original)
            Orig_Setup2dFrustrum(&c->object, 0, &c->position, &c->frame, c->fov, c->range);
        else
            c->object.Setup2dFrustrum(&c->position, &c->frame, c->fov, c->range);
    } else if (c->mode == 1) {
        if (original)
            Orig_InitializePlaneInfo(&c->object, 0, c->fov, c->range);
        else
            c->object.InitializePlaneInfo(c->fov, c->range);
    } else {
        if (original)
            Orig_CullingConstruct(&c->object, 0, c->fov, c->range);
        else
            c->object.Construct(c->fov, c->range);
    }
}

struct CullQuery {
    RWC *object;
    Coord4 sphere;
    float radius;
    bool checkFar;
    float farScale;
    bool wantDistance;
    struct {
        uint32_t inFrustum;
        float distance;
        int32_t containment;
    } out;
};

CullQuery g_cullQuery[2];

void RunCullQuery(void *context, bool original) {
    CullQuery *c = static_cast<CullQuery *>(context);
    c->out.distance = -12345.0f;
    float *distance = c->wantDistance ? &c->out.distance : NULL;
    bool in = original ? Orig_IsInFrustum2d(c->object, 0, &c->sphere, c->radius, c->checkFar, c->farScale, distance)
                       : c->object->IsInFrustum2d(&c->sphere, c->radius, c->checkFar, c->farScale, distance);
    c->out.inFrustum = in ? 1 : 0;
    c->out.containment = original ? Orig_QuadtreeFrustrumCheck2d(c->object, 0, &c->sphere, c->radius)
                                  : c->object->QuadtreeFrustrumCheck2d(&c->sphere, c->radius);
}

void TestCullQueries(RWC *object, int index) {
    const float kFarScales[4] = { 0.3f, 1.0f, 2.0f, 0.0f };
    for (int q = 0; q < 60; q++) {
        CullQuery query;
        memset(&query, 0, sizeof(query));
        query.object = object;
        const Coord4 *around = q % 3 == 0 ? &object->eye : reinterpret_cast<const Coord4 *>(&object->centre);
        float spread = q % 2 == 0 ? object->range * 1.5f : 30.0f;
        query.sphere.x = around->x + Uniform(-spread, spread);
        query.sphere.y = around->y + Uniform(-40.0f, 40.0f);
        query.sphere.z = around->z + Uniform(-spread, spread);
        query.sphere.w = Uniform(0.0f, 50.0f);
        query.radius = q % 7 == 0 ? 0.0f : Uniform(0.0f, q % 5 == 0 ? 200.0f : 25.0f);
        query.checkFar = (Next() & 1) != 0;
        query.farScale = kFarScales[Pick(4)];
        query.wantDistance = (Next() & 3) != 0;
        g_cullQuery[0] = query;
        g_cullQuery[1] = query;
        g_cases++;
        bool ok = Guarded(RunCullQuery, &g_cullQuery[0], true);
        ok = Guarded(RunCullQuery, &g_cullQuery[1], false) && ok;
        if (ok)
            CheckBytes("IsInFrustum2d/QuadtreeFrustrumCheck2d", index * 100 + q, &g_cullQuery[0].out,
                       &g_cullQuery[1].out, sizeof(g_cullQuery[0].out));
    }
}

void TestCulling() {
    const RWC &live = fgWorldCulling;
    Coord4 eye = live.eye;
    float fov = live.fieldOfView, range = live.range;
    for (int i = 0; i < 160; i++) {
        CullSetup setup;
        memset(&setup, 0, sizeof(setup));
        setup.object = live;
        setup.mode = i % 10 == 9 ? 1 : i % 10 == 8 ? 2 : 0;
        RandomFrame(&setup.frame);
        setup.position.x = (i % 2 == 0 ? eye.x : setup.frame.mtx[3][0]) + Uniform(-50.0f, 50.0f);
        setup.position.y = eye.y + Uniform(-10.0f, 30.0f);
        setup.position.z = (i % 2 == 0 ? eye.z : setup.frame.mtx[3][2]) + Uniform(-50.0f, 50.0f);
        setup.position.w = 1.0f;
        setup.fov = i % 4 == 0 ? fov : Uniform(10.0f, 110.0f);
        setup.range = i % 4 == 0 ? range : Uniform(20.0f, 900.0f);
        if (i == 37)
            setup.fov = NAN;
        if (i == 41)
            setup.range = 0.0f;
        g_cullSetup[0] = setup;
        g_cullSetup[1] = setup;
        g_cases++;
        bool ok = Guarded(RunCullSetup, &g_cullSetup[0], true);
        ok = Guarded(RunCullSetup, &g_cullSetup[1], false) && ok;
        if (!ok)
            continue;
        CheckBytes("RRenderWorldCulling set-up", i, &g_cullSetup[0].object, &g_cullSetup[1].object, sizeof(RWC));
        if (setup.mode == 0 && i != 37 && i != 41)
            TestCullQueries(&g_cullSetup[1].object, i);
    }
    static RWC liveCopy;
    liveCopy = live;
    TestCullQueries(&liveCopy, 999);
}

// =============================================================================================================
// The scene objects

const int kMaxObjects = 600;
RSceneObj *g_objects[kMaxObjects];
int g_objectCount = 0;
WMapNode *g_nodes[kMaxVisibleNodes];
int g_nodeCount = 0;

void CollectObjects(WMapNode *node, int depth) {
    if (node == NULL || depth > 16)
        return;
    if (g_nodeCount < kMaxVisibleNodes)
        g_nodes[g_nodeCount++] = node;
    for (RSceneObj *object = node->firstSceneObj; object != NULL && g_objectCount < kMaxObjects; object = object->next)
        g_objects[g_objectCount++] = object;
    if (node->children != NULL) {
        for (int i = 0; i < 4; i++)
            CollectObjects(&node->children[i], depth + 1);
    }
}

bool IsBaseClass(const RSceneObj *object) {
    uint32_t vtable = uint32_t(uintptr_t(object->vtable));
    return vtable == kSceneObjVtable || vtable == kAutonomousObjVtable;
}

// ---- the read-only queries, gathered in one record

const int kMaxBones = 24;
const int kMaxInstances = 12;
const int kMaxEffects = 16;

struct QueryRecord {
    MATRIX4 transform;
    Coord3 position;
    float renderOffset;
    Coord4 dimensions;
    float computedRadius, radius;
    uint32_t geometryCount;
    uintptr_t geometry;
    uint32_t bones;
    int32_t boneIndex[kMaxBones + 2];
    uintptr_t drawList[3];
    uint32_t drawCount[3];
    uintptr_t velocity;
    double viewDistance;
    uint32_t systemId[kMaxInstances + 2];
    uintptr_t local[kMaxInstances + 4];
    uintptr_t fx[kMaxEffects * 2];
    uint32_t placedId[kMaxInstances + 1][2];
    MATRIX4 placed[kMaxInstances + 1][2];
    uint32_t pointId[kMaxInstances + 1];
    Coord4 point[kMaxInstances + 1];
    uint32_t matrixId[kMaxInstances + 1];
    MATRIX4 matrix[kMaxInstances + 1];
    MATRIX4 located[kMaxEffects][2];
};

struct QueryCase {
    RSceneObj *object;
    const char *boneNames[kMaxBones + 2];
    int boneCount;
    CARP::Instance *instances[kMaxInstances + 4];
    int instanceCount;
    Coord4 points[kMaxInstances + 1];
    MATRIX4 matrices[kMaxInstances + 1];
    QueryRecord out;
};

QueryCase g_query[2];

void RunQueries(void *context, bool original) {
    QueryCase *c = static_cast<QueryCase *>(context);
    RSceneObj *o = c->object;
    QueryRecord *r = &c->out;
    memset(r, 0xa5, sizeof(*r));
    original ? Orig_GetTransform(o, 0, &r->transform) : o->GetTransform(&r->transform);
    original ? Orig_GetPosition(o, 0, &r->position) : o->GetPosition(&r->position);
    r->renderOffset = original ? Orig_GetRenderOffset(o, 0) : o->GetRenderOffset();
    r->velocity = uintptr_t(original ? Orig_GetVelocity(o, 0) : o->GetVelocity());
    r->viewDistance = original ? Orig_GetViewDistance(o, 0) : o->GetViewDistance();
    if (o->baseDesc != NULL) {
        original ? Orig_GetBoundingDimensions(o, 0, &r->dimensions) : o->GetBoundingDimensions(&r->dimensions);
        r->computedRadius = original ? Orig_ComputeBoundingRadius(o, 0) : o->ComputeBoundingRadius();
        r->radius = original ? Orig_GetBoundingRadius(o, 0) : o->GetBoundingRadius();
        r->geometry = uintptr_t(original ? Orig_GetCollisionGeometry(o, 0, &r->geometryCount)
                                         : o->GetCollisionGeometry(&r->geometryCount));
        r->bones = original ? Orig_GetNumBones(o, 0) : o->GetNumBones();
        for (int i = 0; i < c->boneCount; i++)
            r->boneIndex[i] = original ? Orig_GetBoneIndex(o, 0, c->boneNames[i]) : o->GetBoneIndex(c->boneNames[i]);
        const uint32_t lists[3] = { 0xffffffff, 0, 1 };
        for (int i = 0; i < 3; i++)
            r->drawList[i] = uintptr_t(original ? Orig_GetViewDrawList(o, 0, lists[i], &r->drawCount[i])
                                                : o->GetViewDrawList(lists[i], &r->drawCount[i]));
    }
    if (o->animHandle == NULL)
        return;
    uint32_t count = o->animHandle->instanceCount;
    for (uint32_t i = 0; i < count + 2 && i < kMaxInstances + 2; i++)
        r->systemId[i] = original ? Orig_GetInstanceSystemID(o, 0, i) : o->GetInstanceSystemID(i);
    for (int i = 0; i < c->instanceCount; i++)
        r->local[i] = uintptr_t(original ? Orig_ConvertInstanceToLocal(o, 0, c->instances[i])
                                         : o->ConvertInstanceToLocal(c->instances[i]));
    ArticleEffect *effects = o->animHandle->effects;
    for (uint32_t i = 0; i < o->animHandle->effectCount && i < kMaxEffects; i++) {
        if (effects[i].type != 7)
            continue;
        const char *name = effects[i].locator.name;
        int32_t instance = effects[i].locator.instance;
        r->fx[i * 2] = uintptr_t(original ? Orig_LookupFXHandle(o, 0, name, instance) : o->LookupFXHandle(name, instance));
        r->fx[i * 2 + 1] = uintptr_t(original ? Orig_LookupFXHandle(o, 0, name, instance + 1)
                                              : o->LookupFXHandle(name, instance + 1));
    }
    if (!IsBaseClass(o))
        return;
    for (uint32_t i = 0; i <= count && i <= kMaxInstances; i++) {
        for (int world = 0; world < 2; world++) {
            r->placed[i][world] = c->matrices[i];
            r->placedId[i][world] = original ? Orig_GetInstancePosition(o, 0, i, &r->placed[i][world], world != 0)
                                             : o->GetInstancePosition(i, &r->placed[i][world], world != 0);
        }
        r->point[i] = c->points[i];
        r->pointId[i] = original ? Orig_TransformPoint(o, 0, i, &r->point[i], (i & 1) != 0)
                                 : o->TransformPointByInstancePosition(i, &r->point[i], (i & 1) != 0);
        r->matrix[i] = c->matrices[i];
        r->matrixId[i] = original ? Orig_TransformMatrix(o, 0, i, &r->matrix[i], (i & 1) == 0)
                                  : o->TransformMatrixByInstancePosition(i, &r->matrix[i], (i & 1) == 0);
    }
    for (uint32_t i = 0; i < o->animHandle->effectCount && i < kMaxEffects; i++) {
        if ((effects[i].flags & 0x0100) != 0)   // the height lookup counts collision queries
            continue;
        for (int world = 0; world < 2; world++) {
            memset(&r->located[i][world], 0, sizeof(MATRIX4));
            original ? Orig_LocateFX(o, 0, &effects[i], &r->located[i][world], world != 0)
                     : o->LocateFX(&effects[i], &r->located[i][world], world != 0);
        }
    }
}

// Bone names from the model's skeleton ('Skel'), and two it has not
int BoneNames(RSceneObj *object, const char **names) {
    static const char kNoBone[] = "no such bone";
    static const char kEmpty[] = "";
    int count = 0;
    UGroup *model = object->ModelGroup();
    UData *skeleton = model->DataLocateTag(0x536b656c);
    if (skeleton != model->DataEnd()) {
        const uint8_t *data = skeleton->Data();
        for (uint32_t i = 0; i < skeleton->count && count < kMaxBones; i++)
            names[count++] = reinterpret_cast<const char *>(data + *reinterpret_cast<const uint16_t *>(data + i * 0x20 + 0x1c));
    }
    names[count++] = kNoBone;
    names[count++] = kEmpty;
    return count;
}

void TestQueries() {
    for (int index = 0; index < g_objectCount; index++) {
        RSceneObj *object = g_objects[index];
        QueryCase &c = g_query[0];
        memset(&c, 0, sizeof(c));
        c.object = object;
        if (object->baseDesc != NULL)
            c.boneCount = BoneNames(object, c.boneNames);
        if (object->animHandle != NULL) {
            CARP::Instance *copies = object->animHandle->Instances();
            uint32_t count = object->animHandle->instanceCount;
            for (uint32_t i = 0; i < count && c.instanceCount < kMaxInstances; i++)
                c.instances[c.instanceCount++] = &copies[i];
            // instances of the model's own lists, where the article leads to them
            if (count > 0 && ArticleOf(&copies[0]) != NULL && ArticleOf(&copies[0])->model != NULL &&
                ArticleOf(&copies[0])->model->group != NULL) {
                UGroup *model = ArticleOf(&copies[0])->model->group;
                UData *list = model->DataLocateFirst(0x696e2020, 0, 0xffffffff);
                if (list != model->DataEnd() && list->count > 0) {
                    CARP::Instance *first = reinterpret_cast<CARP::Instance *>(list->Data());
                    c.instances[c.instanceCount++] = &first[0];
                    c.instances[c.instanceCount++] = &first[list->count - 1];
                }
            }
            c.instances[c.instanceCount++] = static_cast<CARP::Instance *>(object->sourceInstance);
        }
        for (int i = 0; i <= kMaxInstances; i++) {
            RandomFrame(&c.matrices[i]);
            c.points[i].x = Uniform(-20.0f, 20.0f);
            c.points[i].y = Uniform(-20.0f, 20.0f);
            c.points[i].z = Uniform(-20.0f, 20.0f);
            c.points[i].w = i % 3 == 0 ? 0.0f : 1.0f;
        }
        g_query[1] = c;
        g_cases++;
        bool ok = Guarded(RunQueries, &g_query[0], true);
        ok = Guarded(RunQueries, &g_query[1], false) && ok;
        if (ok)
            CheckBytes("scene object queries", index, &g_query[0].out, &g_query[1].out, sizeof(QueryRecord));
    }
}

// ---- the culling walk

const int kMaxWalk = 2048;

struct WalkRecord {
    int item;
    Coord4 sphere;
    float height;
    uint32_t checkFar;
    float farScale;
};

struct WalkCase {
    CachedDrawInfo *list;
    WalkRecord records[kMaxWalk];
    int count;
    uint8_t statics[kSceneObjStaticsBytes];
};

WalkCase g_walk[2];
uint8_t g_staticsBefore[kSceneObjStaticsBytes];
CachedDrawInfo g_walkList;

void RunWalk(void *context, bool original) {
    WalkCase *c = static_cast<WalkCase *>(context);
    memset(c->records, 0xaa, sizeof(c->records));
    c->count = 0;
    original ? Orig_PrepareSceneObjsForCulling(c->list) : RSceneObj::PrepareSceneObjsForCulling(c->list);
    for (int step = 0; step < kMaxWalk; step++) {
        WalkRecord &r = c->records[step];
        bool checkFar = false;
        r.item = original ? Orig_GetNextSceneObjCullInfo(&r.sphere, &r.height, &checkFar, &r.farScale)
                          : RSceneObj::GetNextSceneObjCullInfo(&r.sphere, &r.height, &checkFar, &r.farScale);
        r.checkFar = checkFar;
        c->count = step + 1;
        if (r.item == -1)
            break;
        // kept only within the first 0x1f0 steps: the draw-last list is bounded only by the normal list's count
        bool culled = step >= 0x1f0 || (uint32_t(r.item) >> 4) % 3 == uint32_t(step) % 2;
        original ? Orig_SetSceneObjectCull(r.item, culled, float(step)) : RSceneObj::SetSceneObjectCull(r.item, culled, float(step));
    }
}

void Walk(CachedDrawInfo *list, int index) {
    memcpy(g_staticsBefore, SceneObjStatics, kSceneObjStaticsBytes);
    g_walk[0].list = list;
    g_walk[1].list = list;
    g_cases++;
    bool ok = Guarded(RunWalk, &g_walk[0], true);
    memcpy(g_walk[0].statics, SceneObjStatics, kSceneObjStaticsBytes);
    memcpy(SceneObjStatics, g_staticsBefore, kSceneObjStaticsBytes);
    ok = Guarded(RunWalk, &g_walk[1], false) && ok;
    memcpy(g_walk[1].statics, SceneObjStatics, kSceneObjStaticsBytes);
    memcpy(SceneObjStatics, g_staticsBefore, kSceneObjStaticsBytes);
    if (!ok)
        return;
    CheckBytes("culling walk length", index, &g_walk[0].count, &g_walk[1].count, sizeof(int));
    if (g_walk[0].count == g_walk[1].count)
        CheckBytes("culling walk", index, g_walk[0].records, g_walk[1].records, g_walk[0].count * sizeof(WalkRecord));
    CheckBytes("culling walk statics", index, g_walk[0].statics, g_walk[1].statics, kSceneObjStaticsBytes);
}

void TestWalks() {
    Walk(&VisibleList, 0);
    for (int i = 1; i < 12; i++) {
        memset(&g_walkList, 0, sizeof(g_walkList));
        int count = i == 1 ? 0 : int(Pick(uint32_t(g_nodeCount < 200 ? g_nodeCount : 200))) + 1;
        for (int n = 0; n < count && g_walkList.nodeCount < kMaxVisibleNodes; n++)
            g_walkList.nodes[g_walkList.nodeCount++] = g_nodes[Pick(uint32_t(g_nodeCount))];
        Walk(&g_walkList, i);
    }
}

// ---- writes, on copies of base-class objects

struct ObjectCopy {
    uint8_t bytes[sizeof(RAutonomousObj)];
    RSceneObj *Object() { return reinterpret_cast<RSceneObj *>(bytes); }
};

ObjectCopy g_copy;                      // one copy for both sides, put back between them

struct CollisionCase {
    int mode;                           // 0 GetCollisionInfo, 1 AddCollisionInfo
    int sums;                           // 0 none, 1 this tick's, 2 this tick's with none counted, 3 stale
    SceneObjCollisionInfo info;
    Coord3 centre, extents;
    struct {
        Coord4 centre, extents;
        SceneObjCollisionInfo info;     // the sums after
        uint8_t object[sizeof(RAutonomousObj)];
    } out;
};

CollisionCase g_collision[2];
ObjectCopy g_copyBefore;

void RunCollision(void *context, bool original) {
    CollisionCase *c = static_cast<CollisionCase *>(context);
    RSceneObj *o = g_copy.Object();
    memset(&c->out, 0xa5, sizeof(c->out));
    o->collisionInfo = NULL;
    if (c->sums != 0) {
        o->collisionInfo = static_cast<SceneObjCollisionInfo *>(OperatorNew(sizeof(SceneObjCollisionInfo)));
        *o->collisionInfo = c->info;
    }
    if (c->mode == 0)
        original ? Orig_GetCollisionInfo(o, 0, &c->out.centre, &c->out.extents) : o->GetCollisionInfo(&c->out.centre, &c->out.extents);
    else
        original ? Orig_AddCollisionInfo(o, 0, &c->centre, &c->extents) : o->AddCollisionInfo(&c->centre, &c->extents);
    if (o->collisionInfo != NULL) {
        c->out.info = *o->collisionInfo;
        OperatorDelete(o->collisionInfo);
        o->collisionInfo = reinterpret_cast<SceneObjCollisionInfo *>(uintptr_t(1));   // allocated: compared as such
    }
    memcpy(c->out.object, g_copy.bytes, sizeof(c->out.object));
}

struct EventCase {
    PhysicsObject *physics;
    struct {
        EventDynamicData data;
        uint8_t object[sizeof(RAutonomousObj)];
    } out;
};

EventCase g_event[2];

void RunEvent(void *context, bool original) {
    EventCase *c = static_cast<EventCase *>(context);
    RSceneObj *o = g_copy.Object();
    o->physics = c->physics;
    original ? Orig_SetEventDynamicData(o, 0) : o->SetEventDynamicData();
    c->out.data = gEventDynamicData;
    memcpy(c->out.object, g_copy.bytes, sizeof(c->out.object));
}

struct VisibleCase {
    int calls;
    uint8_t statics[kSceneObjStaticsBytes];
};

VisibleCase g_visible[2];

void RunVisible(void *context, bool original) {
    VisibleCase *c = static_cast<VisibleCase *>(context);
    original ? Orig_PrepareSceneObjsForCulling(&VisibleList) : RSceneObj::PrepareSceneObjsForCulling(&VisibleList);
    for (int i = 0; i < c->calls; i++) {
        RSceneObj *o = g_objects[(i * 7) % g_objectCount];
        original ? Orig_SetNowVisible(o, 0) : o->SetNowVisible();
    }
    memcpy(c->statics, SceneObjStatics, kSceneObjStaticsBytes);
}

// SetMapNode over nodes and objects of our own
struct MapArena {
    WMapNode nodes[4];
    RSceneObj objects[6];
};

struct MapCase {
    int steps;
    uint8_t object[48], node[48];
    MapArena out;
};

MapArena g_arena;
MapCase g_map[2];

void RunMap(void *context, bool original) {
    MapCase *c = static_cast<MapCase *>(context);
    for (int i = 0; i < c->steps; i++) {
        RSceneObj *o = &g_arena.objects[c->object[i]];
        WMapNode *n = &g_arena.nodes[c->node[i]];
        original ? Orig_SetMapNode(o, 0, n) : o->SetMapNode(n);
    }
    c->out = g_arena;
}

void TestWrites() {
    int base = 0;
    for (int index = 0; index < g_objectCount; index++) {
        RSceneObj *object = g_objects[index];
        if (!IsBaseClass(object))
            continue;
        if (base++ >= 40)
            break;
        size_t bytes = uint32_t(uintptr_t(object->vtable)) == kAutonomousObjVtable ? sizeof(RAutonomousObj) : sizeof(RSceneObj);
        memset(g_copyBefore.bytes, 0, sizeof(g_copyBefore.bytes));
        memcpy(g_copyBefore.bytes, object, bytes);
        g_copyBefore.Object()->collisionInfo = NULL;

        for (int mode = 0; mode < 2; mode++) {
            for (int sums = 0; sums < 4; sums++) {
                CollisionCase c;
                memset(&c, 0, sizeof(c));
                c.mode = mode;
                c.sums = sums;
                c.info.tick = sums == 3 ? GameTick - 1 - Pick(5) : GameTick;
                c.info.count = sums == 2 ? 0 : 1 + Pick(6);
                c.info.centreSum.x = Uniform(-900.0f, 900.0f);
                c.info.centreSum.y = Uniform(-90.0f, 90.0f);
                c.info.centreSum.z = Uniform(-900.0f, 900.0f);
                c.info.extentsSum.x = Uniform(-5.0f, 5.0f);
                c.info.extentsSum.y = Uniform(-5.0f, 5.0f);
                c.info.extentsSum.z = Uniform(-5.0f, 5.0f);
                c.centre.x = Uniform(-300.0f, 300.0f);
                c.centre.y = Uniform(-30.0f, 30.0f);
                c.centre.z = Uniform(-300.0f, 300.0f);
                c.extents.x = Uniform(-3.0f, 3.0f);
                c.extents.y = Uniform(-3.0f, 3.0f);
                c.extents.z = Uniform(-3.0f, 3.0f);
                g_collision[0] = c;
                g_collision[1] = c;
                g_cases++;
                g_copy = g_copyBefore;
                bool ok = Guarded(RunCollision, &g_collision[0], true);
                g_copy = g_copyBefore;
                ok = Guarded(RunCollision, &g_collision[1], false) && ok;
                if (ok)
                    CheckBytes(mode == 0 ? "GetCollisionInfo" : "AddCollisionInfo", index * 10 + sums, &g_collision[0].out,
                               &g_collision[1].out, sizeof(g_collision[0].out));
            }
        }

        static EventDynamicData saved;
        saved = gEventDynamicData;
        for (int k = 0; k < 2; k++) {
            RSceneObj *other = g_objects[Pick(uint32_t(g_objectCount))];
            g_event[0].physics = k == 0 ? NULL : other->physics;
            g_event[1].physics = g_event[0].physics;
            g_cases++;
            g_copy = g_copyBefore;
            gEventDynamicData = saved;
            bool ok = Guarded(RunEvent, &g_event[0], true);
            g_copy = g_copyBefore;
            gEventDynamicData = saved;
            ok = Guarded(RunEvent, &g_event[1], false) && ok;
            gEventDynamicData = saved;
            if (ok)
                CheckBytes("SetEventDynamicData", index * 2 + k, &g_event[0].out, &g_event[1].out, sizeof(g_event[0].out));
        }
    }

    if (g_objectCount > 0) {
        const int kCalls[3] = { 5, 300, 0x1f0 };
        for (int k = 0; k < 3; k++) {
            memcpy(g_staticsBefore, SceneObjStatics, kSceneObjStaticsBytes);
            g_visible[0].calls = kCalls[k];
            g_visible[1].calls = kCalls[k];
            g_cases++;
            bool ok = Guarded(RunVisible, &g_visible[0], true);
            memcpy(SceneObjStatics, g_staticsBefore, kSceneObjStaticsBytes);
            ok = Guarded(RunVisible, &g_visible[1], false) && ok;
            memcpy(SceneObjStatics, g_staticsBefore, kSceneObjStaticsBytes);
            if (ok)
                CheckBytes("SetNowVisible", k, g_visible[0].statics, g_visible[1].statics, kSceneObjStaticsBytes);
        }
    }

    for (int k = 0; k < 20; k++) {
        MapArena start;
        memset(&start, 0, sizeof(start));
        for (int i = 0; i < 6; i++)
            start.objects[i].flags = uint8_t(i);
        MapCase c;
        memset(&c, 0, sizeof(c));
        c.steps = 1 + int(Pick(48));
        for (int i = 0; i < c.steps; i++) {
            c.object[i] = uint8_t(Pick(6));
            c.node[i] = uint8_t(Pick(4));
        }
        g_map[0] = c;
        g_map[1] = c;
        g_cases++;
        g_arena = start;
        bool ok = Guarded(RunMap, &g_map[0], true);
        g_arena = start;
        ok = Guarded(RunMap, &g_map[1], false) && ok;
        if (ok)
            CheckBytes("SetMapNode", k, &g_map[0].out, &g_map[1].out, sizeof(MapArena));
    }
}

// ---- the small ones

struct SmallCase {
    CARP::Instance instance;
    uint32_t seed;
    float scales[40];
    Handle handle;
    RCARPFile file;
    uint32_t effectIndex;
    struct {
        double sizes[2];
        uint32_t shorts[40];
        double scaled[40];
        uint32_t seed;
        uint32_t instanceCount;
        uint32_t instancesOffset;       // from the handle
        uint64_t effects;
        uintptr_t root;
    } out;
};

SmallCase g_small[2];

void RunSmall(void *context, bool original) {
    SmallCase *c = static_cast<SmallCase *>(context);
    const CARP::Instance *instance = &c->instance;
    c->out.sizes[0] = original ? Orig_SizeY(&c->instance, 0) : instance->SizeY();
    c->instance.packedDimensions ^= 0x80000000;
    c->out.sizes[1] = original ? Orig_SizeY(&c->instance, 0) : instance->SizeY();
    c->instance.packedDimensions ^= 0x80000000;
    RandomSeed = c->seed;
    for (int i = 0; i < 40; i++) {
        c->out.shorts[i] = original ? Orig_RandomShort() : RandomShort();
        c->out.scaled[i] = original ? Orig_RandomScaled(c->scales[i]) : RandomScaled(c->scales[i]);
    }
    c->out.seed = RandomSeed;
    Handle *handle = &c->handle;
    c->out.instanceCount = original ? Orig_GetInstanceCount(&c->handle, 0) : handle->GetInstanceCount();
    CARP::Instance *instances = original ? Orig_GetInstances(&c->handle, 0) : handle->GetInstances();
    c->out.instancesOffset = uint32_t(reinterpret_cast<uint8_t *>(instances) - reinterpret_cast<uint8_t *>(&c->handle));
    original ? Orig_SetEffectOn(&c->handle, 0, c->effectIndex) : handle->SetEffectOn(c->effectIndex);
    c->out.effects = c->handle.effectsOn;
    c->out.root = uintptr_t(original ? Orig_GetRoot(&c->file, 0) : c->file.GetRoot());
}

void TestSmall() {
    uint32_t savedSeed = RandomSeed;
    uint32_t instances = fgWorld->instanceCount;
    for (uint32_t i = 0; i < instances + 200; i++) {
        SmallCase c;
        memset(&c, 0, sizeof(c));
        if (i < instances)
            c.instance = fgWorld->instances[i];
        else
            c.instance.packedDimensions = Next() ^ (Next() << 16);
        c.seed = i % 3 == 0 ? savedSeed : (Next() & 0xffff);
        for (int k = 0; k < 40; k++)
            c.scales[k] = k % 5 == 0 ? 1.0f : Uniform(-100.0f, 100.0f);
        c.handle.instanceCount = uint8_t(Next());
        c.handle.effectsOn = (uint64_t(Next()) << 40) ^ Next();
        c.effectIndex = Pick(80);
        c.file.root = reinterpret_cast<UGroup *>(uintptr_t(Next()));
        g_small[0] = c;
        g_small[1] = c;
        g_cases++;
        bool ok = Guarded(RunSmall, &g_small[0], true);
        ok = Guarded(RunSmall, &g_small[1], false) && ok;
        RandomSeed = savedSeed;
        if (ok)
            CheckBytes("SizeY, random, getters", int(i), &g_small[0].out, &g_small[1].out, sizeof(g_small[0].out));
    }
}

// RefCounterTree::Min for every node of the live tree
struct MinCase {
    RefCounterNode *node;
    RefCounterNode *out;
};

MinCase g_min[2];

void RunMin(void *context, bool original) {
    MinCase *c = static_cast<MinCase *>(context);
    c->out = original ? Orig_TreeMin(c->node) : RefCounterTree::Min(c->node);
}

void MinOfTree(RefCounterNode *node, int *index) {
    if (node == NULL || node->isNil || *index > 500)
        return;
    g_min[0].node = node;
    g_min[1].node = node;
    g_cases++;
    bool ok = Guarded(RunMin, &g_min[0], true);
    ok = Guarded(RunMin, &g_min[1], false) && ok;
    if (ok)
        CheckBytes("RefCounterTree::Min", (*index)++, &g_min[0].out, &g_min[1].out, sizeof(RefCounterNode *));
    MinOfTree(node->left, index);
    MinOfTree(node->right, index);
}

// ---- RearrangeSplitScreens on views of our own

struct SplitCase {
    uint32_t viewCount;
    int32_t letterbox;
    struct {
        RViewCamera views[RRenderHigh::kMaxViews];
        EAGL::ViewPort viewPorts[RRenderHigh::kMaxViews];
        uint32_t aspectScale;
    } out;
};

SplitCase g_split[2];
RRenderHigh g_high;
RViewCamera g_views[RRenderHigh::kMaxViews];
EAGL::ViewPort g_viewPorts[RRenderHigh::kMaxViews];

void SplitStart(const RViewCamera *live) {
    memset(&g_high, 0, sizeof(g_high));
    for (uint32_t i = 0; i < RRenderHigh::kMaxViews; i++) {
        g_views[i] = *live;
        g_viewPorts[i] = *live->viewPort;
        g_views[i].viewPort = &g_viewPorts[i];
        g_high.views[i].view = &g_views[i];
    }
}

void RunSplit(void *context, bool original) {
    SplitCase *c = static_cast<SplitCase *>(context);
    g_high.viewCount = c->viewCount;
    int32_t savedLetterbox = RendererUnknown48;
    RendererUnknown48 = c->letterbox;
    original ? Orig_RearrangeSplitScreens(&g_high, 0) : g_high.RearrangeSplitScreens();
    RendererUnknown48 = savedLetterbox;
    memcpy(c->out.views, g_views, sizeof(g_views));
    memcpy(c->out.viewPorts, g_viewPorts, sizeof(g_viewPorts));
    c->out.aspectScale = AspectScale;
}

void TestSplitScreens() {
    if (fgRenderHigh == NULL || fgRenderHigh->viewCount == 0 || fgRenderHigh->views[0].view == NULL ||
        fgRenderHigh->views[0].view->viewPort == NULL) {
        printf("[sceneobj] no view - RearrangeSplitScreens skipped\n");
        return;
    }
    const RViewCamera *live = fgRenderHigh->views[0].view;
    uint32_t savedAspect = AspectScale;
    for (uint32_t count = 0; count <= RRenderHigh::kMaxViews; count++) {
        for (int letterbox = 0; letterbox < 2; letterbox++) {
            g_split[0].viewCount = count;
            g_split[0].letterbox = letterbox;
            g_split[1] = g_split[0];
            g_cases++;
            SplitStart(live);
            AspectScale = savedAspect;
            bool ok = Guarded(RunSplit, &g_split[0], true);
            SplitStart(live);
            AspectScale = savedAspect;
            ok = Guarded(RunSplit, &g_split[1], false) && ok;
            AspectScale = savedAspect;
            if (ok)
                CheckBytes("RearrangeSplitScreens", int(count * 2 + letterbox), &g_split[0].out, &g_split[1].out,
                           sizeof(g_split[0].out));
        }
    }
}

} // namespace

void SceneObjShadow_Run(void) {
    char value[16] = "";
    DWORD length = GetEnvironmentVariableA("NIGHTFIRE_SCENEOBJSHADOW", value, sizeof(value));
    if (length == 0 || length >= sizeof(value) || atoi(value) == 0)
        return;
    FpControlGet(&g_x87, &g_sse);
    TestCulling();
    if (fgWorld != NULL && fgWorld->map != NULL) {
        g_objectCount = 0;
        g_nodeCount = 0;
        CollectObjects(fgWorld->map->root, 0);
        // Between frames the renderer has no current view (EndView clears it); GetViewDistance reads its camera, so
        // the first player's view stands in while the queries run.
        RViewCamera *savedView = fgRenderer->currentView;
        if (savedView == NULL && fgRenderHigh != NULL && fgRenderHigh->viewCount != 0)
            fgRenderer->currentView = fgRenderHigh->views[0].view;
        TestQueries();
        fgRenderer->currentView = savedView;
        TestWalks();
        TestWrites();
        TestSmall();
    } else {
        printf("[sceneobj] no world - the scene objects skipped\n");
    }
    if ((CarpFileRefsGuard & 1) != 0 && CarpFileRefsHead != NULL) {
        int index = 0;
        MinOfTree(CarpFileRefsHead->parent, &index);
    }
    TestSplitScreens();
    ResetFpu();
    printf("[sceneobj] culling, scene object queries, walk, writes, split screens vs originals (%d objects): %d cases, "
           "%d checks, %d differ%s\n", g_objectCount, g_cases, g_checks, g_differ, g_faults != 0 ? " (with faults)" : "");
    if (g_faults != 0)
        printf("[sceneobj]   %d calls faulted\n", g_faults);
    fflush(stdout);
}
