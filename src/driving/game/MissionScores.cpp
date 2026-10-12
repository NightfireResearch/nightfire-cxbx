#pragma fp_contract(off)

#include "MissionManager.h"

#include <stddef.h>
#include <stdint.h>

#include "Simulation.h"
#include "../../helpers.h"
#include "../engine/GameInterfaces.hpp"     // GHud
#include "../engine/GameLoop.h"             // Launch
#include "../platform/X87.h"                // Ftol

// ---------------------------------------------------------------------------------------------------------------
// SMissionManager: the player's health and shield, the counts and counters, the timed actions and the scores
// (from 0x000b60a0-0x000b9500; the rest is in MissionManager.cpp).
// ---------------------------------------------------------------------------------------------------------------

// ---- the game's code not ported yet
#define GHud_Stylin ((void (__fastcall *)(GHud *, int))0x000d7d60)

// ---- the game's globals

// 1 at start; UpdateCharInfo clears it the first time it runs before the HUD exists. Nothing reads it otherwise
// (name ours).
// XBE_GLOBAL(0x001c8fc4, 0x1)
uint8_t CharInfoBeforeHud = 1;

namespace {

constexpr int kMagicCounter12 = 12;     // its value 1 or 2 raises event 0x0c or 0x0d
constexpr int kMagicCounter14 = 14;     // 1, 2 or 3: event 0x0e, 0x0f or 0x10

// The per-difficulty arrays' entry for the manager's difficulty
int DifficultyEntry(int difficulty) {
    switch (difficulty) {
    case 1:
        return 1;
    case 2:
        return 2;
    default:
        return 0;
    }
}

// A share clamped to [0, 1] (NaN passes)
double Clamp01(double share) {
    if (share > 1.0)
        return 1.0;
    if (share < 0.0)
        return 0.0;
    return share;
}

// The HUD's pause flag (provisional view of GHud's +0x2b0)
struct MissionScoresHudView {
    uint8_t unknown000[0x2b0];
    uint8_t paused;             // +0x2b0
};

bool HudPaused(GHud *hud) {
    return reinterpret_cast<MissionScoresHudView *>(hud)->paused != 0;
}

}  // namespace

// FUNC_AT(0x000b63c0)
void SMissionManager::RestoreLives() {
    switch (difficulty) {
    case 0:
    case 1:
        Launch.lives = 3;
        break;
    case 2:
        Launch.lives = 2;
        break;
    }
}

// FUNC_AT(0x000b63f0)
void SMissionManager::ClearScoreBuffer(LaunchPage *page) {
    for (int score = 0; score < kScoreCount; score++) {
        page->accumulatedScores[score] = 0;
        page->bestScores[score] = 0;
    }
    page->accumulatedValues = 0;
    page->unknown580 = 0;
    page->shotsFired = 0;
    page->shotsHit = 0;
    page->kills = 0;
    page->unknown594 = 0;
    page->zeroScoreTime = 0;
    page->fullScoreTime = 0;
    page->missionTime = 0;
    page->damageZeroScore = 0;
    page->damage = 0;
    page->stylinCount = 0;
    page->unknown5a4 = 0;
    page->unknown5b4 = 0;
    page->unknown5b8[0] = 0;
    page->unknown5b8[1] = 0;
    page->unknown5b8[2] = 0;
    page->unknown5c4 = 0.0f;
    page->unknown5c8 = 0.0f;
    page->unknown5cc = 0.0f;
    page->healthRatio = 0.0f;
    page->shieldRatio = 0.0f;
    for (int i = 0; i < 27; i++)
        page->summary[i] = 0;
}

// FUNC_AT(0x000b65f0)
void SMissionManager::BoostPlayerHealth(float fraction) {
    double health = double(Ftol(double(fraction) * playerMaxHealth)) + playerHealth;
    playerHealth = health > playerMaxHealth ? playerMaxHealth : float(health);
}

