#pragma fp_contract(off)

#include "Grid.h"

#include "CollisionManager.h"

#include "../engine/CoreFoundation.h"
#include "../engine/UMemory.hpp"
#include "../platform/RealMath.h"
#include "../platform/X87.h"
#include "../../helpers.h"

// ---------------------------------------------------------------------------------------------------------------
// WGrid, its cells' lists and the dynamic elements (0x000c5710..0x000c7230), ported from the listings.
//
// The lists are Dinkumware's std::list as the game compiled it: a sentinel node from FastAlloc, nodes linked
// before it on insertion. The cell vector's helpers are ColStl's (CollisionInstance.h). The C runtime's ceil and
// floor are called at their addresses (they set the x87's rounding mode).
// ---------------------------------------------------------------------------------------------------------------

// ---- originals called by address

#define MoverList_Erase ((WGridMoverNode **(__fastcall *)(WGridMoverList *, int, WGridMoverNode **, WGridMoverNode *, WGridMoverNode *))0x00037160)
#define CellVector_Xlen ((void (__fastcall *)(void *, int))0x00034e50)
#define CRT_ceil ((double (*)(double))0x00133040)
#define CRT_floor ((double (*)(double))0x0013310e)

// ---- the game's state

#define GridData (*(UGroup **)0x0023b3a4)                 // the CARP group the grid came from (for Restart)
#define GridMaxSteps I32_AT(0x001c9fec)                   // the most cells a segment search walks (100)
#define DefaultVector (*(const Coord4 *)0x001d4c00)       // (0, 0, 0, 1)

namespace {

enum : uint32_t {
    kTagCollisionData = 0x43446174,   // 'CDat'
    kTagGrid = 0x43477264,            // 'CGrd'
    kTagGridNode = 0x636e2020,        // 'cn  ', indexed by cell
    kTagElement = 0x64652020,         // 'de  ', a dynamic element
    kTagElementIndexed = 0x64650000,
};

// A dynamic element's record
struct WGridElementRecord {
    uint16_t index;
    uint16_t unknown02;
    uint32_t type;
};

const uint32_t kMaxCells = 0x3fffffff;          // max_size of a vector of words
const uint32_t kMaxCellEntries = 0x1fffffff;    // and of a list of 8-byte entries
const uint32_t kMaxMovers = 0x5d1745d;          // and of 0x2c-byte ones
const uint32_t kMaxBoxSpan = 20;                // a box wider than this many cells shrinks to one row or column
constexpr float kMinDelta = 0.05f;              // the least a segment's run in x or z counts as
constexpr float kEdgeInset = 0.1f;              // a segment clipped to the grid stops this far inside
constexpr float kTinyStep = 0.0001f;
constexpr float kNoCrossing = 10000.0f;
constexpr float kMoveThreshold = 0.01f;         // a dynamic element moving less stays in its cells

// A row or column brought back into [0, count): past the end, the last one; negative (as unsigned), the first
uint32_t Clamp(uint32_t value, uint32_t count) {
    if (value >= count)
        value = value < 0x7fffffff ? count - 1 : 0;
    return value;
}

// The node of a cell, if anything is in it
WGridNode *CellNode(const WGrid *grid, uint32_t cell) {
    uint32_t row = cell / grid->columns;
    uint32_t column = Clamp(cell - grid->columns * row, grid->columns);
    row = Clamp(row, grid->rows);
    return grid->nodes[grid->columns * row + column];
}

// Whether a static entry of the type and number is in the cell
bool HasStatic(const WGridNode *node, uint32_t type, uint32_t index) {
    const uint16_t *indices = node->StaticIndices(type);
    for (int i = 0; i < node->staticCount[type]; i++)
        if (indices[i] == index)
            return true;
    return false;
}

template <class List, class Node>
void Unlink(List *list, Node *node, uint32_t bytes) {
    node->prev->next = node->next;
    node->next->prev = node->prev;
    UMemory::FastFree(node, bytes);
    list->size--;
}

void AppendMover(const WGridMover &mover) {
    WGridMoverNode *head = GridMovers.head;
    WGridMoverNode *node = GridMovers.BuyNode(head, head->prev, &mover);
    GridMovers.IncreaseSize(1);
    head->prev = node;
    node->prev->next = node;
}

void ClearMovers() {
    WGridMoverNode *head = GridMovers.head;
    WGridMoverNode *ignored;
    MoverList_Erase(&GridMovers, 0, &ignored, head == NULL ? NULL : head->next, head);
}

// Moved further than the threshold along an axis (an unordered difference has not)
bool Beyond(double difference) {
    return (difference < 0.0 ? -difference : difference) > kMoveThreshold;
}

}  // namespace

