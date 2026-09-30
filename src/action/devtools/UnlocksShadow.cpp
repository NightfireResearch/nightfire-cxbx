// Shadow test for the progress and unlock functions (src/action/ui/MenuUnlocks.cpp, Menu_GetMPSkins aside): each
// is called through the original and through ours, from the same starting states and over its inputs, and what it
// leaves behind is compared - every player's rewards, the missions open, the multiplayer characters and scenarios
// open, the flags they set - with the result. Run from a menu replay script ("unlockstest", MenuProbe.cpp) once a
// codename is loaded; it puts everything back afterwards.

#include "UnlocksShadow.h"

#include "../../common/xbeOriginal.h"
#include "../game.h"
#include "../ui/ui.h"
#include "../ui/Menu.h"

#include <stdio.h>
#include <string.h>

// Everything the functions write.
struct UnlockState {
    ulong bonus[8];
    uchar spBefore;                     // the byte before sp_level: Menu_SetLevelBonus of an unknown level marks it
    uchar sp[12], chars[29], charsSmall[29], scenarios[13];
    uchar replace, earned, scenery, upgradeFlag;
};

// The lists as the original code sees them (the game's copies, which nothing else uses now) or as ours does.
#define GAME_LIST(address, count) (*(M_ITEM(*)[count])(address))
static M_ITEM *SpLevel(bool game) { return game ? GAME_LIST(0x17c580, 12) : sp_level; }
static M_ITEM *Characters(bool game) { return game ? GAME_LIST(0x17c8e0, 29) : mp_characters; }
static M_ITEM *CharactersSmall(bool game) { return game ? GAME_LIST(0x17cb98, 29) : mp_characters_small; }
static M_ITEM *Scenarios(bool game) { return game ? GAME_LIST(0x17c7a8, 13) : mp_scenario; }

static void Save(UnlockState *s, bool game = false) {
    memset(s, 0, sizeof(*s));   // padding too, since states are compared whole
    memcpy(s->bonus, (void *)0x0025d6e8, sizeof(s->bonus));
    s->spBefore = *(uchar *)0x0017c578;
    for (int i = 0; i < 12; i++) s->sp[i] = ITEM_ENABLED(SpLevel(game)[i]);
    for (int i = 0; i < 29; i++) s->chars[i] = ITEM_ENABLED(Characters(game)[i]);
    for (int i = 0; i < 29; i++) s->charsSmall[i] = ITEM_ENABLED(CharactersSmall(game)[i]);
    for (int i = 0; i < 13; i++) s->scenarios[i] = ITEM_ENABLED(Scenarios(game)[i]);
    s->replace = U8_AT(0x0025d78d);
    s->earned = U8_AT(0x0025d78c);
    s->scenery = U8_AT(0x002456a8);
    s->upgradeFlag = GameState.WeaponUpgradeRelated;
}

static void Load(const UnlockState *s) {
    memcpy((void *)0x0025d6e8, s->bonus, sizeof(s->bonus));
    *(uchar *)0x0017c578 = s->spBefore;
    for (int game = 0; game < 2; game++) {   // both copies: the original runs on the game's, ours on ours
        for (int i = 0; i < 12; i++) ITEM_ENABLED(SpLevel(game)[i]) = s->sp[i];
        for (int i = 0; i < 29; i++) ITEM_ENABLED(Characters(game)[i]) = s->chars[i];
        for (int i = 0; i < 29; i++) ITEM_ENABLED(CharactersSmall(game)[i]) = s->charsSmall[i];
        for (int i = 0; i < 13; i++) ITEM_ENABLED(Scenarios(game)[i]) = s->scenarios[i];
    }
    U8_AT(0x0025d78d) = s->replace;
    U8_AT(0x0025d78c) = s->earned;
    U8_AT(0x002456a8) = s->scenery;
    GameState.WeaponUpgradeRelated = s->upgradeFlag;
}

static int g_runs, g_mismatches;

// Runs 'call' as the original (with the originals of every function in 'originals' in place) and as ours, from
// 'start', and compares.
template <class F>
static void Compare(const char *what, const UnlockState &start, const unsigned *originals, int count, F call) {
    Load(&start);
    unsigned long long theirs;
    {
        XbeOriginalScope s0(originals[0]), s1(count > 1 ? originals[1] : originals[0]),
            s2(count > 2 ? originals[2] : originals[0]);
        theirs = call(true);
    }
    UnlockState a;
    Save(&a, true);
    Load(&start);
    unsigned long long ours = call(false);
    UnlockState b;
    Save(&b);
    g_runs++;
    if (theirs != ours || memcmp(&a, &b, sizeof(a)) != 0) {
        g_mismatches++;
        if (g_mismatches <= 20)
            printf("[unlocks] MISMATCH %s: original %llx, ours %llx%s\n", what, theirs, ours,
                   memcmp(&a, &b, sizeof(a)) != 0 ? ", state differs" : "");
    }
}

typedef uint(__stdcall *GetStatusFn)(void);
typedef void(__cdecl *SetStatusFn)(uint);
typedef void(__cdecl *SetBonusFn)(uint, uint, byte, char);
typedef bool(__cdecl *SetLevelBonusFn)(int, uint, byte);
typedef undefined4(__cdecl *GetUpgradeFn)(uint, byte);
typedef void(__cdecl *UnlockSkinsFn)(byte);
typedef void(__cdecl *UnlockSettingsFn)(void);

