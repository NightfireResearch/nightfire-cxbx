// Shadow tests for the player stats' loggers and the score tables' set-up (game/sp/PlayerStats.cpp): each function
// runs, the original and ours, on the same random mission stats, bond moments and score tables. Mission_Status and
// psiGetTimeIn100ths answer from ShadowHarness.cpp's fakes. Run at start with MenuShadowTests=on (MenuProbe.cpp);
// everything it touches is put back afterwards (the best scores are save data).
//
// Covered: PlrStat_Init, PlrStats_ResetScoring, PlrStats_SetupScoreTable, PlrStats_GetDrivingData and
// PlrStats_SetDrivingData (none of which the time demos reach but PlrStat_Init), PlrStat_LogShotFired,
// PlrStat_LogShotHitEnemy, PlrStat_LogShotHitScenery, PlarStat_LogUpdateElapsedTime, PlrStat_DoneBondMoment and,
// already ours before, PlarStat_LogTimerPause and PlarStat_LogTimerUnpause.

#include "PlrStatShadow.h"
#include "AllOriginal.h"
#include "ShadowHarness.h"

#include "../../common/xbeOriginal.h"
#include "../actionhelpers.h"
#include "../game/mp/multiplayer.h"
#include "../game/sp/PlayerStats.h"

#include <stdio.h>
#include <string.h>

namespace {

// The ScoringTable and the category tables it points at, as one block of the XBE's data
#define SCORE_DATA_START 0x0017e8f8
#define SCORING_TABLE_START 0x0017f198
#define SCORE_DATA_END (SCORING_TABLE_START + 12 * 0x3c)
#define SCORING_TABLE GameSpan<SCORETABLE>{SCORING_TABLE_START}
#define SCORE_DATA_SIZE (SCORE_DATA_END - SCORE_DATA_START)

#define BondMomentsBlock (GameSpan<uchar>{0x00278d8e})
#define BOND_MOMENTS_SIZE (113 * 2)
#define MissionStatsBlock ((uchar *)(size_t)XbeOurs(0x00278e70))
#define MISSION_STATS_SIZE (10 * 0x38)

struct World {
    uint8_t blokes[4][0x900];
    SCORETABLE saved[12];
    ScoreCategory savedCategories[12][9];
    uint bestScores[12];
    uint bondMoments[12];
};
World world;

ShadowRegions regions;
int cases, differ;
uint playerNum, moment, count;

void Compare(const char *what, int trial, uint32_t (*theirs)(void), uint32_t (*ours)(void)) {
    cases++;
    if (!ShadowCompare(regions, "plrstat", what, trial, theirs, ours))
        differ++;
}

uint RandomCount() {
    static const uint edges[] = {0, 1, 2, 0x7fffffff, 0x80000000, 0xffffffff};
    return ShadowRand(4) == 0 ? edges[ShadowRand(6)] : ShadowRand(50);
}

void RandomStats() {
    // The category tables and ScoringTable's figures, but not its hashcodes, thresholds and pointers
    for (uint32_t at = SCORE_DATA_START; at < SCORE_DATA_END; at++) {
        if (at >= SCORING_TABLE_START) {
            uint32_t field = (at - SCORING_TABLE_START) % 0x3c;
            if (field < 0x1c && field != 0x14)
                continue;
        }
        U8_AT(XbeOurs(at)) = (uchar)ShadowRand(256);
    }
    for (int i = 0; i < MISSION_STATS_SIZE; i += 4)
        *(uint *)(MissionStatsBlock + i) = RandomCount();
    for (int p = 0; p < 10; p++)
        MissionStatsBlock[p * 0x38 + 0x34] = ShadowRand(3) == 0;   // timerPaused
    for (int i = 0; i < BOND_MOMENTS_SIZE; i++)
        BondMomentsBlock[i] = ShadowRand(3) == 0;

    memset(world.blokes, 0, sizeof(world.blokes));
    for (int p = 0; p < 4; p++)
        glb_blokes[p] = (BLData *)world.blokes[p];
    for (int i = 0; i < 12; i++) {
        world.saved[i] = SCORING_TABLE[i];
        world.saved[i].statsTable = world.savedCategories[i];
        for (int c = 0; c < 9; c++) {
            uint *raw = (uint *)&world.savedCategories[i][c];
            for (int k = 0; k < 5; k++)
                raw[k] = ShadowRand(0x10000) * 0x10001u;
        }
        world.saved[i].timeTaken = ShadowRand(1000);
        world.saved[i].bestScore = ShadowRand(1000);
        world.saved[i].score = ShadowRand(1000);
        world.bestScores[i] = ShadowRand(100000);
        world.bondMoments[i] = ShadowRand(20);
    }
    MPSettings.isMultiplayer = ShadowRand(4) == 0;
    ShadowFakes.missionStatus = ShadowRand(8);
    ShadowFakes.time = ShadowRand(100000);
}

typedef void (__cdecl *VoidFn)(void);
typedef void (__cdecl *PlayerFn)(uint);

void TestTables() {
    for (int trial = 0; trial < 50; trial++) {
        RandomStats();
        Compare("PlrStat_Init", trial,
                [] {
                    XbeOriginalScope original(0x000b19b0);
                    ((VoidFn)0x000b19b0)();
                    return 0u;
                },
                [] {
                    PlrStat_Init();
                    return 0u;
                });
        RandomStats();
        Compare("PlrStats_ResetScoring", trial,
                [] {
                    XbeOriginalScope original(0x000b13d0);
                    ((VoidFn)0x000b13d0)();
                    return 0u;
                },
                [] {
                    PlrStats_ResetScoring();
                    return 0u;
                });
        RandomStats();
        count = ShadowRand(13);
        Compare("PlrStats_SetupScoreTable", trial,
                [] {
                    XbeOriginalScope original(0x000b1530);
                    ((void (__cdecl *)(SCORETABLE *, uint))0x000b1530)(world.saved, count);
                    return 0u;
                },
                [] {
                    PlrStats_SetupScoreTable(world.saved, count);
                    return 0u;
                });
        RandomStats();
        Compare("PlrStats_GetDrivingData", trial,
                [] {
                    XbeOriginalScope original(0x000b1250);
                    ((void (__cdecl *)(uint *, uint *))0x000b1250)(world.bestScores, world.bondMoments);
                    return 0u;
                },
                [] {
                    PlrStats_GetDrivingData(world.bestScores, world.bondMoments);
                    return 0u;
                });
        RandomStats();
        Compare("PlrStats_SetDrivingData", trial,
                [] {
                    XbeOriginalScope original(0x000b1310);
                    ((void (__cdecl *)(uint *, uint *))0x000b1310)(world.bestScores, world.bondMoments);
                    return 0u;
                },
                [] {
                    PlrStats_SetDrivingData(world.bestScores, world.bondMoments);
                    return 0u;
                });
    }
}

void TestLoggers() {
    static const uint players[] = {0, 1, 2, 3, 9, 10, 11, 0xffffffff};
    for (int trial = 0; trial < 800; trial++) {
        RandomStats();
        playerNum = players[ShadowRand(8)];
        Compare("PlrStat_LogShotFired", trial,
                [] {
                    XbeOriginalScope original(0x000b07a0);
                    ((PlayerFn)0x000b07a0)(playerNum);
                    return 0u;
                },
                [] {
                    PlrStat_LogShotFired(playerNum);
                    return 0u;
                });
        Compare("PlrStat_LogShotHitEnemy", trial,
                [] {
                    XbeOriginalScope original(0x000b06f0);
                    ((PlayerFn)0x000b06f0)(playerNum);
                    return 0u;
                },
                [] {
                    PlrStat_LogShotHitEnemy(playerNum);
                    return 0u;
                });
        Compare("PlrStat_LogShotHitScenery", trial,
                [] {
                    XbeOriginalScope original(0x000b0740);
                    ((PlayerFn)0x000b0740)(playerNum);
                    return 0u;
                },
                [] {
                    PlrStat_LogShotHitScenery(playerNum);
                    return 0u;
                });
        Compare("PlarStat_LogUpdateElapsedTime", trial,
                [] {
                    XbeOriginalScope original(0x000b05c0);
                    ((PlayerFn)0x000b05c0)(playerNum);
                    return 0u;
                },
                [] {
                    PlarStat_LogUpdateElapsedTime(playerNum);
                    return 0u;
                });
        Compare("PlarStat_LogTimerPause", trial,
                [] {
                    XbeOriginalScope original(0x000b0570);
                    ((PlayerFn)0x000b0570)(playerNum);
                    return 0u;
                },
                [] {
                    PlarStat_LogTimerPause(playerNum);
                    return 0u;
                });
        Compare("PlarStat_LogTimerUnpause", trial,
                [] {
                    XbeOriginalScope original(0x000b0590);
                    ((PlayerFn)0x000b0590)(playerNum);
                    return 0u;
                },
                [] {
                    PlarStat_LogTimerUnpause(playerNum);
                    return 0u;
                });
        // glb_blokes has four players, and the moment sets the player's timer
        if (playerNum > 3 && playerNum < 10)
            playerNum = 3;
        moment = 0x60 + ShadowRand(0x12);
        Compare("PlrStat_DoneBondMoment", trial,
                [] {
                    XbeOriginalScope original(0x000b0500);
                    ((void (__cdecl *)(uint, ushort))0x000b0500)(playerNum, moment);
                    return 0u;
                },
                [] {
                    PlrStat_DoneBondMoment(playerNum, moment);
                    return 0u;
                });
    }
}

} // namespace

