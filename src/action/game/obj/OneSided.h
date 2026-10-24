#ifndef ONESIDED_H_
#define ONESIDED_H_

#include "../../actionhelpers.h"

obj_tag * OneSided_Create(_VECTOR *pos, _VECTOR *rot, celglist_tag *celgl);
void OneSided_Update(obj_tag *obj);

#endif // ONESIDED_H_
