#include "ui.h"
#include "Menu.h"

#include "../game.h"

// The campaign's progress and what it unlocks: the missions open (sp_level[].enabled), and each codename's rewards
// - a 64-bit mask per player (docs/ui/secrets.md, "How the bonus bits work") - which open multiplayer characters,
// scenarios and gadget upgrades.

#define menu_bonus              ((ulong *)0x0025d6e8)   // 4 players x {lo, hi}
#define menu_bonus_replace      U8_AT(0x0025d78d)       // Menu_SetBonus always replaces, never ORs
#define menu_level_bonus_earned U8_AT(0x0025d78c)       // Menu_SetLevelBonus gave a new reward
#define mp_explosive_scenery_unlocked U8_AT(0x002456a8)

// AUTOGEN
void PlrStarts_ProcessRewardCounter(ulong *bonus, HASHCODE hashcode, uint upgradeLevel, char set, REWARDINFO_tag *op);
// AUTOGEN
void ResetUpgrade(byte player);
// AUTOGEN
void Set_Upgrade(byte player, short objId, undefined1 level);

// The level whose rewards are slot 'index' of the rewards table, as sp_level lists them.
static HASHCODE LevelAt(uint index) {
    for (uint i = 0; i < ARRAY_SIZE(sp_level); i++)
        if (i == index)
            return (HASHCODE)sp_level[i].identifier;
    return (HASHCODE)0xffffffff;
}

// The missions open, a bit each.
// AUTOINJECT
uint __stdcall Menu_GetNightfireStatus(void) {
    uint status = 0;
    for (uint i = 0; i < ARRAY_SIZE(sp_level); i++)
        if (ITEM_ENABLED(sp_level[i]))
            status |= 1u << i;
    return status;
}

// AUTOINJECT
void Menu_SetNightfireStatus(uint status) {
    if (status == 0)
        status = 3;   // the first two missions are always open
    for (uint i = 0; i < ARRAY_SIZE(sp_level); i++)
        ITEM_ENABLED(sp_level[i]) = (status & (1u << i)) != 0;
}

// Sets (or ORs in) a player's rewards and rebuilds their gadget upgrades from them.
// AUTOINJECT
void Menu_SetBonus(uint lo, uint hi, byte player, char orIn) {
    ulong *bonus = &menu_bonus[player * 2];
    if (!menu_bonus_replace && orIn) {
        bonus[0] |= lo;
        bonus[1] |= hi;
    } else {
        bonus[0] = lo;
        bonus[1] = hi;
    }
    ResetUpgrade(player);
    REWARDINFO_tag info;
    for (uint level = 0; level < 12; level++)
        for (uint slot = 1; slot < 5; slot++) {
            PlrStarts_ProcessRewardCounter(bonus, LevelAt(level), slot, 0, &info);
            if (info.hasMedal && info.objType == REWARD_UPGRADE)
                Set_Upgrade(player, (short)info.objId, (undefined1)info.upgradeLevel);
        }
}

// A mission finished with a medal of 'medal' (1-4): the next mission opens (after Equinox, the last, the game is
// complete), and the rewards up to that medal are given. True for Equinox.
// AUTOINJECT
bool Menu_SetLevelBonus(int level, uint medal, byte player) {
    ulong bonus[2] = { menu_bonus[0], menu_bonus[1] };   // the first player's, whoever finished it
    menu_level_bonus_earned = 0;
    int index = 0;
    while (index < ARRAY_SIZE(sp_level) && (int)sp_level[index].identifier != level)
        index++;
    if (index == ARRAY_SIZE(sp_level)) {
        // As the original, which then marks sp_level[-1] - in the game's memory the byte at 0x17c578, which is what
        // it still is - and opens the first mission.
        index = -1;
        U8_AT(0x0017c578) = 1;
    } else {
        ITEM_ENABLED(sp_level[index]) = 1;
    }
    if (level != HT_Level_SpaceStationD)
        ITEM_ENABLED(sp_level[index + 1]) = 1;
    else
        GameState.WeaponUpgradeRelated = 1;
    bool lastMission = level == HT_Level_SpaceStationD;
    REWARDINFO_tag info = {};
    if (medal < 5)
        for (; (int)medal > 0; medal--) {
            PlrStarts_ProcessRewardCounter(bonus, (HASHCODE)level, medal, 0, &info);
            if (!info.hasMedal) {
                PlrStarts_ProcessRewardCounter(bonus, (HASHCODE)level, medal, 1, &info);
                menu_level_bonus_earned = 1;
            }
        }
    Menu_SetBonus(bonus[0], bonus[1], player, 0);
    return lastMission;
}

