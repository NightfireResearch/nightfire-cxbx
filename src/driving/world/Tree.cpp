#pragma fp_contract(off)

#include "Tree.h"

#include "../platform/RealMath.h"

// ---------------------------------------------------------------------------------------------------------------
// WTree (0x000cedd0..0x000cf1f0) and the renderer's helpers DistanceCheck2d, aLessThanB and
// someComparisonOperator (0x000c7230..0x000c7330), ported from the listings. The comparisons keep the original's
// unordered (NaN) behaviour; the arithmetic is the x87's, in double, in the original's order.
// ---------------------------------------------------------------------------------------------------------------

#define TreeLevelScale ((const float *)0x001ca36c)   // a node's half-width over the root's, by depth: 1, 1/2, 1/4...
#define ViewCull (*(const WViewCull *)0x0023dfd0)

namespace {

constexpr float kHalf = 0.5f;

// Whether [low, high] lies in the low half of the box (from its edge to its centre), in one axis
bool InLowHalf(double low, double high, float centre, float halfWidth) {
    return (low - (double(centre) - halfWidth)) * (double(centre) - high) >= 0.0;
}

// ... or in the high half (from its centre to its edge)
bool InHighHalf(double low, double high, float centre, float halfWidth) {
    return ((double(halfWidth) + centre) - high) * (low - centre) >= 0.0;
}

}  // namespace

// FUNC_AT(0x000cedd0)
WMapNode* WTree::FindNode(WMapNode *node, const Coord4 *box, const Coord4 *circle) {
    WMapNode *children = node->children;
    if (children == NULL)
        return node;
    float quarter = box->w * kHalf;
    double lowX = double(circle->x) - circle->w;
    double highX = double(circle->w) + circle->x;
    double lowZ = double(circle->z) - circle->w;
    double highZ = double(circle->w) + circle->z;
    Coord4 child;
    child.y = 0.0f;
    child.w = quarter;
    if (InLowHalf(lowX, highX, box->x, box->w)) {
        child.x = box->x - quarter;
        if (InLowHalf(lowZ, highZ, box->z, box->w)) {
            child.z = box->z - quarter;
            return FindNode(&children[0], &child, circle);
        }
        if (!InHighHalf(lowZ, highZ, box->z, box->w))
            return node;
        child.z = quarter + box->z;
        return FindNode(&children[2], &child, circle);
    }
    if (!InHighHalf(lowX, highX, box->x, box->w))
        return node;
    child.x = quarter + box->x;
    if (InLowHalf(lowZ, highZ, box->z, box->w)) {
        child.z = box->z - quarter;
        return FindNode(&children[1], &child, circle);
    }
    if (!InHighHalf(lowZ, highZ, box->z, box->w))
        return node;
    child.z = quarter + box->z;
    return FindNode(&children[3], &child, circle);
}

// The node's square from its depth and cell: the root's corner plus (2 cell + 1) half-widths
// FUNC_AT(0x000cefe0)
bool WTree::CheckContainment(WMapNode *node, const Coord4 *box, const Coord4 *circle) {
    float halfWidth = TreeLevelScale[node->depth] * box->w;
    double centreX = (double(node->cellX) + node->cellX + 1.0) * halfWidth + box->x - box->w;
    float centreZ = float((double(node->cellZ) + node->cellZ + 1.0) * halfWidth + box->z - box->w);
    return centreX - halfWidth <= double(circle->x) - circle->w &&
           centreX + halfWidth >= double(circle->w) + circle->x &&
           double(centreZ) - halfWidth <= double(circle->z) - circle->w &&
           double(centreZ) + halfWidth >= double(circle->w) + circle->z;
}

// FUNC_AT(0x000cf0b0)
WMapNode* WTree::FindNode(WMapNode *root, WMapNode *current, const Coord4 *box, const Coord4 *circle) {
    if (CheckContainment(current, box, circle))
        return current;
    return FindNode(root, box, circle);
}

// FUNC_AT(0x000cf0f0)
void WTree::InitializeTree(WMapNode *node, int depth, uint8_t cellX, uint8_t cellZ) {
    node->depth = uint8_t(depth);
    node->cellX = cellX;
    node->cellZ = cellZ;
    node->firstSceneObj = NULL;
    if (node->children == NULL)
        return;
    InitializeTree(&node->children[0], depth + 1, uint8_t(cellX * 2), uint8_t(cellZ * 2));
    InitializeTree(&node->children[1], depth + 1, uint8_t(cellX * 2 + 1), uint8_t(cellZ * 2));
    InitializeTree(&node->children[2], depth + 1, uint8_t(cellX * 2), uint8_t(cellZ * 2 + 1));
    InitializeTree(&node->children[3], depth + 1, uint8_t(cellX * 2 + 1), uint8_t(cellZ * 2 + 1));
}

// FUNC_AT(0x000cf1a0)
void WTree::WalkTree(WMapNode *node, MapNodeCallback callback) {
    callback(node);
    if (node->children == NULL)
        return;
    WalkTree(&node->children[0], callback);
    WalkTree(&node->children[1], callback);
    WalkTree(&node->children[2], callback);
    WalkTree(&node->children[3], callback);
}

// ---- the renderer's helpers

// The circle's distance from the eye (less the view's radius) against its radius, and how far it is ahead
// FUNC_AT(0x000c7270)
int DistanceCheck2d(const Coord4 *position, float radius) {
    alignas(16) Coord4 offset;
    VU0_v4sub(position, &ViewCull.eye, &offset);
    float ahead = float(double(ViewCull.facing.z) * offset.z + double(ViewCull.facing.x) * offset.x);
    double distance = double(VU0_v3lengthxz(&offset)) - ViewCull.radius;
    int where;
    if (distance > 0.0) {
        if (distance > radius)
            return 0;
        where = 2;
    } else {
        where = distance > -radius ? 2 : 1;
    }
    if (ahead > radius)
        return where;
    return double(ahead) + radius > 0.0 ? 2 : 0;
}

// FUNC_AT(0x000c7230)
bool aLessThanB(const WRenderSortEntry *a, const WRenderSortEntry *b) {
    return a->key < b->key;
}

// FUNC_AT(0x000c7250)
bool someComparisonOperator(const WRenderSortEntry *a, const WRenderSortEntry *b) {
    return a->distance > b->distance;
}
