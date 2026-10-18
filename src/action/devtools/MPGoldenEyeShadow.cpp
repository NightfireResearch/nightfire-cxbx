// Shadow test for MP_GoldenEyeUpdate (game/mp/multiplayer_goldeneye.cpp), on live play: with MPGoldenEyeShadow=on,
// each call through the original's address (MP_ObjectUpdate's, per part per frame) runs the original, and then -
// from the same state - ours, and compares everything either can write: MPGame, the RNG words, both parts' MPOBJECTs
// and objects, GoldenEye's record and the ray's object.
//
// A call whose original took, dropped or returned a part, fired the ray or put it out sent bot messages, queued
// text, played sounds, killed an agent or reset objects, so it cannot be run twice: the original's result is kept
// and ours is not run ("kept" in the summary). What is compared is the frame-to-frame work: a part at its place or
// on the floor with nobody touching it (the 30 second count), a carried part following its holder, the ray
// following its target, and state 99. MP_GoldeneyeResetObject is reached only from ours, on kept paths, and the
// dropped path only from our MP_PlayerKilled; both are left to the replays.

#include "MPGoldenEyeShadow.h"

#include "../../common/xbeOriginal.h"
#include "../game.h"
#include "../game/mp/multiplayer.h"

#include <stdio.h>
#include <string.h>
#include <windows.h>

namespace {

const unsigned kGoldenEyeUpdate = 0x000a0370;

const size_t kObjSize = 0xe4;
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
    unsigned calls, kept, differing, reported;
};

Counts s_counts;

int CollectRegions(Region *regions, MPOBJECT *mpObj, obj_tag *gameObj) {
    int n = 0;
    regions[n++] = { "MPGame", &MPGame, sizeof(MPGameStruct) };
    regions[n++] = { "rng", (void *)0x0018cdf8, 8 };
    regions[n++] = { "GoldenEye", &GoldenEye, sizeof(GoldenEyeStruct) };
    regions[n++] = { "mpObj", mpObj, sizeof(MPOBJECT) };
    regions[n++] = { "gameObj", gameObj, kObjSize };
    obj_tag *otherObj = GoldenEye.keys[gameObj->subState == 0].gameObj;
    if (otherObj != NULL && otherObj != gameObj) {
        regions[n++] = { "otherObj", otherObj, kObjSize };
        if (otherObj->extraObjectData != NULL && otherObj->extraObjectData != mpObj)
            regions[n++] = { "otherMpObj", otherObj->extraObjectData, sizeof(MPOBJECT) };
    }
    if (GoldenEye.deathRay != NULL)
        regions[n++] = { "ray", GoldenEye.deathRay, kObjSize };
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
void Compare(const Region *regions, int n, const Snapshot *s) {
    bool differs = false;
    for (int i = 0; i < n; i++) {
        const unsigned char *a = s->bytes[i], *b = (const unsigned char *)regions[i].at;
        for (size_t at = 0; at < regions[i].size; at++) {
            if (a[at] == b[at])
                continue;
            differs = true;
            if (s_counts.reported < 20) {
                s_counts.reported++;
                printf("[mpgoldeneye] call %u: %s +0x%03x original %02x ours %02x\n", s_counts.calls,
                       regions[i].name, (unsigned)at, a[at], b[at]);
            }
        }
    }
    if (differs)
        s_counts.differing++;
}

void Count(void) {
    // every 500 calls, and at 1, 2, 4 ... before that, so a rarely called function reports too
    unsigned calls = ++s_counts.calls;
    if (calls % 500 == 0 || (calls < 500 && (calls & (calls - 1)) == 0))
        printf("[mpgoldeneye] GoldenEyeUpdate: %u calls, %u kept, %u differ\n", calls, s_counts.kept,
               s_counts.differing);
}

// The parts' states and the ray, to tell whether the original went down a path with side effects.
struct Watch {
    ushort state, keyState[2];
    obj_tag *ray, *target, *holder;
};

Watch Read(MPOBJECT *mpObj, obj_tag *gameObj) {
    Watch w;
    w.state = gameObj->curState;
    for (int i = 0; i < 2; i++)
        w.keyState[i] = GoldenEye.keys[i].gameObj != NULL ? GoldenEye.keys[i].gameObj->curState : 0;
    w.ray = GoldenEye.deathRay;
    w.target = GoldenEye.targetedPlayer;
    w.holder = mpObj->holder;
    return w;
}

bool SideEffects(const Watch &was, const Watch &now) {
    if (now.state != was.state && was.state != 99)
        return true;
    if (now.keyState[0] != was.keyState[0] && was.keyState[0] != 99)
        return true;
    if (now.keyState[1] != was.keyState[1] && was.keyState[1] != 99)
        return true;
    if (now.ray != was.ray || now.target != was.target)
        return true;
    return now.holder != NULL && now.holder != was.holder;
}

// The original takes gameObj in EAX, the other two on the stack, removed by the caller.
void __declspec(naked) OriginalGoldenEyeUpdate(MPOBJECT *mpObj, bool dropped, obj_tag *gameObj) {
    _asm {
        mov eax, [esp + 12]         // gameObj
        push dword ptr [esp + 8]    // dropped
        push dword ptr [esp + 8]    // mpObj
        mov edx, 0x000a0370
        call edx
        add esp, 8
        ret
    }
}

void __cdecl ShadowGoldenEyeUpdate(MPOBJECT *mpObj, bool dropped, obj_tag *gameObj) {
    static Snapshot before, original;
    Region regions[kMaxRegions];
    int n = CollectRegions(regions, mpObj, gameObj);

    Save(regions, n, &before);
    Watch was = Read(mpObj, gameObj);
    {
        XbeOriginalScope scope(kGoldenEyeUpdate);
        OriginalGoldenEyeUpdate(mpObj, dropped, gameObj);
    }
    if (dropped || SideEffects(was, Read(mpObj, gameObj))) {
        s_counts.kept++;
        Count();
        return;
    }
    Save(regions, n, &original);
    Load(regions, n, &before);
    _MP_GoldenEyeUpdate(mpObj, dropped, gameObj);
    Compare(regions, n, &original);
    Count();
}

// Where the patched entry now jumps: gameObj in EAX, as the original.
void __declspec(naked) ShadowGoldenEyeEntry(void) {
    _asm {
        push eax                    // gameObj
        push dword ptr [esp + 12]   // dropped
        push dword ptr [esp + 12]   // mpObj
        call ShadowGoldenEyeUpdate
        add esp, 12
        ret
    }
}

} // namespace

void MPGoldenEyeShadow_Install(void) {
    char v[16] = "";
    GetPrivateProfileStringA("Settings", "MPGoldenEyeShadow", "", v, sizeof(v), ".\\settings.ini");
    if (_stricmp(v, "on") != 0 && strcmp(v, "1") != 0)
        return;
    if (XbeOriginal_Redirect(kGoldenEyeUpdate, (const void *)&ShadowGoldenEyeEntry))
        printf("[mpgoldeneye] comparing MP_GoldenEyeUpdate with the original on every call\n");
}
