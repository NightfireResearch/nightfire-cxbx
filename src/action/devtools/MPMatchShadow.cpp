// Shadow test for the multiplayer match functions in game/mp/multiplayer.cpp, on live play: with MPMatchShadow=on,
// every call runs the original and ours from the same state and compares everything either can write - MPGame,
// MPSettings, the pickups, the radar list, the switch channels, the assassin globals, the random state, the
// agents' objects and the time and status sprites - and the results. The game carries on with ours. A summary
// per function goes to the log every 500 calls, and the first differences in full.
//
// Covered: MP_HitBy, MP_GetTarget, MP_playerIsDead, MP_ResetBotPickupTimes, MP_GetRadarObjects at their
// original addresses (the callers still the game's reach them there), and MP_Pickup_Process with
// MP_CheckForEndCondition through MP_Update, which Game_Run (ours) calls directly: once a frame, from the devtools
// tick before it, MP_Update is run as the original and as ours from the same state, compared, and the state put
// back, while EndGameFlowState is 0 (the state in which it calls nothing else that can't be run twice). A run
// during which the hundredths clock moves is not compared. Not covered: MP_Start (creates objects and sprites) and
// MP_assassinReset (plays a sound).

#include "MPMatchShadow.h"

#include "../../common/xbeOriginal.h"
#include "../actionhelpers.h"
#include "../game.h"
#include "../game/mp/multiplayer.h"
#include "../game/sp/SwitchChannels.h"

#include <stdio.h>
#include <string.h>
#include <windows.h>

namespace {

const unsigned kHitBy = 0x0009c880;
const unsigned kGetTarget = 0x0009c980;
const unsigned kCheckForEndCondition = 0x0009cc50;
const unsigned kGetRadarObjects = 0x0009da80;
const unsigned kPickupProcess = 0x0009dec0;
const unsigned kPlayerIsDead = 0x0009e6a0;
const unsigned kResetBotPickupTimes = 0x0009e850;
const unsigned kUpdate = 0x000a2b80;

const size_t kObjSize = 0xe4;
const size_t kTextBytes = 64;

struct Region {
    const char *name;
    int index;
    void *at;
    size_t size;
};

const int kMaxRegions = 32;

struct State {
    Region regions[kMaxRegions];
    int count;
    size_t total;
};

struct Snapshot {
    unsigned char bytes[0x4000];
};

struct Counter {
    const char *name;
    unsigned calls, differing, skipped;
};

Counter g_hitBy = {"MP_HitBy"}, g_getTarget = {"MP_GetTarget"}, g_playerIsDead = {"MP_playerIsDead"},
        g_resetTimes = {"MP_ResetBotPickupTimes"}, g_radar = {"MP_GetRadarObjects"}, g_update = {"MP_Update"};
unsigned g_reported;


void Add(State *s, const char *name, int index, void *at, size_t size) {
    if (s->count == kMaxRegions || s->total + size > sizeof(Snapshot::bytes))
        return;
    s->regions[s->count++] = {name, index, at, size};
    s->total += size;
}

void AddSprite(State *s, const char *name, sprite *spr) {
    if (spr == NULL)
        return;
    Add(s, name, -1, spr, sizeof(sprite));
    if (spr->text != NULL)
        Add(s, name, 0, spr->text, kTextBytes);
}

void Collect(State *s) {
    s->count = 0;
    s->total = 0;
    Add(s, "MPGame", -1, &MPGame, sizeof(MPGame));
    Add(s, "MPSettings", -1, &MPSettings, sizeof(MPSettings));
    Add(s, "MPpickups", -1, &MPpickups, sizeof(MPpickups));
    Add(s, "radar", -1, &MPRadarObjects, sizeof(MPRadarObjects));
    Add(s, "switch_channels", -1, &switch_channels, sizeof(switch_channels));
    Add(s, "assassin", -1, (void *)0x00261788, 8);    // AssassinTarget, CurrentAssassinObjId
    Add(s, "rand", -1, (void *)0x0018cdf8, 16);
    for (int i = 0; i < NUM_AGENTS; i++) {
        if (MPGame.players[i].playerObj != NULL)
            Add(s, "agent", i, MPGame.players[i].playerObj, kObjSize);
    }
    AddSprite(s, "TimeSpr", TimeSpr);
    AddSprite(s, "StatusSpr", StatusSpr);
}

void Save(const State *s, Snapshot *snap) {
    size_t at = 0;
    for (int i = 0; i < s->count; i++) {
        memcpy(snap->bytes + at, s->regions[i].at, s->regions[i].size);
        at += s->regions[i].size;
    }
}

// Writes back only what changed: a sprite's text may sit in read-only data, and then nothing wrote it.
void Load(const State *s, const Snapshot *snap) {
    size_t at = 0;
    for (int i = 0; i < s->count; i++) {
        if (memcmp(s->regions[i].at, snap->bytes + at, s->regions[i].size) != 0)
            memcpy(s->regions[i].at, snap->bytes + at, s->regions[i].size);
        at += s->regions[i].size;
    }
}

// Compares the original's state (saved) with the live state left by ours
bool Compare(const Counter &c, const State *s, const Snapshot *original) {
    bool differs = false;
    size_t at = 0;
    for (int i = 0; i < s->count; i++) {
        const unsigned char *a = original->bytes + at, *b = (const unsigned char *)s->regions[i].at;
        for (size_t k = 0; k < s->regions[i].size; k++) {
            if (a[k] == b[k])
                continue;
            differs = true;
            if (g_reported < 20) {
                g_reported++;
                printf("[mpmatch] %s call %u: %s[%d] +0x%03x original %02x ours %02x\n", c.name, c.calls,
                       s->regions[i].name, s->regions[i].index, (unsigned)k, a[k], b[k]);
            }
        }
        at += s->regions[i].size;
    }
    return differs;
}

void ReportResult(const Counter &c, const char *what, unsigned original, unsigned ours) {
    if (g_reported < 20) {
        g_reported++;
        printf("[mpmatch] %s call %u: %s original %08x ours %08x\n", c.name, c.calls, what, original, ours);
    }
}

void Finish(Counter &c, bool differs) {
    if (differs)
        c.differing++;
    // every 500 calls, and at 1, 2, 4 ... before that, so a rarely called function reports too
    if (++c.calls % 500 == 0 || (c.calls < 500 && (c.calls & (c.calls - 1)) == 0))
        printf("[mpmatch] %s: %u calls, %u differ, %u not compared (clock moved)\n", c.name, c.calls, c.differing,
               c.skipped);
}

Snapshot g_before, g_original;

} // namespace

