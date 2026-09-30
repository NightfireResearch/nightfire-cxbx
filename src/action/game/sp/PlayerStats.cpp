#include "PlayerStats.h"
#include "../mp/multiplayer.h"
#include "../../game.h"

#include "string.h"

#pragma pack(push, 1)
typedef struct {
    uint timesDetected;
    uint shotsFired;
    uint shotsHitEnemy;
    uint enemiesDispatched;
    uint enemiesDisabled;
    uint enemiesSurrendered;
    uint enemiesSpawned;
    float health;             // 0x1c - PlrStat_LogHealth is handed a float (Player_SetHealth passes BLData.health)
    uint bondMoments;         // 0x20
    uint bondBonuses;         // 0x24
    uint unknown3;            // 0x28
    int timerUnpauseTime;     // 0x2c (Ghidra's name)
    uint timeElapsed;         // 0x30 centiseconds; PlarStat_LogUpdateElapsedTime writes it
    char timerPaused;
    char unknown4[3];
} PlayerMissionStats;

static_assert(sizeof(PlayerMissionStats) == 0x38, "PlayerMissionStats size mismatch");
static_assert(offsetof(PlayerMissionStats, health) == 0x1c, "PlayerMissionStats health offset");
static_assert(offsetof(PlayerMissionStats, bondMoments) == 0x20, "PlayerMissionStats bondMoments offset");
static_assert(offsetof(PlayerMissionStats, timeElapsed) == 0x30, "PlayerMissionStats timeElapsed offset");
static_assert(offsetof(PlayerMissionStats, timerPaused) == 0x34, "PlayerMissionStats timerPaused offset");

#pragma pack(pop)

#define PlrMissionStats (*(PlayerMissionStats(*)[10])0x00278e70)
#define BondMoments (*(short(*)[113])0x00278d8e)

// AUTOINJECT
void PlrStat_ResetForMission(void) {

    memset(PlrMissionStats, 0, sizeof(PlrMissionStats));

    // 0x65 to 0x6e inclusive, maybe got unrolled in Xbox code
    for(int i = 0x65; i <= 0x6e; i++) {
        BondMoments[i] = 0;
    }
}

// Likely a helper function which was inlined into the below funcs
// AUTOINJECT
bool PlrStat_OkToUpdate(void) {

  if(!MPSettings.isMultiplayer) {
    int missionStatus = Mission_Status();
    if (missionStatus > 1 || missionStatus < 0)
      return false;
  }

  return true;
}

// AUTOINJECT
void PlrStat_LogBondBonus(uint playerNum) {

    if(!PlrStat_OkToUpdate())
        return;

    PlrMissionStats[0].bondBonuses++;
}

// AUTOINJECT
void PlrStat_LogEnemySurrender(uint playerNum) {

    if(playerNum >= ARRAY_SIZE(PlrMissionStats)) {
        NF_WARN("ERROR : Invalid Plr ID\n"); // GC check
        return;
    }

    if(!PlrStat_OkToUpdate())
        return;

    PlrMissionStats[playerNum].enemiesSurrendered++;
}

// AUTOINJECT
void PlrStat_LogEnemyDispatched(uint playerNum) {

    if(playerNum >= ARRAY_SIZE(PlrMissionStats)) {
        NF_WARN("ERROR : Invalid Plr ID\n"); // GC check
        return;
    }
    
    if(!PlrStat_OkToUpdate())
        return;

    PlrMissionStats[playerNum].enemiesDispatched++;
}

// AUTOINJECT
void PlrStat_LogEnemyDisabled(uint playerNum) {

    if(playerNum >= ARRAY_SIZE(PlrMissionStats)) {
        NF_WARN("ERROR : Invalid Plr ID\n"); // GC check
        return;
    }
    
    if(!PlrStat_OkToUpdate())
        return;

    PlrMissionStats[playerNum].enemiesDisabled++;
}

// AUTOINJECT
void PlrStat_LogEnemyDetectedPlayer(uint playerNum) {

    if(playerNum >= ARRAY_SIZE(PlrMissionStats)) {
        NF_WARN("ERROR : Invalid Plr ID\n"); // GC check
        return;
    }
    
    if(!PlrStat_OkToUpdate())
        return;

    PlrMissionStats[playerNum].timesDetected++;
}

// AUTOINJECT
void PlrStat_LogHealth(float health, uint playerNum) {

    if(playerNum >= ARRAY_SIZE(PlrMissionStats)) {
        NF_WARN("ERROR : Invalid Plr ID\n"); // GC check
        return;
    }
    
    if(!PlrStat_OkToUpdate())
        return;
    
    PlrMissionStats[playerNum].health = health;
}