// ---- the cell vector

// FUNC_AT(0x000c5be0)
void WGridCellList::InsertN(uint32_t *where, uint32_t count, const uint32_t *value) {
    ColStl::InsertN(this, where, count, value, kMaxCells, [this] {
        WORLD_UNTESTED("WGridCellList's _Xlen");
        CellVector_Xlen(this, 0);
    });
}

// FUNC_AT(0x000c5ea0)
void WGridCellList::PushBack(const uint32_t *value) {
    ColStl::PushBack(this, value, [this](uint32_t *where, uint32_t count, const uint32_t *what) {
        InsertN(where, count, what);
    });
}

// ---- a cell's list of moving things

// FUNC_AT(0x000c5830)
WGridDynamicNode** WGridDynamicList::Erase(WGridDynamicNode **result, WGridDynamicNode *first, WGridDynamicNode *last) {
    while (first != last) {
        WGridDynamicNode *node = first;
        first = first->next;
        if (node != head)
            Unlink(this, node, sizeof(WGridDynamicNode));
    }
    *result = first;
    return result;
}

// FUNC_AT(0x000c5880)
WGridDynamicNode* WGridDynamicList::BuyNode(WGridDynamicNode *next, WGridDynamicNode *prev, const WGridDynamicEntry *value) {
    WGridDynamicNode *node = static_cast<WGridDynamicNode *>(UMemory::FastAlloc(sizeof(WGridDynamicNode), "STL"));
    if (node != NULL) {
        node->next = next;
        node->prev = prev;
        node->value = *value;
    }
    return node;
}

// FUNC_AT(0x000c5970)
WGridDynamicNode* WGridDynamicList::BuyHead() {
    WGridDynamicNode *node = static_cast<WGridDynamicNode *>(UMemory::FastAlloc(sizeof(WGridDynamicNode), "STL"));
    if (node != NULL)
        node->next = node;
    node->prev = node;   // the original tests &node->prev, which is never NULL
    return node;
}

// FUNC_AT(0x000c59b0)
void WGridDynamicList::Destruct() {
    WGridDynamicNode *ignored;
    Erase(&ignored, head == NULL ? NULL : head->next, head);
    if (head != NULL)
        UMemory::FastFree(head, sizeof(WGridDynamicNode));
    head = NULL;
    size = 0;
}

// FUNC_AT(0x000c5a80)
void WGridDynamicList::IncreaseSize(uint32_t count) {
    if (kMaxCellEntries - size < count) {
        WORLD_UNTESTED("WGridDynamicList::IncreaseSize's length error");
        ThrowLengthError("list<T> too long");
        return;
    }
    size += count;
}

// FUNC_AT(0x000c5900)
void WGridNode::RemoveDynamic(uint32_t index, uint32_t type) {
    WGridDynamicList *list = dynamic;
    if (list == NULL)
        return;
    WGridDynamicNode *head = list->head;
    for (WGridDynamicNode *entry = head == NULL ? NULL : head->next; entry != head; entry = entry->next) {
        if (entry->value.index == index && entry->value.type == type) {
            if (entry != list->head)
                Unlink(list, entry, sizeof(WGridDynamicNode));
            return;
        }
    }
}

// FUNC_AT(0x000c5f10)
void WGridNode::AddDynamic(uint16_t index, uint32_t type) {
    if (dynamic == NULL) {
        WGridDynamicList *made = static_cast<WGridDynamicList *>(OperatorNew(sizeof(WGridDynamicList)));
        if (made != NULL) {
            made->head = made->BuyHead();
            made->size = 0;
        }
        dynamic = made;
    }
    WGridDynamicList *list = dynamic;
    WGridDynamicNode *head = list->head;
    WGridDynamicEntry entry = { index, 0, type };
    WGridDynamicNode *node = list->BuyNode(head, head->prev, &entry);
    list->IncreaseSize(1);
    head->prev = node;
    node->prev->next = node;
}