void UnlocksShadow_Run(void) {
    const unsigned GET_STATUS = 0x76240, SET_STATUS = 0x762d0, SET_BONUS = 0x7cb10, SET_LEVEL_BONUS = 0x7cbf0,
                   GET_UPGRADE = 0x7ced0, UNLOCK_SKINS = 0x7c680, UNLOCK_SETTINGS = 0x7c9b0;
    g_runs = g_mismatches = 0;

    UnlockState saved;
    Save(&saved);
    UnlockState starts[4];
    starts[0] = saved;                              // the codename as loaded
    starts[1] = saved;                              // nothing earned, only the first missions open
    memset(starts[1].bonus, 0, sizeof(starts[1].bonus));
    memset(starts[1].sp, 0, 12);
    starts[1].sp[0] = starts[1].sp[1] = 1;
    starts[2] = saved;                              // everything
    memset(starts[2].bonus, 0xff, sizeof(starts[2].bonus));
    memset(starts[2].sp, 1, 12);
    starts[3] = saved;                              // a mixture per player, and the replace flag set
    for (int i = 0; i < 8; i++) starts[3].bonus[i] = 0x5a5a1234u * (i + 1) ^ 0x00c0ffeeu;
    for (int i = 0; i < 12; i++) starts[3].sp[i] = (uchar)(i & 1);
    starts[3].replace = 1;

    for (int s = 0; s < 4; s++) {
        const UnlockState &st = starts[s];
        char what[64];
        unsigned o[3];

        o[0] = GET_STATUS;
        Compare("Menu_GetNightfireStatus", st, o, 1, [](bool orig) -> unsigned long long {
            return orig ? ((GetStatusFn)0x76240)() : Menu_GetNightfireStatus(); });

        static const uint statuses[] = { 0, 1, 3, 0x5a5, 0xfff, 0xffffffff, 0x800 };
        for (uint v : statuses) {
            snprintf(what, sizeof(what), "Menu_SetNightfireStatus(0x%x) start %d", v, s);
            o[0] = SET_STATUS;
            Compare(what, st, o, 1, [v](bool orig) -> unsigned long long {
                if (orig) ((SetStatusFn)0x762d0)(v); else Menu_SetNightfireStatus(v); return 0; });
        }

        for (byte p = 0; p < 4; p++)
            for (char orIn = 0; orIn < 2; orIn++) {
                snprintf(what, sizeof(what), "Menu_SetBonus(player %d, or %d) start %d", p, orIn, s);
                o[0] = SET_BONUS;
                Compare(what, st, o, 1, [p, orIn](bool orig) -> unsigned long long {
                    if (orig) ((SetBonusFn)0x7cb10)(0x12345678, 0x9abcdef0, p, orIn);
                    else Menu_SetBonus(0x12345678, 0x9abcdef0, p, orIn);
                    return 0; });
            }

        for (int l = -1; l < 12; l++)
            for (uint medal = 0; medal < 6; medal++) {
                int level = l < 0 ? 0x12345 : (int)sp_level[l].identifier;   // -1: a level not in the list
                snprintf(what, sizeof(what), "Menu_SetLevelBonus(level %d, medal %u) start %d", l, medal, s);
                o[0] = SET_LEVEL_BONUS; o[1] = SET_BONUS;
                Compare(what, st, o, 2, [level, medal](bool orig) -> unsigned long long {
                    return orig ? ((SetLevelBonusFn)0x7cbf0)(level, medal, 1) : Menu_SetLevelBonus(level, medal, 1); });
            }

        for (uint obj = 0; obj < 0x41; obj++)
            for (byte p = 0; p < 4; p++) {
                snprintf(what, sizeof(what), "Menu_GetObjectUpgradeLevel(%u, player %d) start %d", obj, p, s);
                o[0] = GET_UPGRADE;
                Compare(what, st, o, 1, [obj, p](bool orig) -> unsigned long long {
                    return orig ? ((GetUpgradeFn)0x7ced0)(obj, p) : Menu_GetObjectUpgradeLevel(obj, p); });
            }

        static const byte players[] = { 0, 1, 2, 3, 0xff };
        for (byte p : players) {
            snprintf(what, sizeof(what), "Menu_UnlockMPSkins(%d) start %d", p, s);
            o[0] = UNLOCK_SKINS;
            Compare(what, st, o, 1, [p](bool orig) -> unsigned long long {
                if (orig) ((UnlockSkinsFn)0x7c680)(p); else Menu_UnlockMPSkins(p); return 0; });
        }

        snprintf(what, sizeof(what), "Menu_UnlockMPSettings start %d", s);
        o[0] = UNLOCK_SETTINGS;
        Compare(what, st, o, 1, [](bool orig) -> unsigned long long {
            if (orig) ((UnlockSettingsFn)0x7c9b0)(); else Menu_UnlockMPSettings(); return 0; });
    }

    Load(&saved);
    Menu_SetBonus(saved.bonus[0], saved.bonus[1], 0, 0);   // rebuild player 1's upgrades as they were
    printf("[unlocks] 7 functions, 4 starting states: %d runs, %d mismatches\n", g_runs, g_mismatches);
}