// FUNC_AT(0x000b6640)
void SMissionManager::BoostPlayerShield(float fraction) {
    double shield = double(Ftol(double(fraction) * playerMaxShield)) + playerShield;
    playerShield = shield > playerMaxShield ? playerMaxShield : float(shield);
}

// FUNC_AT(0x000b6690)
void SMissionManager::SetPlayerShieldTimes(int times) {
    playerShield = playerMaxShield = float(missionSet->maxShield * times);
}

// FUNC_AT(0x000b66c0)
void SMissionManager::IncPlayerDamage(float amount) {
    if (cinematicMode == 0) {
        damage += amount;
        lastDamageStep = fgSimulation.stepCount;
    }
}

// FUNC_AT(0x000b66f0)
void SMissionManager::IncShotsFired(int shots) {
    shotsFired += shots;
    lastShotStep = fgSimulation.stepCount;
}

// FUNC_AT(0x000b6720)
void SMissionManager::IncShotsHit(bool hit) {
    // A hit counts within a tenth of the step count (not of a second) of the last shot
    if (shotsHit < shotsFired && (!hit || fgSimulation.stepCount <= lastShotStep + fgSimulation.stepCount / 10))
        shotsHit++;
}

// FUNC_AT(0x000b6770)
void SMissionManager::IncKills(int count) {
    kills += count;
    if (kills < 0)
        kills = 0;
}

// FUNC_AT(0x000b67a0)
int SMissionManager::GetMagicCounter(int counter) {
    return magicCounters[counter];
}

// FUNC_AT(0x000b67b0)
void SMissionManager::SetMagicCounterThreshold(int counter, int threshold) {
    if (counter < kMagicCounters)
        magicThresholds[counter] = threshold;
}

// FUNC_AT(0x000b7a40)
void SMissionManager::IncMagicCounter(int counter) {
    if (counter >= kMagicCounters)
        return;
    int value = ++magicCounters[counter];
    // A counter reaching its threshold raises the event of the same number
    if (magicThresholds[counter] >= 0 && value >= magicThresholds[counter])
        events[counter] = 1;
    switch (magicCounters[kMagicCounter12]) {
    case 1:
        events[kEvent0C] = 1;
        break;
    case 2:
        events[kEvent0D] = 1;
        break;
    }
    switch (magicCounters[kMagicCounter14]) {
    case 1:
        events[kEvent0E] = 1;
        break;
    case 2:
        events[kEvent0F] = 1;
        break;
    case 3:
        events[kEvent10] = 1;
        break;
    }
}

// FUNC_AT(0x000b7ac0)
void SMissionManager::SetMagicCounter(int counter, int value) {
    if (counter < kMagicCounters) {
        magicCounters[counter] = value - 1;
        IncMagicCounter(counter);
    }
}

// FUNC_AT(0x000b67d0)
double SMissionManager::GetPlayerHealth(bool fraction) {
    if (fraction)
        return double(playerHealth) / playerMaxHealth;
    return playerHealth;
}

