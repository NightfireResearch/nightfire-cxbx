#ifndef SS_H_
#define SS_H_

#include "../../actionhelpers.h"

obj_tag * SS_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl);
void SS_Activate(obj_tag *obj);
void SS_Update(obj_tag *obj);

#endif // SS_H_
