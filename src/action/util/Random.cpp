#include "Random.h"

static const double kUintToUnit = 1.0 / 4294967296.0;   // a 32-bit random number to [0, 1)

// AUTOGEN
unsigned int Rand_Rand(unsigned int max);

// AUTOGEN
uint __stdcall Rand_Random(void);

// The results stay at the x87's precision: callers carry on from them.

// AUTOINJECT
double Rand_FRandHalf(float range) {
    return Rand_Random() * kUintToUnit * range - range * 0.5;   // [-range/2, range/2)
}

// AUTOINJECT
void Rand_FRandHalf_Vec(_VECTOR *vecOut, float range) {
    vecOut->x = Rand_FRandHalf(range);
    vecOut->y = Rand_FRandHalf(range);
    vecOut->z = Rand_FRandHalf(range);
}

// AUTOINJECT
double Float_FRand(float range) {
    return Rand_Random() * kUintToUnit * range;   // [0, range)
}

// AUTOINJECT
double Rand_FRand_MVar2(float range, float offset) {
    return Float_FRand(range) - offset;
}

// AUTOINJECT
void Rand_FRand_MVar2_Vec(_VECTOR *vec, float range, float offset) {
    vec->x = Float_FRand(range) - offset + vec->x;
    vec->y = Float_FRand(range) - offset + vec->y;
    vec->z = Float_FRand(range) - offset + vec->z;
}
