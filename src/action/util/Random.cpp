#include "Random.h"

// AUTOGEN
unsigned int Rand_Rand(unsigned int max);


// UNINJECTABLE
float Rand_FRandHalf(float range) {

    // Get a 32-bit random number from the RNG, and scale it to a float in the range 0,1
    float randF = (float)(Rand_Rand(0x7fffffff) & 0x7fffffff) / (float)0x7fffffff;

    // Rescale it to the range -range/2, range/2
    return (randF * range) - (range * 0.5f);

}

// AUTOINJECT
void Rand_FRandHalf_Vec(_VECTOR *vecOut, float range) {
    vecOut->x = Rand_FRandHalf(range);
    vecOut->y = Rand_FRandHalf(range);
    vecOut->z = Rand_FRandHalf(range);
}


// AUTOGEN
uint __stdcall Rand_Random(void);

