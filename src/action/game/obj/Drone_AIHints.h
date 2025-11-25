#ifndef DRONE_AIHINTS_H_
#define DRONE_AIHINTS_H_

#include "../../actionhelpers.h"

void Drone_CoverCornerNode(_VECTOR *pos, _VECTOR *rot, level_tag* lvl);
void Drone_CoverLowNode(_VECTOR *pos, _VECTOR *rot, level_tag* lvl);
void Drone_AIPoint(_VECTOR *pos, _VECTOR *rot, level_tag* lvl);
obj_tag * Drone_AIVolume_Create(_VECTOR *param_1,_VECTOR *param_2,_VECTOR *param_3,level_tag *param_4,celglist_tag *param_5);

#endif // DRONE_AIHINTS_H_