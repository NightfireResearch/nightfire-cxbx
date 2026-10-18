// Shadow test for MP_FlagUpdate and MP_BluePrintUpdate (game/mp/multiplayer_flag.cpp), on live play: with
// MPFlagShadow=on, each call through the original's address (MP_ObjectUpdate's, per object per frame) runs the
// original, and then - from the same state - ours, and compares everything either can write: MPGame, the RNG
// words, the MPOBJECT and the object.
//
// Every path that sends bot messages, queues a text message, plays a sound or scores changes the object's state
// (taken, dropped, returned, scored), so a call whose original changed gameObj->curState keeps the original's
// result and ours is not run ("kept" in the summary). What is compared: the object waiting to be taken, carried
// (its placement on the holder, the distance to the base or the base's hit list), and the 30 second count on
// the floor. The drops from MP_PlayerKilled call our code directly and are not seen here.

#include "MPFlagShadow.h"

#include "../../common/xbeOriginal.h"
#include "../game.h"
#include "../game/mp/multiplayer.h"

#include <stdio.h>
#include <string.h>
#include <windows.h>

namespace {

const unsigned kFlagUpdate = 0x0009f2c0;
const unsigned kBluePrintUpdate = 0x000a19f0;

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

Counts s_flag = { "Flag" }, s_bluePrint = { "BluePrint" };

int CollectRegions(Region *regions, MPOBJECT *mpObj, obj_tag *gameObj) {
    int n = 0;
    regions[n++] = { "MPGame", &MPGame, sizeof(MPGameStruct) };
    regions[n++] = { "rng", (void *)0x0018cdf8, 8 };
    regions[n++] = { "mpObj", mpObj, sizeof(MPOBJECT) };
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
                printf("[mpflag] %s call %u: %s +0x%03x original %02x ours %02x\n", c.name, c.calls,
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
        printf("[mpflag] %s: %u calls, %u kept, %u differ\n", c.name, c.calls, c.kept, c.differing);
}

// The original takes gameObj in EAX, mpObj in EDI, dropped on the stack, removed by the caller.
void __declspec(naked) OriginalFlagUpdate(obj_tag *gameObj, MPOBJECT *mpObj, bool dropped) {
    _asm {
        push edi
        mov eax, [esp + 8]          // gameObj
        mov edi, [esp + 12]         // mpObj
        push dword ptr [esp + 16]   // dropped
        mov edx, 0x0009f2c0
        call edx
        add esp, 4
        pop edi
        ret
    }
}

void __cdecl ShadowFlagUpdate(obj_tag *gameObj, MPOBJECT *mpObj, bool dropped) {
    static Snapshot before, original;
    Region regions[4];
    int n = CollectRegions(regions, mpObj, gameObj);

    Save(regions, n, &before);
    ushort state = gameObj->curState;
    {
        XbeOriginalScope scope(kFlagUpdate);
        OriginalFlagUpdate(gameObj, mpObj, dropped);
    }
    if (gameObj->curState != state) {
        s_flag.kept++;
        Count(s_flag);
        return;
    }
    Save(regions, n, &original);
    Load(regions, n, &before);
    _MP_FlagUpdate(gameObj, mpObj, dropped);
    Compare(s_flag, regions, n, &original);
    Count(s_flag);
}

// Where the patched entry now jumps: gameObj in EAX, mpObj in EDI, as the original.
void __declspec(naked) ShadowFlagEntry(void) {
    _asm {
        push dword ptr [esp + 4]    // dropped
        push edi                    // mpObj
        push eax                    // gameObj
        call ShadowFlagUpdate
        add esp, 12
        ret
    }
}

// The original takes gameObj in EAX, mpObj and dropped on the stack, removed by the caller.
void __declspec(naked) OriginalBluePrintUpdate(MPOBJECT *mpObj, bool dropped, obj_tag *gameObj) {
    _asm {
        mov eax, [esp + 12]         // gameObj
        push dword ptr [esp + 8]    // dropped
        push dword ptr [esp + 8]    // mpObj (8 again: the push moved it along)
        mov edx, 0x000a19f0
        call edx
        add esp, 8
        ret
    }
}

void __cdecl ShadowBluePrintUpdate(MPOBJECT *mpObj, bool dropped, obj_tag *gameObj) {
    static Snapshot before, original;
    Region regions[4];
    int n = CollectRegions(regions, mpObj, gameObj);

    Save(regions, n, &before);
    ushort state = gameObj->curState;
    {
        XbeOriginalScope scope(kBluePrintUpdate);
        OriginalBluePrintUpdate(mpObj, dropped, gameObj);
    }
    if (gameObj->curState != state) {
        s_bluePrint.kept++;
        Count(s_bluePrint);
        return;
    }
    Save(regions, n, &original);
    Load(regions, n, &before);
    _MP_BluePrintUpdate(mpObj, dropped, gameObj);
    Compare(s_bluePrint, regions, n, &original);
    Count(s_bluePrint);
}

// Where the patched entry now jumps: gameObj in EAX, as the original.
void __declspec(naked) ShadowBluePrintEntry(void) {
    _asm {
        push eax                    // gameObj
        push dword ptr [esp + 12]   // dropped
        push dword ptr [esp + 12]   // mpObj
        call ShadowBluePrintUpdate
        add esp, 12
        ret
    }
}

} // namespace

void MPFlagShadow_Install(void) {
    char v[16] = "";
    GetPrivateProfileStringA("Settings", "MPFlagShadow", "", v, sizeof(v), ".\\settings.ini");
    if (_stricmp(v, "on") != 0 && strcmp(v, "1") != 0)
        return;
    if (XbeOriginal_Redirect(kFlagUpdate, (const void *)&ShadowFlagEntry))
        printf("[mpflag] comparing MP_FlagUpdate with the original on every call\n");
    if (XbeOriginal_Redirect(kBluePrintUpdate, (const void *)&ShadowBluePrintEntry))
        printf("[mpflag] comparing MP_BluePrintUpdate with the original on every call\n");
}
