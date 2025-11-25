#ifndef PICKUP_H_
#define PICKUP_H_

#include "../actionhelpers.h"

obj_tag * Pickup_Create(_VECTOR *pos,_VECTOR *rot,_MATRIX *mtx,celglist_tag *param_4,ushort maybePickupType,ushort param_6,uint param_7,uint param_8,uint param_9,char param_10,ushort param_11,ushort param_12,uint param_13);

#endif // PICKUP_H_