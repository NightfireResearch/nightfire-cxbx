#ifndef ENV_H_
#define ENV_H_

#include "../../actionhelpers.h"

void Env_Create(_VECTOR *pos, _VECTOR *rot, celglist_tag *celgl, level_tag *lvl);
obj_tag* Env_Tree_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl);


#endif // ENV_H_