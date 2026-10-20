#ifndef CORONA_H_
#define CORONA_H_

#include "../../actionhelpers.h"

obj_tag * Corona_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl);
void Corona_Update(obj_tag *obj);
void Sunreflection_Update(obj_tag *obj);

#endif // CORONA_H_