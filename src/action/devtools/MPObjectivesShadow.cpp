// Shadow test for MP_ObjectUpdate and MP_DemolitionProtectionUpdate (game/mp/multiplayer_objects.cpp), on live
// play: with MPObjectivesShadow=on, each scenario object's update runs the original MP_ObjectUpdate (with the
// original of the update it dispatches to), and then - from the same state - ours, and compares everything either
// can write: MPGame, the RNG words, the MPOBJECT, the object, the demolition and protection MP_OBJ_EXTs and the
// hit list scratch. The game carries on with ours.
//
// Our MP_ObjectUpdate calls the uplink and hill updates directly, so MPModesShadow's redirects no longer see them;
// they are compared here instead, with its rules.
//
// A call whose original played a sound, queued a message, switched the script, exploded the object or sent a bot
// message cannot be run twice, so the original's result is kept and ours is not run ("kept" in the summary):
// - demolition and protection: any damage (curState changes), the time-out or the destruction (subState changes),
//   a "protect it" message (a friendly-fire timer goes from 0), and a change of the objective's object;
// - hill: an agent entering or leaving, the entry sound, the beep at every fifth point;
// - uplink: a change of team.
// Flags, bases, blueprints and GoldenEye objects run ours only (their updates are other files' ports), and so does
// MP_BluePrintReachedBase, which always plays a sound, sends bot messages, draws a random number and queues a
// message; those are left to the replays.

#include "MPObjectivesShadow.h"

#include "../../common/xbeOriginal.h"
#include "../game.h"
#include "../game/mp/multiplayer.h"

#include <stdio.h>
#include <string.h>
#include <windows.h>

