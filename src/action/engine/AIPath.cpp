// The level's AI paths and bounds: map block 0x05, read in place (AIPath_Parse), and bound to the cels once the level
// is loaded (AIPath_BindNodes). See docs/drone/bots-and-navigation/README.md, section 2.

#include "AIPath.h"
#include "Collide.h"
#include "viewer.h"
#include "../memory.h"
#include "../math/math.h"
#include "../game/obj/build.h"

#include <string.h>

#pragma fp_contract(off)

// The block's version: anything else and the block is passed over
#define AIPATH_BLOCK_VERSION 8
// A file node's flags when it has none
#define AINODE_NO_FLAGS 0x2468abce

// How far up AIBounds_Link2Cel looks for a ceiling over a bound node, and the least headroom it leaves
#define BOUND_CEILING_SEARCH 256.0f
#define BOUND_MIN_HEADROOM 2.0f
// Collide_RayIntersect's collision types for that ray
#define BOUND_CEILING_COLLISION 0x90

// The block's version, as read
// XBE_GLOBAL(0x00274ca8, 0x4)
uint32_t AIPath_BlockVersion;

// AUTOINJECT
void AIPath_Init(void) {
    AIPathCount = 0;
    AIBoundCount = 0;
    MoveTest_MaxSlope = 0.0f;
    AIPath_MaxNodes = 0;
    AIPath_MaxLinks = 0;
}

// The file data is patched where it lies: the node and link records become the runtime ones, and the tables point
// into them. Neither table's count is checked.
// AUTOINJECT
void AIPath_Parse(AIPathBlock *block) {
    AIPath_BlockVersion = block->version;
    if (block->version != AIPATH_BLOCK_VERSION)
        return;

    uchar *at = (uchar *)(block + 1);
    for (uint n = block->count; n != 0; n--) {
        AIPathRecord *record = (AIPathRecord *)at;
        uchar *nodeData = at + sizeof(AIPathRecord) + record->extraBytes;

        if (!(record->flags & AIPATH_BOUND)) {
            AIPath_tag *path = &AIPaths[AIPathCount];
            path->flags = record->flags;
            path->index = AIPathCount;
            path->record = at;
            memcpy(path->fromFile, record->fromFile, sizeof(path->fromFile));
            path->nodeLinks = NULL;
            path->nodeCount = record->nodeCount;
            if (path->nodeCount != 0)
                path->runtimeNodes = (AINode_tag *)Mem_Malloc(path->nodeCount * sizeof(AINode_tag),
                                                              MALLOC_PATHPOINTERS, 0);
            else
                path->runtimeNodes = NULL;
            if (path->nodeCount > AIPath_MaxNodes)
                AIPath_MaxNodes = path->nodeCount;

            path->fileNodes = (AIPathNodeRecord *)nodeData;
            for (uint i = 0; i < path->nodeCount; i++) {
                MAPNODE_tag *node = &path->fileNodes[i].node;
                node->index = i;
                node->path = AIPathCount;
                if (node->flags == AINODE_NO_FLAGS)
                    node->flags = 0;
                node->cel = NULL;
                node->pos.y += 0.4f;
                path->runtimeNodes[i].door = NULL;
                path->runtimeNodes[i].fileNode = node;
            }

            uint *linkCount = (uint *)(path->fileNodes + path->nodeCount);
            path->linkCount = *linkCount;
            path->links = (AILink_tag *)(linkCount + 1);
            if (path->linkCount > AIPath_MaxLinks)
                AIPath_MaxLinks = path->linkCount;
            for (uint i = 0; i < path->linkCount; i++) {
                AILink_tag *link = &path->links[i];
                link->path = AIPathCount;
                link->flags = 0;
                link->used = 0;
                link->length = 0.0f;
            }
            at = (uchar *)(path->links + path->linkCount);
            AIPathCount++;
        } else {
            AIBound_tag *bound = &AIBounds[AIBoundCount];
            bound->flags = record->flags;
            bound->index = AIBoundCount;
            bound->record = at;
            memcpy(bound->fromFile, record->fromFile, sizeof(bound->fromFile));
            bound->nodeCount = record->nodeCount;
            if (bound->nodeCount != 0)
                bound->runtimeNodes = (AIBoundNode_tag *)Mem_Malloc(bound->nodeCount * sizeof(AIBoundNode_tag),
                                                                    MALLOC_PATHPOINTERS, 0);
            else
                bound->runtimeNodes = NULL;

            bound->fileNodes = (MAPNODE_tag *)nodeData;
            for (uint i = 0; i < bound->nodeCount; i++) {
                MAPNODE_tag *node = &bound->fileNodes[i];
                node->index = i;
                node->path = AIBoundCount;
                node->cel = NULL;
                node->pos.y += 0.4f;
                node->headroom = 0.0f;
                bound->runtimeNodes[i].fileNode = node;
            }

            uint *linkCount = (uint *)(bound->fileNodes + bound->nodeCount);
            bound->linkCount = *linkCount;
            bound->links = (AIBoundLink_tag *)(linkCount + 1);
            for (uint i = 0; i < bound->linkCount; i++) {
                AIBoundLink_tag *link = &bound->links[i];
                link->bound = AIBoundCount;
                link->flags = 0;
                link->nextA = NULL;
                link->nextB = NULL;
            }
            at = (uchar *)(bound->links + bound->linkCount);
            AIBoundCount++;
        }
    }
}

