#ifndef EMITTER_H_
#define EMITTER_H_

#include "../../actionhelpers.h"

obj_tag * Emitter_Create(HASHCODE hc, _VECTOR *param_2, _VECTOR *param_3, float param_4, uint param_5, uint param_6, uint param_7, float param_8);
void Emitter_CreatePlist(_MATRIX *param_1,level_tag *param_2);

#endif // EMITTER_H_