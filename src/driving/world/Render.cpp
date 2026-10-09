#pragma fp_contract(off)

#include "Render.h"

#include "../../helpers.h"
#include "../engine/CoreFoundation.h"     // NullFunction
#include "../engine/UGroup.h"
#include "../engine/UMemory.hpp"
#include "../platform/RealMath.h"
#include "../render/Lights.h"
#include "../render/Renderer.h"
#include "../render/SkyWater.h"
#include "../render/WorldCulling.h"

// ---------------------------------------------------------------------------------------------------------------
// WRender (0x000c7330..0x000c8940), ported from the listing, with the compiled copy of std::sort for its draws
// that the linker put among its methods. The x87 code keeps the original's order and roundings.
// ---------------------------------------------------------------------------------------------------------------

// ---- the game's globals
#define VisibleList (*(CachedDrawInfo *)0x0023b3c0)
#define CurrentList (*(CachedDrawInfo **)0x0023e014)     // the list the tree walks fill
#define VisibleNodeCount I32_AT(0x0023e02c)               // DrawWorldAtPoint's, never read
#define ViewCull (*(WViewCull *)0x0023dfd0)

// The cursor of GetNextPoint's walk (0x0023e008)
struct CullCursor {
    int32_t node;                       // the index of the visible node
    int32_t item;                       // the index of its instance
    CachedDrawInfo *list;               // PrepareForCull's
};
#define Cursor (*(CullCursor *)0x0023e008)

namespace {

constexpr uint32_t kTagMap = 0x4d617020;            // 'Map '
constexpr uint32_t kTagInstances = 0x696e2020;      // 'in  '
constexpr uint32_t kTagProcAnims = 0x70732020;      // 'ps  '
constexpr uint32_t kTagVisCurtains = 0x56697343;    // 'VisC'
constexpr uint32_t kTagShadows = 0x53686164;        // 'Shad'

constexpr float kHalf = 0.5f;
constexpr float kRootTwo = 1.414f;                  // a square's half-diagonal over its half-side

// QuadtreeFrustrumCheck2d's and DistanceCheck2d's answers
enum {
    kOutside = 0,
    kInside = 1,
    kAcross = 2,
};

// DrawWorldAtPoint skips instances with any of these
constexpr uint8_t kNotDrawnAtPoint = kWorldInstanceSceneObj | kInstanceSky | kInstanceNoFarCull;

// The instance's position, and its packed dimensions in the w word
const Coord4 *PositionOf(const CARP::Instance *instance) {
    return reinterpret_cast<const Coord4 *>(instance->position);
}

// A packed dimension: ten bits in steps of 0.25, or of 16 with bit 30 set
constexpr float kDimensionStep[2] = { 0.25f, 16.0f };
constexpr uint32_t kDimensionsSeparate = 0x80000000;

double Dimension(uint32_t packed, int shift) {
    return double((packed >> shift) & 0x3ff) * kDimensionStep[(packed >> 30) & 1];
}

// CARP::Instance::SizeY, as DrawWorldAtPoint has it inline: y when the three were given separately, else twice x
double SizeY(const CARP::Instance *instance) {
    uint32_t packed = instance->packedDimensions;
    if ((packed & kDimensionsSeparate) == kDimensionsSeparate)
        return Dimension(packed, 10);
    double x = Dimension(packed, 0);
    return x + x;
}

CARP::Instance *InstanceOf(const WRenderSortEntry &draw) {
    return reinterpret_cast<CARP::Instance *>(uintptr_t(draw.key));
}

// The map's square as a box: its centre, and its half-side in w
Coord4 RootBox(const WMapHeader *map) {
    double half = double(map->size) * kHalf;
    Coord4 box;
    box.x = float(half + map->corner.x);
    box.y = map->corner.y;
    box.z = float(half + map->corner.z);
    box.w = float(half);
    return box;
}

// std::sort(first, last, pred)
void SortDraws(CachedDrawInfo *list, RenderSortPredicate pred) {
    WRenderSortEntry *first = list->draws;
    WRenderSortEntry *last = list->draws + list->drawCount;
    RenderSort::Sort(first, last, last - first, pred);
}

}  // namespace

