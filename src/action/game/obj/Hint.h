#ifndef HINT_H_
#define HINT_H_

#include "../../actionhelpers.h"

obj_tag * Hint_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl);
void Hint_Update(obj_tag *obj);

#endif // HINT_H_