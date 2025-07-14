#include "Random.h"

#include <math.h>

// AUTOGEN
unsigned int Rand_Rand(unsigned int max);

// AUTOGEN
uint __stdcall Rand_Random(void);

// AUTOINJECT
float Rand_FRandHalf(float range) {
    uint rand_u32 = Rand_Random();
    float rand_01 = rand_u32 * powf(2, -32); // Scale from range of uint32 to range 0, 1
    return (rand_01 * range) - (range * 0.5f); // Output is in [-range/2, range/2]
}

// AUTOINJECT
void Rand_FRandHalf_Vec(_VECTOR *vecOut, float range) {
    vecOut->x = Rand_FRandHalf(range);
    vecOut->y = Rand_FRandHalf(range);
    vecOut->z = Rand_FRandHalf(range);
}

// AUTOINJECT
float Float_FRand(float range) {
    uint rand_u32 = Rand_Random();
    float rand_01 = rand_u32 * powf(2, -32); // Scale from range of uint32 to range 0, 1
    return rand_01 * range; // Output is in [0, range]
}

// AUTOINJECT
float Rand_FRand_MVar2(float range, float offset) {
    return Float_FRand(range) - offset;
}

// AUTOINJECT
void Rand_FRand_MVar2_Vec(_VECTOR *vec, float range, float offset) {
    vec->x += (Float_FRand(range) - offset);
    vec->y += (Float_FRand(range) - offset);
    vec->z += (Float_FRand(range) - offset);
}