#ifndef ROTOR_H_
#define ROTOR_H_

#include "../../actionhelpers.h"

obj_tag * rotor_init(_VECTOR *pos, _VECTOR *rot, celglist_tag *celgl, ushort param_4, uchar param_5, uchar switchChannel);
void rotor_update(obj_tag* gameObj);

#endif // ROTOR_H_