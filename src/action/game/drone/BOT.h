#ifndef BOT_H
#define BOT_H

#include "../../actionhelpers.h"

#pragma pack(push, 1)
// A multiplayer bot's statistics: the defaults per character (BOT_getDefaultStats, 29 entries at 0x00163628, indexed by
// the mp_characters id), copied into each bot and edited on the bot setup page. BOT_setDroneStats puts them in the
// bot's drone. See docs/drone/bots-and-navigation/README.md, 5.3.
typedef struct BOT_stats_t {
    uchar accuracy;             // 0x00 - lower is better: 1 very good, 3 good, 5 average, 8 poor
    uchar unused1;              // 0x01 - 0 in every entry
    uchar baseAggression;       // 0x02 - 2 normal, 3 high, 4 very high
    uchar unused3;              // 0x03 - 0 in every entry
    ushort health;              // 0x04 - 50..300 (Jaws has 300: the one value over 255, so this is 16-bit)
    uchar speed;                // 0x06 - 0 slow, 1 normal, 2 fast
    uchar reaction;             // 0x07 - 50..200
    uchar recover;              // 0x08 - 50..200: frames without sight after being hit, x2/3
    uchar isBad;                // 0x09 - on the Phoenix side
    uchar preferredWeaponClass; // 0x0a - 0 none, 1..5
    uchar personality;          // 0x0b - 0 None, 1 Collector, 2 Guardian, 3 Team Player, 4 Judge, 5 Berserker,
                                //        6 Greedy, 7 Vengeful, 8 Assassin
    uchar traitFlags;           // 0x0c - 0x10 regenerates health, 8 attacks on sight, 4 prefers fists up close; 1, 2
                                //        only partly decoded
    uchar editable;             // 0x0d - 1 for the characters whose stats the bot setup page may change
} BOT_stats_t;

static_assert(sizeof(BOT_stats_t) == 0xe, "BOT_stats_t size mismatch");
static_assert(offsetof(BOT_stats_t, health) == 0x4, "BOT_stats_t health offset mismatch");
static_assert(offsetof(BOT_stats_t, isBad) == 0x9, "BOT_stats_t isBad offset mismatch");
static_assert(offsetof(BOT_stats_t, editable) == 0xd, "BOT_stats_t editable offset mismatch");

#pragma pack(pop)

BOT_stats_t* BOT_getDefaultStats(uint identifier);
bool BOT_respawn(obj_tag* gameObj, int playerNum, char param_3);

#endif