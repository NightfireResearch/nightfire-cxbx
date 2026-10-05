#ifndef DRIVING_WORLD_TREE_H_
#define DRIVING_WORLD_TREE_H_

#include <stddef.h>
#include <stdint.h>

#include "CollisionInstance.h"

// ---------------------------------------------------------------------------------------------------------------
// WTree: the world's scene quadtree (the CARP map's nodes), which the scene objects file themselves under - the
// smallest node whose square holds their bounding circle - and which the renderer walks. And the renderer's small
// visibility helpers beside it in the binary (DistanceCheck2d and the two sort predicates). See Tree.cpp.
//
// A node's square: the root's is the map's (WMapHeader), a child's a quarter of its parent's. A box is passed as
// a Coord4: the square's centre in x and z, and its half-width in w; a circle likewise, its radius in w.
// ---------------------------------------------------------------------------------------------------------------

class RSceneObj;

// A node of the tree (CARP::MapNode, 16 bytes): its four children (an array, or none for a leaf) and the first of
// the scene objects filed under it. Children: 0 low x low z, 1 high x low z, 2 low x high z, 3 high x high z.
struct WMapNode {
    WMapNode *children;            // +0x00
    RSceneObj *firstSceneObj;      // +0x04
    uint16_t curtainListOffset;    // +0x08
    uint16_t curtainListStart;     // +0x0a
    uint8_t numCurtains;           // +0x0c
    uint8_t depth;                 // +0x0d the root is 0
    uint8_t cellX;                 // +0x0e the node's column and row among the nodes of its depth
    uint8_t cellZ;                 // +0x0f
};
static_assert(sizeof(WMapNode) == 0x10, "a map node is 16 bytes");

// The map's header (fgWorld's +0x1c): the root, and the map's square (only the fields read here)
struct WMapHeader {
    WMapNode *root;                // +0x00
    uint8_t unknown04[0x18];
    Coord3 corner;                 // +0x1c the low x, low z corner
    uint32_t unknown28;
    float size;                    // +0x2c the side
};
static_assert(offsetof(WMapHeader, size) == 0x2c, "WMapHeader::size");

typedef void (*MapNodeCallback)(WMapNode *node);

class WTree {
public:
    // The smallest node under `node` whose square holds the circle (`box` is node's square)
    static WMapNode* FindNode(WMapNode *node, const Coord4 *box, const Coord4 *circle);                    // 0x000cedd0
    // Whether the circle is inside `node`'s square (`box` the root's)
    static bool CheckContainment(WMapNode *node, const Coord4 *box, const Coord4 *circle);                 // 0x000cefe0
    // `current` if the circle is still inside it, else the search from the root
    static WMapNode* FindNode(WMapNode *root, WMapNode *current, const Coord4 *box, const Coord4 *circle);  // 0x000cf0b0
    // Sets each node's depth and cell, and empties its scene object list
    static void InitializeTree(WMapNode *node, int depth, uint8_t cellX, uint8_t cellZ);                  // 0x000cf0f0
    // The callback on every node, parents before their children
    static void WalkTree(WMapNode *node, MapNodeCallback callback);                                       // 0x000cf1a0
};

// The renderer's view for its culling (0x0023dfd0)
struct WViewCull {
    float radius;                  // +0x00
    uint8_t unknown04[0xc];
    Coord4 eye;                    // +0x10
    Coord4 facing;                 // +0x20 the view direction in the ground plane (x, z)
};
static_assert(sizeof(WViewCull) == 0x30, "the renderer's view block");

// Where a circle is against the view: 0 out of it, 1 inside its radius, 2 across an edge
int DistanceCheck2d(const Coord4 *position, float radius);                                                // 0x000c7270

// An entry of the renderer's sorted lists: a key, and a distance
struct WRenderSortEntry {
    uint32_t key;
    float distance;
};

// The render lists' sort predicates: by key (unsigned), and by distance, farthest first
bool aLessThanB(const WRenderSortEntry *a, const WRenderSortEntry *b);                                   // 0x000c7230
bool someComparisonOperator(const WRenderSortEntry *a, const WRenderSortEntry *b);                       // 0x000c7250

#endif // DRIVING_WORLD_TREE_H_
