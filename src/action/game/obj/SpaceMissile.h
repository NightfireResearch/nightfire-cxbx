#ifndef SPACEMISSILE_H_
#define SPACEMISSILE_H_

#include "../../actionhelpers.h"

void SpaceMissile_Update(obj_tag *obj);
obj_tag * Create_SpaceMissile(_VECTOR* pos, _VECTOR* rot, level_tag* level);

#endif // SPACEMISSILE_H_