// ---- the scene tree walks

// FUNC_AT(0x000c7330)
void WRender::FindVisibleTreeNodesAndCurtains(WMapNode *node, const Coord4 *box, bool clip, int depth) {
    if (clip) {
        int where = fgWorldCulling.QuadtreeFrustrumCheck2d(box, box->w * kRootTwo);
        if (where == kOutside)
            return;
        clip = where == kAcross;
    }
    depth++;
    CurrentList->nodes[CurrentList->nodeCount] = node;
    CurrentList->nodeCount++;

    Coord4 eye = *MatrixRow(&camera->matrix, 3);
    const uint16_t *indices = node->IndexList();
    for (int i = 0; i < node->numCurtains; i++) {
        WVisCurtain *curtain = &curtains[indices[node->numInstances + i]];
        Coord4 middle = {};
        VU0_v4addscale(&curtain->start, &curtain->end, kHalf, &middle);
        if (fgWorldCulling.IsInFrustum2d(&middle, curtain->start.w, true, 1.0f, NULL))
            AddActiveCurtain(curtain, &eye);
    }

    if (node->children == NULL)
        return;
    Coord4 quarter;
    VU0_v4copy(box, &quarter);
    double half = double(box->w) * kHalf;
    quarter.w = float(half);
    quarter.x = float(quarter.x - half);
    quarter.z = float(quarter.z - half);
    FindVisibleTreeNodesAndCurtains(&node->children[0], &quarter, clip, depth);
    quarter.x += box->w;
    FindVisibleTreeNodesAndCurtains(&node->children[1], &quarter, clip, depth);
    quarter.z += box->w;
    FindVisibleTreeNodesAndCurtains(&node->children[3], &quarter, clip, depth);
    quarter.x -= box->w;
    FindVisibleTreeNodesAndCurtains(&node->children[2], &quarter, clip, depth);
}

// FUNC_AT(0x000c7510)
CachedDrawInfo* WRender::GenerateCurtainsAndNodes(const RCamera *camera, float unknown) {
    CurrentList = &VisibleList;
    VisibleList.nodeCount = 0;
    this->camera = camera;
    unknownD8 = unknown;
    WMapHeader *map = fgWorld->map;
    Coord4 box = RootBox(map);
    InitActiveCurtainList();
    NullFunction();     // an empty function in this build; the original passes it 1
    FindVisibleTreeNodesAndCurtains(map->root, &box, true, 0);
    return CurrentList;
}

// FUNC_AT(0x000c77f0)
void WRender::SimpleDrawVisibleTreeNodes(WMapNode *node, const Coord4 *box, bool clip, int depth) {
    if (clip) {
        int where = DistanceCheck2d(box, box->w * kRootTwo);
        if (where == kOutside)
            return;
        clip = where == kAcross;
    }
    depth++;
    CurrentList->nodes[CurrentList->nodeCount] = node;
    CurrentList->nodeCount++;

    if (node->children == NULL)
        return;
    Coord4 quarter;
    VU0_v4copy(box, &quarter);
    double half = double(box->w) * kHalf;
    quarter.w = float(half);
    quarter.x = float(quarter.x - half);
    quarter.z = float(quarter.z - half);
    SimpleDrawVisibleTreeNodes(&node->children[0], &quarter, clip, depth);
    quarter.x += box->w;
    SimpleDrawVisibleTreeNodes(&node->children[1], &quarter, clip, depth);
    quarter.z += box->w;
    SimpleDrawVisibleTreeNodes(&node->children[3], &quarter, clip, depth);
    quarter.x -= box->w;
    SimpleDrawVisibleTreeNodes(&node->children[2], &quarter, clip, depth);
}

// ---- the culling's walk over the visible instances

// FUNC_AT(0x000c75b0)
void WRender::PrepareForCull(CachedDrawInfo *list) {
    CurrentList->drawCount = 0;
    Cursor.node = 0;
    Cursor.item = -1;
    Cursor.list = list;
}

