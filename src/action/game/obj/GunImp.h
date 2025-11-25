#ifndef GUNIMP_H_
#define GUNIMP_H_

#include "../../actionhelpers.h"

void GunImp_Deactivate(obj_tag *gameObj);
obj_tag * GunImp_Create(_VECTOR *pos, quaternion_tag *quat, celglist_tag *celgl);

#endif // GUNIMP_H_