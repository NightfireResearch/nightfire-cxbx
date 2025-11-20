#ifndef DRONE_AIHINTS_H_
#define DRONE_AIHINTS_H_

#include "../../actionhelpers.h"

void Drone_CoverCornerNode(_VECTOR *pos, _VECTOR *rot, level_tag* lvl);
void Drone_CoverLowNode(_VECTOR *pos, _VECTOR *rot, level_tag* lvl);
void Drone_AIPoint(_VECTOR *pos, _VECTOR *rot, level_tag* lvl);

#endif // DRONE_AIHINTS_H_