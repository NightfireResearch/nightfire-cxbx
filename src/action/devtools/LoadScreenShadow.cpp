// Shadow test for ResetMap_LevelCode2Img (game.cpp): the loading screen's image, hint and objective for a level,
// from the original (0x000bedf0, register arguments) and from ours, over every level hashcode, several random
// generator states and every combination of missing outputs. Compared: the image, both texts, and the random
// generator's state afterwards (the hints are drawn from it, so a call drawn too many or too few times shows).
// Run from a menu replay script ("loadscreentest", MenuProbe.cpp) at the main menu, once the text bank is loaded;
// it puts the generator back afterwards.

#include "LoadScreenShadow.h"

#include "../../common/xbeOriginal.h"
#include "../game.h"

#include <stdio.h>
#include <string.h>

namespace {

// Rand_Random's state: two multiply-with-carry words and their multipliers.
#define RandState ((uint32_t *)0x0018cdf8)   // [4]

const uint32_t kSeeds[][2] = {
    { 0x1f123bb5, 0x159a55e5 }, { 0x00000001, 0x00000001 }, { 0x12345678, 0x9abcdef0 }, { 0xffffffff, 0x00000000 },
    { 0x0000beef, 0xdead0000 }, { 0x7fff7fff, 0x80008000 }, { 0x00010001, 0x00020002 }, { 0x55aa55aa, 0xaa55aa55 },
};

char *const kUntouched = (char *)0x0badf00d;   // what the outputs hold before a call, to see which it writes

// The original: level in EAX, hint pointer in ESI, objective pointer in ECX.
__declspec(naked) HASHCODE __cdecl CallOriginal(HASHCODE level, char **hint, char **objective) {
    __asm {
        push esi
        mov eax, [esp + 8]
        mov esi, [esp + 12]
        mov ecx, [esp + 16]
        mov edx, 0x000bedf0
        call edx
        pop esi
        ret
    }
}

struct Result {
    HASHCODE image;
    char *hint, *objective;
    uint32_t rand[2];
};

void Run(bool original, HASHCODE level, const uint32_t seed[2], int outputs, Result *r) {
    RandState[0] = seed[0];
    RandState[1] = seed[1];
    r->hint = kUntouched;
    r->objective = kUntouched;
    char **hint = (outputs & 1) ? &r->hint : NULL;
    char **objective = (outputs & 2) ? &r->objective : NULL;
    if (original) {
        XbeOriginalScope scope(0x000bedf0);
        r->image = CallOriginal(level, hint, objective);
    } else {
        r->image = ResetMap_LevelCode2ImgCore(level, hint, objective);
    }
    r->rand[0] = RandState[0];
    r->rand[1] = RandState[1];
}

}   // namespace

void LoadScreenShadow_Run(void) {
    uint32_t saved[4];
    memcpy(saved, RandState, sizeof(saved));

    HASHCODE levels[0x60];
    int count = 0;
    for (uint32_t level = 0x07000000; level <= 0x07000050; level++)
        levels[count++] = (HASHCODE)level;
    levels[count++] = (HASHCODE)0;
    levels[count++] = (HASHCODE)0xffffffff;
    levels[count++] = (HASHCODE)0x06ffffff;

    int cases = 0, differ = 0;
    for (int l = 0; l < count; l++) {
        for (const auto &seed : kSeeds) {
            for (int outputs = 0; outputs < 4; outputs++) {
                // Space Station D writes its hint through the objective pointer whenever there is a hint pointer:
                // with no objective pointer, the original (and ours) write to address 0.
                if (levels[l] == HT_Level_SpaceStationD && outputs == 1)
                    continue;
                Result a, b;
                Run(true, levels[l], seed, outputs, &a);
                Run(false, levels[l], seed, outputs, &b);
                cases++;
                if (memcmp(&a, &b, sizeof(a)) != 0) {
                    if (differ++ < 10)
                        printf("[loadscreen]   level 0x%08x seed %08x outputs %d: original %08x %p %p %08x,%08x / "
                               "ours %08x %p %p %08x,%08x\n", levels[l], seed[0], outputs, a.image, a.hint,
                               a.objective, a.rand[0], a.rand[1], b.image, b.hint, b.objective, b.rand[0], b.rand[1]);
                }
            }
        }
    }
    memcpy(RandState, saved, sizeof(saved));
    printf("[loadscreen] ResetMap_LevelCode2Img vs the original: %d cases, %d differ\n", cases, differ);
    fflush(stdout);
}