// FUNC_AT(0x000c75e0)
int WRender::GetNextPoint(Coord4 *sphere, float *height, bool *checkFar, float *farScale) {
    CachedDrawInfo *list = Cursor.list;
    if (list->nodeCount == 0)
        return kNoMorePoints;
    Cursor.item++;
    if (Cursor.item >= list->nodes[Cursor.node]->numInstances) {
        do {
            Cursor.node++;
            if (Cursor.node >= list->nodeCount)
                return kNoMorePoints;
        } while (list->nodes[Cursor.node]->numInstances == 0);
        Cursor.item = 0;
    }

    CARP::Instance *instance = &fgRender->instances[list->nodes[Cursor.node]->IndexList()[Cursor.item]];
    *height = float(instance->SizeY());
    *sphere = *PositionOf(instance);
    sphere->w = float(Dimension(instance->packedDimensions, 0));
    *checkFar = !(instance->flags & kInstanceNoFarCull);
    if (instance->flags & kInstance40) {
        *farScale = *checkFar ? 0.3f : 2.0f;
        *checkFar = true;
    } else {
        *farScale = 1.0f;
    }
    return int(uintptr_t(instance));
}

// FUNC_AT(0x000c7700)
void WRender::SetCull(int item, bool culled, float distance) {
    if (culled)
        return;
    CachedDrawInfo *list = Cursor.list;
    if (list->drawCount + 1 >= kMaxDraws)
        return;
    list->draws[list->drawCount].key = item;
    list->draws[list->drawCount].distance = distance;
    list->drawCount++;
}

// FUNC_AT(0x000c7750)
void WRender::CopyDrawPasses(const CachedDrawInfo *from, CachedDrawInfo *to, int firstPass, int lastPass) {
    to->drawCount = 0;
    for (int pass = lastPass;; pass--) {
        const CARP::Instance *first = passFirst[pass];
        const CARP::Instance *end = first + passCount[pass];
        for (int i = 0; i < from->drawCount; i++) {
            const CARP::Instance *instance = InstanceOf(from->draws[i]);
            if (instance >= first && instance < end) {
                to->draws[to->drawCount] = from->draws[i];
                to->drawCount++;
            }
        }
        if (pass == firstPass)
            break;
    }
}

// ---- construction

// FUNC_AT(0x000c7a50)
WRender* WRender::Construct() {
    CurrentList = &VisibleList;
    UGroup *mapGroup = fgWorld->group->GroupLocateTag(kTagMap);

    // the passes: each a run of the instances, found from the last instance to the first; one without an article
    // stays in the pass of the one after it
    UData *instanceData = mapGroup->DataLocateFirst(kTagInstances, 0, 0xffffffff);
    uint32_t count = instanceData->count;
    instances = reinterpret_cast<CARP::Instance *>(instanceData->Data());
    for (int pass = 0; pass < kPassCount; pass++) {
        passFirst[pass] = NULL;
        passCount[pass] = 0;
    }
    passFirst[kSkyPass] = instances;
    int pass = 0;
    for (uint32_t i = count; i > 0; i--) {
        CARP::Instance *instance = &instances[i - 1];
        const WorldArticle *article = ArticleOf(instance);
        if (article != NULL)
            pass = (instance->flags & kInstanceSky) ? kSkyPass : article->drawPass;
        passFirst[pass] = instance;
        passCount[pass]++;
    }
    // an empty pass starts where the next one ends
    for (int empty = kSkyPass - 1; empty >= 0; empty--) {
        if (passFirst[empty] == NULL) {
            passFirst[empty] = passFirst[empty + 1] + passCount[empty + 1];
            passCount[empty] = 0;
        }
    }

    UData *procAnimData = mapGroup->DataLocateFirst(kTagProcAnims, 0, 0xffffffff);
    if (procAnimData != mapGroup->DataEnd())
        procAnims = reinterpret_cast<ProcAnimState *>(procAnimData->Data());
    else
        procAnims = NULL;

    // the curtains: new WVisCurtain[count], each made from the record's two ends
    UData *curtainData = mapGroup->DataLocateFirst(kTagVisCurtains, -1, 0xffffffff);
    if (curtainData != mapGroup->DataEnd()) {
        const WVisCurtain *records = reinterpret_cast<const WVisCurtain *>(curtainData->Data());
        curtainCount = curtainData->count;
        WVisCurtain *block = static_cast<WVisCurtain *>(OperatorNewArray(curtainCount * sizeof(WVisCurtain)));
        if (block != NULL) {
            for (uint32_t i = 0; i < curtainCount; i++)
                block[i] = WVisCurtain();
        }
        curtains = block;
        for (uint32_t i = 0; i < curtainCount; i++)
            curtains[i].Construct(&records[i].start, &records[i].end);
    } else {
        curtains = NULL;
    }

    UData *shadowData = mapGroup->DataLocateTag(kTagShadows);
    if (shadowData != mapGroup->DataEnd() && fgLightManager->lightMapsEnabled)
        RInstanceRender_SetShadowInformation(reinterpret_cast<const ShadowMapInfo *>(shadowData->Data()));
    else
        RInstanceRender_SetDefaultShadowInformation();
    return this;
}