// The original: obj in EAX, team in CX, exclude and teamOut on the stack
static __declspec(naked) obj_tag* __cdecl CallOriginalHitBy(obj_tag *obj, ushort team, obj_tag *exclude,
                                                            ushort *teamOut) {
    __asm {
        mov eax, [esp + 4]
        mov ecx, [esp + 8]
        push dword ptr [esp + 16]
        push dword ptr [esp + 16]
        mov edx, 0x0009c880
        call edx
        add esp, 8
        ret
    }
}

static obj_tag* __cdecl ShadowHitBy(obj_tag *obj, ushort team, obj_tag *exclude, ushort *teamOut) {
    State s;
    Collect(&s);
    Save(&s, &g_before);
    ushort originalTeam = 0xbeef, ourTeam = 0xbeef;
    obj_tag *original;
    {
        XbeOriginalScope scope(kHitBy);
        original = CallOriginalHitBy(obj, team, exclude, teamOut != NULL ? &originalTeam : NULL);
    }
    Save(&s, &g_original);
    Load(&s, &g_before);
    obj_tag *ours = _MP_HitBy(obj, team, exclude, teamOut != NULL ? &ourTeam : NULL);

    bool differs = Compare(g_hitBy, &s, &g_original);
    if (original != ours) {
        differs = true;
        ReportResult(g_hitBy, "return", (unsigned)original, (unsigned)ours);
    }
    if (originalTeam != ourTeam) {
        differs = true;
        ReportResult(g_hitBy, "teamOut", originalTeam, ourTeam);
    }
    Finish(g_hitBy, differs);
    if (teamOut != NULL && ourTeam != 0xbeef)
        *teamOut = ourTeam;
    return ours;
}

static __declspec(naked) void ShadowHitByEntry(void) {
    __asm {
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        movzx ecx, cx
        push ecx
        push eax
        call ShadowHitBy
        add esp, 16
        ret
    }
}

static obj_tag* __cdecl ShadowGetTarget(ushort team, obj_tag *exclude, bool aliveOnly) {
    State s;
    Collect(&s);
    Save(&s, &g_before);
    obj_tag *original;
    {
        XbeOriginalScope scope(kGetTarget);
        original = reinterpret_cast<obj_tag *(__cdecl *)(ushort, obj_tag *, bool)>(kGetTarget)(team, exclude,
                                                                                              aliveOnly);
    }
    Save(&s, &g_original);
    Load(&s, &g_before);
    obj_tag *ours = MP_GetTarget(team, exclude, aliveOnly);

    bool differs = Compare(g_getTarget, &s, &g_original);
    if (original != ours) {
        differs = true;
        ReportResult(g_getTarget, "return", (unsigned)original, (unsigned)ours);
    }
    Finish(g_getTarget, differs);
    return ours;
}