void PlrStatShadow_Run(void) {
    static uchar scores[SCORE_DATA_SIZE], bondMoments[BOND_MOMENTS_SIZE], missionStats[MISSION_STATS_SIZE];
    static BLData *blokes[4];
    XbeOursRead(scores, SCORE_DATA_START, sizeof(scores));
    XbeOursRead(bondMoments, BondMomentsBlock.at, sizeof(bondMoments));
    memcpy(missionStats, MissionStatsBlock, sizeof(missionStats));
    memcpy(blokes, glb_blokes, sizeof(blokes));
    undefined4 multiplayer = MPSettings.isMultiplayer;

    regions.Clear();
    regions.Add((void *)SCORE_DATA_START, SCORE_DATA_SIZE);
    regions.Add(BondMomentsBlock.game(), BOND_MOMENTS_SIZE);
    regions.Add(MissionStatsBlock, MISSION_STATS_SIZE);
    regions.Add(&world, sizeof(world));

    memset(&ShadowFakes, 0, sizeof(ShadowFakes));
    ShadowFakes_Install();
    NfWarnMuted++;

    cases = differ = 0;
    TestTables();
    TestLoggers();

    NfWarnMuted--;
    ShadowHook_RemoveAll();
    XbeOursWrite(SCORE_DATA_START, scores, sizeof(scores));
    XbeOursWrite(BondMomentsBlock.at, bondMoments, sizeof(bondMoments));
    memcpy(MissionStatsBlock, missionStats, sizeof(missionStats));
    memcpy(glb_blokes, blokes, sizeof(blokes));
    MPSettings.isMultiplayer = multiplayer;

    printf("[plrstat] PlrStat_*, PlrStats_*: %d cases, %d differ\n", cases, differ);
}