// ---- the dynamic elements' list

// FUNC_AT(0x000c58c0)
WGridMoverNode* WGridMoverList::BuyNode(WGridMoverNode *next, WGridMoverNode *prev, const WGridMover *value) {
    WGridMoverNode *node = static_cast<WGridMoverNode *>(UMemory::FastAlloc(sizeof(WGridMoverNode), "STL"));
    if (node != NULL) {
        node->next = next;
        node->prev = prev;
        node->value = *value;
    }
    return node;
}

// FUNC_AT(0x000c5990)
WGridMoverNode* WGridMoverList::BuyHead() {
    WGridMoverNode *node = static_cast<WGridMoverNode *>(UMemory::FastAlloc(sizeof(WGridMoverNode), "STL"));
    if (node != NULL)
        node->next = node;
    node->prev = node;   // the original tests &node->prev, which is never NULL
    return node;
}

// FUNC_AT(0x000c5b30)
void WGridMoverList::IncreaseSize(uint32_t count) {
    if (kMaxMovers - size < count) {
        WORLD_UNTESTED("WGridMoverList::IncreaseSize's length error");
        ThrowLengthError("list<T> too long");
        return;
    }
    size += count;
}

// ---- the grid

// FUNC_AT(0x000c57b0)
WGrid* WGrid::Construct(const Coord4 *origin, uint32_t rows, uint32_t columns, float cellSize) {
    float inverse = 1.0f / cellSize;
    this->origin = *origin;
    this->rows = rows;
    this->cellSize = cellSize;
    this->columns = columns;
    inverseCellSize = inverse;
    int count = int(rows * columns);
    nodes = static_cast<WGridNode **>(UMemory::Alloc(count * sizeof(WGridNode *), 0, "WGrid Nodes"));
    for (int i = 0; i < count; i++)
        nodes[i] = NULL;
    return this;
}

// FUNC_AT(0x000c5710)
void WGrid::RangeCheckROWCOL(const Coord4 *point, uint32_t *row, uint32_t *column) {
    *column = Truncate(float((double(point->x) - origin.x) * inverseCellSize));
    *row = Truncate(float((double(point->z) - origin.z) * inverseCellSize));
    *column = Clamp(*column, columns);
    *row = Clamp(*row, rows);
}

// FUNC_AT(0x000c62b0)
void WGrid::FindNodesBox(const Coord4 *box, WGridCellList *cells) {
    uint32_t row0, column0, row1, column1;
    RangeCheckROWCOL(&box[0], &row0, &column0);
    RangeCheckROWCOL(&box[1], &row1, &column1);
    if (row0 > row1) {
        uint32_t swap = row0;
        row0 = row1;
        row1 = swap;
    }
    if (column0 > column1) {
        uint32_t swap = column0;
        column0 = column1;
        column1 = swap;
    }
    if (row1 - row0 > kMaxBoxSpan)
        row1 = row0;
    if (column1 - column0 > kMaxBoxSpan)
        column1 = column0;
    column0 = Clamp(column0, columns);
    row0 = Clamp(row0, rows);
    column1 = Clamp(column1, columns);
    row1 = Clamp(row1, rows);
    for (uint32_t column = column0; column <= column1; column++) {
        for (uint32_t row = row0; row <= row1; row++) {
            uint32_t cell = columns * row + column;
            cells->PushBack(&cell);
        }
    }
}

// FUNC_AT(0x000c6450)
void WGrid::FindNodes(const Coord3 *centre, float radius, WGridCellList *cells) {
    Coord4 box[2];
    box[0].x = centre->x - radius;
    box[0].y = 0.0f;
    box[0].z = centre->z - radius;
    box[1].x = radius + centre->x;
    box[1].y = 0.0f;
    box[1].z = radius + centre->z;
    FindNodesBox(box, cells);
}