// FUNC_AT(0x000b67f0)
void SMissionManager::ResetCharInfo() {
    MissionSet *set = missionSet;
    unknown494 = 0;
    unknown498 = 100;
    if (set->health != 0) {
        playerHealth = float(set->health);
        playerMaxHealth = float(set->maxHealth);
        playerShield = float(set->shield);
        playerMaxShield = float(set->maxShield);
    } else {
        playerHealth = 10000.0f;
        playerMaxHealth = 10000.0f;
        playerShield = 0.0f;
        playerMaxShield = 1000.0f;
    }
    if (gPlayerCanNeverLose || MissionHealthTimes100) {
        playerHealth *= 100.0f;
        playerMaxHealth *= 100.0f;
    }

    // What the last part left (TallyScores), and from the second load at least half health
    if (Launch.healthRatio > 0.0f)
        playerHealth = Launch.healthRatio * playerHealth;
    if (Launch.shieldRatio > 0.0f) {
        double shield = double(Launch.shieldRatio) * playerMaxShield;
        playerShield = shield < playerShield ? playerShield : float(shield);
    }
    if (loadCount > 0) {
        double half = playerMaxHealth * 0.5;
        playerHealth = half < playerHealth ? playerHealth : float(half);
    }

    int defaultScale = set->healthScale[0];
    if (defaultScale > 0) {
        int healthScale = set->healthScale[DifficultyEntry(difficulty)];
        int shieldScale = set->shieldScale[DifficultyEntry(difficulty)];
        playerHealth = float(double(healthScale) * playerHealth * 0.01f);
        playerMaxHealth = float(double(healthScale) * playerMaxHealth * 0.01f);
        playerShield = float(double(shieldScale) * playerShield * 0.01f);
        playerMaxShield = float(double(shieldScale) * playerMaxShield * 0.01f);
    }

    charInfo[0].unknown04 = 0.0f;
    charInfo[0].unknown08 = 1000.0f;
    charInfo[0].unknown0c = 1000.0f;
    charInfo[0].unknown10 = 0.0f;
    charInfo[0].unknown14 = 1000.0f;
    charInfo[1].unknown04 = 0.0f;
    charInfo[1].unknown08 = 1000.0f;
    charInfo[1].unknown0c = 1000.0f;
    charInfo[1].unknown10 = 0.0f;
    charInfo[1].unknown14 = 1000.0f;
    charInfo[0].id = 1;
    charInfo[1].id = 2;

    if (Launch.flags & kLaunchShieldTimes2)
        playerShield = playerMaxShield = float(missionSet->maxShield * 2);
    if (Launch.flags & kLaunchShieldTimes3)
        playerShield = playerMaxShield = float(missionSet->maxShield * 3);
    if (Launch.flags & kLaunchShieldTimes4)
        playerShield = playerMaxShield = float(missionSet->maxShield * 4);
}

// FUNC_AT(0x000b6b20)
void SMissionManager::UpdateCharInfo() {
    if (GHud::TheApp() == NULL) {
        if (CharInfoBeforeHud)
            CharInfoBeforeHud = 0;
        return;
    }
    if (HudPaused(GHud::TheApp()))
        return;
    if (Launch.flags & kLaunchRegenerateHealth) {
        if (double(playerHealth) / playerMaxHealth < 0.33f && fgSimulation.stepCount - lastDamageStep >= 0) {
            double health = 0.01f / double(fgSimulation.stepsPerSecond) * playerMaxHealth + playerHealth;
            playerHealth = health > playerMaxHealth ? playerMaxHealth : float(health);
        }
    }
    if (Launch.flags & kLaunchInvulnerable) {
        playerMaxHealth = 10000.0f;
        playerHealth = 10000.0f;
    }
}

// FUNC_AT(0x000b6bd0)
void SMissionManager::StartTimedAction(int action, float fullPointsTime, float timeLimit, int minPoints,
                                       int maxPoints) {
    MissionTimedAction &timed = timedActions[action];
    if (timed.active)
        return;
    timed.timeLimit = timeLimit;
    timed.fullPointsTime = fullPointsTime;
    timed.minPoints = minPoints;
    timed.maxPoints = maxPoints;
    timed.active = 1;
    timed.startStep = fgSimulation.stepCount;
}

// FUNC_AT(0x000b7de0)
void SMissionManager::CompleteTimedAction(int action) {
    MissionTimedAction &timed = timedActions[action];
    if (!timed.active)
        return;
    int startStep = timed.startStep;
    timed.active = 0;
    float elapsed = float(double(fgSimulation.stepCount - startStep) * fgSimulation.timeStep);
    int points = timed.minPoints;
    if (elapsed < timed.fullPointsTime) {
        points = timed.maxPoints;
    } else if (elapsed <= timed.timeLimit) {
        double window = double(timed.timeLimit) - timed.fullPointsTime;
        float windowRounded = float(window);
        if (window > 0.0) {
            int spread = timed.maxPoints - points;
            if (spread > 0)
                points += Ftol((1.0 - (double(elapsed) - timed.fullPointsTime) / windowRounded) * spread);
        }
    }
    if (points > 0)
        AddScore(kScoreStylin, points);
}