// AUTOINJECT
void PlarStat_LogTimerPause(uint playerNum) {
    
    if (playerNum >= ARRAY_SIZE(PlrMissionStats)) {
        NF_WARN("ERROR : Invalid Plr ID\n"); // GC check
        return;
    }

    PlrMissionStats[playerNum].timerPaused = 1;
}

// AUTOINJECT
void PlarStat_LogTimerUnpause(uint playerNum) {
    
    if (playerNum >= ARRAY_SIZE(PlrMissionStats)) {
        NF_WARN("ERROR : Invalid Plr ID\n"); // GC check
        return;
    }

    PlrMissionStats[playerNum].timerPaused = 0;
}



// AUTOINJECT
void PlrStat_LogEnemySpawned(void) {

    // Original game code does not check PlrStat_OkToUpdate, we replicate that behaviour here
    // Also does not apply to a playerNum, it's a global count

    PlrMissionStats[0].enemiesSpawned++;
}

#define ScoringTable (*(SCORETABLE(*)[12])0x0017f198)

static bool DoneBetter;

// The driving engine's results for a driving level: PTPDATA.Scoring, nine categories of {achieved, target,
// maxPoints}. bootup_bootup (game.cpp) points it there; it stays NULL on a boot that never ran it.
// XBE_GLOBAL(0x002790a0, 0x4)
undefined4 (*NewScoresRef)[9][3];

// The whole score is multiplied by this per difficulty and added as the bonus: nothing on the easiest, the score
// again on normal, twice on the hardest ones. Indexed as PlrStat_GetScore picks it (difficulty 1, 2, other).
// XBE_GLOBAL(0x0017f468, 0xc)
static float ScoringTableDifMul[3] = { 0.0f, 1.0f, 2.0f };

// Mission_Status values this function tests (see Mission_Update in Mission.cpp)
#define MISSION_STATUS_QUIT     3
#define MISSION_STATUS_COMPLETE 6

// Driving levels' hashcodes are 0x09xxxxxx; everything else is scored from the action engine's own counters.
#define HASHCODE_TYPE_MASK   0xff000000
#define HASHCODE_TYPE_DRIVING 0x09000000

// The driving engine reports its time in seconds; the action side keeps it in centiseconds (PlrMissionStats'
// timeElapsed), which is what the time category expects.
#define SCORE_CENTISECONDS_PER_SECOND 100

// A detection category with no penalty in the table uses this per detection over the allowance.
#define DEFAULT_DETECTION_PENALTY 0.03f

// Points are rounded up unless the value is already this close to a whole number.
#define ROUND_UP_TOLERANCE 1e-05f

// Everything below is x87 arithmetic in the original. Where the original keeps a value on the FPU stack it is
// done here in double; where it stores to a float (the category's rating/points, or a float argument) it is
// rounded to float at the same point. The float constants are the original's single-precision ones.

// The original's __ftol2: truncates towards zero to 64 bits, of which the caller keeps the low 32.
static uint ToUint(double value) {
    return (uint)(int64_t)value;
}

// Truncates, then adds one unless value was already within ROUND_UP_TOLERANCE of that integer - so 2.0 stays 2
// and 2.3 becomes 3. Only ever given non-negative values.
static int RoundUpPoints(double value) {
    int whole = (int)ToUint(value);
    double fraction = value - whole;
    if (fraction < 0.0) {
        fraction = -fraction;
    }
    if (fraction >= (double)ROUND_UP_TOLERANCE) {
        whole++;
    }
    return whole;
}

// Round half away from zero (FUN_000b0860 in the original; the ordinary categories inline the same code, but on a
// value still on the FPU stack rather than one passed as a float - hence double here and a float caller below).
static int RoundHalfAway(double value) {
    int whole = (int)ToUint(value);
    double fraction = value - whole;
    if (fraction < 0.0) {
        fraction = -fraction;
    }
    if (fraction >= 0.5) {
        if (whole < 0) {
            whole--;
        } else {
            whole++;
        }
    }
    return whole;
}

// A percentage (0-100) rounded to a whole percent, as a 0..1 rating
static float PercentToRating(double percent) {
    double rating = RoundHalfAway(percent) * (double)0.01f;
    if (rating < 0.0) {
        return 0.0f;
    }
    if (rating > 1.0) {
        rating = 1.0;
    }
    return (float)rating;
}

// FUN_000b08b0 in the original: a 0..1 fraction to a rating rounded to a whole percent. The fraction arrives as a
// float and is scaled into a float argument before rounding; kept as two roundings to float, like the original.
static float FractionToRating(float fraction) {
    float percent = (float)((double)fraction * (double)100.0f);
    return PercentToRating((double)percent);
}