// FUNC_AT(0x000c7ce0)
void WRender::Init() {
    if (fgRender != NULL)
        return;
    WRender *render = static_cast<WRender *>(OperatorNew(sizeof(WRender)));
    fgRender = render != NULL ? render->Construct() : NULL;
}

// No destructor runs: the curtains stay allocated.
// FUNC_AT(0x000c7d50)
void WRender::ShutDown() {
    if (fgRender != NULL)
        OperatorDelete(fgRender);
    fgRender = NULL;
}

// ---- drawing

// FUNC_AT(0x000c8370)
void WRender::DrawPass(CachedDrawInfo *list, int firstPass, int lastPass, int sort) {
    if (sort == kSortByDistance)
        SortDraws(list, someComparisonOperator);
    else if (sort == kSortByInstance)
        SortDraws(list, aLessThanB);

    for (int pass = lastPass;; pass--) {
        const CARP::Instance *first = passFirst[pass];
        const CARP::Instance *end = first + passCount[pass];
        for (int i = 0; i < list->drawCount; i++) {
            CARP::Instance *instance = InstanceOf(list->draws[i]);
            if (instance >= first && instance < end)
                QuickDrawInstance(instance, procAnims);
        }
        if (pass == firstPass)
            break;
    }
}

// FUNC_AT(0x000c8450)
void WRender::DrawWorld(CachedDrawInfo *list, int sort) {
    CurrentList = &VisibleList;
    RSky::Draw(instances, false);
    DrawPass(list, 2, 3, sort);
}

// The sky with the positional lights cleared but for `ambient` in row 4, then every instance of the visible nodes
// near enough the eye, by address.
// FUNC_AT(0x000c8490)
void WRender::DrawWorldAtPoint(CachedDrawInfo *list, const Coord3 *eye, float radius, float ambient, Coord4 facing) {
    CurrentList = list;
    RPositionalLights *lights = fgLightManager->positionalLights;
    RPositionalLights saved = *lights;
    *lights = RPositionalLights();
    lights->colours[0].x = ambient;
    lights->colours[0].y = ambient;
    lights->colours[0].z = ambient;
    RSky::Draw(instances, false);
    *lights = saved;

    ViewCull.facing = facing;
    ViewCull.eye.x = eye->x;
    ViewCull.eye.y = eye->y;
    ViewCull.eye.z = eye->z;
    list->nodeCount = 0;
    ViewCull.radius = radius;
    WMapHeader *map = fgWorld->map;
    Coord4 box = RootBox(map);
    SimpleDrawVisibleTreeNodes(map->root, &box, true, 0);

    list->drawCount = 0;
    VisibleNodeCount = list->nodeCount;
    for (int n = 0; n < list->nodeCount; n++) {
        const WMapNode *node = list->nodes[n];
        const uint16_t *indices = node->IndexList();
        for (int i = 0; i < node->numInstances; i++) {
            CARP::Instance *instance = &instances[indices[i]];
            float height = float(SizeY(instance));
            float size = float(Dimension(instance->packedDimensions, 0));
            if (DistanceCheck2d(PositionOf(instance), size) == kOutside)
                continue;
            float distance = VU0_v3distancexz(&ViewCull.eye, instance->position);
            if (!(double(height) * 0.75 + size + 25.0 > distance))
                continue;
            if (instance->flags & kNotDrawnAtPoint)
                continue;
            list->draws[list->drawCount].key = uintptr_t(instance);
            list->draws[list->drawCount].distance = 0.0f;
            list->drawCount++;
        }
    }

    SortDraws(list, aLessThanB);
    for (int i = 0; i < list->drawCount; i++)
        QuickDrawInstance(InstanceOf(list->draws[i]), procAnims);
}

