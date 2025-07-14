#ifndef RANDOM_H
#define RANDOM_H

#include "../actionhelpers.h"

unsigned int Rand_Rand(unsigned int max);
float Rand_FRandHalf(float range);
void Rand_FRandHalf_Vec(_VECTOR *param_1, float param_2);
uint __stdcall Rand_Random(void);
float Float_FRand(float param_1);
float Rand_FRand_MVar2(float range, float offset);
void Rand_FRand_MVar2_Vec(_VECTOR *param_1,float range,float param_3);

#endif // RANDOM_H

