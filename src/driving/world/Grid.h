#ifndef DRIVING_WORLD_GRID_H_
#define DRIVING_WORLD_GRID_H_

#include <stddef.h>
#include <stdint.h>

#include "CollisionInstance.h"
#include "CollisionTypes.h"

// ---------------------------------------------------------------------------------------------------------------
// WGrid: the collision system's spatial grid over the track (the CARP collision data's 'CGrd' record). Square
// cells in the ground plane, numbered row * columns + column (rows along z, columns along x). Each cell that holds
// anything has a node in the CARP data ('cn  ' records): the static things in the cell by type, and a list of the
// moving things currently in it (made at run time). The moving things are the track's dynamic elements ('de  '
// records: collision instances and objects that move with their render instances); every frame
// UpdateDynamicNodes moves those that moved from their old cells to their new ones. See Grid.cpp.
//
// The searches answer cell numbers in a vector: those a box or circle covers, or those a segment crosses. The cells'
// records and lists are CollisionTypes.h's.
// ---------------------------------------------------------------------------------------------------------------

// A dynamic element (0x2c): a collision instance or object whose render instance moves. The grid keeps it in the
// cells round where it last was.
struct WGridMover {
    const Coord4 *position;        // +0x00 its render instance's translation
    Coord4 lastPosition;           // +0x04 where the grid has it; w its radius
    uint16_t index;                // +0x14
    uint16_t unknown16;            // (the original leaves its stack's bytes here)
    uint32_t type;                 // +0x18 WGridElementType
    const Coord4 *objectSource;    // +0x1c an object: what its position follows
    WCollisionObject *object;      // +0x20
    const MATRIX4 *matrix;         // +0x24 an instance: its render instance
    WCollisionInstance *instance;  // +0x28

    void Update();                                                           // 0x000c6fd0
};
static_assert(sizeof(WGridMover) == 0x2c, "a dynamic element is 0x2c bytes");

struct WGridMoverNode {
    WGridMoverNode *next;
    WGridMoverNode *prev;
    WGridMover value;
};
static_assert(sizeof(WGridMoverNode) == 0x34, "the dynamic elements' node is 0x34 bytes");

// The dynamic elements (a static std::list at 0x0023b3ac)
struct WGridMoverList {
    uint32_t allocator;
    WGridMoverNode *head;
    uint32_t size;

    WGridMoverNode* BuyNode(WGridMoverNode *next, WGridMoverNode *prev, const WGridMover *value);   // 0x000c58c0
    WGridMoverNode* BuyHead();                                               // 0x000c5990
    void IncreaseSize(uint32_t count);                                       // 0x000c5b30
};

// The 'CGrd' record
struct WGridRecord {
    Coord4 origin;
    float cellSize;
    uint32_t unknown14;
    uint32_t rows;
    uint32_t columns;
};

class WGrid {
public:
    Coord4 origin;                 // +0x00 the low x, low z corner
    float cellSize;                // +0x10
    float inverseCellSize;         // +0x14
    uint32_t rows;                 // +0x18 along z
    uint32_t columns;              // +0x1c along x
    WGridNode **nodes;             // +0x20 by cell

    WGrid* Construct(const Coord4 *origin, uint32_t rows, uint32_t columns, float cellSize);   // 0x000c57b0
    // The cell under the point, clamped into the grid
    void RangeCheckROWCOL(const Coord4 *point, uint32_t *row, uint32_t *column);             // 0x000c5710
    // The cells a box covers (its two corners in `box`; at most 21 by 21, else one row or column)
    void FindNodesBox(const Coord4 *box, WGridCellList *cells);                              // 0x000c62b0
    // The cells the square round a circle covers
    void FindNodes(const Coord3 *centre, float radius, WGridCellList *cells);                // 0x000c6450
    // The cells a segment crosses, in order (clipped to the grid; past GridMaxSteps cells, the start's square)
    void FindNodes(const Coord4 *segment, WGridCellList *cells);                             // 0x000c64b0

    static void Init(UGroup *carp);                                                          // 0x000c5fc0
    static void Shutdown();                                                                  // 0x000c59f0
    static void Restart();                                                                   // 0x000c6290
    // Moves an entry from the cells round `last` to those round `position` (radii in w)
    static void AddGridNodeDynamicElement(Coord4 *last, const Coord4 *position, uint32_t type, uint32_t index);   // 0x000c6d70
    static void UpdateDynamicNodes();                                                        // 0x000c7200
};
static_assert(sizeof(WGrid) == 0x24, "the grid is 0x24 bytes");

#define TheGrid (*(WGrid **)0x0023b3a0)
#define GridMovers (*(WGridMoverList *)0x0023b3ac)

#endif // DRIVING_WORLD_GRID_H_
