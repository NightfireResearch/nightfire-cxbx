// Shadow test for MP_KOHUpdate and MP_UplinkUpdate (game/mp/multiplayer_modes.cpp), on live play: with
// MPModesShadow=on, each call runs the original, and then - from the same state - ours, and compares everything
// either can write: MPGame, the RNG words, and for the uplink its MPOBJECT and object.
//
// A call whose original sent bot messages, called the script player or played a sound cannot be run twice, so
// the original's result is kept and ours is not run ("kept" in the summary): for the hill, an agent entering or
// leaving it, the entry sound and the beep at every fifth point; for the uplink, a change of team. Those paths
// are left to the replays. MP_PlayerKilled is not shadowed: it routes drone messages, queues a text message,
// sets health, frees hit lists and drops carried objects.

#include "MPModesShadow.h"

#include "../../common/xbeOriginal.h"
#include "../game.h"
#include "../game/mp/multiplayer.h"

#include <stdio.h>
#include <string.h>
#include <windows.h>

namespace {

const unsigned kKOHUpdate = 0x0009f8b0;
const unsigned kUplinkUpdate = 0x0009fb80;

const size_t kObjSize = 0xe4;

struct Region {
    const char *name;
    void *at;
    size_t size;
};

struct Snapshot {
    unsigned char bytes[4][0x230];
};

struct Counts {
    const char *name;
    unsigned calls, kept, differing, reported;
};

Counts s_koh = { "KOH" }, s_uplink = { "Uplink" };

int CollectRegions(Region *regions, MPOBJECT *mpObj, obj_tag *gameObj) {
    int n = 0;
    regions[n++] = { "MPGame", &MPGame, sizeof(MPGameStruct) };
    regions[n++] = { "rng", (void *)0x0018cdf8, 8 };
    if (mpObj != NULL)
        regions[n++] = { "mpObj", mpObj, sizeof(MPOBJECT) };
    if (gameObj != NULL)
        regions[n++] = { "gameObj", gameObj, kObjSize };
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
                printf("[mpmodes] %s call %u: %s +0x%03x original %02x ours %02x\n", c.name, c.calls,
                       regions[i].name, (unsigned)at, a[at], b[at]);
            }
        }
    }
    if (differs)
        c.differing++;
}

void Count(Counts &c) {
    // every 500 calls, and at 1, 2, 4 ... before that, so a rarely called function reports too
    if (++c.calls % 500 == 0 || (c.calls < 500 && (c.calls & (c.calls - 1)) == 0))
        printf("[mpmodes] %s: %u calls, %u kept, %u differ\n", c.name, c.calls, c.kept, c.differing);
}

// Whether the original's hill update went down a path with messages or sounds.
bool KOHSideEffects(const MPGameStruct &before) {
    for (int i = 0; i < NUM_AGENTS; i++) {
        const MPGamePlayer &was = before.players[i], &now = MPGame.players[i];
        if ((was.flags ^ now.flags) & MPPLAYER_IN_HILL)
            return true;
        if (memcmp(&was.hillSoundTime, &now.hillSoundTime, sizeof(float)) != 0)
            return true;
        int a = (int)was.points, b = (int)now.points;
        if (a != b && (unsigned)b % 5 == 0)
            return true;
    }
    return false;
}

void __cdecl ShadowKOHUpdate(obj_tag *hill) {
    static Snapshot before, original;
    Region regions[4];
    int n = CollectRegions(regions, NULL, NULL);

    Save(regions, n, &before);
    {
        XbeOriginalScope scope(kKOHUpdate);
        reinterpret_cast<void (__cdecl *)(obj_tag *)>(kKOHUpdate)(hill);
    }
    if (KOHSideEffects(*(const MPGameStruct *)before.bytes[0])) {
        s_koh.kept++;
        Count(s_koh);
        return;
    }
    Save(regions, n, &original);
    Load(regions, n, &before);
    MP_KOHUpdate(hill);
    Compare(s_koh, regions, n, &original);
    Count(s_koh);
}

// The original takes gameObj in EAX, mpObj on the stack, removed by the caller.
void __declspec(naked) OriginalUplinkUpdate(MPOBJECT *mpObj, obj_tag *gameObj) {
    _asm {
        mov eax, [esp + 8]          // gameObj
        push dword ptr [esp + 4]    // mpObj
        mov edx, 0x0009fb80
        call edx
        add esp, 4
        ret
    }
}

void __cdecl ShadowUplinkUpdate(MPOBJECT *mpObj, obj_tag *gameObj) {
    static Snapshot before, original;
    Region regions[4];
    int n = CollectRegions(regions, mpObj, gameObj);

    Save(regions, n, &before);
    ushort state = gameObj->curState;
    {
        XbeOriginalScope scope(kUplinkUpdate);
        OriginalUplinkUpdate(mpObj, gameObj);
    }
    if (gameObj->curState != state) {
        s_uplink.kept++;
        Count(s_uplink);
        return;
    }
    Save(regions, n, &original);
    Load(regions, n, &before);
    _MP_UplinkUpdate(mpObj, gameObj);
    Compare(s_uplink, regions, n, &original);
    Count(s_uplink);
}

// Where the patched entry now jumps: gameObj in EAX, as the original.
void __declspec(naked) ShadowUplinkEntry(void) {
    _asm {
        push eax                    // gameObj
        push dword ptr [esp + 8]    // mpObj
        call ShadowUplinkUpdate
        add esp, 8
        ret
    }
}

} // namespace

void MPModesShadow_Install(void) {
    char v[16] = "";
    GetPrivateProfileStringA("Settings", "MPModesShadow", "", v, sizeof(v), ".\\settings.ini");
    if (_stricmp(v, "on") != 0 && strcmp(v, "1") != 0)
        return;
    if (XbeOriginal_Redirect(kKOHUpdate, (const void *)&ShadowKOHUpdate))
        printf("[mpmodes] comparing MP_KOHUpdate with the original on every call\n");
    if (XbeOriginal_Redirect(kUplinkUpdate, (const void *)&ShadowUplinkEntry))
        printf("[mpmodes] comparing MP_UplinkUpdate with the original on every call\n");
}