// ---- std::sort over the draws

namespace {

void Swap(WRenderSortEntry *a, WRenderSortEntry *b) {
    WRenderSortEntry swapped = *a;
    *a = *b;
    *b = swapped;
}

// std::rotate
void RotateRange(WRenderSortEntry *first, WRenderSortEntry *mid, WRenderSortEntry *last) {
    if (first != mid && mid != last)
        RenderSort::Rotate(first, mid, last);
}

constexpr int kSortMax = 32;        // _ISORT_MAX: shorter ranges get the insertion sort

}  // namespace

// FUNC_AT(0x000c7920)
void RenderSort::PushHeap(WRenderSortEntry *first, int hole, int top, WRenderSortEntry value, RenderSortPredicate pred) {
    for (int parent = (hole - 1) / 2; top < hole && pred(&first[parent], &value); parent = (hole - 1) / 2) {
        first[hole] = first[parent];
        hole = parent;
    }
    first[hole] = value;
}

// FUNC_AT(0x000c7990)
void RenderSort::Rotate(WRenderSortEntry *first, WRenderSortEntry *mid, WRenderSortEntry *last) {
    int shift = mid - first;
    int count = last - first;
    for (int factor = shift; factor != 0;) {    // count becomes the greatest common divisor
        int remainder = count % factor;
        count = factor;
        factor = remainder;
    }
    if (count < last - first) {
        for (; 0 < count; count--) {
            WRenderSortEntry *hole = first + count;
            WRenderSortEntry *next = hole;
            WRenderSortEntry holeValue = *hole;
            WRenderSortEntry *next1 = next + shift == last ? first : next + shift;
            while (next1 != hole) {
                *next = *next1;
                next = next1;
                next1 = shift < last - next1 ? next1 + shift : first + (shift - (last - next1));
            }
            *next = holeValue;
        }
    }
}

// FUNC_AT(0x000c7d70)
void RenderSort::Med3(WRenderSortEntry *first, WRenderSortEntry *mid, WRenderSortEntry *last, RenderSortPredicate pred) {
    if (pred(mid, first))
        Swap(mid, first);
    if (pred(last, mid))
        Swap(last, mid);
    if (pred(mid, first))
        Swap(mid, first);
}

// FUNC_AT(0x000c7df0)
void RenderSort::AdjustHeap(WRenderSortEntry *first, int hole, int bottom, WRenderSortEntry value, RenderSortPredicate pred) {
    int top = hole;
    int child = 2 * hole + 2;
    for (; child < bottom; child = 2 * child + 2) {
        if (pred(&first[child], &first[child - 1]))
            child--;
        first[hole] = first[child];
        hole = child;
    }
    if (child == bottom) {
        first[hole] = first[bottom - 1];
        hole = bottom - 1;
    }
    PushHeap(first, hole, top, value, pred);
}

// FUNC_AT(0x000c7e80)
void RenderSort::Median(WRenderSortEntry *first, WRenderSortEntry *mid, WRenderSortEntry *last, RenderSortPredicate pred) {
    if (40 < last - first) {
        int step = (last - first + 1) / 8;
        Med3(first, first + step, first + 2 * step, pred);
        Med3(mid - step, mid, mid + step, pred);
        Med3(last - 2 * step, last - step, last, pred);
        Med3(first + step, mid, last - step, pred);
    } else {
        Med3(first, mid, last, pred);
    }
}