static bool __cdecl ShadowPlayerIsDead(obj_tag *obj) {
    State s;
    Collect(&s);
    Save(&s, &g_before);
    bool original;
    {
        XbeOriginalScope scope(kPlayerIsDead);
        original = reinterpret_cast<bool (__cdecl *)(obj_tag *)>(kPlayerIsDead)(obj);
    }
    Save(&s, &g_original);
    Load(&s, &g_before);
    bool ours = MP_playerIsDead(obj);

    bool differs = Compare(g_playerIsDead, &s, &g_original);
    if (original != ours) {
        differs = true;
        ReportResult(g_playerIsDead, "return", original, ours);
    }
    Finish(g_playerIsDead, differs);
    return ours;
}

static void __cdecl ShadowResetBotPickupTimes(ushort botNum) {
    State s;
    Collect(&s);
    Save(&s, &g_before);
    {
        XbeOriginalScope scope(kResetBotPickupTimes);
        reinterpret_cast<void (__cdecl *)(ushort)>(kResetBotPickupTimes)(botNum);
    }
    Save(&s, &g_original);
    Load(&s, &g_before);
    MP_ResetBotPickupTimes(botNum);
    Finish(g_resetTimes, Compare(g_resetTimes, &s, &g_original));
}

static ushort __cdecl ShadowGetRadarObjects(obj_tag *viewer, MP_RADAR_OBJECT **objects) {
    State s;
    Collect(&s);
    Save(&s, &g_before);
    MP_RADAR_OBJECT *originalList = NULL, *ourList = NULL;
    ushort original;
    {
        XbeOriginalScope scope(kGetRadarObjects);
        original = reinterpret_cast<ushort (__cdecl *)(obj_tag *, MP_RADAR_OBJECT **)>(kGetRadarObjects)(
            viewer, &originalList);
    }
    Save(&s, &g_original);
    Load(&s, &g_before);
    ushort ours = MP_GetRadarObjects(viewer, &ourList);

    bool differs = Compare(g_radar, &s, &g_original);
    if (original != ours) {
        differs = true;
        ReportResult(g_radar, "count", original, ours);
    }
    if (originalList != ourList) {
        differs = true;
        ReportResult(g_radar, "list", (unsigned)originalList, (unsigned)ourList);
    }
    Finish(g_radar, differs);
    *objects = ourList;
    return ours;
}

static bool g_on;

void MPMatchShadow_Tick(void) {
    if (!g_on || !MPSettings.isMultiplayer || MPGame.EndGameFlowState != 0)
        return;

    State s;
    Collect(&s);
    Save(&s, &g_before);
    uint32_t clockBefore = (uint32_t)psiGetTimeIn100ths();
    {
        XbeOriginalScope update(kUpdate), pickups(kPickupProcess), endCondition(kCheckForEndCondition);
        reinterpret_cast<void (__cdecl *)(void)>(kUpdate)();
    }
    Save(&s, &g_original);
    Load(&s, &g_before);
    MP_Update();
    uint32_t clockAfter = (uint32_t)psiGetTimeIn100ths();

    if (clockBefore != clockAfter)
        g_update.skipped++;
    Finish(g_update, clockBefore == clockAfter && Compare(g_update, &s, &g_original));
    Load(&s, &g_before);   // a dry run: the frame's own MP_Update comes after
}

void MPMatchShadow_Install(void) {
    char v[16] = "";
    GetPrivateProfileStringA("Settings", "MPMatchShadow", "", v, sizeof(v), ".\\settings.ini");
    if (_stricmp(v, "on") != 0 && strcmp(v, "1") != 0)
        return;
    g_on = true;
    bool ok = XbeOriginal_Redirect(kHitBy, (const void *)&ShadowHitByEntry);
    ok &= XbeOriginal_Redirect(kGetTarget, (const void *)&ShadowGetTarget);
    ok &= XbeOriginal_Redirect(kPlayerIsDead, (const void *)&ShadowPlayerIsDead);
    ok &= XbeOriginal_Redirect(kResetBotPickupTimes, (const void *)&ShadowResetBotPickupTimes);
    ok &= XbeOriginal_Redirect(kGetRadarObjects, (const void *)&ShadowGetRadarObjects);
    printf("[mpmatch] comparing the multiplayer match functions with the originals on every call%s\n",
           ok ? "" : " (some not patched)");
}