// FUNC_AT(0x000b7f10)
void SMissionManager::UpdateTimedActions() {
    for (int action = 0; action < kTimedActions; action++) {
        MissionTimedAction &timed = timedActions[action];
        if (timed.active && double(fgSimulation.stepCount - timed.startStep) * fgSimulation.timeStep > timed.timeLimit)
            CompleteTimedAction(action);
    }
}

// FUNC_AT(0x000b6e30)
void SMissionManager::AddScore(int score, int points) {
    scores[score] += points;
    if (points > 0 && score == kScoreStylin) {
        if (GHud::TheApp() != NULL)
            GHud_Stylin(GHud::TheApp(), 0);
        stylinCount++;
    }
}

// FUNC_AT(0x000b6e80)
void SMissionManager::CalcEndMissionScores() {
    MissionSet *set = missionSet;

    double accuracy = 1.0;
    if (shotsFired != 0)
        accuracy = Clamp01(double(shotsHit) / shotsFired);
    scores[kScoreAccuracy] = Ftol(double(set->accuracyValue) * accuracy);

    double share3 = 1.0;
    if (shotsFired != 0)
        share3 = Clamp01(double(set->unknown2e0) / shotsFired);
    scores[kScore3] = Ftol(double(set->value3) * share3);

    double killShare = 1.0;
    if (unknown500 != 0)
        killShare = Clamp01(double(kills) / unknown500);
    scores[kScoreKills] = Ftol(double(set->killsValue) * killShare);

    scores[kScore4] = Ftol(double(set->value4) * 0.0);
    scores[kScoreHealth] = Ftol(double(set->healthValue) * Clamp01(double(playerHealth) / playerMaxHealth));

    double shieldShare = 0.0;
    if (playerMaxShield > 0.0f)
        shieldShare = Clamp01(1.0 / playerMaxShield * playerShield);
    scores[kScoreShield] = Ftol(double(set->shieldValue) * shieldShare);

    // The time: the full score up to fullScoreTime seconds (this part's and the parts' before it), none from
    // zeroScoreTime
    double timeShare = 0.0;
    int seconds = Ftol(double(fgSimulation.stepCount) * fgSimulation.timeStep) + Launch.missionTime;
    if (seconds < set->zeroScoreTime) {
        int window = set->zeroScoreTime - set->fullScoreTime;
        timeShare = window > 0 ? Clamp01(double(set->zeroScoreTime - seconds) / window) : 1.0;
    }
    scores[kScoreTime] = Ftol(double(set->timeValue) * timeShare);

    double damageShare = 0.0;
    float damageLimit = float(set->damageZeroScore);
    if (damage < damageLimit) {
        int margin = Ftol(double(damageLimit) - damage);
        damageShare = Clamp01(double(margin) / (set->damageZeroScore - set->damageFullScore));
    }
    scores[kScoreDamage] = Ftol(double(set->damageValue) * damageShare);

    scores[kScore9] = Ftol(double(set->value9) * Clamp01((accuracy + share3) * 0.5));
}

// FUNC_AT(0x000b7260)
int SMissionManager::GetEnemyAccuracyBoost() {
    return missionSet->enemyAccuracyBoost[DifficultyEntry(difficulty)];
}

// FUNC_AT(0x000b7290)
float SMissionManager::GetEnemyGlueFactor() {
    return missionSet->enemyGlueFactor[DifficultyEntry(difficulty)];
}

// FUNC_AT(0x000b72c0)
float SMissionManager::ShowMissionText() {
    return missionSet->missionText[DifficultyEntry(difficulty)];
}