// Each link's length, and each node's list of links. The original takes the path in ESI.
void AIPath_Prepare(AIPath_tag *path) {
    for (int i = 0; i < (int)path->linkCount; i++) {
        AILink_tag *link = &path->links[i];
        _VECTOR along;
        Vec_Subtract(&path->runtimeNodes[link->a].fileNode->pos, &path->runtimeNodes[link->b].fileNode->pos, &along);
        link->length = Vec_Magnitude(&along);
    }

    int entries = 0;
    for (int node = 0; node < (int)path->nodeCount; node++) {
        for (int i = 0; i < (int)path->linkCount; i++) {
            if (path->links[i].a == node || path->links[i].b == node)
                entries++;
        }
    }
    if (entries == 0) {
        path->nodeLinks = NULL;
        return;
    }

    path->nodeLinks = (ushort *)Mem_Malloc(entries * sizeof(ushort), MALLOC_PATHPOINTERS, 0);
    int first = 0;
    for (int node = 0; node < (int)path->nodeCount; node++) {
        int count = 0;
        for (int i = 0; i < (int)path->linkCount; i++) {
            if (path->links[i].a == node || path->links[i].b == node) {
                path->nodeLinks[first + count] = i;
                count++;
            }
        }
        path->runtimeNodes[node].linkCount = count;
        path->runtimeNodes[node].firstLink = first;
        first += count;
    }
}

// Puts a bound node on its cel's list, and gives it the headroom up to the ceiling (at least 2 m). The original takes
// the node in EAX and the bound's record (its name) on the stack.
void AIBounds_Link2Cel(AIBoundNode_tag *node, uchar *record) {
    cel_tag *cel = build_FindCel(&node->fileNode->pos, glb_world);
    if (cel == NULL)
        return;
    node->nextInCel = cel->boundNodes;
    node->fileNode->cel = cel;
    cel->boundNodes = node;

    HITDATA_tag *hits = NULL;
    _VECTOR *pos = &node->fileNode->pos;
    _VECTOR up = {pos->x, pos->y + BOUND_CEILING_SEARCH, pos->z};
    Collide_RayIntersect(pos, &up, cel, NULL, NULL, &hits, 1, BOUND_CEILING_COLLISION, 0);
    // the hit's distance up the ray
    node->fileNode->headroom = hits != NULL ? hits->dist : BOUND_CEILING_SEARCH;
    if (node->fileNode->headroom < BOUND_MIN_HEADROOM)
        node->fileNode->headroom = BOUND_MIN_HEADROOM;
    Collide_FreeHitList(&hits);
}

// Puts each bound link on the list of the cel of its end a, and of its end b when that is another cel. The original
// takes the bound in EDI.
void AIBounds_Prepare(AIBound_tag *bound) {
    for (uint i = 0; i < bound->linkCount; i++) {
        AIBoundLink_tag *link = &bound->links[i];
        cel_tag *celA = bound->runtimeNodes[link->a].fileNode->cel;
        cel_tag *celB = bound->runtimeNodes[link->b].fileNode->cel;
        link->nextA = celA->boundLinks;
        celA->boundLinks = link;
        if (celA == celB) {
            link->nextB = link->nextA;
        } else {
            link->nextB = celB->boundLinks;
            celB->boundLinks = link;
        }
    }
}

// Once the level is loaded: every path node onto its cel's list (AIPath_Link2Cel in the PS2 build), the paths
// prepared, and the same for the bounds
// AUTOINJECT
void AIPath_BindNodes(void) {
    for (uint p = 0; p < AIPathCount; p++) {
        AIPath_tag *path = &AIPaths[p];
        for (uint i = 0; i < path->nodeCount; i++) {
            AINode_tag *node = &path->runtimeNodes[i];
            cel_tag *cel = build_FindCel(&node->fileNode->pos, glb_world);
            if (cel == NULL) {
                // GC check (0x8005a760)
                NF_WARN("FATAL: AIPath '%s' NODE (%d) IS NOT IN A ROOM: %f, %f, %f\n", (char *)path->record,
                        node->fileNode->index, node->fileNode->pos.x, node->fileNode->pos.y, -node->fileNode->pos.z);
                continue;
            }
            node->nextInCel = cel->aiNodes;
            node->fileNode->cel = cel;
            cel->aiNodes = node;
        }
        AIPath_Prepare(path);
    }

    for (uint b = 0; b < AIBoundCount; b++) {
        AIBound_tag *bound = &AIBounds[b];
        for (uint i = 0; i < bound->nodeCount; i++)
            AIBounds_Link2Cel(&bound->runtimeNodes[i], bound->record);
        AIBounds_Prepare(bound);
    }
}
