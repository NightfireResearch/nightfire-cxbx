// Shadow test for the DroneMP group: NDrone2_FindOpponent (game/drone/NDrone2.cpp), on live play. With
// DroneMPShadow=on, every call runs the original and ours from the same state and compares the return value and
// everything either can write: the drone and its object, its old opponent (object and drone), every bot's BOT_vars,
// NPCGlobals, MPGame, the delayed-message pool and the RNG. The game carries on with ours. A summary goes to the log
// every 1000 calls, and the first differences in full.
//
// What it cannot restore: NDrone2_SetOpponent and the objective case send the bot message 0x45 / 0x3b, which
// BotGlobal handles at once (Drone_SM_SendMsgSelf with no delay). Whatever that handler writes outside the regions
// below is written twice. The opponent search's other callees (sight tests, BOT_handleOpponentHistory) write the
// drone, its BOT_vars and NPCGlobals, which are covered.
//
// Drone_SM_RouteMsg is not shadowed: it delivers into the live drone system (every state handler), which cannot be
// re-run from a snapshot. The replays cover it.

#include "DroneMPShadow.h"

#include "../../common/xbeOriginal.h"
#include "../game/drone/NDrone2.h"
#include "../game/drone/BOT.h"

#include <stdio.h>
#include <string.h>
#include <windows.h>

namespace {

const unsigned kFindOpponent = 0x00037c10;

// The real sizes where our headers do not lay the whole thing out
const size_t kObjSize = 0xe4;
const unsigned kMsgPool = 0x001ee438, kMsgPoolSize = 0x1f6448 - 0x1ee438; // 1024 nodes, then the two list heads
const unsigned kRandState = 0x0018cdf8;

struct Region {
    const char *name;
    void *at;
    size_t size;
};

const int kMaxRegions = 10;
const size_t kMaxBytes = 0x8100;
static_assert(sizeof(BOT_vars) <= kMaxBytes && kMsgPoolSize <= kMaxBytes, "a region outgrows the snapshot");

struct Snapshot {
    unsigned char bytes[kMaxRegions][kMaxBytes];
};

unsigned s_calls, s_differing, s_reported;

int CollectRegions(Drone_tag *drone, Region *regions) {
    int n = 0;
    regions[n++] = { "drone", drone, sizeof(Drone_tag) };
    if (drone->gameObj != NULL)
        regions[n++] = { "object", drone->gameObj, kObjSize };
    obj_tag *opponent = drone->opponent;
    if (opponent != NULL && opponent != drone->gameObj) {
        regions[n++] = { "opponent", opponent, kObjSize };
        if (opponent->objectType == OBJECTTYPE_DRONE && opponent->extraObjectData != drone)
            regions[n++] = { "opponent drone", opponent->extraObjectData, sizeof(Drone_tag) };
    }
    regions[n++] = { "BOT_vars", &BOT_vars, sizeof(BOT_vars) };
    regions[n++] = { "NPCGlobals", &NPCGlobals, sizeof(NPCGlobals) };
    regions[n++] = { "MPGame", &MPGame, sizeof(MPGame) };
    regions[n++] = { "message pool", (void *)kMsgPool, kMsgPoolSize };
    regions[n++] = { "rand", (void *)kRandState, 8 };
    return n;
}

void Save(const Region *regions, int n, Snapshot *s) {
    for (int i = 0; i < n; i++)
        memcpy(s->bytes[i], regions[i].at, regions[i].size);
}

void Load(const Region *regions, int n, const Snapshot *s) {
    for (int i = 0; i < n; i++)
        memcpy(regions[i].at, s->bytes[i], regions[i].size);
}

int __cdecl ShadowFindOpponent(Drone_tag *drone) {
    static Snapshot before, original;
    Region regions[kMaxRegions];
    int n = CollectRegions(drone, regions);

    Save(regions, n, &before);
    int originalResult;
    {
        XbeOriginalScope scope(kFindOpponent);
        originalResult = reinterpret_cast<int (__cdecl *)(Drone_tag *)>(kFindOpponent)(drone);
    }
    Save(regions, n, &original);
    Load(regions, n, &before);
    int ours = NDrone2_FindOpponent(drone);

    bool differs = false;
    if (ours != originalResult) {
        differs = true;
        if (s_reported < 20) {
            s_reported++;
            printf("[dronempshadow] call %u: returned original %08x ours %08x\n", s_calls, originalResult, ours);
        }
    }
    for (int i = 0; i < n; i++) {
        const unsigned char *a = original.bytes[i], *b = (const unsigned char *)regions[i].at;
        for (size_t at = 0; at < regions[i].size; at++) {
            if (a[at] == b[at])
                continue;
            differs = true;
            if (s_reported < 20) {
                s_reported++;
                printf("[dronempshadow] call %u, dtype %u: %s +0x%04x original %02x ours %02x\n", s_calls, drone->dtype,
                       regions[i].name, (unsigned)at, a[at], b[at]);
            }
        }
    }
    if (differs)
        s_differing++;
    if (++s_calls % 1000 == 0)
        printf("[dronempshadow] %u calls, %u differ\n", s_calls, s_differing);
    return ours;
}

} // namespace

void DroneMPShadow_Install(void) {
    char v[16] = "";
    GetPrivateProfileStringA("Settings", "DroneMPShadow", "", v, sizeof(v), ".\\settings.ini");
    if (_stricmp(v, "on") != 0 && strcmp(v, "1") != 0)
        return;
    if (XbeOriginal_Redirect(kFindOpponent, (const void *)&ShadowFindOpponent))
        printf("[dronempshadow] comparing NDrone2_FindOpponent with the original on every call\n");
}
