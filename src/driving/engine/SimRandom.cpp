#pragma fp_contract(off)

#include "SimRandom.h"
#include "../platform/RealMath.h"
#include "../platform/X87.h"
#include "../../helpers.h"

#include <bit>

// ---------------------------------------------------------------------------------------------------------------
// SimRandom, the simulation's generator, and Noise, the 1D gradient noise (docs/driving/maths.md for the x87
// rules). Noise1 is one x87 expression in the original: it is written in double in the original's order, with the
// smoothing weight stored to a float where the original stores it.
// ---------------------------------------------------------------------------------------------------------------

// noise_init's table: 256 values and the first again at the end.
#define NoiseTable ((float *)0x001e5220)
#define NoiseTableMade I32_AT(0x001e7a58)
// Noise::Init's: 267 gradients (each lattice point k uses the pair k, k + 1), and the 4096-entry permutation.
#define NoiseGradient ((float *)0x001e5628)
#define NoisePermutation ((int16_t *)0x001e5a58)
// The generator the gradients are drawn from: SimRandom's scheme, a state and a multiplier, and a count Init clears.
#define NoiseRandomState U32_AT(0x001c34f8)
#define NoiseRandomMultiplier U32_AT(0x001c34fc)
#define NoiseRandomCount U32_AT(0x001c3500)

// The C runtime's rand, the game's: its state is the game's.
#define CRT_rand ((int (*)(void))0x00133ee0)

static const uint32_t kSimRandomSeed = 123456789;
static const uint32_t kSimRandomResetState = 0xf874af01;
static const uint32_t kNoiseSeed = 12345;
static const int kNoiseTableSize = 256;
static const int kGradientCount = 267;
static const int kPermutationSize = 0x1000;
// rand() & 0x7fffffff to [0, 2): not quite 2^-30 - the original's constant (0x0018ecb8) is 1 / (2^30 - 0.5)
static constexpr double kRandScale = 0x1.00000002p-30;
static_assert(std::bit_cast<uint64_t>(kRandScale) == 0x3e10000000200000ull, "noise_init's scale");
static const float kOneOver65536 = 0x1p-16f;

// ---- SimRandom

// FUNC_AT(0x0005cc80)
SimRandom* SimRandom::Construct() {
    seed = kSimRandomSeed;
    return this;
}

// FUNC_AT(0x0005cc90)
uint32_t SimRandom::Generate() {
    counter++;
    lastProduct = seed * state;
    state = lastProduct & 0xffff;
    return (lastProduct >> 8) & 0xffff;
}

// FUNC_AT(0x0005ccb0)
void SimRandom::Reset() {
    uint32_t steps = seed % 50;
    state = kSimRandomResetState;
    lastProduct = 0;
    counter = 0;
    if (steps != 0) {
        uint32_t s = kSimRandomResetState, product = 0;
        for (uint32_t i = steps; i != 0; i--) {
            product = seed * s;
            s = product & 0xffff;
        }
        state = s;
        lastProduct = product;
        counter = steps;
    }
}

// ---- Noise

// FUNC_AT(0x0005ca80)
double Noise::Noise1(float x) {
    const float t = x + 10000.0f;   // the original subtracts -10000: the same in IEEE arithmetic
    const int i = Truncate(t);
    const int k0 = (uint8_t)NoisePermutation[NoisePermutation[i & 0xfff] & 0xfff];
    const int k1 = (uint8_t)NoisePermutation[NoisePermutation[(i + 1) & 0xfff] & 0xfff];

    const double f = double(t) - i;
    const float s = float((3.0 - (f + f)) * f * f);   // the smoothing weight, 3f^2 - 2f^3

    const double nearer = (double(t) - i) * NoiseGradient[k0 + 1] + NoiseGradient[k0] * 0.5;
    const double weighted = nearer * (1.0 - s);
    const double further = (double(t) - (i + 1)) * NoiseGradient[k1 + 1] + NoiseGradient[k1] * 0.5;
    return weighted + further * s;
}

// FUNC_AT(0x0005cb50)
void noise_init() {
    if (NoiseTableMade != 0)
        return;
    NoiseTableMade = 1;
    for (int i = 0; i < kNoiseTableSize; i++) {
        int r = CRT_rand() & 0x7fffffff;
        NoiseTable[i] = float(double(r) * kRandScale - 1.0);
    }
    NoiseTable[kNoiseTableSize] = NoiseTable[0];
}

// FUNC_AT(0x0005cbb0)
void Noise::Init() {
    noise_init();
    NoiseRandomCount = 0;
    uint32_t saved = REAL_random();
    seedrandom(kNoiseSeed);

    const uint32_t multiplier = NoiseRandomMultiplier;
    uint32_t state = NoiseRandomState;
    for (int i = 0; i < kGradientCount; i++) {
        uint32_t product = multiplier * state;
        state = product & 0xffff;
        double h = double(int((product >> 8) & 0xffff)) * kOneOver65536 - 0.5f;
        NoiseGradient[i] = float(h + h);
    }
    NoiseRandomState = state;

    for (int i = 0; i < kPermutationSize; i++)
        NoisePermutation[i] = (int16_t)i;
    for (int i = kPermutationSize - 1; i >= 0; i--) {
        uint32_t r = REAL_random() & 0xfff;
        int16_t swapped = NoisePermutation[i];
        NoisePermutation[i] = NoisePermutation[r];
        NoisePermutation[r] = swapped;
    }
    seedrandom(saved);
}

// ---- the bit set

// FUNC_AT(0x0005cd00)
void BitSet192::AndNotFrom(const BitSet192 *other) {
    for (int i = 0; i < 6; i++)
        words[i] = ~words[i] & other->words[i];
}