// FUNC_AT(0x000c7f20)
void RenderSort::MakeHeap(WRenderSortEntry *first, WRenderSortEntry *last, RenderSortPredicate pred) {
    int bottom = last - first;
    for (int hole = bottom / 2; 0 < hole;) {
        hole--;
        AdjustHeap(first, hole, bottom, first[hole], pred);
    }
}

// FUNC_AT(0x000c7f70)
RenderSortRange* RenderSort::UnguardedPartition(RenderSortRange *result, WRenderSortEntry *first, WRenderSortEntry *last,
                                                RenderSortPredicate pred) {
    WRenderSortEntry *mid = first + (last - first) / 2;
    Median(first, mid, last - 1, pred);
    WRenderSortEntry *pfirst = mid;
    WRenderSortEntry *plast = pfirst + 1;
    while (first < pfirst && !pred(pfirst - 1, pfirst) && !pred(pfirst, pfirst - 1))
        pfirst--;
    while (plast < last && !pred(plast, pfirst) && !pred(pfirst, plast))
        plast++;

    WRenderSortEntry *gfirst = plast;
    WRenderSortEntry *glast = pfirst;
    for (;;) {
        for (; gfirst < last; gfirst++) {
            if (pred(pfirst, gfirst))
                ;
            else if (pred(gfirst, pfirst))
                break;
            else
                Swap(plast++, gfirst);
        }
        for (; first < glast; glast--) {
            if (pred(glast - 1, pfirst))
                ;
            else if (pred(pfirst, glast - 1))
                break;
            else
                Swap(--pfirst, glast - 1);
        }
        if (glast == first && gfirst == last) {
            result->first = pfirst;
            result->last = plast;
            return result;
        }

        if (glast == first) {
            if (plast != gfirst)
                Swap(pfirst, plast);
            plast++;
            Swap(pfirst++, gfirst++);
        } else if (gfirst == last) {
            if (--glast != --pfirst)
                Swap(glast, pfirst);
            Swap(pfirst, --plast);
        } else {
            Swap(gfirst++, --glast);
        }
    }
}

// FUNC_AT(0x000c8180)
void RenderSort::InsertionSort(WRenderSortEntry *first, WRenderSortEntry *last, RenderSortPredicate pred) {
    if (first == last)
        return;
    for (WRenderSortEntry *next = first; ++next != last;) {
        if (pred(next, first)) {
            RotateRange(first, next, next + 1);
        } else {
            WRenderSortEntry *dest = next;
            for (WRenderSortEntry *dest0 = dest; pred(next, --dest0);)
                dest = dest0;
            if (dest != next)
                RotateRange(dest, next, next + 1);
        }
    }
}

// FUNC_AT(0x000c8220)
void RenderSort::SortHeap(WRenderSortEntry *first, WRenderSortEntry *last, RenderSortPredicate pred) {
    for (; 1 < last - first; last--) {
        WRenderSortEntry value = last[-1];
        last[-1] = *first;
        AdjustHeap(first, 0, last - 1 - first, value, pred);
    }
}

// FUNC_AT(0x000c8280)
void RenderSort::Sort(WRenderSortEntry *first, WRenderSortEntry *last, int ideal, RenderSortPredicate pred) {
    int count;
    while (kSortMax < (count = last - first) && 0 < ideal) {
        RenderSortRange mid;
        UnguardedPartition(&mid, first, last, pred);
        ideal /= 2;
        ideal += ideal / 2;
        if (mid.first - first < last - mid.last) {
            Sort(first, mid.first, ideal, pred);
            first = mid.last;
        } else {
            Sort(mid.last, last, ideal, pred);
            last = mid.first;
        }
    }
    if (kSortMax < count) {
        if (1 < count)
            MakeHeap(first, last, pred);
        SortHeap(first, last, pred);
    } else if (1 < count) {
        InsertionSort(first, last, pred);
    }
}
