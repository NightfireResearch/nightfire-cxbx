#ifndef AIPATH_H_
#define AIPATH_H_

#include "AINetwork.h"

#pragma pack(push, 1)

// Map block 0x05 as stored: count records follow
typedef struct AIPathBlock {
    uint unknown0;
    uint version;           // 0x04 - must be 8
    uint count;             // 0x08
} AIPathBlock;

// One path or bound in the block. After it: extraBytes of padding, the nodes (AIPathNodeRecord for a path,
// MAPNODE_tag for a bound), the link count and the links (AILink_tag or AIBoundLink_tag).
typedef struct AIPathRecord {
    char name[0x80];
    uint flags;             // 0x80 - AIPATH_*
    uint fromFile[8];       // 0x84
    uint nodeCount;         // 0xa4
    uint extraBytes;        // 0xa8
} AIPathRecord;

#pragma pack(pop)

static_assert(sizeof(AIPathBlock) == 0xc, "AIPathBlock is 0xc bytes");
static_assert(sizeof(AIPathRecord) == 0xac, "AIPathRecord is 0xac bytes");

void AIPath_Init(void);
void AIPath_Parse(AIPathBlock *block);
void _AIPath_Prepare(AIPath_tag *path);
void AIPath_Prepare(void);
void _AIBounds_Link2Cel(AIBoundNode_tag *node, uchar *record);
void AIBounds_Link2Cel(void);
void _AIBounds_Prepare(AIBound_tag *bound);
void AIBounds_Prepare(void);
void AIPath_BindNodes(void);

#endif // AIPATH_H_
