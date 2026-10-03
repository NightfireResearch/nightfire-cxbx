#pragma once

#include <stdint.h>

// The simulation's random numbers (Ghidra: SimRandom, 16 bytes; the Simulation's, constructed by
// Simulation::Simulation): a 16-bit multiplicative generator, state = state * seed & 0xffff, each number the middle
// sixteen bits of the product. Reset by Simulation::Reset, so a level's AI draws the same sequence every run.
struct SimRandom {
    uint32_t state;             // +0x00
    uint32_t lastProduct;       // +0x04 state * seed before the mask
    uint32_t seed;              // +0x08 the multiplier, 123456789
    uint32_t counter;           // +0x0c numbers drawn since Reset

    SimRandom* Construct();     // 0x0005cc80
    uint32_t Generate();        // 0x0005cc90: 0-65535
    void Reset();               // 0x0005ccb0: a fixed state, advanced seed % 50 times
};
static_assert(sizeof(SimRandom) == 16, "SimRandom is 16 bytes");

// Perlin-style 1D noise (Ghidra: Noise) for camera shake, particles and weapon fire: a gradient table and a
// permutation table, made by Init at start-up from a fixed seed, so the same every run.
class Noise {
public:
    // The noise at x (0x0005ca80). The original leaves its result on the x87 stack unrounded - double precision -
    // so this answers a double, which is returned the same way.
    static double Noise1(float x);
    // The tables (0x0005cbb0), with the platform's random numbers reseeded for the purpose and put back after.
    static void Init();
};

// The other noise table, 257 floats of the C runtime's rand() in [-1, 1) (0x0005cb50, once).
void noise_init();

// Six words: each becomes the bits of `other`'s that it did not have (0x0005cd00, Explosion::Simulate's). The
// stray function linked after the noise.
struct BitSet192 {
    uint32_t words[6];

    void AndNotFrom(const BitSet192 *other);
};
