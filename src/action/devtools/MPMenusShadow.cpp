// Shadow test for the multiplayer menu functions on globals only (src/action/ui/ui_mp.cpp): Menu_PrepareBots,
// Menu_StoreMPSettings and Menu_RestoreMPSettings. Each runs as the original and as ours from the same synthetic
// state - every combination of playing bots, on Ravine and elsewhere, with distinct records and names; settings,
// controllers and the backup filled with patterns, controller styles with both bytes set - and everything any of
// them can write is compared: mpbots, MPSettings, PlayerInputs and the backup. The live state is put back after.

#include "MPMenusShadow.h"

#include "../../common/xbeOriginal.h"
#include "../game/mp/multiplayer.h"
#include "../input.h"
#include "../ui/Menu.h"
#include "../assets.h"

#include <stdio.h>
#include <string.h>
#include <windows.h>

namespace {

const unsigned kPrepareBots = 0x00075dd0, kStoreMPSettings = 0x0007eb00, kRestoreMPSettings = 0x0007eb30;

// The backup: the settings at 0x2245f8, four bytes nobody uses, then the controllers at 0x224838.
const unsigned kBackupAt = 0x002245f8;
const size_t kBackupSize = 0x00224838 + 0x560 - kBackupAt;

struct State {
    MPBOTS bots;
    MPSettings_t settings;
    PlayerInput inputs[NUM_PLAYERS];
    unsigned char backup[kBackupSize];
};

void Save(State *s) {
    memset(s, 0, sizeof(*s));   // padding too, since states are compared whole
    s->bots = mpbots;
    s->settings = MPSettings;
    memcpy(s->inputs, PlayerInputs, sizeof(s->inputs));
    memcpy(s->backup, (void *)kBackupAt, sizeof(s->backup));
}

void Load(const State *s) {
    mpbots = s->bots;
    MPSettings = s->settings;
    memcpy(PlayerInputs, s->inputs, sizeof(s->inputs));
    memcpy((void *)kBackupAt, s->backup, sizeof(s->backup));
}

unsigned g_seed;
unsigned char Next() {
    g_seed = g_seed * 1103515245u + 12345u;
    return (unsigned char)(g_seed >> 16);
}

void Fill(void *at, size_t size) {
    for (size_t i = 0; i < size; i++)
        ((unsigned char *)at)[i] = Next();
}

// Everything patterned; names NUL-terminated and distinct.
void Randomise(State *s, unsigned seed) {
    g_seed = seed;
    Fill(s, sizeof(*s));
    for (int i = 0; i < NUM_AGENTS; i++)
        snprintf(s->settings.Player[i].Name, sizeof(s->settings.Player[i].Name), "agent %d seed %u", i, seed);
}

typedef void(__stdcall *VoidFn)(void);

int g_runs, g_differ;

void Compare(const char *what, const State &start, unsigned original, VoidFn ours) {
    Load(&start);
    {
        XbeOriginalScope scope(original);
        ((VoidFn)original)();
    }
    State a;
    Save(&a);
    Load(&start);
    ours();
    State b;
    Save(&b);
    g_runs++;
    if (memcmp(&a, &b, sizeof(a)) == 0)
        return;
    g_differ++;
    if (g_differ > 20)
        return;
    const unsigned char *pa = (const unsigned char *)&a, *pb = (const unsigned char *)&b;
    size_t first = 0;
    while (pa[first] == pb[first])
        first++;
    const char *region = first < sizeof(MPBOTS) ? "mpbots"
        : first < offsetof(State, inputs) ? "MPSettings"
        : first < offsetof(State, backup) ? "PlayerInputs" : "backup";
    printf("[mpmenus] DIFFER %s: first at state +0x%x (%s), original %02x ours %02x\n", what, (unsigned)first,
           region, pa[first], pb[first]);
}

}  // namespace

void MPMenusShadow_Run(void) {
    g_runs = g_differ = 0;
    State saved;
    Save(&saved);

    char what[96];
    for (int ravine = 0; ravine < 2; ravine++)
        for (unsigned mask = 0; mask < (1u << NUM_BOTS); mask++) {
            State start;
            Randomise(&start, mask * 2 + ravine);
            for (int b = 0; b < NUM_BOTS; b++)
                start.bots.bot[b].isPlaying = (mask >> b & 1) ? (char)(1 + b) : 0;   // any non-zero plays
            start.settings.multiplayerLevelHashcode = ravine ? HT_Level_Ravine : HT_Level_SkyRail;
            snprintf(what, sizeof(what), "Menu_PrepareBots(playing 0x%02x%s)", mask, ravine ? ", Ravine" : "");
            Compare(what, start, kPrepareBots, Menu_PrepareBots);
        }

    for (unsigned seed = 0; seed < 16; seed++) {
        State start;
        Randomise(&start, 0x1000 + seed);
        for (int p = 0; p < NUM_PLAYERS; p++)
            start.inputs[p].controlStyle = (short)(0x8100 + seed * 0x111 + p);   // both bytes, and the sign
        snprintf(what, sizeof(what), "Menu_StoreMPSettings(seed %u)", seed);
        Compare(what, start, kStoreMPSettings, Menu_StoreMPSettings);
        snprintf(what, sizeof(what), "Menu_RestoreMPSettings(seed %u)", seed);
        Compare(what, start, kRestoreMPSettings, Menu_RestoreMPSettings);
    }

    Load(&saved);
    printf("[mpmenus] Menu_PrepareBots, Menu_StoreMPSettings, Menu_RestoreMPSettings: %d runs, %d differ\n", g_runs,
           g_differ);
}

void MPMenusShadow_Install(void) {
    char v[16] = "";
    GetPrivateProfileStringA("Settings", "MPMenusShadow", "", v, sizeof(v), ".\\settings.ini");
    if (_stricmp(v, "on") != 0 && strcmp(v, "1") != 0)
        return;
    MPMenusShadow_Run();
}
