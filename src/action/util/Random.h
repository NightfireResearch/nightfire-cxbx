#ifndef RANDOM_H
#define RANDOM_H

#include "../actionhelpers.h"

unsigned int Rand_Rand(unsigned int max);
double Rand_FRandHalf(float range);
void Rand_FRandHalf_Vec(_VECTOR *vecOut, float range);
uint __stdcall Rand_Random(void);
extern uint32_t RandStateA, RandStateB;
extern uint RandMultiplierA, RandMultiplierB;
double Float_FRand(float range);
double Rand_FRand_MVar2(float range, float offset);
void Rand_FRand_MVar2_Vec(_VECTOR *vec,float range,float offset);

#endif // RANDOM_H

