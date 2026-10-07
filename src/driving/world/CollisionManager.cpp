#include "CollisionManager.h"

#include "CollisionQueries.h"             // the vectors' uninitialized_fill_n copies
#include "Grid.h"
#include "SoundMap.h"                       // BuyMapHead
#include "WorldMath.h"
#include "World.h"
#include "WorldPos.h"

#include <math.h>
#include <stdio.h>

#include <bit>

#include "../../helpers.h"
#include "../EventManager.hpp"            // Event::operator new
#include "../data/Tree.h"                 // TreeThrow
#include "../engine/CoreFoundation.h"     // NullFunction
#include "../engine/PhysicsUtil.h"
#include "../engine/UGroup.h"
#include "../engine/UMemory.hpp"
#include "../platform/RealMath.h"
#include "../platform/X87.h"

#pragma fp_contract(off)

// ---------------------------------------------------------------------------------------------------------------
// WCollisionMgr's lists, hit checks and lifecycle (0x000c2ff0-0x000c5710), ported from the listing. See
// CollisionManager.h for the data. The STL containers' compiled copies that serve only this file (the window map's
// insert, erase and node helpers, two vectors' push_back) are here too; the helpers the game shares with other
// containers outside the collision system (rotations, stepping an iterator) are called at their addresses.
// ---------------------------------------------------------------------------------------------------------------

// ---- the game's code not ported yet (or ported elsewhere for other node types)
// the pointer vectors' uninitialized_fill_n, shared with the rest of the game
#define ObjectList_Construct ((void (*)(WCollisionObject **where, int count, WCollisionObject *const *value, ObjectList *vector, WCollisionObject *const *same))0x000b2ec0)
#define StripList_Construct ((void (*)(const CollisionStrip **where, int count, const CollisionStrip *const *value, StripList *vector, const CollisionStrip *const *same))0x000b2ec0)
#define WorldCollisionInfo_Construct ((WorldCollisionInfo *(__fastcall *)(WorldCollisionInfo *, int))0x0001d9f0)
#define WWorldPos_FaceNormal ((void (__fastcall *)(const WWorldPos *, int, Coord4 *normal))0x0005d3f0)
#define GameRandom ((uint32_t (*)())0x0001aab0)
#define EHitWindow_Construct ((void (__fastcall *)(void *, int, Coord4 point, Coord4 normal, WindowHit hit, int flag))0x000403f0)
// The tree helpers every map of the game shares (identical code folded by the linker), on this file's node types.
// 0x00053580 and 0x000527e0, 0x00052a20 are the attribute system's ported copies (of its own node types; one warns
// as provisional), called here at their addresses; 0x00118f20 is CARP::ResolverMap::Find's.
#define WindowMap_Buyhead ((WindowMapNode *(*)())0x00053580)
#define WindowMap_Lrotate ((void (__fastcall *)(WindowMap *, int, WindowMapNode *node))0x001238d0)
#define WindowMap_Rrotate ((void (__fastcall *)(WindowMap *, int, WindowMapNode *node))0x000527e0)
#define WindowMap_Increment ((void (__fastcall *)(WindowMapNode **it, int))0x00052a20)
#define WindowMap_Decrement ((void (__fastcall *)(WindowMapNode **it, int))0x00123930)
#define WindowMap_Max ((WindowMapNode *(*)(WindowMapNode *node))0x00123850)
#define ArticleMap_Find ((ArticleMapNode **(__fastcall *)(ArticleMap *, int, ArticleMapNode **result, WCollisionInstance *const *key))0x00118f20)
#define ArticleMap_Increment ((void (__fastcall *)(ArticleMapNode **it, int))0x000b2920)