// The segment's cells by stepping from boundary to boundary (a 2D DDA). An end outside the grid is first moved
// back along the segment to just inside its edge; both ends outside, nothing. The boundaries are found from the
// world origin (ceil or floor of position / cell size), the cells from the grid's.
// FUNC_AT(0x000c64b0)
void WGrid::FindNodes(const Coord4 *segment, WGridCellList *cells) {
    // The cell of a coordinate: the original truncates the offset before scaling it
    auto cellOf = [this](double value, float corner) {
        return Truncate(float(double(Truncate(float(value - corner))) * inverseCellSize));
    };
    float startX = segment[0].x;
    float startZ = segment[0].z;
    float endX = segment[1].x;
    float endZ = segment[1].z;
    float dx = endX - startX;
    float dz = endZ - startZ;
    int column0 = cellOf(startX, origin.x);
    int row0 = cellOf(startZ, origin.z);
    bool startOutside = !(column0 >= 0 && column0 < int(columns) && row0 >= 0 && row0 < int(rows));
    int column1 = cellOf(endX, origin.x);
    int row1 = cellOf(endZ, origin.z);
    bool endOutside = !(column1 >= 0 && column1 < int(columns) && row1 >= 0 && row1 < int(rows));
    if (fabsf(dx) < kMinDelta)
        dx = kMinDelta;
    if (fabsf(dz) < kMinDelta)
        dz = kMinDelta;

    if (startOutside) {
        if (endOutside)
            return;
        float backX = -dx;
        float backZ = -dz;
        float edgeX = backX > 0.0f ? float(double(columns) * cellSize + origin.x - kEdgeInset) : origin.x + kEdgeInset;
        double edgeZ = backZ > 0.0f ? double(rows) * cellSize + origin.z - kEdgeInset : double(origin.z) + kEdgeInset;
        double toEdgeX = (double(edgeX) - endX) / backX;
        float toEdgeZ = float((edgeZ - endZ) / backZ);
        if (toEdgeX < toEdgeZ) {
            startX = edgeX;
            startZ = float(double(float(toEdgeX)) * backZ + endZ);
        } else {
            startX = float(double(toEdgeZ) * backX + endX);
            startZ = float(edgeZ);
        }
        column0 = cellOf(startX, origin.x);
        row0 = cellOf(startZ, origin.z);
    } else if (endOutside) {
        double edgeX = dx > 0.0f ? double(columns) * cellSize + origin.x - kEdgeInset : double(origin.x) + kEdgeInset;
        float edgeZ = dz > 0.0f ? float(double(rows) * cellSize + origin.z - kEdgeInset) : origin.z + kEdgeInset;
        double toEdgeX = (edgeX - startX) / dx;
        float toEdgeZ = float((double(edgeZ) - startZ) / dz);
        if (toEdgeX < toEdgeZ) {
            endZ = float(double(float(toEdgeX)) * dz + startZ);
            column1 = cellOf(edgeX, origin.x);   // from the unrounded edge
        } else {
            endZ = edgeZ;
            column1 = cellOf(double(toEdgeZ) * dx + startX, origin.x);
        }
        row1 = cellOf(endZ, origin.z);
    }

    uint32_t cell = columns * row0 + column0;
    cells->PushBack(&cell);
    int steps = 1;
    float length = VU0_sqrt(float(double(dz) * dz + double(dx) * dx));
    if (length <= cellSize && (column0 == column1 || row0 == row1)) {
        // A short segment: its end's cell, if not the start's
        if (column0 == column1 && row0 == row1)
            return;
        cell = columns * row1 + column1;
        cells->PushBack(&cell);
        return;
    }

    double inverseLength = 1.0 / length;
    float stepX = float(dx * inverseLength);
    double stepZ = inverseLength * dz;
    float stepZRounded = float(stepZ);
    float crossX = fabsf(stepX) >= kTinyStep ? float(1.0 / stepX) : kNoCrossing;    // per unit of x, of the run
    float crossZ = fabs(stepZ) >= kTinyStep ? float(1.0 / stepZ) : kNoCrossing;
    bool increasingX = stepX >= 0.0f;
    bool increasingZ = stepZ >= 0.0;
    float nextX = float((increasingX ? CRT_ceil : CRT_floor)(double(startX) * inverseCellSize) * cellSize);
    float nextZ = float((increasingZ ? CRT_ceil : CRT_floor)(double(startZ) * inverseCellSize) * cellSize);
    float x = startX;
    float z = startZ;
    int column = column0;
    int row = row0;

    // Too many cells: the start's square instead
    auto overflow = [&] {
        ColStl::Tidy(cells);   // the vector's _Tidy (0x0004f400)
        TheGrid->FindNodes(reinterpret_cast<const Coord3 *>(&segment[0]), 1.0f, cells);
    };

    while (column != column1) {
        if (row == row1) {
            // On the end's row: along it to the end's column
            int direction = increasingX ? 1 : -1;
            for (column += direction; increasingX ? column <= column1 : column >= column1; column += direction) {
                cell = columns * row + column;
                cells->PushBack(&cell);
                if (++steps > GridMaxSteps) {
                    overflow();
                    return;
                }
            }
            return;
        }
        double toX = (double(nextX) - x) * crossX;
        float toZ = float((double(nextZ) - z) * crossZ);
        if (toX < toZ) {
            z = float(toX * stepZRounded + z);
            x = nextX;
            if (increasingX) {
                nextX = nextX + cellSize;
                column++;
            } else {
                nextX = nextX - cellSize;
                column--;
            }
        } else {
            z = nextZ;
            x = float(double(toZ) * stepX + x);
            if (increasingZ) {
                nextZ = nextZ + cellSize;
                row++;
            } else {
                nextZ = nextZ - cellSize;
                row--;
            }
        }
        cell = columns * row + column;
        cells->PushBack(&cell);
        if (++steps > GridMaxSteps) {
            overflow();
            return;
        }
    }

    // On the end's column: along it to the end's row
    if (row == row1)
        return;
    int direction = increasingZ ? 1 : -1;
    for (row += direction; increasingZ ? row <= row1 : row >= row1; row += direction) {
        cell = columns * row + column;
        cells->PushBack(&cell);
        if (++steps > GridMaxSteps) {
            overflow();
            return;
        }
    }
}