// Works out the level's score from what the player did: fills in every category's achieved value (from the
// driving engine's results on a driving level, from PlrMissionStats otherwise), then each category's points and
// 0..1 rating, the total, the difficulty bonus and - for player 0 - the final and best score and DoneBetter.
// Unless the mission was completed the points and scores are wiped again, so the pause menu's stats show the
// ratings with no scores. Returns the level's SCORETABLE entry, or NULL for a level with no entry (or a driving
// level when no driving results are there).
//
// Callers (all still original): P_NFSTATS_Handler, P_NFRESULTS_Handler, C_GCPAUSE_Handler, Boot_LoadPTPData.
// AUTOINJECT
undefined4* PlrStat_GetScore(char playerNum) {

    HASHCODE level = Mission_BaseMapHCode();
    SCORETABLE *table = NULL;
    for (int i = 0; i < ARRAY_SIZE(ScoringTable); i++) {
        if (ScoringTable[i].levelHashcode == level) {
            table = &ScoringTable[i];
            break;
        }
    }
    // (The original also tests the entry's address for NULL here, which it never is.)
    if (table == NULL) {
        NF_WARN("ERROR : No scoring table found for level 0x%x\n", level); // GC check (0x800e6e14)
        return NULL;
    }

    table->isAction = ((level & HASHCODE_TYPE_MASK) == HASHCODE_TYPE_DRIVING) ? 0 : 1;

    ScoreCategory *stats = table->statsTable;

    if (table->isAction == 0) {
        if (NewScoresRef == NULL) {
            NF_WARN("ERROR : No pointer to shared scoring data for level 0x%x\n", level); // GC check (0x800e6e14)
            return NULL;
        }
        // The driving engine's entries are {achieved, target, maxPoints}; its time is in seconds
        for (uint i = 0; i < SCORE_NUM_CATEGORIES; i++) {
            uint achieved = (*NewScoresRef)[i][0];
            if (i == SCORE_TIME) {
                achieved *= SCORE_CENTISECONDS_PER_SECOND;
            }
            stats[i].target = (*NewScoresRef)[i][1];
            stats[i].maxPoints = (*NewScoresRef)[i][2];
            stats[i].achieved = achieved;
        }
    } else {
        stats[SCORE_BOND_MOMENTS].achieved = PlrMissionStats[0].bondMoments;
        stats[SCORE_ENEMIES_DISPATCHED].achieved = PlrMissionStats[0].enemiesDispatched;
        stats[SCORE_ENEMIES_DISABLED].achieved = PlrMissionStats[0].enemiesDisabled;
        stats[SCORE_ENEMIES_SURRENDERED].achieved = PlrMissionStats[0].enemiesSurrendered;
        stats[SCORE_TIMES_DETECTED].achieved = PlrMissionStats[0].timesDetected;
        // Accuracy as a share of the category's target (100 in every table, so a percentage). With no shots
        // fired it counts as perfect. The multiply wraps at 32 bits, as the original's IMUL does.
        stats[SCORE_ACCURACY].achieved = stats[SCORE_ACCURACY].target;
        if (PlrMissionStats[0].shotsFired != 0) {
            stats[SCORE_ACCURACY].achieved =
                (stats[SCORE_ACCURACY].target * PlrMissionStats[0].shotsHitEnemy) / PlrMissionStats[0].shotsFired;
        }
        stats[SCORE_HEALTH].achieved = ToUint(PlrMissionStats[0].health);
        stats[SCORE_TIME].achieved = PlrMissionStats[0].timeElapsed;
        stats[SCORE_BOND_BONUSES].achieved = PlrMissionStats[0].bondBonuses;
    }

    // Quitting scores no health
    if (Mission_Status() == MISSION_STATUS_QUIT) {
        stats[SCORE_HEALTH].achieved = 0;
    }

    table->timeTaken = stats[SCORE_TIME].achieved;
    table->parTime = stats[SCORE_TIME].target;
    table->baseScore = 0;

    for (uint i = 0; i < SCORE_NUM_CATEGORIES; i++) {
        ScoreCategory *category = &stats[i];
        category->rating = 0.0f;
        category->points = 0.0f;

        // A category with no target is not scored on this level. The original keeps the target as a float for
        // the division in the ordinary case below.
        float target = (float)(double)category->target;
        if (target == 0.0f) {
            continue;
        }

        if (i == SCORE_TIMES_DETECTED) {
            // Full points up to the allowance, then each detection over it takes detectionPenalty off
            if (category->achieved <= category->target) {
                category->points = (float)(double)category->maxPoints;
                category->rating = 1.0f;
            } else {
                double penalty = table->detectionPenalty;
                if (penalty == 0.0) {
                    penalty = DEFAULT_DETECTION_PENALTY;
                }
                double share = 1.0 - (double)(category->achieved - category->target) * penalty;
                if (share <= 0.0) {
                    share = 0.0;
                }
                category->points = (float)RoundUpPoints(share * (double)category->maxPoints);
                double maxPoints = (double)category->maxPoints;
                if (maxPoints != 0.0) {
                    category->rating = FractionToRating((float)(category->points / maxPoints));
                }
            }
        } else if (i == SCORE_TIME) {
            // Par time is in seconds, the achieved time in centiseconds. Beating par scores MORE than maxPoints
            // (maxPoints * par / time, time at least 1s); going over scores maxPoints * par / time too.
            uint seconds = category->achieved / SCORE_CENTISECONDS_PER_SECOND;
            uint par = category->target;
            if (seconds > par) {
                double overPar = (double)seconds / (double)par;
                if (overPar != 0.0) {
                    category->points = (float)RoundUpPoints((double)category->maxPoints / overPar);
                }
            } else {
                double time = (double)seconds;
                if (time <= 1.0) {
                    time = 1.0;
                }
                uint scaled = category->maxPoints * par; // wraps at 32 bits, as the original's IMUL does
                category->points = (float)RoundUpPoints((double)scaled / time);
            }
            double maxPoints = (double)category->maxPoints;
            if (maxPoints != 0.0) {
                // Unlike FractionToRating, the fraction is scaled while still on the FPU stack
                float percent = (float)((double)category->points / maxPoints * (double)100.0f);
                category->rating = PercentToRating((double)percent);
            }
        } else if (i == SCORE_BOND_BONUSES) {
            // All or nothing
            if (category->achieved >= category->target) {
                category->points = (float)(double)category->maxPoints;
                category->rating = 1.0f;
            }
        } else {
            // In proportion, up to the target
            uint achieved = category->achieved;
            if (achieved >= category->target) {
                achieved = category->target;
            }
            double achievedF = (double)achieved;
            category->points = (float)RoundUpPoints((double)category->maxPoints * achievedF / (double)target);
            category->rating = PercentToRating(achievedF / (double)category->target * (double)100.0f);
        }

        table->baseScore = ToUint((double)table->baseScore + (double)category->points);
    }

    float multiplier;
    if (GameState.difficultyModifier == 1) {
        multiplier = ScoringTableDifMul[0];
    } else if (GameState.difficultyModifier == 2) {
        multiplier = ScoringTableDifMul[1];
    } else {
        multiplier = ScoringTableDifMul[2];
    }
    table->difficultyMultiplier = ToUint(multiplier);
    table->difficultyBonus = table->difficultyMultiplier * table->baseScore;

    // Only player 0's result is recorded as the level's score
    if (playerNum == 0) {
        uint score = table->difficultyBonus + table->baseScore;
        DoneBetter = false;
        if (score != 0) {
            DoneBetter = (score >= table->bestScore);
        }
        table->score = score;
        if (score > table->bestScore) {
            table->bestScore = score;
        }
    }

    // Until the mission is complete there is no score to show: wipe the points, keeping the achieved values and
    // ratings (and the best score)
    if (Mission_Status() != MISSION_STATUS_COMPLETE) {
        for (uint i = 0; i < SCORE_NUM_CATEGORIES; i++) {
            table->statsTable[i].points = 0.0f;
        }
        table->difficultyMultiplier = 0;
        table->difficultyBonus = 0;
        table->baseScore = 0;
        table->score = 0;
        DoneBetter = false;
    }

    return (undefined4 *)table;
}

// AUTOINJECT
SCORETABLE * PlrStats_GetLevelTotals(HASHCODE hashcode) {

    for(int i = 0; i < ARRAY_SIZE(ScoringTable); i++) {
        if(ScoringTable[i].levelHashcode == hashcode) {
            return &ScoringTable[i];
        }
    }
    return NULL;
}

// AUTOINJECT
bool PlrStats_HasGoldMedal(void) {
    return Menu_HasMedal(Mission_BaseMapHCode(), 3, 0);
}

// AUTOINJECT
SCORETABLE * PlrStats_GetScoreTable(undefined4 *numItems) {
  *numItems = ARRAY_SIZE(ScoringTable);
  return ScoringTable;
}

// AUTOINJECT
bool PlrStats_DoneBetter(void) {
    return DoneBetter;
}

// AUTOGEN
void PlrStat_LogShotHitScenery(uint playerNum);