// FUNC_AT(0x000b7470)
void SMissionManager::TallyScores(LaunchPage *page, bool endOfMission) {
    if (endOfMission) {
        if (missionState != kMissionWon)
            return;
        // A score within 2 of the next multiple of 50 is rounded up to it
        for (int score = 0; score < kScoreCount; score++) {
            int remainder = scores[score] % 50;
            if (remainder >= 48)
                scores[score] = scores[score] - remainder + 50;
        }
        for (int score = 0; score < kScoreCount; score++)
            page->accumulatedScores[score] = startScores[score] + scores[score];
    }

    MissionSet *set = missionSet;
    page->bestScores[kScoreObjectives] += set->objectivesValue;
    page->bestScores[kScoreStylin] += set->stylinValue;
    page->bestScores[kScoreAccuracy] += set->accuracyValue;
    page->bestScores[kScore3] += set->value3;
    page->bestScores[kScore4] += set->value4;
    page->bestScores[kScoreHealth] += set->healthValue;
    page->bestScores[kScoreShield] += set->shieldValue;
    page->bestScores[kScoreTime] += set->timeValue;
    page->bestScores[kScoreDamage] += set->damageValue;
    page->bestScores[kScore9] += set->value9;
    page->bestScores[kScore10] += set->value10;
    page->bestScores[kScoreKills] += set->killsValue;
    page->accumulatedValues += set->damageValue + set->killsValue + set->value3 + set->timeValue +
                               set->accuracyValue + set->stylinValue;
    page->unknown580 += unknown4fc;
    page->shotsFired += shotsFired;
    page->shotsHit += shotsHit;
    page->kills += kills;
    page->unknown594 += unknown500;
    page->zeroScoreTime += set->zeroScoreTime;
    page->fullScoreTime += set->fullScoreTime;
    page->missionTime += missionEndTime;
    page->damageZeroScore += set->damageZeroScore;
    page->damage += Ftol(damage);
    page->stylinCount += stylinCount;
    page->unknown5a4 += set->unknown2e4;
    page->unknown5b4 += set->unknown2e8;
    page->unknown5b8[0] = set->unknown2d0[0];
    page->unknown5b8[1] = set->unknown2d0[1];
    page->unknown5b8[2] = set->unknown2d0[2];
    page->unknown5c4 = float(1.0 - double(page->accumulatedScores[kScoreDamage]) / page->bestScores[kScoreDamage]);
    page->unknown5c8 = float(double(page->accumulatedScores[kScore3]) / page->bestScores[kScore3]);
    page->unknown5cc = float(double(page->accumulatedScores[kScoreAccuracy]) / page->bestScores[kScoreAccuracy]);
    page->healthRatio = float(double(playerHealth) / playerMaxHealth);
    page->shieldRatio = playerMaxShield > 0.0f ? float(1.0 / playerMaxShield * playerShield) : 0.0f;

    int hits = page->shotsHit < page->shotsFired ? page->shotsHit : page->shotsFired;
    page->summary[0] = page->stylinCount;
    page->summary[1] = page->unknown5b4;
    page->summary[2] = page->bestScores[kScoreStylin];
    page->summary[3] = page->kills;
    page->summary[4] = page->unknown594;
    page->summary[5] = page->bestScores[kScoreKills];
    page->summary[15] = page->shotsFired > 0 ? hits * 100 / page->shotsFired : 100;
    page->summary[16] = 100;
    page->summary[17] = page->bestScores[kScoreAccuracy];
    double healthCap = set->healthPercentCap;
    double healthPercent = double(playerHealth) / playerMaxHealth * 100.0f;
    page->summary[18] = Ftol(healthPercent < healthCap ? healthPercent : healthCap);
    page->summary[19] = set->healthPercentCap;
    page->summary[20] = page->bestScores[kScoreHealth];
    page->summary[21] = page->missionTime;
    page->summary[22] = page->fullScoreTime;
    page->summary[23] = page->bestScores[kScoreTime];
    page->summary[24] = page->unknown580;
    page->summary[25] = page->unknown5a4;
    page->summary[26] = page->bestScores[kScore10];
    for (int i = 6; i <= 14; i++)
        page->summary[i] = 0;
}
