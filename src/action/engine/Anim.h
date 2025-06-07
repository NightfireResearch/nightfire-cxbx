#ifndef ANIM_H
#define ANIM_H

#include "../actionhelpers.h"

void AnimLoadFile(HASHCODE hashcode, char param_2);
uchar AnimGetBoneWorldTrans(obj_tag *param_1,uint whichBoneMatrix,int param_3,_VECTOR *param_4,_MATRIX *param_5);
bool AnimObjectIsClose(obj_tag* obj, uint unk);

#endif // ANIM_H