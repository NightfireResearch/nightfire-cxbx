// Shadow test for the bot goal code (game/drone/BOTSTATE.cpp), on live play: with BotStateShadow=on, every call of
// BOTSTATE_isObjAlreadyAnotherTeamObjective, BOTSTATE_getPreferredTraitOpponentObjIndex, BOTSTATE_processGoals and
// BOTSTATE_pickGoal runs the original and ours from the same state and compares the return value and everything
// either can write: the bot's drone and object, every BOT_vars entry, MPpickups, MPGame and the RNG words. The game
// carries on with ours. A summary per function every 500 calls, and the first differences in full.
//
// Not compared - ours runs alone, counted as skipped:
// - processGoals in the Blueprint mode: an arrival there calls MP_BluePrintReachedBase (scores, messages, a sound).
// - pickGoal when either goal slot has pick flag 8: it plans and follows a route (NDrone2_MoveToGoalPosition).
// Both still reach BOTSTATE_gotoGoal and the emitter lookups, whose AI node marks and search lists are scratch that
// every search rebuilds, so running them twice leaves nothing behind; that scratch is not compared.
//
// The original processGoals and pickGoal call the original addresses of getPreferredTraitOpponentObjIndex and
// isObjAlreadyAnotherTeamObjective, so those are compared inside them too (ours calls ours directly).

#include "BotStateShadow.h"

#include "../../common/xbeOriginal.h"
#include "../game/drone/BOTSTATE.h"
#include "../game/mp/multiplayer.h"
#include "../game.h"

#include <stdio.h>
#include <string.h>
#include <windows.h>

