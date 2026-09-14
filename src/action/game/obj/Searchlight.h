#ifndef SEARCHLIGHT_H_
#define SEARCHLIGHT_H_

#include "../../actionhelpers.h"

obj_tag* Searchlight_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl);
void Searchlight_Update(obj_tag *searchlight);

#endif // SEARCHLIGHT_H_