// Shadow test for PlrStat_GetScore (src/action/game/sp/PlayerStats.cpp): the original and ours work out the score
// of every ScoringTable level from the same player stats, and must leave the same score tables behind.
//
// The original does its sums on the x87 FPU and ours with SSE2, so this is mostly a check that the points and
// ratings round the same way: for each level, mission status (running, quit, complete), difficulty and player 0/1,
// a few hand-picked stat fillings (edge cases: no shots, detections at the allowance, par time) and many random
// ones. Before each call the score tables are put back as they were, so both start from the same bestScore; after
// the pair, the return value, the whole ScoringTable, the per-level category tables and DoneBetter are compared.
//
// The original uses the game's copies of NewScoresRef (0x002790a0) and DoneBetter (0x002790a4), which nothing else
// uses now; ours use PlayerStats.cpp's. Run at start with MenuShadowTests=on (MenuProbe.cpp); everything it
// touches is put back afterwards (bestScore is save data).

#include "ScoreShadow.h"

#include "../../common/xbeOriginal.h"
#include "../actionhelpers.h"
#include "../game.h"
#include "../game/mp/multiplayer.h"
#include "../game/sp/Mission.h"
#include "../game/sp/PlayerStats.h"

#include <stdio.h>
#include <string.h>

static const unsigned kPlrStatGetScore = 0x000b0900;

// The game's copies
#define GameNewScoresRef U32_AT(0x002790a0)
#define GameDoneBetter U8_AT(0x002790a4)

// The ScoringTable and the category tables it points at, as one block of the XBE's data
#define SCORE_DATA_START 0x0017e8f8
#define SCORE_DATA_END (0x0017f198 + 12 * 0x3c)
#define SCORING_TABLE ((SCORETABLE *)0x0017f198)

// PlrMissionStats[0], as PlayerStats.cpp lays it out
#pragma pack(push, 1)
typedef struct {
    uint timesDetected, shotsFired, shotsHitEnemy, enemiesDispatched, enemiesDisabled, enemiesSurrendered,
        enemiesSpawned;
    float health;
    uint bondMoments, bondBonuses, unknown3;
    int timerUnpauseTime;
    uint timeElapsed;
    char timerPaused;
    char unknown4[3];
} MissionStats;
#pragma pack(pop)
static_assert(sizeof(MissionStats) == 0x38, "MissionStats is PlayerMissionStats");
#define Stats (*(MissionStats *)0x00278e70)

typedef undefined4 *(__cdecl *GetScoreFn)(char);

static uint32_t rng = 0x5eed;
static uint32_t Rand(uint32_t n) {
    rng = rng * 1103515245u + 12345u;
    return n ? (rng >> 8) % n : 0;
}

static int mismatches, reported;

static void Fill(int kind, undefined4 (*driving)[9][3]) {
    memset(&Stats, 0, sizeof(Stats));
    switch (kind) {
    case 0: break;                                  // nothing done at all (no shots: perfect accuracy)
    case 1:                                          // detections exactly at a small allowance, par-ish time
        Stats.timesDetected = 3; Stats.shotsFired = 100; Stats.shotsHitEnemy = 50; Stats.health = 37.5f;
        Stats.timeElapsed = 60000; Stats.bondMoments = 5; Stats.bondBonuses = 3;
        break;
    default:
        Stats.timesDetected = Rand(12);
        Stats.shotsFired = Rand(600);
        Stats.shotsHitEnemy = Rand(Stats.shotsFired + 1);
        Stats.enemiesDispatched = Rand(80);
        Stats.enemiesDisabled = Rand(40);
        Stats.enemiesSurrendered = Rand(20);
        Stats.health = (float)Rand(100001) / 1000.0f;
        Stats.bondMoments = Rand(15);
        Stats.bondBonuses = Rand(10);
        Stats.timeElapsed = Rand(8) == 0 ? Rand(200) : Rand(200000);
        break;
    }
    for (int i = 0; i < 9; i++) {
        (*driving)[i][0] = Rand(kind < 2 ? 1 : 2000);
        (*driving)[i][1] = Rand(kind < 2 ? 1 : 600) + (Rand(4) ? 1 : 0);
        (*driving)[i][2] = Rand(3000);
    }
}

