#ifndef GT_H_
#define GT_H_

#include "../../actionhelpers.h"

void GT_LoseControl(obj_tag * gameObj);
obj_tag* GT_Create(_VECTOR *pos, _VECTOR *rot, quaternion_tag *param_3, level_tag *lvl, celglist_tag *celgl, obj_tag* param_5);

#endif