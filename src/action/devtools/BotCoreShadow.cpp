// Shadow test for the BotCore group (game/drone/BOT.cpp), on live play: with BotCoreShadow=on, every call of
// BOT_setOtherPlayerInfo (0x1a660, once per bot per frame) runs the original and ours from the same state and
// compares everything either can write - the bot's BOT_vars_t, its drone and object, NPCGlobals (the line-of-sight
// counter NDrone2_CanSeeObject bumps) and the RNG words. The game carries on with ours. A summary goes to the log
// every 1000 calls, and the first differences in full.
//
// NDrone2_CanSeeObject is the game's and reads only positions, cels and collision, which neither run changes, so
// both runs make the same sight test with the same result.
//
// BOT_init and BOT_respawn are not shadowed: they create objects (Drone_Create) and start drones, which cannot be
// re-run from a snapshot. The MP replays cover them.

#include "BotCoreShadow.h"

#include "../../common/xbeOriginal.h"
#include "../game/drone/BOT.h"
#include "../game/drone/NDrone2.h"

#include <stdio.h>
#include <string.h>
#include <windows.h>

namespace {

const unsigned kSetOtherPlayerInfo = 0x0001a660;
const size_t kObjSize = 0xe4;

struct Region {
    const char *name;
    void *at;
    size_t size;
};

const int kMaxRegions = 5;

struct Snapshot {
    unsigned char bytes[kMaxRegions][sizeof(NPCGlobals_t)];
};

unsigned s_calls, s_differing, s_reported;

int CollectRegions(DCVars_tag *dcv, Region *regions) {
    int n = 0;
    regions[n++] = { "BOT_vars", dcv->drone->botVars, sizeof(BOT_vars_t) };
    regions[n++] = { "drone", dcv->drone, sizeof(Drone_tag) };
    regions[n++] = { "object", dcv->gameObj, kObjSize };
    regions[n++] = { "NPCGlobals", &NPCGlobals, sizeof(NPCGlobals_t) };
    regions[n++] = { "RNG", (void *)0x0018cdf8, 8 };
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

void __cdecl ShadowSetOtherPlayerInfo(DCVars_tag *dcv) {
    if (dcv->drone == NULL || dcv->drone->botVars == NULL) {
        BOT_setOtherPlayerInfo(dcv);
        return;
    }

    static Snapshot before, original;
    Region regions[kMaxRegions];
    int n = CollectRegions(dcv, regions);

    Save(regions, n, &before);
    {
        XbeOriginalScope scope(kSetOtherPlayerInfo);
        reinterpret_cast<void (__cdecl *)(DCVars_tag *)>(kSetOtherPlayerInfo)(dcv);
    }
    Save(regions, n, &original);
    Load(regions, n, &before);
    BOT_setOtherPlayerInfo(dcv);

    bool differs = false;
    for (int i = 0; i < n; i++) {
        const unsigned char *a = original.bytes[i], *b = (const unsigned char *)regions[i].at;
        for (size_t at = 0; at < regions[i].size; at++) {
            if (a[at] == b[at])
                continue;
            differs = true;
            if (s_reported < 20) {
                s_reported++;
                printf("[botcore] call %u, player %d: %s +0x%03x original %02x ours %02x\n", s_calls,
                       dcv->drone->botVars->playerIndex, regions[i].name, (unsigned)at, a[at], b[at]);
            }
        }
    }
    if (differs)
        s_differing++;
    if (++s_calls % 1000 == 0)
        printf("[botcore] %u calls, %u differ\n", s_calls, s_differing);
}

} // namespace

void BotCoreShadow_Install(void) {
    char v[16] = "";
    GetPrivateProfileStringA("Settings", "BotCoreShadow", "", v, sizeof(v), ".\\settings.ini");
    if (_stricmp(v, "on") != 0 && strcmp(v, "1") != 0)
        return;
    if (XbeOriginal_Redirect(kSetOtherPlayerInfo, (const void *)&ShadowSetOtherPlayerInfo))
        printf("[botcore] comparing BOT_setOtherPlayerInfo with the original on every call\n");
}