// How far a weapon or gadget is upgraded for a player (0 = not at all).
// AUTOINJECT
undefined4 Menu_GetObjectUpgradeLevel(uint objId, byte player) {
    if (objId > 0x3e)
        return 0;
    REWARDINFO_tag info;
    for (uint level = 0; level < 12; level++)
        for (uint slot = 1; slot < 5; slot++) {
            PlrStarts_ProcessRewardCounter(&menu_bonus[player * 2], LevelAt(level), slot, 0, &info);
            if (info.hasMedal && info.objType == REWARD_UPGRADE && info.objId == objId)
                return info.upgradeLevel;
        }
    return 0;
}

// The rewards of one player, or of all four (0xff) OR-ed together.
static void PlayerRewards(byte player, ulong out[2]) {
    if (player == 0xff) {
        out[0] = menu_bonus[6] | menu_bonus[4] | menu_bonus[2] | menu_bonus[0];
        out[1] = menu_bonus[7] | menu_bonus[5] | menu_bonus[3] | menu_bonus[1];
    } else {
        out[0] = menu_bonus[player * 2];
        out[1] = menu_bonus[player * 2 + 1];
    }
}

// The multiplayer characters that are rewards: reward id (0x26-0x37) -> mp_characters index. 0x2c is unused.
static const struct { uint objId; uchar character; } reward_characters[] = {
    { 0x26, 0x1a }, { 0x27, 0x0c }, { 0x28, 0x14 }, { 0x29, 0x1b }, { 0x2a, 0x11 }, { 0x2b, 0x18 },
    { 0x2d, 0x19 }, { 0x2e, 0x15 }, { 0x2f, 0x0f }, { 0x30, 0x16 }, { 0x31, 0x17 }, { 0x32, 0x13 },
    { 0x33, 0x12 }, { 0x34, 0x10 }, { 0x35, 0x1c }, { 0x36, 0x0d }, { 0x37, 0x0e },
};

// Locks the reward characters (items 12-28) and opens those the player (or anyone, 0xff) has earned.
// AUTOINJECT
void Menu_UnlockMPSkins(byte player) {
    ulong bonus[2];
    PlayerRewards(player, bonus);
    for (int i = 0; i < ARRAY_SIZE(reward_characters); i++) {
        ITEM_ENABLED(mp_characters[reward_characters[i].character]) = 0;
        ITEM_ENABLED(mp_characters_small[reward_characters[i].character]) = 0;
    }
    REWARDINFO_tag info;
    for (uint level = 0; level < 12; level++)
        for (uint slot = 1; slot < 5; slot++) {
            PlrStarts_ProcessRewardCounter(bonus, LevelAt(level), slot, 0, &info);
            if (!info.hasMedal || info.objType != REWARD_MP_CHARACTER)
                continue;
            for (int i = 0; i < ARRAY_SIZE(reward_characters); i++)
                if (reward_characters[i].objId == info.objId) {
                    ITEM_ENABLED(mp_characters[reward_characters[i].character]) = 1;
                    ITEM_ENABLED(mp_characters_small[reward_characters[i].character]) = 1;
                }
        }
}

// The multiplayer scenarios that are rewards: reward id (0x38-0x3d) -> mp_scenario index.
static const struct { uint objId; uchar scenario; } reward_scenarios[] = {
    { 0x38, 10 }, { 0x39, 4 }, { 0x3a, 12 }, { 0x3b, 6 }, { 0x3c, 7 }, { 0x3d, 9 },
};
#define REWARD_EXPLOSIVE_SCENERY 0x3e

// Opens the reward scenarios, and the Explosive Scenery option, that any player has earned.
// AUTOINJECT
void Menu_UnlockMPSettings(void) {
    ulong bonus[2];
    PlayerRewards(0xff, bonus);
    for (int i = 0; i < ARRAY_SIZE(reward_scenarios); i++)
        ITEM_ENABLED(mp_scenario[reward_scenarios[i].scenario]) = 0;
    mp_explosive_scenery_unlocked = 0;
    REWARDINFO_tag info;
    for (uint level = 0; level < 12; level++)
        for (uint slot = 1; slot < 5; slot++) {
            PlrStarts_ProcessRewardCounter(bonus, LevelAt(level), slot, 0, &info);
            if (!info.hasMedal)
                continue;
            if (info.objType == REWARD_MP_SCENARIO) {
                for (int i = 0; i < ARRAY_SIZE(reward_scenarios); i++)
                    if (reward_scenarios[i].objId == info.objId)
                        ITEM_ENABLED(mp_scenario[reward_scenarios[i].scenario]) = 1;
            } else if (info.objType == REWARD_MP_MODIFIER && info.objId == REWARD_EXPLOSIVE_SCENERY) {
                mp_explosive_scenery_unlocked = 1;
            }
        }
}
