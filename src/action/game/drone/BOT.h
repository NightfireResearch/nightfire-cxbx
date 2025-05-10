#ifndef BOT_H
#define BOT_H

#include "../../actionhelpers.h"

#pragma pack(push, 1)
typedef struct BOT_stats_t {
    char unknown_1[2];
    char baseAggression; // 0x02
    char unknown_2;
    char health; // 0x04
    char unknown_3[4];
    char isBad;
    char unknown_4[4];
} BOT_stats_t;

static_assert(sizeof(BOT_stats_t) == 0xe, "BOT_stats_t size mismatch");
static_assert(offsetof(BOT_stats_t, isBad) == 0x9, "BOT_stats_t isBad offset mismatch");

#pragma pack(pop)

BOT_stats_t* BOT_getDefaultStats(uint identifier);

#endif