// FUNC_AT(0x000c59f0)
void WGrid::Shutdown() {
    ClearMovers();
    for (int i = 0; i < int(TheGrid->columns * TheGrid->rows); i++) {
        WGridNode *node = TheGrid->nodes[i];
        if (node != NULL) {
            WGridDynamicList *list = node->dynamic;
            if (list != NULL) {
                list->Destruct();
                OperatorDelete(list);
            }
            node->dynamic = NULL;
        }
    }
    WGrid *grid = TheGrid;
    UMemory::Free(grid->nodes);
    UMemory::Free(grid);
}

// FUNC_AT(0x000c5fc0)
void WGrid::Init(UGroup *carp) {
    GridData = carp;
    UGroup *collision = carp->GroupLocateTag(kTagCollisionData);
    const WGridRecord *record = reinterpret_cast<const WGridRecord *>(collision->DataLocateTag(kTagGrid)->Data());
    WGrid *grid = static_cast<WGrid *>(UMemory::Alloc(sizeof(WGrid), 0, "WGrid"));
    TheGrid = grid != NULL ? grid->Construct(&record->origin, record->rows, record->columns, record->cellSize) : NULL;

    // The cells' nodes
    uint32_t nodeCount = collision->DataCountType(kTagGridNode);
    UData *item = collision->DataLocateFirst(kTagGridNode, 0, 0xffffffff);
    for (; nodeCount != 0; nodeCount--) {
        if (item != collision->DataEnd()) {
            TheGrid->nodes[item->count] = reinterpret_cast<WGridNode *>(item->Data());
            item++;
        }
    }

    // The dynamic elements
    ClearMovers();
    for (uint32_t i = 0; i < uint32_t(collision->DataCountType(kTagElement)); i++) {
        uint32_t tag = i == 0xffffffff ? kTagElement : i | kTagElementIndexed;
        const WGridElementRecord *element =
            reinterpret_cast<const WGridElementRecord *>(collision->DataLocateTag(tag)->Data());
        uint16_t index = element->index;
        WGridMover mover;
        if (element->type == kGridInstance) {
            WCollisionInstance *instance = &fgCollisionMgr->instances[index];
            MATRIX4 *placed = instance->GetRenderInstance();
            mover.position = MatrixRow(placed, 3);
            mover.lastPosition = DefaultVector;
            mover.index = index;
            mover.unknown16 = 0;
            mover.type = kGridInstance;
            mover.objectSource = NULL;
            mover.object = NULL;
            mover.matrix = placed;
            mover.instance = instance;
            AppendMover(mover);
        } else if (element->type == kGridObject) {
            WCollisionObject *object = &fgCollisionMgr->objects[index];
            const Coord4 *translation = MatrixRow(&fgWorld->renderInstances[object->renderIndex].matrix, 3);
            mover.position = translation;
            mover.lastPosition = DefaultVector;
            mover.index = index;
            mover.unknown16 = 0;
            mover.type = kGridObject;
            mover.objectSource = translation;
            mover.object = object;
            mover.matrix = NULL;
            mover.instance = NULL;
            AppendMover(mover);
        }
    }
}

