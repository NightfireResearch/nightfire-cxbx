#ifndef PCQWORM_H_
#define PCQWORM_H_

#include "../../actionhelpers.h"

// The placement's keys (level_tag)
typedef struct PCQWormPlacement {
    PlacementHeader header;     // 0x00
    uint keyCode;               // 0x2c - KeyCodes[], 0 for none
    uint channel;               // 0x30 - set when it is triggered
    uint activatable;           // 0x34 - PCQWorm_Activate triggers it
    uint shootable;             // 0x38 - a Q-Worm bullet's hit triggers it
} PCQWormPlacement;

obj_tag * PCQWorm_Create(_VECTOR *pos,_VECTOR *rot,level_tag *lvl,celglist_tag *celgl);
void PCQWorm_Update(obj_tag *gameObj);
void PCQWorm_Activate(obj_tag *gameObj);

#endif // PCQWORM_H_