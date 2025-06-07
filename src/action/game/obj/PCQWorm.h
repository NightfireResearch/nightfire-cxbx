#ifndef PCQWORM_H_
#define PCQWORM_H_

#include "../../actionhelpers.h"

obj_tag * PCQWorm_Create(_VECTOR *pos,_VECTOR *rot,level_tag *lvl,celglist_tag *celgl);
void PCQWorm_Update(obj_tag *gameObj);
void PCQWorm_Activate(obj_tag *gameObj);

#endif // PCQWORM_H_