namespace {

const unsigned kIsObjAlreadyAnotherTeamObjective = 0x0001c1c0;
const unsigned kGetPreferredTraitOpponentObjIndex = 0x0001da80;
const unsigned kProcessGoals = 0x0001df80;
const unsigned kPickGoal = 0x0001cb20;

const unsigned kRandomState = 0x0018cdf8;  // Rand_Random's two words
const size_t kObjSize = 0xe4;

struct Region {
    const char *name;
    void *at;
    size_t size;
};

const int kRegions = 6;
const size_t kLargest = sizeof(BOT_vars);

struct Snapshot {
    unsigned char bytes[kRegions][kLargest];
};

static_assert(sizeof(MPpickups) <= kLargest && sizeof(MPGameStruct) <= kLargest && sizeof(Drone_tag) <= kLargest,
              "a region is larger than a snapshot slot");

struct Stats {
    const char *name;
    unsigned calls, differing, skipped;
};

Stats s_isObj = { "isObjAlreadyAnotherTeamObjective" };
Stats s_preferred = { "getPreferredTraitOpponentObjIndex" };
Stats s_process = { "processGoals" };
Stats s_pick = { "pickGoal" };
unsigned s_reported;

void CollectRegions(Drone_tag *drone, Region *regions) {
    regions[0] = { "drone", drone, sizeof(Drone_tag) };
    regions[1] = { "object", drone->gameObj, kObjSize };
    regions[2] = { "BOT_vars", &BOT_vars, sizeof(BOT_vars) };
    regions[3] = { "MPpickups", &MPpickups, sizeof(MPpickups) };
    regions[4] = { "MPGame", &MPGame, sizeof(MPGameStruct) };
    regions[5] = { "RNG", (void *)kRandomState, 8 };
}

void Save(const Region *regions, Snapshot *s) {
    for (int i = 0; i < kRegions; i++)
        memcpy(s->bytes[i], regions[i].at, regions[i].size);
}

void Load(const Region *regions, const Snapshot *s) {
    for (int i = 0; i < kRegions; i++)
        memcpy(regions[i].at, s->bytes[i], regions[i].size);
}

// The state after ours against the original's saved in 'original'
bool Compare(const Stats &st, const Region *regions, const Snapshot *original) {
    bool differs = false;
    for (int i = 0; i < kRegions; i++) {
        const unsigned char *a = original->bytes[i], *b = (const unsigned char *)regions[i].at;
        for (size_t at = 0; at < regions[i].size; at++) {
            if (a[at] == b[at])
                continue;
            differs = true;
            if (s_reported < 20) {
                s_reported++;
                printf("[botstate] %s call %u: %s +0x%04x original %02x ours %02x\n", st.name, st.calls,
                       regions[i].name, (unsigned)at, a[at], b[at]);
            }
        }
    }
    return differs;
}

bool CompareResult(const Stats &st, unsigned original, unsigned ours) {
    if (original == ours)
        return false;
    if (s_reported < 20) {
        s_reported++;
        printf("[botstate] %s call %u: returned original %u ours %u\n", st.name, st.calls, original, ours);
    }
    return true;
}

void Count(Stats &st, bool differs) {
    if (differs)
        st.differing++;
    // every 500 calls, and at 1, 2, 4 ... before that, so a rarely called function reports too
    if (++st.calls % 500 == 0 || (st.calls < 500 && (st.calls & (st.calls - 1)) == 0))
        printf("[botstate] %s: %u calls, %u differ, %u not compared\n", st.name, st.calls, st.differing, st.skipped);
}

bool __cdecl ShadowIsObjAlreadyAnotherTeamObjective(obj_tag *obj, int playerIndex) {
    bool original;
    {
        XbeOriginalScope scope(kIsObjAlreadyAnotherTeamObjective);
        original = reinterpret_cast<bool (__cdecl *)(obj_tag *, int)>(kIsObjAlreadyAnotherTeamObjective)(obj, playerIndex);
    }
    bool ours = BOTSTATE_isObjAlreadyAnotherTeamObjective(obj, playerIndex);
    Count(s_isObj, CompareResult(s_isObj, original, ours));
    return ours;
}

uchar __cdecl ShadowGetPreferredTraitOpponentObjIndex(Drone_tag *drone) {
    static Snapshot before, after;
    Region regions[kRegions];
    CollectRegions(drone, regions);

    Save(regions, &before);
    uchar original;
    {
        XbeOriginalScope scope(kGetPreferredTraitOpponentObjIndex);
        original = reinterpret_cast<uchar (__cdecl *)(Drone_tag *)>(kGetPreferredTraitOpponentObjIndex)(drone);
    }
    Save(regions, &after);
    Load(regions, &before);
    uchar ours = BOTSTATE_getPreferredTraitOpponentObjIndex(drone);

    bool differs = CompareResult(s_preferred, original, ours);
    differs |= Compare(s_preferred, regions, &after);
    Count(s_preferred, differs);
    return ours;
}

void __cdecl ShadowProcessGoals(DCVars_tag *dc) {
    if (MPSettings.GameMode == GM_BLUEPRINT) {
        s_process.skipped++;
        BOTSTATE_processGoals(dc);
        return;
    }

    static Snapshot before, after;
    Region regions[kRegions];
    CollectRegions(dc->drone, regions);

    Save(regions, &before);
    {
        XbeOriginalScope scope(kProcessGoals);
        reinterpret_cast<void (__cdecl *)(DCVars_tag *)>(kProcessGoals)(dc);
    }
    Save(regions, &after);
    Load(regions, &before);
    BOTSTATE_processGoals(dc);

    Count(s_process, Compare(s_process, regions, &after));
}

uchar __cdecl ShadowPickGoal(DCVars_tag *dc, int slot) {
    BOT_vars_t *bot = dc->drone->botVars;
    if ((bot->goals[0].pickFlags | bot->goals[1].pickFlags) & BOT_PICK_AVOID_OPPONENT) {
        s_pick.skipped++;
        return BOTSTATE_pickGoal(dc, slot);
    }

    static Snapshot before, after;
    Region regions[kRegions];
    CollectRegions(dc->drone, regions);

    Save(regions, &before);
    uchar original;
    {
        XbeOriginalScope scope(kPickGoal);
        original = reinterpret_cast<uchar (__cdecl *)(DCVars_tag *, int)>(kPickGoal)(dc, slot);
    }
    Save(regions, &after);
    Load(regions, &before);
    uchar ours = BOTSTATE_pickGoal(dc, slot);

    bool differs = CompareResult(s_pick, original, ours);
    differs |= Compare(s_pick, regions, &after);
    Count(s_pick, differs);
    return ours;
}

} // namespace

void BotStateShadow_Install(void) {
    char v[16] = "";
    GetPrivateProfileStringA("Settings", "BotStateShadow", "", v, sizeof(v), ".\\settings.ini");
    if (_stricmp(v, "on") != 0 && strcmp(v, "1") != 0)
        return;
    bool ok = XbeOriginal_Redirect(kIsObjAlreadyAnotherTeamObjective, (const void *)&ShadowIsObjAlreadyAnotherTeamObjective);
    ok &= XbeOriginal_Redirect(kGetPreferredTraitOpponentObjIndex, (const void *)&ShadowGetPreferredTraitOpponentObjIndex);
    ok &= XbeOriginal_Redirect(kProcessGoals, (const void *)&ShadowProcessGoals);
    ok &= XbeOriginal_Redirect(kPickGoal, (const void *)&ShadowPickGoal);
    if (ok)
        printf("[botstate] comparing the bot goal functions with the originals on every call\n");
    else
        printf("[botstate] could not redirect every bot goal function\n");
}
