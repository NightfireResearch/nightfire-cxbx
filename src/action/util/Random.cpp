#include "Random.h"

static const float kUintToUnit = 2.3283064e-10f;   // 2^-32: a 32-bit random number to [0, 1)

// AUTOGEN
unsigned int Rand_Rand(unsigned int max);

// AUTOGEN
uint __stdcall Rand_Random(void);

// AUTOINJECT
float Rand_FRandHalf(float range) {
    float unit = (float)Rand_Random() * kUintToUnit;
    return unit * range - range * 0.5f;   // [-range/2, range/2)
}

// AUTOINJECT
void Rand_FRandHalf_Vec(_VECTOR *vecOut, float range) {
    vecOut->x = Rand_FRandHalf(range);
    vecOut->y = Rand_FRandHalf(range);
    vecOut->z = Rand_FRandHalf(range);
}

// AUTOINJECT
__declspec(noinline) float Float_FRand(float range) {
    float unit = (float)Rand_Random() * kUintToUnit;
    return unit * range;   // [0, range)
}

// AUTOINJECT
float Rand_FRand_MVar2(float range, float offset) {
    float unit = (float)Rand_Random() * kUintToUnit;
    return unit * range - offset;
}

// AUTOINJECT
void Rand_FRand_MVar2_Vec(_VECTOR *vec, float range, float offset) {
    vec->x += Float_FRand(range) - offset;
    vec->y += Float_FRand(range) - offset;
    vec->z += Float_FRand(range) - offset;
}