namespace {

const unsigned kObjectUpdate = 0x000a1ee0;
const unsigned kDemolitionProtectionUpdate = 0x0009ff70;
const unsigned kKOHUpdate = 0x0009f8b0;
const unsigned kUplinkUpdate = 0x0009fb80;

const size_t kObjSize = 0xe4;
void *const kRandWords = (void *)0x0018cdf8;   // U32_AT(0x0018cdf8) and U32_AT(0x0018cdfc)
void *const kHitByList = (void *)0x00261c58;   // obj_tag *[64]
MP_OBJ_EXT *const kDemolition = (MP_OBJ_EXT *)0x00261af8;
MP_OBJ_EXT *const kProtection = (MP_OBJ_EXT *)0x00261b40;

const int kMaxRegions = 8;

struct Region {
    const char *name;
    void *at;
    size_t size;
};

struct Snapshot {
    unsigned char bytes[kMaxRegions][0x230];
};

struct Counts {
    const char *name;
    unsigned calls, kept, differing, reported;
};

Counts s_demolition = { "Demolition" }, s_protection = { "Protection" }, s_uplink = { "Uplink" },
       s_koh = { "KOH" }, s_other = { "other (ours only)" };

int CollectRegions(Region *regions, MPOBJECT *mpObj, obj_tag *gameObj) {
    int n = 0;
    regions[n++] = { "MPGame", &MPGame, sizeof(MPGameStruct) };
    regions[n++] = { "rng", kRandWords, 8 };
    regions[n++] = { "mpObj", mpObj, sizeof(MPOBJECT) };
    regions[n++] = { "gameObj", gameObj, kObjSize };
    regions[n++] = { "Demolition", kDemolition, sizeof(MP_OBJ_EXT) };
    regions[n++] = { "Protection", kProtection, sizeof(MP_OBJ_EXT) };
    regions[n++] = { "HitByList", kHitByList, 64 * sizeof(obj_tag *) };
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

// The original's results (in s) against what is in memory now, after ours.
void Compare(Counts &c, const Region *regions, int n, const Snapshot *s) {
    bool differs = false;
    for (int i = 0; i < n; i++) {
        const unsigned char *a = s->bytes[i], *b = (const unsigned char *)regions[i].at;
        for (size_t at = 0; at < regions[i].size; at++) {
            if (a[at] == b[at])
                continue;
            differs = true;
            if (c.reported < 20) {
                c.reported++;
                printf("[mpobjectives] %s call %u: %s +0x%03x original %02x ours %02x\n", c.name, c.calls,
                       regions[i].name, (unsigned)at, a[at], b[at]);
            }
        }
    }
    if (differs)
        c.differing++;
}

void Count(Counts &c) {
    if (++c.calls % 500 == 0 || (c.calls < 500 && (c.calls & (c.calls - 1)) == 0))
        printf("[mpobjectives] %s: %u calls, %u kept, %u differ\n", c.name, c.calls, c.kept, c.differing);
}

// Whether the original's demolition or protection update went down a path with sounds, messages or the script.
bool ObjectiveSideEffects(const Snapshot &before, const obj_tag *gameObj, const MPOBJECT *mpObj) {
    const MPGameStruct &was = *(const MPGameStruct *)before.bytes[0];
    const obj_tag &wasObj = *(const obj_tag *)before.bytes[3];
    if (wasObj.curState != gameObj->curState || wasObj.subState != gameObj->subState)
        return true;
    if (was.EndGameFlowState != MPGame.EndGameFlowState)
        return true;
    for (int i = 0; i < NUM_AGENTS; i++) {
        if (was.players[i].friendlyFireProtectionLabelTimer == 0 && MPGame.players[i].friendlyFireProtectionLabelTimer != 0)
            return true;
    }
    const MP_OBJ_EXT &wasExt = *(const MP_OBJ_EXT *)before.bytes[mpObj->type == DEMOLITION ? 4 : 5];
    const MP_OBJ_EXT &ext = mpObj->type == DEMOLITION ? *kDemolition : *kProtection;
    return wasExt.gameObj != ext.gameObj;
}

// As MPModesShadow: an agent entering or leaving the hill, the entry sound, the beep at every fifth point.
bool KOHSideEffects(const Snapshot &before) {
    const MPGameStruct &was = *(const MPGameStruct *)before.bytes[0];
    for (int i = 0; i < NUM_AGENTS; i++) {
        const MPGamePlayer &a = was.players[i], &b = MPGame.players[i];
        if ((a.flags ^ b.flags) & MPPLAYER_IN_HILL)
            return true;
        if (memcmp(&a.hillSoundTime, &b.hillSoundTime, sizeof(float)) != 0)
            return true;
        int pa = (int)a.points, pb = (int)b.points;
        if (pa != pb && (unsigned)pb % 5 == 0)
            return true;
    }
    return false;
}

void __cdecl ShadowObjectUpdate(obj_tag *gameObj) {
    static Snapshot before, original;
    MPOBJECT *mpObj = (MPOBJECT *)gameObj->extraObjectData;

    Counts *c;
    unsigned inner;
    switch (mpObj->type) {
    case DEMOLITION: c = &s_demolition; inner = kDemolitionProtectionUpdate; break;
    case PROTECTION: c = &s_protection; inner = kDemolitionProtectionUpdate; break;
    case UPLINK: c = &s_uplink; inner = kUplinkUpdate; break;
    case KOH: c = &s_koh; inner = kKOHUpdate; break;
    default:
        MP_ObjectUpdate(gameObj);
        Count(s_other);
        return;
    }

    Region regions[kMaxRegions];
    int n = CollectRegions(regions, mpObj, gameObj);
    Save(regions, n, &before);
    {
        XbeOriginalScope scope(kObjectUpdate);
        XbeOriginalScope innerScope(inner);
        reinterpret_cast<void (__cdecl *)(obj_tag *)>(kObjectUpdate)(gameObj);
    }

    bool kept;
    if (c == &s_demolition || c == &s_protection)
        kept = ObjectiveSideEffects(before, gameObj, mpObj);
    else if (c == &s_koh)
        kept = KOHSideEffects(before);
    else
        kept = ((const obj_tag *)before.bytes[3])->curState != gameObj->curState;
    if (kept) {
        c->kept++;
        Count(*c);
        return;
    }

    Save(regions, n, &original);
    Load(regions, n, &before);
    MP_ObjectUpdate(gameObj);
    Compare(*c, regions, n, &original);
    Count(*c);
}

} // namespace

void MPObjectivesShadow_Install(void) {
    char v[16] = "";
    GetPrivateProfileStringA("Settings", "MPObjectivesShadow", "", v, sizeof(v), ".\\settings.ini");
    if (_stricmp(v, "on") != 0 && strcmp(v, "1") != 0)
        return;
    if (XbeOriginal_Redirect(kObjectUpdate, (const void *)&ShadowObjectUpdate))
        printf("[mpobjectives] comparing MP_ObjectUpdate (demolition, protection, uplink, hill) with the original on every call\n");
}
