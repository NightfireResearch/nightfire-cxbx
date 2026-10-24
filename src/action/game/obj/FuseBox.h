#ifndef FUSEBOX_H_
#define FUSEBOX_H_

#include "../../actionhelpers.h"

obj_tag * FuseBox_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl);
void FuseBox_Update(obj_tag *obj);
void FuseBox_Register(obj_tag *fuse, ushort list, void *fuseBox);

#endif // FUSEBOX_H_