void ScoreShadow_Run(void) {
    static uchar saved[SCORE_DATA_END - SCORE_DATA_START], start[SCORE_DATA_END - SCORE_DATA_START];
    static uchar theirs[SCORE_DATA_END - SCORE_DATA_START];
    memcpy(saved, (void *)SCORE_DATA_START, sizeof(saved));
    MissionStats savedStats = Stats;
    HASHCODE savedBaseMap = Mission_BaseMapHCode();
    undefined4 savedStatus = Mission_Status();
    undefined4 savedDifficulty = GameState.difficultyModifier;
    undefined4 savedMultiplayer = MPSettings.isMultiplayer;
    undefined4 (*savedNewScores)[9][3] = NewScoresRef;
    uint32_t savedGameNewScores = GameNewScoresRef;
    uchar savedGameDoneBetter = GameDoneBetter;

    static undefined4 driving[9][3];
    MPSettings.isMultiplayer = 0;
    NewScoresRef = &driving;
    GameNewScoresRef = (uint32_t)(uintptr_t)&driving;

    // Some best scores to beat or not
    for (int i = 0; i < 12; i++)
        SCORING_TABLE[i].bestScore = Rand(3) ? Rand(20000) : 0;
    memcpy(start, (void *)SCORE_DATA_START, sizeof(start));

    static const undefined4 statuses[] = {1, 3, 6};
    int runs = 0;
    mismatches = 0;
    reported = 0;
    for (int level = 0; level < 12; level++) {
        Mission_SetMapHCode(SCORING_TABLE[level].levelHashcode);
        for (int fill = 0; fill < 60; fill++) {
            for (int s = 0; s < 3; s++) {
                Mission_SetStatus(statuses[s]);
                for (undefined4 difficulty = 1; difficulty <= 3; difficulty++) {
                    GameState.difficultyModifier = difficulty;
                    for (char player = 0; player < 2; player++) {
                        uint32_t seed = rng;
                        Fill(fill, &driving);

                        memcpy((void *)SCORE_DATA_START, start, sizeof(start));
                        GameDoneBetter = 0xcc;
                        undefined4 *a;
                        {
                            XbeOriginalScope original(kPlrStatGetScore);
                            a = ((GetScoreFn)(uintptr_t)kPlrStatGetScore)(player);
                        }
                        memcpy(theirs, (void *)SCORE_DATA_START, sizeof(theirs));
                        bool theirDone = GameDoneBetter != 0;

                        rng = seed;
                        Fill(fill, &driving);
                        memcpy((void *)SCORE_DATA_START, start, sizeof(start));
                        undefined4 *b = PlrStat_GetScore(player);
                        runs++;

                        bool same = a == b && memcmp(theirs, (void *)SCORE_DATA_START, sizeof(theirs)) == 0;
                        // The original leaves DoneBetter alone for player 1 (0xcc stays); ours is not reset between
                        // calls either, so compare only where the original wrote it
                        if (GameDoneBetter != 0xcc && theirDone != PlrStats_DoneBetter())
                            same = false;
                        if (!same) {
                            mismatches++;
                            if (reported++ < 10) {
                                int at = -1;
                                for (int k = 0; k < (int)sizeof(theirs); k++)
                                    if (theirs[k] != ((uchar *)SCORE_DATA_START)[k]) { at = k; break; }
                                printf("[score] level %d fill %d status %u difficulty %u player %d: differs at 0x%08x "
                                       "(theirs %02x, ours %02x), return %p/%p, DoneBetter %d/%d\n",
                                       level, fill, statuses[s], difficulty, player,
                                       at < 0 ? 0 : SCORE_DATA_START + at, at < 0 ? 0 : theirs[at],
                                       at < 0 ? 0 : ((uchar *)SCORE_DATA_START)[at], (void *)a, (void *)b, theirDone,
                                       PlrStats_DoneBetter());
                            }
                        }
                    }
                }
            }
        }
    }
    printf("[score] PlrStat_GetScore: %d runs, %d mismatches\n", runs, mismatches);

    memcpy((void *)SCORE_DATA_START, saved, sizeof(saved));
    Stats = savedStats;
    Mission_SetMapHCode(savedBaseMap);
    Mission_SetStatus(savedStatus);
    GameState.difficultyModifier = savedDifficulty;
    MPSettings.isMultiplayer = savedMultiplayer;
    NewScoresRef = savedNewScores;
    GameNewScoresRef = savedGameNewScores;
    GameDoneBetter = savedGameDoneBetter;
}
