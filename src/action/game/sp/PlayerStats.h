#ifndef PLAYERSTATS_H
#define PLAYERSTATS_H

#include "../../actionhelpers.h"

#pragma pack(push, 1)

// The scored categories, in the order of every level's category table. A driving level fills the same nine slots
// from the driving engine's results (PTPDATA.Scoring), whose meaning per slot is the driving engine's.
enum {
    SCORE_BOND_MOMENTS = 0,
    SCORE_ENEMIES_DISPATCHED = 1,
    SCORE_ENEMIES_DISABLED = 2,
    SCORE_ENEMIES_SURRENDERED = 3,
    SCORE_TIMES_DETECTED = 4,   // an allowance: full points up to target, less for each detection over it
    SCORE_ACCURACY = 5,         // achieved = target * hits / shots
    SCORE_HEALTH = 6,
    SCORE_TIME = 7,             // target = par in seconds, achieved in centiseconds
    SCORE_BOND_BONUSES = 8,     // all or nothing
    SCORE_NUM_CATEGORIES = 9
};

// One category of a level's score (PlrStat_GetScore). The level tables (0x0017e8f8, 0xb8 apart: nine of these and
// 4 bytes nobody uses) hold target and maxPoints; the rest is filled in each time the score is worked out.
typedef struct ScoreCategory {
    uint target;      // 0x00 what earns full points (a count, a percentage, par seconds); 0 = not scored here
    uint maxPoints;   // 0x04
    uint achieved;    // 0x08 what the player did
    float rating;     // 0x0c 0..1, rounded to a whole percent
    float points;     // 0x10 points awarded (whole numbers); wiped unless the mission was completed
} ScoreCategory;

static_assert(sizeof(ScoreCategory) == 0x14, "ScoreCategory size mismatch");

typedef struct SCORETABLE {
    HASHCODE levelHashcode;
    uint thresholdScoreBronze;
    uint thresholdScoreSilver;
    uint thresholdScoreGold;
    uint thresholdScorePlatinum;
    float detectionPenalty;        // 0x14 rating lost per detection over the allowance; 0 means 0.03
    ScoreCategory *statsTable;     // 0x18 SCORE_NUM_CATEGORIES of them
    uint timeTaken;                // 0x1c centiseconds (the time category's achieved)
    uint parTime;                  // 0x20 seconds (the time category's target)
    uint difficultyMultiplier;     // 0x24 ScoringTableDifMul for the difficulty: 0, 1 or 2
    uint difficultyBonus;          // 0x28 difficultyMultiplier * baseScore
    uint baseScore;                // 0x2c the categories' points added up
    uint bestScore;                // 0x30 best score so far (player 0); never wiped
    uint score;                    // 0x34 baseScore + difficultyBonus
    char isAction;                 // 0x38 0 on a driving level
    char unknown3[3];
} SCORETABLE;

static_assert(sizeof(SCORETABLE) == 0x3c, "SCORETABLE size mismatch");
static_assert(offsetof(SCORETABLE, detectionPenalty) == 0x14, "SCORETABLE detectionPenalty offset");
static_assert(offsetof(SCORETABLE, statsTable) == 0x18, "SCORETABLE statsTable offset");
static_assert(offsetof(SCORETABLE, timeTaken) == 0x1c, "SCORETABLE timeTaken offset");
static_assert(offsetof(SCORETABLE, parTime) == 0x20, "SCORETABLE parTime offset");
static_assert(offsetof(SCORETABLE, difficultyMultiplier) == 0x24, "SCORETABLE difficultyMultiplier offset");
static_assert(offsetof(SCORETABLE, difficultyBonus) == 0x28, "SCORETABLE difficultyBonus offset");
static_assert(offsetof(SCORETABLE, baseScore) == 0x2c, "SCORETABLE baseScore offset");
static_assert(offsetof(SCORETABLE, bestScore) == 0x30, "SCORETABLE bestScore offset");
static_assert(offsetof(SCORETABLE, score) == 0x34, "SCORETABLE score offset");
static_assert(offsetof(SCORETABLE, isAction) == 0x38, "SCORETABLE isAction offset");

#pragma pack(pop)

bool PlrStat_OkToUpdate(void);
void PlrStat_LogBondBonus(uint playerNum);
void PlrStat_ResetForMission(void);
void PlrStat_LogEnemySurrender(uint playerNum);
void PlrStat_LogEnemySpawned(void);
void PlrStat_LogEnemyDispatched(uint playerNum);
void PlrStat_LogEnemyDisabled(uint playerNum);
void PlrStat_LogEnemyDetectedPlayer(uint playerNum);
void PlrStat_LogHealth(float health, uint playerNum);
void PlrStat_LogShotHitScenery(uint playerNum);
SCORETABLE * PlrStats_GetLevelTotals(HASHCODE hashcode);
void PlarStat_LogTimerPause(uint playerNum);
void PlarStat_LogTimerUnpause(uint playerNum);
bool PlrStats_HasGoldMedal(void);
SCORETABLE * PlrStats_GetScoreTable(undefined4 *numItems);
bool PlrStats_DoneBetter(void);
undefined4 *PlrStat_GetScore(char playerNum);

// The driving engine's results (PTPDATA.Scoring), set by bootup_bootup; owned by PlayerStats.cpp
extern undefined4 (*NewScoresRef)[9][3];

#endif // PLAYERSTATS_H