namespace {

// The warning beside a provisional port: a path no shipped data reaches (the containers' length and iterator
// errors). It says so once, the first time it runs.
void CollisionUntested(const char *what) {
    printf("[world] WARNING: %s ran - a provisional port that no shipped data reaches, UNTESTED. Check it against "
           "the original.\n", what);
    fflush(stdout);
}

#define COLLISION_UNTESTED(what) \
    do { \
        static bool warned_; \
        if (!warned_) { \
            warned_ = true; \
            CollisionUntested(what); \
        } \
    } while (0)

// The track's CARP tags
constexpr uint32_t kCollisionGroupTag = 0x43446174;   // 'CDat'
constexpr uint32_t kInstancesTag = 0x63690000;        // 'ci', index 0
constexpr uint32_t kObjectsTag = 0x636f0000;          // 'co', index 0
constexpr uint32_t kArticleTag = 0x63610000;          // 'ca' and the article's index
constexpr uint32_t kArticleType = 0x63612020;         // 'ca  ', any index

constexpr int kInstanceVariantShift = 2;             // WCollisionInstance::flags >> 2: its window variant

constexpr uint32_t kBarrierMask = 0xf0;
constexpr uint32_t kBadWindowGroupSize = 0x8000;
constexpr uint8_t kBrokenWindowFace = 0x70;           // WorldCollisionInfo::faceType once a window broke
constexpr size_t kHitWindowEventSize = 0x54;          // sizeof(EHitWindow)

constexpr float kStepLengthScale = -1.0f / 48.0f;     // StepCheckHitWorld: one step per 48 units
static_assert(std::bit_cast<uint32_t>(kStepLengthScale) == 0xbcaaaaab, "the original's -1/48");
constexpr float kOneThird = 1.0f / 3.0f;
static_assert(std::bit_cast<uint32_t>(kOneThird) == 0x3eaaaaab, "the original's 1/3");
constexpr float kShortSegment = 0.1f;                 // CheckHitWorld stretches a shorter segment to 0.25
static_assert(std::bit_cast<uint32_t>(kShortSegment) == 0x3dcccccd, "the original's 0.1");
constexpr float kPaneOffset = 0.05f;                  // a broken pane's corners, off the glass along its normal
static_assert(std::bit_cast<uint32_t>(kPaneOffset) == 0x3d4ccccd, "the original's 0.05");
constexpr double kPaneAlongScale = 1.0 / 8192;        // WindowPane quantisation
constexpr double kPaneHeightScale = 1.0 / 256;
constexpr double kStripRadiusScale = 1.0 / 16;        // strip and vertex radii
constexpr double kHighPane = 10.0;                    // panes higher up break with the other kinds

const Coord3 kZero = {0.0f, 0.0f, 0.0f};              // the game's zero vector (0x00243030)

// A vector's storage goes back to the fast allocator (std::vector's destructor, inlined).
template <class T>
void DestroyVector(GameVector<T> *vector) {
    if (vector->first != NULL)
        UMemory::FastFree(vector->first, unsigned((vector->end - vector->first) * sizeof(T)));
}

// The window map's ++iterator, inlined in its erase(first, last); end() stays where it is.
WindowMapNode *WindowMapNext(WindowMapNode *node) {
    if (node->isNil)
        return node;
    if (!node->right->isNil) {
        node = node->right;
        while (!node->left->isNil)
            node = node->left;
        return node;
    }
    WindowMapNode *parent = node->parent;
    while (!parent->isNil && node == parent->right) {
        node = parent;
        parent = parent->parent;
    }
    return parent;
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// The vectors' push_back (the 8-byte and 40-byte ones compiled once each; the others inlined)
// ---------------------------------------------------------------------------------------------------------------

// FUNC_AT(0x000c3180)
void InstanceList::PushBack(const InstanceListEntry *entry) {
    size_t size = first == NULL ? 0 : last - first;
    if (first != NULL && size < size_t(end - first)) {
        InstanceUninitializedFill(last, 1, entry);
        last++;
    } else {
        InsertN(last, 1, entry);
    }
}

// FUNC_AT(0x000c30f0)
void BarrierList::PushBack(const BarrierListEntry *entry) {
    size_t size = first == NULL ? 0 : last - first;
    if (first != NULL && size < size_t(end - first)) {
        BarrierUninitializedFill(last, 1, entry);
        last++;
    } else {
        BarrierListEntry *where;
        Insert(&where, last, entry);
    }
}

void ObjectList::PushBack(WCollisionObject *const *object) {
    size_t size = first == NULL ? 0 : last - first;
    if (first != NULL && size < size_t(end - first)) {
        ObjectList_Construct(last, 1, object, this, object);
        last++;
    } else {
        InsertN(last, 1, object);
    }
}

void StripList::PushBack(const CollisionStrip *const *strip) {
    size_t size = first == NULL ? 0 : last - first;
    if (first != NULL && size < size_t(end - first)) {
        StripList_Construct(last, 1, strip, this, strip);
        last++;
    } else {
        InsertN(last, 1, strip);
    }
}

// ---------------------------------------------------------------------------------------------------------------
// The article swap
// ---------------------------------------------------------------------------------------------------------------

// FUNC_AT(0x000c2ff0)
bool WCollisionMgr::SetCollisionArticle(CARP::Instance *renderInstance, uint32_t article) {
    // the render instance's index, as an unsigned difference
    uint32_t renderIndex =
        uint32_t(uintptr_t(renderInstance) - uintptr_t(fgWorld->instances)) / sizeof(CARP::Instance);
    uint32_t i = 0;
    while (i < instanceCount && instances[i].renderIndex != renderIndex)
        i++;
    if (i == instanceCount)
        return false;
    WCollisionInstance *instance = &instances[i];

    // the first change of an instance remembers its own article, for Restart
    ArticleMapNode *found;
    ArticleMap_Find(&articles, 0, &found, &instance);
    if (found == articles.head) {
        ArticleMapValue original = {instance, instance->article};
        ArticleMapInsert inserted;
        articles.InsertUnique(&inserted, &original);
    }

    if (article == 0xffffffff) {
        instance->article = NULL;
        return true;
    }
    UGroup *model = ArticleOf(renderInstance)->model->group;
    UData *data = model->DataLocateTag(kArticleTag | article);
    if (uint32_t(model->DataCountType(kArticleType)) <= article)
        return false;
    instance->article = reinterpret_cast<CollisionArticle *>(data->Data());
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// The instance lists
// ---------------------------------------------------------------------------------------------------------------

// FUNC_AT(0x000c31f0)
void WCollisionMgr::GetInstanceListGuts(WGridCellList *cells, InstanceList *out, const Coord4 *segment) {
    queryStamp++;
    WGrid *grid = TheGrid;
    const Coord4 &start = segment[0];
    const Coord4 &end = segment[1];
    Coord4 direction;
    VU0_v4sub(&end, &start, &direction);
    float inverseLengthSquared =
        float(1.0 / (double(direction.z) * direction.z + double(direction.y) * direction.y +
                     double(direction.x) * direction.x));
    float bottom = start.y < end.y ? start.y : end.y;
    float top = start.y > end.y ? start.y : end.y;

    for (uint32_t *cellIndex = cells->first; cellIndex != cells->last; cellIndex++) {
        WGridNode *cell = grid->nodes[*cellIndex];
        if (cell == NULL)
            continue;
        GridCellIterator it;
        it.Construct(cell, kGridInstance);
        for (const uint16_t *index = it.Next(); index != NULL; index = it.Next()) {
            WCollisionInstance *instance = &instances[*index];
            if (instance->queryStamp == queryStamp)
                continue;
            instance->queryStamp = queryStamp;
            if (instance->article == NULL)
                continue;
            float radius = instance->radius;
            MATRIX4 matrix;
            instance->MakeMatrix(&matrix, true);
            OrthoInverse(&matrix);
            const Coord4 *centre = MatrixRow(&matrix, 3);
            Coord4 closest, offset;
            Util_NearestPointOnSegment(centre, &start, inverseLengthSquared, &direction, &closest);
            VU0_v4sub(centre, &closest, &offset);
            if (!(double(radius) * radius > double(offset.z) * offset.z + double(offset.x) * offset.x))
                continue;
            if (!(instance->flags & kInstanceTilted)) {
                // its height range has to meet the segment's
                if (!(double(centre->y) + instance->halfHeight > bottom))
                    continue;
                if (!(double(centre->y) - instance->halfHeight < top))
                    continue;
            }
            InstanceListEntry entry = {instance, NULL};
            out->PushBack(&entry);
        }
    }
}

// FUNC_AT(0x000c3410)
void WCollisionMgr::GetInstanceList(InstanceList *out, const Coord4 *segment) {
    WGridCellList cells;
    cells.first = NULL;
    cells.last = NULL;
    cells.end = NULL;
    cells.Reserve(0x40);
    TheGrid->FindNodes(segment, &cells);
    out->Reserve(0x20);
    GetInstanceListGuts(&cells, out, segment);
    DestroyVector(&cells);
}

// FUNC_AT(0x000c4120)
InstanceListEntry* WCollisionMgr::GetInstanceStripList(InstanceListEntry *result, WCollisionInstance *instance,
                                                       const Coord3 *point, float radius, bool flat) {
    CollisionArticle *article = instance->article;
    StripList *strips = NULL;
    if (article != NULL) {
        Coord4 world;                   // w is left as it is, as the original leaves it
        world.x = point->x;
        world.y = point->y;
        world.z = point->z;
        MATRIX4 toLocal;
        instance->MakeMatrix(&toLocal, true);
        Coord4 local;
        VU0_MATRIX4_vect3mult(&world, &toLocal, &local);

        const CollisionStrip *strip = article->Strips();
        for (int i = 0; i < article->stripCount; i++, strip++) {
            Coord3 offset;
            v3sub(1, strip, &local, &offset);
            double distanceSquared =
                (flat ? double(offset.z) * offset.z : double(offset.y) * offset.y + double(offset.z) * offset.z) +
                double(offset.x) * offset.x;
            double reach = int(strip->radius) * kStripRadiusScale + radius;
            if (!(distanceSquared < reach * reach))
                continue;

            // the strip's sphere is near: is one of its triangles?
            const StripVertex *vertex = article->Vertices(strip);
            int triangles = vertex[0].count - 2;
            for (int j = 0; j < triangles; j++) {
                const StripVertex &a = vertex[j], &b = vertex[j + 1], &c = vertex[j + 2];
                Coord4 centre;
                centre.x = float((double(a.x) + c.x + b.x) * kOneThird);
                centre.y = float((double(c.y) + a.y + b.y) * kOneThird);
                centre.z = float((double(c.z) + a.z + b.z) * kOneThird);
                double triangleReach = int(c.tag.id) * kStripRadiusScale + radius;   // the id: the triangle's radius
                float reachSquared = float(triangleReach * triangleReach);
                bool nearby;
                if (flat) {
                    double dz = double(centre.z) - local.z;
                    double dx = double(centre.x) - local.x;
                    nearby = dx * dx + dz * dz < reachSquared;
                } else {
                    nearby = VU0_v3distancesquare(&centre, &local) < reachSquared;
                }
                if (nearby) {
                    if (strips == NULL) {
                        strips = static_cast<StripList *>(OperatorNew(sizeof(StripList)));
                        if (strips != NULL) {
                            strips->first = NULL;
                            strips->last = NULL;
                            strips->end = NULL;
                            strips->article = article;
                        }
                    }
                    strips->PushBack(&strip);
                    break;
                }
            }
        }
    }
    result->strips = strips;
    result->instance = instance;
    return result;
}

// FUNC_AT(0x000c43c0)
void WCollisionMgr::GetInstanceListGuts(WGridCellList *cells, InstanceList *out, const Coord3 *point, float radius, bool wantStrips, bool flat) {
    queryStamp++;
    WGrid *grid = TheGrid;
    for (uint32_t *cellIndex = cells->first; cellIndex != cells->last; cellIndex++) {
        WGridNode *cell = grid->nodes[*cellIndex];
        if (cell == NULL)
            continue;
        GridCellIterator it;
        it.Construct(cell, kGridInstance);
        for (const uint16_t *index = it.Next(); index != NULL; index = it.Next()) {
            WCollisionInstance *instance = &instances[*index];
            if (instance->queryStamp == queryStamp)
                continue;
            instance->queryStamp = queryStamp;
            if (instance->article == NULL)
                continue;
            Coord3 position;
            instance->CalcPosition(&position);
            // the distance in x and z, roughly: the larger plus a quarter of the smaller
            double dx = fabs(double(position.x) - point->x);
            double dz = fabs(double(position.z) - point->z);
            double distance = dx > dz ? dz * 0.25 + dx : dx * 0.25 + dz;
            if (!(distance < double(radius) + instance->radius))
                continue;
            if (!wantStrips) {
                InstanceListEntry entry = {instance, NULL};
                out->PushBack(&entry);
            } else {
                InstanceListEntry entry;
                GetInstanceStripList(&entry, instance, point, radius, flat);
                if (entry.strips != NULL)
                    out->PushBack(&entry);
            }
        }
    }
}

// FUNC_AT(0x000c4510)
void WCollisionMgr::GetInstanceList(InstanceList *out, const Coord3 *point, float radius, bool wantStrips, bool flat) {
    WGridCellList cells;
    cells.first = NULL;
    cells.last = NULL;
    cells.end = NULL;
    cells.Reserve(0x40);
    TheGrid->FindNodes(point, radius, &cells);
    out->Reserve(0x20);
    GetInstanceListGuts(&cells, out, point, radius, wantStrips, flat);
    DestroyVector(&cells);
}

// ---------------------------------------------------------------------------------------------------------------
// The object and barrier lists
// ---------------------------------------------------------------------------------------------------------------

// FUNC_AT(0x000c34c0)
void WCollisionMgr::GetObjectListsGuts(WGridCellList *cells, ObjectList *cylinders, ObjectList *boxes,
                                       const Coord3 *point, float radius) {
    WGrid *grid = TheGrid;
    for (uint32_t *cellIndex = cells->first; cellIndex != cells->last; cellIndex++) {
        WGridNode *cell = grid->nodes[*cellIndex];
        if (cell == NULL)
            continue;
        GridCellIterator it;
        it.Construct(cell, kGridObject);
        for (const uint16_t *index = it.Next(); index != NULL; index = it.Next()) {
            WCollisionObject *object = &objects[*index];
            // as GetInstanceListGuts' distance, except that the x difference is stored to a float on the way
            float dx = float(fabs(double(object->position.x) - point->x));
            double dz = fabs(double(object->position.z) - point->z);
            double distance = dx > dz ? dz * 0.25 + dx : dz + dx * 0.25;
            if (!(distance < double(radius) + object->radius))
                continue;
            if (object->isCylinder)
                cylinders->PushBack(&object);
            else
                boxes->PushBack(&object);
        }
    }
}

// FUNC_AT(0x000c36a0)
void WCollisionMgr::GetObjectLists(ObjectList *cylinders, ObjectList *boxes, const Coord4 *segment) {
    WGridCellList cells;
    cells.first = NULL;
    cells.last = NULL;
    cells.end = NULL;
    cells.Reserve(0x40);
    TheGrid->FindNodes(segment, &cells);
    // the segment's middle, and half its length
    Coord4 middle;
    VU0_v3add(&segment[0], &segment[1], &middle);
    VU0_v4scale(&middle, 0.5f, &middle);
    double dx = double(middle.x) - segment->x;
    double dy = double(middle.y) - segment->y;
    double dz = double(middle.z) - segment->z;
    float radius = VU0_sqrt(float(dz * dz + dy * dy + dx * dx));
    cylinders->Reserve(0x10);
    boxes->Reserve(0x10);
    GetObjectListsGuts(&cells, cylinders, boxes, reinterpret_cast<const Coord3 *>(&middle), radius);
    DestroyVector(&cells);
}

// FUNC_AT(0x000c37c0)
void WCollisionMgr::GetObjectLists(ObjectList *cylinders, ObjectList *boxes, const Coord3 *point, float radius) {
    WGridCellList cells;
    cells.first = NULL;
    cells.last = NULL;
    cells.end = NULL;
    cells.Reserve(0x40);
    TheGrid->FindNodes(point, radius, &cells);
    cylinders->Reserve(0x10);
    boxes->Reserve(0x10);
    GetObjectListsGuts(&cells, cylinders, boxes, point, radius);
    DestroyVector(&cells);
}

// FUNC_AT(0x000c3880)
void WCollisionMgr::GetBarrierList(BarrierList *out, const InstanceList *instanceList, const Coord3 *point,
                                   float radius) {
    out->Reserve(0x15);
    float radiusSquared = radius * radius;
    for (const InstanceListEntry *entry = instanceList->first; entry != instanceList->last; entry++) {
        WCollisionInstance *instance = entry->instance;
        const CollisionArticle *article = instance->article;
        if (article == NULL || article->barrierCount == 0)
            continue;
        MATRIX4 toLocal;
        instance->MakeMatrix(&toLocal, true);
        Coord4 local;
        VU0_MATRIX4_vect3mult(point, &toLocal, &local);

        const CollisionBarrier *barrier = article->Barriers();
        bool haveInverse = false;
        MATRIX4 toWorld;
        for (int i = 0; i < article->barrierCount; i++, barrier++) {
            if (barrierMask & barrier->tag.subType)
                continue;
            if (!(barrier->DistanceSquared2D(&local) < radiusSquared))
                continue;
            if (!(double(local.y) + radius > barrier->y0))
                continue;
            if (!(double(local.y) - radius <= barrier->y1))
                continue;
            if (!haveInverse) {
                toWorld = toLocal;
                toWorld.mtx[0][3] = 0.0f;
                toWorld.mtx[1][3] = 0.0f;
                toWorld.mtx[2][3] = 0.0f;
                toWorld.mtx[3][3] = 1.0f;
                OrthoInverse(&toWorld);
                haveInverse = true;
            }
            BarrierListEntry found;
            found.barrier = *barrier;
            VU0_MATRIX4_vect3mult(&found.barrier.x0, &toWorld, &found.barrier.x0);
            VU0_MATRIX4_vect3mult(&found.barrier.x1, &toWorld, &found.barrier.x1);
            if (found.barrier.y0 > found.barrier.y1) {
                float y = found.barrier.y1;
                found.barrier.y1 = found.barrier.y0;
                found.barrier.y0 = y;
            }
            found.instance = instance;
            found.index = i;
            out->PushBack(&found);
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------
// The hit checks
// ---------------------------------------------------------------------------------------------------------------

// FUNC_AT(0x000c3b40)
int WCollisionMgr::CheckHitWorld(const Coord4 *segment, WorldCollisionInfo *info) {
    Coord4 local[2];
    VU0_v4copy(&segment[0], &local[0]);
    VU0_v4copy(&segment[1], &local[1]);
    float length = vec3distance(&local[0], &local[1]);
    if (length == 0.0f)
        return 0;
    if (length < kShortSegment) {
        Coord4 step;
        VU0_v4sub(&local[1], &local[0], &step);
        VU0_v4scale(&step, 0.25f / length, &step);
        VU0_v3add(&local[0], &step, &local[1]);
    }
    // the segment's bounding sphere, which nothing reads (as in the original)
    Coord4 sphere;
    VU0_v3add(&local[0], &local[1], &sphere);
    VU0_v4scale(&sphere, 0.5f, &sphere);
    sphere.w = length * 0.5f;

    int hit = 0;                        // 1 a face, 2 a barrier
    InstanceList instanceList;
    instanceList.first = NULL;
    instanceList.last = NULL;
    instanceList.end = NULL;
    GetInstanceList(&instanceList, local);
    if (GetBarrierNormal(&instanceList, local, info)) {
        hit = 2;
        VU0_v4copy(&info->point, &local[1]);
    }

    // the world's faces; the candidate is reused for the objects below, and its second construction leaves what
    // the first construction does not set (the normal)
    WorldCollisionInfo candidate;
    WWorldPos position;
    position.Construct();
    position.FindClosestFace(&instanceList, &local[0], &local[1]);
    if (position.valid) {
        WorldCollisionInfo_Construct(&candidate, 0);
        candidate.hitType = kHitWorld;
        if (position.valid) {
            WWorldPos_FaceNormal(&position, 0, &candidate.normal);
        } else {
            candidate.normal.z = 0.0f;
            candidate.normal.x = 0.0f;
            candidate.normal.y = 1.0f;
        }
        candidate.normal.w = 1.0f;
        float t;
        if (WWorldMath::IntersectSegPlane(&local[0], &local[1], &position.face.corner[0].Position(), &candidate.normal,
                                          &candidate.point, &t)) {
            candidate.faceType = position.face.corner[2].tag.type;
            candidate.faceSubType = position.face.corner[2].tag.subType;
            candidate.faceInstance = position.instance;
            candidate.flag52 = (position.instance->flags & kInstanceFlag02) >> 1;
            Coord4 back;
            VU0_v4sub(&local[0], &candidate.point, &back);
            if (v3dotprod(&candidate.normal, &back) < 0.0f)
                VU0_v3negate(&candidate.normal);
            bool nearer = true;
            if (hit != 0) {
                float barrierDistance = VU0_v3distancesquare(&local[0], &info->point);
                nearer = VU0_v3distancesquare(&local[0], &candidate.point) < barrierDistance;
            }
            if (nearer) {
                *info = candidate;
                hit = 1;
            }
        }
    }
    NullFunction();                     // ~WWorldPos
    if (hit != 0)
        VU0_v4copy(&info->point, &local[1]);

    // the collision objects along what is left of the segment
    ObjectList cylinders, boxes;
    cylinders.first = NULL;
    cylinders.last = NULL;
    cylinders.end = NULL;
    boxes.first = NULL;
    boxes.last = NULL;
    boxes.end = NULL;
    fgCollisionMgr->GetObjectLists(&cylinders, &boxes, local);
    WorldCollisionInfo_Construct(&candidate, 0);
    if ((cylinders.first != NULL && cylinders.last - cylinders.first != 0 &&
         fgCollisionMgr->GetCylObjectCollision(local, &cylinders, &candidate)) ||
        (boxes.first != NULL && boxes.last - boxes.first != 0 &&
         fgCollisionMgr->GetOBBObjectCollision(local, &boxes, &candidate))) {
        bool nearer = true;
        if (hit != 0) {
            float earlierDistance = VU0_v3distancesquare(&local[0], &info->point);
            nearer = VU0_v3distancesquare(&local[0], &candidate.point) < earlierDistance;
        }
        if (nearer)
            *info = candidate;
    }
    DestroyVector(&boxes);
    DestroyVector(&cylinders);
    DestroyVector(&instanceList);
    return info->hitType != kHitNone ? 1 : 0;
}

// FUNC_AT(0x000c4000)
int WCollisionMgr::StepCheckHitWorld(Coord4 *segment, float step) {
    (void)step;
    Coord4 &from = segment[0];
    Coord4 &to = segment[1];
    float length = vec3distance(&from, &to);
    int steps = 1 - Ftol(double(length) * kStepLengthScale);
    Coord4 delta;
    VU0_v4sub4(&to, &from, &delta);
    VU0_v4unit(&delta, &delta);
    VU0_v4scale4(&delta, float(length / double(steps)), &delta);
    delta.w = 1.0f;
    WorldCollisionInfo info;
    WorldCollisionInfo_Construct(&info, 0);
    for (int i = 0; i < steps; i++) {
        VU0_v3add(&from, &delta, &to);
        to.w = 1.0f;
        from.w = 1.0f;
        fgCollisionMgr->CheckHitWorld(segment, &info);
        if (info.hitType != kHitNone)
            return 1;
        from = to;
    }
    return 0;
}

// FUNC_AT(0x000c4d70)
bool WCollisionMgr::CheckHitWindow(WorldCollisionInfo *info, bool create, int kind) {
    if (info->hitType != kHitBarrier || info->instance->article->windowGroupCount == 0)
        return false;

    // the barrier and the hit in the instance's frame; how far along the barrier (horizontally) the hit is
    MATRIX4 toLocal;
    info->instance->MakeMatrix(&toLocal, true);
    Coord4 start, end, point;
    VU0_MATRIX4_vect3mult(&info->segmentStart, &toLocal, &start);
    VU0_MATRIX4_vect3mult(&info->segmentEnd, &toLocal, &end);
    VU0_MATRIX4_vect3mult(&info->point, &toLocal, &point);
    Coord4 direction, offset;
    VU0_v4sub(&end, &start, &direction);
    direction.y = 0.0f;
    float along = 0.0f;
    VU0_v4sub(&point, &start, &offset);
    if (fabsf(direction.x) > 0.0f || fabsf(direction.z) > 0.0f)
        along = fabsf(direction.x) > fabsf(direction.z) ? offset.x / direction.x : offset.z / direction.z;

    // the barrier's window group
    WCollisionInstance *instance = info->instance;
    const CollisionArticle *article = instance->article;
    const WindowGroup *group = article->WindowGroups();
    uint32_t groups = article->windowGroupCount;
    if (groups == 0)
        return false;
    for (;;) {
        groups--;
        if (group->id == info->groupId)
            break;
        if (group->size >= kBadWindowGroupSize || groups == 0)   // "CheckHitWindow - bad wGroup." on the PS2
            return false;
        group = reinterpret_cast<const WindowGroup *>(reinterpret_cast<const uint8_t *>(group) + group->size);
    }
    const WindowSet *set = group->FindSet(instance->flags >> kInstanceVariantShift);
    if (set == NULL)
        return false;

    // the pane the hit went through
    uint32_t panes = set->paneCount;
    if (panes == 0)
        return false;
    const WindowPane *pane = set->Panes();
    for (;; pane++) {
        panes--;
        double paneStart = pane->start * kPaneAlongScale;
        double paneEnd = pane->width * kPaneAlongScale + paneStart;
        double paneTop = pane->top * kPaneHeightScale;
        double paneBottom = paneTop - pane->height * kPaneHeightScale;
        if (along >= paneStart && along <= paneEnd && offset.y <= paneTop && offset.y >= paneBottom)
            break;
        if (panes == 0)
            return false;
    }
    if (!create)
        return false;

    // the instance's panes in the window map, then this pane's damage
    WindowPaneMap empty;
    empty.Construct();
    WindowMapValue entry;
    entry.Construct(&info->instance, &empty);
    WindowMapInsert windowSlot;
    windows.InsertUnique(&windowSlot, &entry);
    WindowPaneMap *paneMap = &windowSlot.node->value.panes;
    entry.Destruct();
    empty.Destruct();

    WindowHit fresh;
    WindowPaneValue paneEntry;
    paneEntry.pane = pane;
    paneEntry.hit = *fresh.Construct(0, 0, &kZero, &kZero);
    WindowPaneInsert paneSlot;
    paneMap->InsertUnique(&paneSlot, &paneEntry);
    WindowHit *hit = &paneSlot.node->value.hit;
    if (!paneSlot.inserted) {
        hit->hits++;
    } else {
        // a new break: its kind and the pane's corners, a little off the glass
        MATRIX4 toWorld;
        info->instance->MakeMatrix(&toWorld, false);
        Coord3 normal;
        MATRIX4_vect3mult(&info->normal, &toWorld, &normal);
        Coord4 origin;
        origin.x = float(double(normal.x) * kPaneOffset + start.x);
        origin.y = float(double(normal.y) * kPaneOffset + start.y);
        origin.z = float(double(normal.z) * kPaneOffset + start.z);
        Coord3 corners[4];
        pane->MakeCorners(&origin, &direction, corners);
        MATRIX4 inverse = toLocal;
        OrthoInverse(&inverse);
        uint32_t breakKind = GameRandom() & 3;
        if (pane->top * kPaneHeightScale + info->segmentStart.y > kHighPane)
            breakKind += 4;
        *hit = *fresh.Construct(breakKind, 0, &corners[0], &corners[2]);
        MATRIX4_vect4mult(&hit->corner0, &inverse, &hit->corner0);
        MATRIX4_vect4mult(&hit->corner1, &inverse, &hit->corner1);
    }

    void *event = Event::operator new(kHitWindowEventSize);
    if (event != NULL)
        EHitWindow_Construct(event, 0, info->point, info->normal, *hit, kind == 1);
    info->faceType = kBrokenWindowFace;
    info->faceSubType = 0;
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// The window map (std::map<WCollisionInstance *, WindowPaneMap>) and its pane maps
// ---------------------------------------------------------------------------------------------------------------

// The allocator byte the game copies from an uninitialised stack slot is left alone.
// FUNC_AT(0x000c45d0)
WindowPaneMap* WindowPaneMap::Construct() {
    head = BuyHeadNode();
    head->isNil = 1;
    head->parent = head;
    head->left = head;
    head->right = head;
    size = 0;
    return this;
}

// FUNC_AT(0x000c3ab0)
WindowPaneMap* WindowPaneMap::CopyConstruct(const WindowPaneMap *other) {
    allocator = other->allocator;
    head = BuyHeadNode();
    head->isNil = 1;
    head->parent = head;
    head->left = head;
    head->right = head;
    size = 0;
    CopyTree(other);
    return this;
}

// FUNC_AT(0x000c4610)
void WindowPaneMap::Destruct() {
    WindowPaneNode *next;
    EraseRange(&next, head->left, head);
    if (head != NULL)
        UMemory::FastFree(head, sizeof(WindowPaneNode));
    head = NULL;
    size = 0;
}

// FUNC_AT(0x000c46d0)
WindowMapValue* WindowMapValue::Construct(WCollisionInstance *const *key, const WindowPaneMap *other) {
    instance = *key;
    panes.CopyConstruct(other);
    return this;
}

// FUNC_AT(0x000c4650)
void WindowMapValue::Destruct() {
    panes.Destruct();
}

// FUNC_AT(0x000c4690)
void WindowMapNode::DestroyValue() {
    value.panes.Destruct();
}

// FUNC_AT(0x000c46f0)
WindowMapNode* WindowMap::Buynode(WindowMapNode *left, WindowMapNode *parent, WindowMapNode *right,
                                  const WindowMapValue *value, uint8_t color) {
    WindowMapNode *node = static_cast<WindowMapNode *>(UMemory::FastAlloc(sizeof(WindowMapNode), "STL"));
    if (node != NULL) {
        node->left = left;
        node->parent = parent;
        node->right = right;
        node->value.instance = value->instance;
        node->value.panes.CopyConstruct(&value->panes);
        node->color = color;
        node->isNil = 0;
    }
    return node;
}

// FUNC_AT(0x000c47a0)
WindowMapNode** WindowMap::Insert(WindowMapNode **result, bool addLeft, WindowMapNode *where,
                                  const WindowMapValue *value) {
    if (size >= 0x0ffffffe) {
        COLLISION_UNTESTED("the window map's insert past max_size");
        TreeThrow("map/set<T> too long", kLengthErrorVtable, kLengthErrorThrowInfo);
    }
    WindowMapNode *node = Buynode(head, where, head, value, kTreeRed);
    size++;
    if (where == head) {
        head->parent = node;
        head->left = node;
        head->right = node;
    } else if (addLeft) {
        where->left = node;
        if (where == head->left)
            head->left = node;
    } else {
        where->right = node;
        if (where == head->right)
            head->right = node;
    }

    for (WindowMapNode *x = node; x->parent->color == kTreeRed;) {
        WindowMapNode *parent = x->parent;
        WindowMapNode *grandparent = parent->parent;
        if (parent == grandparent->left) {
            WindowMapNode *uncle = grandparent->right;
            if (uncle->color == kTreeRed) {
                parent->color = kTreeBlack;
                uncle->color = kTreeBlack;
                x->parent->parent->color = kTreeRed;
                x = x->parent->parent;
            } else {
                if (x == parent->right) {
                    x = parent;
                    WindowMap_Lrotate(this, 0, x);
                }
                x->parent->color = kTreeBlack;
                x->parent->parent->color = kTreeRed;
                WindowMap_Rrotate(this, 0, x->parent->parent);
            }
        } else {
            WindowMapNode *uncle = grandparent->left;
            if (uncle->color == kTreeRed) {
                parent->color = kTreeBlack;
                uncle->color = kTreeBlack;
                x->parent->parent->color = kTreeRed;
                x = x->parent->parent;
            } else {
                if (x == parent->left) {
                    x = parent;
                    WindowMap_Rrotate(this, 0, x);
                }
                x->parent->color = kTreeBlack;
                x->parent->parent->color = kTreeRed;
                WindowMap_Lrotate(this, 0, x->parent->parent);
            }
        }
    }
    head->parent->color = kTreeBlack;
    *result = node;
    return result;
}

// FUNC_AT(0x000c4980)
WindowMapNode** WindowMap::EraseAt(WindowMapNode **result, WindowMapNode *where) {
    if (where->isNil) {
        COLLISION_UNTESTED("the window map's erase of end()");
        TreeThrow("invalid map/set<T> iterator", kOutOfRangeVtable, kOutOfRangeThrowInfo);
    }
    WindowMapNode *erased = where;
    WindowMapNode *next = where;
    WindowMap_Increment(&next, 0);

    // unlink it: the node that takes its place (pnode), the subtree that moves up (fixnode) and its new parent
    WindowMapNode *pnode = erased;
    WindowMapNode *fixnode;
    WindowMapNode *fixparent;
    if (erased->left->isNil) {
        fixnode = erased->right;
    } else if (erased->right->isNil) {
        fixnode = erased->left;
    } else {
        pnode = next;
        fixnode = pnode->right;
    }
    if (pnode == erased) {
        fixparent = erased->parent;
        if (!fixnode->isNil)
            fixnode->parent = fixparent;
        if (head->parent == erased)
            head->parent = fixnode;
        else if (fixparent->left == erased)
            fixparent->left = fixnode;
        else
            fixparent->right = fixnode;
        if (head->left == erased)
            head->left = fixnode->isNil ? fixparent : WindowMapMin(fixnode);
        if (head->right == erased)
            head->right = fixnode->isNil ? fixparent : WindowMap_Max(fixnode);
    } else {
        erased->left->parent = pnode;
        pnode->left = erased->left;
        if (pnode == erased->right) {
            fixparent = pnode;
        } else {
            fixparent = pnode->parent;
            if (!fixnode->isNil)
                fixnode->parent = fixparent;
            fixparent->left = fixnode;
            pnode->right = erased->right;
            erased->right->parent = pnode;
        }
        if (head->parent == erased)
            head->parent = pnode;
        else if (erased->parent->left == erased)
            erased->parent->left = pnode;
        else
            erased->parent->right = pnode;
        pnode->parent = erased->parent;
        uint8_t color = pnode->color;
        pnode->color = erased->color;
        erased->color = color;
    }

    // rebalance
    if (erased->color == kTreeBlack) {
        for (; fixnode != head->parent && fixnode->color == kTreeBlack;
             fixnode = fixparent, fixparent = fixparent->parent) {
            if (fixnode == fixparent->left) {
                pnode = fixparent->right;
                if (pnode->color == kTreeRed) {
                    pnode->color = kTreeBlack;
                    fixparent->color = kTreeRed;
                    WindowMap_Lrotate(this, 0, fixparent);
                    pnode = fixparent->right;
                }
                if (pnode->isNil)
                    continue;
                if (pnode->left->color == kTreeBlack && pnode->right->color == kTreeBlack) {
                    pnode->color = kTreeRed;
                    continue;
                }
                if (pnode->right->color == kTreeBlack) {
                    pnode->left->color = kTreeBlack;
                    pnode->color = kTreeRed;
                    WindowMap_Rrotate(this, 0, pnode);
                    pnode = fixparent->right;
                }
                pnode->color = fixparent->color;
                fixparent->color = kTreeBlack;
                pnode->right->color = kTreeBlack;
                WindowMap_Lrotate(this, 0, fixparent);
                break;
            } else {
                pnode = fixparent->left;
                if (pnode->color == kTreeRed) {
                    pnode->color = kTreeBlack;
                    fixparent->color = kTreeRed;
                    WindowMap_Rrotate(this, 0, fixparent);
                    pnode = fixparent->left;
                }
                if (pnode->isNil)
                    continue;
                if (pnode->right->color == kTreeBlack && pnode->left->color == kTreeBlack) {
                    pnode->color = kTreeRed;
                    continue;
                }
                if (pnode->left->color == kTreeBlack) {
                    pnode->right->color = kTreeBlack;
                    pnode->color = kTreeRed;
                    WindowMap_Lrotate(this, 0, pnode);
                    pnode = fixparent->left;
                }
                pnode->color = fixparent->color;
                fixparent->color = kTreeBlack;
                pnode->left->color = kTreeBlack;
                WindowMap_Rrotate(this, 0, fixparent);
                break;
            }
        }
        fixnode->color = kTreeBlack;
    }

    erased->value.panes.Destruct();
    UMemory::FastFree(erased, sizeof(WindowMapNode));
    if (size > 0)
        size--;
    *result = next;
    return result;
}

// FUNC_AT(0x000c4c60)
WindowMapInsert* WindowMap::InsertUnique(WindowMapInsert *result, const WindowMapValue *value) {
    WindowMapNode *where = head;
    bool addLeft = true;
    for (WindowMapNode *x = head->parent; !x->isNil; x = addLeft ? x->left : x->right) {
        where = x;
        addLeft = uintptr_t(value->instance) < uintptr_t(x->value.instance);
    }
    WindowMapNode *at = where;
    if (addLeft) {
        if (where == head->left) {
            Insert(&at, true, where, value);
            result->node = at;
            result->inserted = true;
            return result;
        }
        WindowMap_Decrement(&at, 0);
    }
    if (uintptr_t(at->value.instance) < uintptr_t(value->instance)) {
        Insert(&at, addLeft, where, value);
        result->node = at;
        result->inserted = true;
        return result;
    }
    result->node = at;
    result->inserted = false;
    return result;
}

// FUNC_AT(0x000c4d20)
void WindowMap::EraseSubtree(WindowMapNode *node) {
    while (!node->isNil) {
        EraseSubtree(node->right);
        WindowMapNode *left = node->left;
        node->DestroyValue();
        UMemory::FastFree(node, sizeof(WindowMapNode));
        node = left;
    }
}

// FUNC_AT(0x000c52c0)
WindowMapNode** WindowMap::EraseRange(WindowMapNode **result, WindowMapNode *first, WindowMapNode *last) {
    if (first == head->left && last == head) {
        EraseSubtree(head->parent);
        head->parent = head;
        size = 0;
        head->left = head;
        head->right = head;
        *result = head->left;
        return result;
    }
    while (first != last) {
        WindowMapNode *erase = first;
        first = WindowMapNext(first);
        WindowMapNode *next;
        EraseAt(&next, erase);
    }
    *result = first;
    return result;
}

// ---------------------------------------------------------------------------------------------------------------
// Lifecycle (WWorld::Open, Reset and Close)
// ---------------------------------------------------------------------------------------------------------------

// FUNC_AT(0x000c5500)
WCollisionMgr* WCollisionMgr::Construct() {
    // (the maps' allocator bytes the game copies from uninitialised stack slots are left alone)
    windows.head = WindowMap_Buyhead();
    windows.head->isNil = 1;
    windows.head->parent = windows.head;
    windows.head->left = windows.head;
    windows.head->right = windows.head;
    windows.size = 0;
    articles.head = BuyMapHead<ArticleMapNode>();
    articles.head->isNil = 1;
    articles.head->parent = articles.head;
    articles.head->left = articles.head;
    articles.head->right = articles.head;
    articles.size = 0;
    unknown00 = 0;
    queryStamp = 0;
    instances = NULL;
    instanceCount = 0;
    objects = NULL;
    unknown30 = 0;
    barrierMask = kBarrierMask;
    return this;
}

// FUNC_AT(0x000c5460)
void WCollisionMgr::Destruct() {
    ArticleMapNode *articleNext;
    articles.EraseRange(&articleNext, articles.head->left, articles.head);
    if (articles.head != NULL)
        UMemory::FastFree(articles.head, sizeof(ArticleMapNode));
    articles.head = NULL;
    articles.size = 0;
    WindowMapNode *windowNext;
    windows.EraseRange(&windowNext, windows.head->left, windows.head);
    if (windows.head != NULL)
        UMemory::FastFree(windows.head, sizeof(WindowMapNode));
    windows.head = NULL;
    windows.size = 0;
}

// FUNC_AT(0x000c55b0)
void WCollisionMgr::Init(UGroup *world) {
    UGroup *collision = world->GroupLocateTag(kCollisionGroupTag);
    UData *instanceData = collision->DataLocateTag(kInstancesTag);
    collision->GetArray();              // the original calls it and drops the answer, twice
    UData *objectData = collision->DataLocateTag(kObjectsTag);
    collision->GetArray();
    WCollisionMgr *manager = static_cast<WCollisionMgr *>(OperatorNew(sizeof(WCollisionMgr)));
    fgCollisionMgr = manager != NULL ? manager->Construct() : NULL;
    fgCollisionMgr->unknown00 = 0;
    fgCollisionMgr->instances = reinterpret_cast<WCollisionInstance *>(instanceData->Data());
    fgCollisionMgr->instanceCount = instanceData->count;
    fgCollisionMgr->objects = reinterpret_cast<WCollisionObject *>(objectData->Data());

    // each instance's article: from its resolved group to the group's "ca" data
    WCollisionInstance *instance = fgCollisionMgr->instances;
    for (uint32_t n = fgCollisionMgr->instanceCount; n != 0; n--, instance++) {
        if (instance->articleGroup == NULL)
            instance->GetName();
        instance->article = reinterpret_cast<CollisionArticle *>(instance->articleGroup->DataLocateTag(kArticleTag)->Data());
    }
}

// FUNC_AT(0x000c5380)
void WCollisionMgr::Restart() {
    WindowMap &windows = fgCollisionMgr->windows;
    windows.EraseSubtree(windows.head->parent);
    windows.head->parent = windows.head;
    windows.size = 0;
    windows.head->left = windows.head;
    windows.head->right = windows.head;

    // the instances whose articles SetCollisionArticle changed get their own back
    for (ArticleMapNode *node = fgCollisionMgr->articles.head->left; node != fgCollisionMgr->articles.head;
         ArticleMap_Increment(&node, 0))
        node->value.instance->article = node->value.article;
    ArticleMap &articles = fgCollisionMgr->articles;
    articles.EraseSubtree(articles.head->parent);
    articles.head->parent = articles.head;
    articles.size = 0;
    articles.head->left = articles.head;
    articles.head->right = articles.head;
}

// FUNC_AT(0x000c56d0)
void WCollisionMgr::Shutdown() {
    WCollisionMgr *manager = fgCollisionMgr;
    if (manager != NULL) {
        manager->Destruct();
        OperatorDelete(manager);
    }
    fgCollisionMgr = NULL;
}
