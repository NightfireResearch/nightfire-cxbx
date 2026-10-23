// Shadow test for the random float helpers (util/Random.cpp): ours and the original, from the same Rand_Random
// state, on the same ranges. The results, bit for bit, and the state afterwards must agree. Run at start with
// MenuShadowTests=on (MenuProbe.cpp).

#include "RandomShadow.h"
#include "AllOriginal.h"

#include "../../common/xbeOriginal.h"
#include "../util/Random.h"
#include "../math/math.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace {

// The two seed words Rand_Random steps (its multipliers follow them)
#define RandSeeds (GameSpan<uint32_t>{0x0018cdf8})

const unsigned kFloat_FRand = 0x000d5360;
const unsigned kRand_FRandHalf = 0x000d53d0;
const unsigned kRand_FRandHalf_Vec = 0x000d5450;
const unsigned kRand_FRand_MVar2 = 0x000d5480;
const unsigned kRand_FRand_MVar2_Vec = 0x000d54a0;

typedef double(__cdecl *RangeFn)(float);
typedef double(__cdecl *RangeOffsetFn)(float, float);
typedef void(__cdecl *VecRangeFn)(_VECTOR *, float);
typedef void(__cdecl *VecRangeOffsetFn)(_VECTOR *, float, float);

const float kRanges[] = {1.0f, 2.0f, 0.2f, 3.0f, TWO_PI, 100.0f, 0.5f, -7.25f, 0.0f, -0.0f, 1e-30f, 3e38f,
                         1.17549435e-38f, 0.1f};
const int kTrials = 64;

unsigned s_lcg = 0x2545f491u;
uint32_t NextWord(void) {
    s_lcg = s_lcg * 1664525u + 1013904223u;
    return s_lcg;
}

float RandomFloat(void) {
    uint32_t w = NextWord();
    float f;
    memcpy(&f, &w, sizeof(f));
    return f;   // any bit pattern: denormals, infinities and NaNs too
}

int s_failures;

void Report(const char *name, int trial, const void *ours, const void *theirs, size_t size, const uint32_t *ourSeeds,
            const uint32_t *theirSeeds) {
    if (memcmp(ours, theirs, size) == 0 && memcmp(ourSeeds, theirSeeds, 8) == 0)
        return;
    if (s_failures++ < 8) {
        uint32_t a[3] = {}, b[3] = {};
        memcpy(a, ours, size < sizeof(a) ? size : sizeof(a));
        memcpy(b, theirs, size < sizeof(b) ? size : sizeof(b));
        printf("[random] %s trial %d: ours %08x %08x %08x, the original's %08x %08x %08x; seeds %08x %08x / %08x %08x\n",
               name, trial, a[0], a[1], a[2], b[0], b[1], b[2], ourSeeds[0], ourSeeds[1], theirSeeds[0],
               theirSeeds[1]);
    }
}

void Seed(uint32_t s0, uint32_t s1) {
    RandSeeds[0] = s0;
    RandSeeds[1] = s1;
}

void Save(uint32_t *seeds) {
    seeds[0] = RandSeeds[0];
    seeds[1] = RandSeeds[1];
}

void Check(int trial, float range, float offset, _VECTOR start) {
    uint32_t s0 = NextWord(), s1 = NextWord();
    uint32_t ourSeeds[2], theirSeeds[2];

    {
        Seed(s0, s1);
        double ours = Float_FRand(range);
        Save(ourSeeds);
        Seed(s0, s1);
        double theirs;
        {
            XbeOriginalScope original(kFloat_FRand);
            theirs = ((RangeFn)(uintptr_t)kFloat_FRand)(range);
        }
        Save(theirSeeds);
        Report("Float_FRand", trial, &ours, &theirs, sizeof(ours), ourSeeds, theirSeeds);
    }
    {
        Seed(s0, s1);
        double ours = Rand_FRandHalf(range);
        Save(ourSeeds);
        Seed(s0, s1);
        double theirs;
        {
            XbeOriginalScope original(kRand_FRandHalf);
            theirs = ((RangeFn)(uintptr_t)kRand_FRandHalf)(range);
        }
        Save(theirSeeds);
        Report("Rand_FRandHalf", trial, &ours, &theirs, sizeof(ours), ourSeeds, theirSeeds);
    }
    {
        Seed(s0, s1);
        double ours = Rand_FRand_MVar2(range, offset);
        Save(ourSeeds);
        Seed(s0, s1);
        double theirs;
        {
            XbeOriginalScope original(kRand_FRand_MVar2);
            XbeOriginalScope callee(kFloat_FRand);
            theirs = ((RangeOffsetFn)(uintptr_t)kRand_FRand_MVar2)(range, offset);
        }
        Save(theirSeeds);
        Report("Rand_FRand_MVar2", trial, &ours, &theirs, sizeof(ours), ourSeeds, theirSeeds);
    }
    {
        _VECTOR ours = start, theirs = start;
        Seed(s0, s1);
        Rand_FRandHalf_Vec(&ours, range);
        Save(ourSeeds);
        Seed(s0, s1);
        {
            XbeOriginalScope original(kRand_FRandHalf_Vec);
            XbeOriginalScope callee(kRand_FRandHalf);
            ((VecRangeFn)(uintptr_t)kRand_FRandHalf_Vec)(&theirs, range);
        }
        Save(theirSeeds);
        Report("Rand_FRandHalf_Vec", trial, &ours, &theirs, 12, ourSeeds, theirSeeds);
    }
    {
        _VECTOR ours = start, theirs = start;
        Seed(s0, s1);
        Rand_FRand_MVar2_Vec(&ours, range, offset);
        Save(ourSeeds);
        Seed(s0, s1);
        {
            XbeOriginalScope original(kRand_FRand_MVar2_Vec);
            XbeOriginalScope callee(kFloat_FRand);
            ((VecRangeOffsetFn)(uintptr_t)kRand_FRand_MVar2_Vec)(&theirs, range, offset);
        }
        Save(theirSeeds);
        Report("Rand_FRand_MVar2_Vec", trial, &ours, &theirs, 12, ourSeeds, theirSeeds);
    }
}

}  // namespace

void RandomShadow_Run(void) {
    uint32_t saved[2];
    Save(saved);
    s_failures = 0;
    int trials = 0;
    for (float range : kRanges) {
        for (int t = 0; t < kTrials; t++) {
            _VECTOR start = {};
            start.x = (float)(int)(NextWord() % 2001 - 1000) * 0.37f;
            start.y = (float)(int)(NextWord() % 2001 - 1000) * 0.0013f;
            start.z = (float)(int)(NextWord() % 2001 - 1000) * 11.0f;
            Check(trials++, range, range * 0.5f, start);
        }
    }
    for (int t = 0; t < kTrials; t++) {   // any bit patterns
        _VECTOR start = {RandomFloat(), RandomFloat(), RandomFloat()};
        Check(trials++, RandomFloat(), RandomFloat(), start);
    }
    Seed(saved[0], saved[1]);
    printf("[random] random float helpers: %d of %d x 5 trials differ from the originals\n", s_failures, trials);
}
