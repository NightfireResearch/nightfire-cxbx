// Shadow test for Player_PositionCamera (game/obj/Player.cpp), on live play: with CameraShadow=on, every call
// runs the original and ours from the same state and compares everything either can write - the player, the
// player's BLData, the viewer, the weapon model, and the remote device or followed object when there is one.
// The game carries on with ours. A summary goes to the log every 500 calls, and the first differences in full.
//
// Both run on the same helpers (all still the game's, or ours and shared), so the two should agree to the bit -
// with FOV=60, where the scope correction is exactly 1.

#include "CameraShadow.h"

#include "../../common/xbeOriginal.h"
#include "../game/obj/Player.h"
#include "../engine/viewer.h"

#include <stdio.h>
#include <string.h>
#include <windows.h>

static const unsigned kPositionCamera = 0x000a8a60;

// The real sizes, from the game's own structures: not every one is fully laid out in our headers yet.
static const size_t kObjSize = 0xe4, kBLDataSize = 0x8fc, kViewerSize = 0x208;

struct Region {
    const char *name;
    void *at;
    size_t size;
};

struct Snapshot {
    unsigned char bytes[6][0x900];
};

static unsigned s_calls, s_differing, s_reported;

static int CollectRegions(obj_tag *player, Region *regions) {
    BLData *blData = (BLData*)player->extraObjectData;
    int n = 0;
    regions[n++] = { "player", player, kObjSize };
    regions[n++] = { "BLData", blData, kBLDataSize };
    regions[n++] = { "viewer", glb_viewer[blData->playerNum], kViewerSize };
    if (blData->weaponObject != NULL)
        regions[n++] = { "weapon", blData->weaponObject, kObjSize };
    if (blData->remoteControlDevice != NULL)
        regions[n++] = { "device", blData->remoteControlDevice, kObjSize };
    if (blData->cameraFollowObject != NULL)
        regions[n++] = { "follow", blData->cameraFollowObject, kObjSize };
    return n;
}

static void Save(const Region *regions, int n, Snapshot *s) {
    for (int i = 0; i < n; i++)
        memcpy(s->bytes[i], regions[i].at, regions[i].size);
}

static void Load(const Region *regions, int n, const Snapshot *s) {
    for (int i = 0; i < n; i++)
        memcpy(regions[i].at, s->bytes[i], regions[i].size);
}

static void __cdecl ShadowPositionCamera(obj_tag *player) {
    static Snapshot before, original;
    Region regions[6];
    int n = CollectRegions(player, regions);

    Save(regions, n, &before);
    {
        XbeOriginalScope scope(kPositionCamera);
        reinterpret_cast<void (__cdecl *)(obj_tag *)>(kPositionCamera)(player);
    }
    Save(regions, n, &original);
    Load(regions, n, &before);
    Player_PositionCamera(player);

    bool differs = false;
    for (int i = 0; i < n; i++) {
        const unsigned char *a = original.bytes[i], *b = (const unsigned char *)regions[i].at;
        for (size_t at = 0; at < regions[i].size; at++) {
            if (a[at] == b[at])
                continue;
            differs = true;
            if (s_reported < 20) {
                s_reported++;
                printf("[camshadow] call %u, camera mode %d: %s +0x%03x original %02x ours %02x\n", s_calls,
                       ((BLData*)player->extraObjectData)->camMode, regions[i].name, (unsigned)at, a[at], b[at]);
            }
        }
    }
    if (differs)
        s_differing++;
    if (++s_calls % 500 == 0)
        printf("[camshadow] %u calls, %u differ\n", s_calls, s_differing);
}

void CameraShadow_Install(void) {
    char v[16] = "";
    GetPrivateProfileStringA("Settings", "CameraShadow", "", v, sizeof(v), ".\\settings.ini");
    if (_stricmp(v, "on") != 0 && strcmp(v, "1") != 0)
        return;
    if (XbeOriginal_Redirect(kPositionCamera, (const void *)&ShadowPositionCamera))
        printf("[camshadow] comparing Player_PositionCamera with the original on every call\n");
}
