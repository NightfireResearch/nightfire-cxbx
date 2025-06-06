#ifndef RANDOM_H
#define RANDOM_H

#include "../actionhelpers.h"

unsigned int Rand_Rand(unsigned int max);
float Rand_FRandHalf(float range);
void Rand_FRandHalf_Vec(_VECTOR *param_1, float param_2);
uint __stdcall Rand_Random(void);

#endif // RANDOM_H

