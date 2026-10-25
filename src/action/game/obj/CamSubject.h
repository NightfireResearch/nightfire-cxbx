#ifndef CAMSUBJECT_H_
#define CAMSUBJECT_H_

#include "../../actionhelpers.h"

obj_tag * CamSubject_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl);
void CamSubject_Update(obj_tag *obj);

#endif // CAMSUBJECT_H_