// FUNC_AT(0x000c6290)
void WGrid::Restart() {
    Shutdown();
    Init(GridData);
}

// FUNC_AT(0x000c6d70)
void WGrid::AddGridNodeDynamicElement(Coord4 *last, const Coord4 *position, uint32_t type, uint32_t index) {
    WGridCellList cells = {};
    TheGrid->FindNodes(reinterpret_cast<const Coord3 *>(last), last->w, &cells);
    for (int i = 0; i < int(cells.Size()); i++) {
        WGridNode *node = CellNode(TheGrid, cells.first[i]);
        if (node != NULL)
            node->RemoveDynamic(index, type);
    }
    ColStl::Tidy(&cells);

    Coord4 box[2];
    float radius = position->w;
    box[0].x = position->x - radius;
    box[0].y = 0.0f;
    box[0].z = position->z - radius;
    box[1].x = radius + position->x;
    box[1].y = 0.0f;
    box[1].z = radius + position->z;
    TheGrid->FindNodesBox(box, &cells);
    for (int i = 0; i < int(cells.Size()); i++) {
        WGridNode *node = CellNode(TheGrid, cells.first[i]);
        if (node != NULL && !HasStatic(node, type, index))
            node->AddDynamic(uint16_t(index), type);
    }
    if (cells.first != NULL)
        UMemory::FastFree(cells.first, uint32_t(cells.end - cells.first) * sizeof(uint32_t));
}

// An object copies its render instance's translation; an instance takes its rotation and position from the
// inverse of its render instance's matrix. Either moves cells if it moved.
// FUNC_AT(0x000c6fd0)
void WGridMover::Update() {
    if (object != NULL) {
        object->position = { objectSource->x, objectSource->y, objectSource->z };
        if (Beyond(double(position->x) - lastPosition.x) || Beyond(double(position->y) - lastPosition.y) ||
            Beyond(double(position->z) - lastPosition.z)) {
            WGrid::AddGridNodeDynamicElement(&lastPosition, position, type, index);
            lastPosition = *position;
        }
        return;
    }
    if (instance == NULL)
        return;
    alignas(16) MATRIX4 inverse = *matrix;
    inverse.mtx[0][3] = 0.0f;
    inverse.mtx[1][3] = 0.0f;
    inverse.mtx[2][3] = 0.0f;
    inverse.mtx[3][3] = 1.0f;
    OrthoInverse(&inverse);
    instance->right = { inverse.mtx[0][0], inverse.mtx[0][1], inverse.mtx[0][2] };
    instance->forward = { inverse.mtx[2][0], inverse.mtx[2][1], inverse.mtx[2][2] };
    instance->position = { inverse.mtx[3][0], inverse.mtx[3][1], inverse.mtx[3][2] };
    // The z difference goes through a float (the original's fabs helper, 0x0001b160, takes a float)
    if (Beyond(double(position->x) - lastPosition.x) || Beyond(double(position->y) - lastPosition.y) ||
        Beyond(position->z - lastPosition.z)) {
        Coord4 moved = { position->x, position->y, position->z, instance->radius };
        WGrid::AddGridNodeDynamicElement(&lastPosition, &moved, type, index);
        lastPosition = moved;
    }
}

// FUNC_AT(0x000c7200)
void WGrid::UpdateDynamicNodes() {
    WGridMoverNode *head = GridMovers.head;
    for (WGridMoverNode *node = head == NULL ? NULL : head->next; node != GridMovers.head; node = node->next)
        node->value.Update();
}
