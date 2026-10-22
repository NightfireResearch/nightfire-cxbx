#include "Random.h"

static const double kUintToUnit = 1.0 / 4294967296.0;   // a 32-bit random number to [0, 1)

// Two multiply-with-carry generators, a 16-bit half each: the state's low half times the multiplier, plus the high
// half (the carry)
// XBE_GLOBAL(0x0018cdf8, 0x4)
uint32_t RandStateA = 521288629;
// XBE_GLOBAL(0x0018cdfc, 0x4)
uint32_t RandStateB = 362436069;
// XBE_GLOBAL(0x0018ce00, 0x4) readonly relocate
uint RandMultiplierB = 30903;
// XBE_GLOBAL(0x0018ce04, 0x4) readonly relocate
uint RandMultiplierA = 18000;

// A 32-bit random number: A's state in the high half, B's in the low
// AUTOINJECT
uint __stdcall Rand_Random(void) {
    RandStateA = (RandStateA & 0xffff) * RandMultiplierA + (RandStateA >> 16);
    RandStateB = (RandStateB & 0xffff) * RandMultiplierB + (RandStateB >> 16);
    return (RandStateA << 16) + (RandStateB & 0xffff);
}

// A random number below max, from B's state alone (both generators step); the product wraps for max above 0x10000
// AUTOINJECT
unsigned int Rand_Rand(unsigned int max) {
    RandStateA = (RandStateA & 0xffff) * RandMultiplierA + (RandStateA >> 16);
    RandStateB = (RandStateB & 0xffff) * RandMultiplierB + (RandStateB >> 16);
    return ((RandStateB & 0xffff) * max) >> 16;
}

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
