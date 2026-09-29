// Shadow test for the secret-code check (src/action/ui/ui_secrets.cpp): every code in the game's own table, and a
// few that are not codes, from several starting states, through the original Menu_SpecialCodenameCheck and ours,
// comparing what each leaves behind - the codename's bonus, the missions' enabled flags and the result. Run from a
// menu replay script ("secretstest", MenuProbe.cpp) once a codename is loaded; it puts the state back afterwards.

#include "SecretsShadow.h"

#include "../../common/xbeOriginal.h"
#include "../ui/ui.h"
#include "../ui/Menu.h"

#include <stdio.h>
#include <string.h>

void Menu_SetBonus(uint lo, uint hi, byte param_3, char param_4);

static const unsigned kSpecialCodenameCheck = 0x0007d110;
static const unsigned kUpgradeCheat = 0x0007cfb0;
static const unsigned kCodesFirst = 0x00160d90, kCodesEnd = 0x00160f68;   // the code strings in .rdata

#define cn_bonus (*(uint64_t *)0x0025d6e8)

typedef uint64_t(__cdecl *CheckFn)(byte *);

struct State {
    uint64_t bonus;
    uchar enabled[12];
};

static void Save(State *s) {
    s->bonus = cn_bonus;
    for (int i = 0; i < 12; i++)
        s->enabled[i] = *(uchar *)&sp_level[i].enabled;
}

static void Load(const State *s) {
    for (int i = 0; i < 12; i++)
        *(uchar *)&sp_level[i].enabled = s->enabled[i];
    Menu_SetBonus((uint)s->bonus, (uint)(s->bonus >> 32), 0, 0);   // also rebuilds the upgrades from it
}

static bool Same(const State *a, const State *b) {
    return a->bonus == b->bonus && memcmp(a->enabled, b->enabled, 12) == 0;
}

void SecretsShadow_Run(void) {
    // The codes, as the game has them.
    const char *codes[80];
    int count = 0;
    for (unsigned at = kCodesFirst; at < kCodesEnd && count < 70;) {
        const char *s = (const char *)at;
        size_t n = strlen(s);
        if (n > 0 && strcmp(s, "EUROCOM") != 0)
            codes[count++] = s;
        at += (unsigned)((n + 4) & ~3u);   // each string starts on a 4-byte boundary
    }
    codes[count++] = "TNT";   // the two short enough to be stored as immediates in the code, not in .rdata
    codes[count++] = "ZAP";
    codes[count++] = "passport";   // case matters
    codes[count++] = "PASSPORTS";
    codes[count++] = "";
    codes[count++] = "BOND";

    State saved;
    Save(&saved);
    State starts[4];
    starts[0] = saved;                                    // the codename as loaded
    starts[1].bonus = 0;                                  // nothing earned, only the first mission open
    memset(starts[1].enabled, 0, 12);
    starts[1].enabled[0] = 1;
    starts[2].bonus = ~0ull;                              // everything
    memset(starts[2].enabled, 1, 12);
    starts[3].bonus = 0x00005a5a0000a5a5ull;              // a mixture, and no mission open at all
    memset(starts[3].enabled, 0, 12);

    int runs = 0, mismatches = 0;
    for (int s = 0; s < 4; s++)
        for (int c = 0; c < count; c++) {
            char code[32];
            snprintf(code, sizeof(code), "%s", codes[c]);

            Load(&starts[s]);
            uint64_t theirs;
            {
                XbeOriginalScope original(kSpecialCodenameCheck), upgrades(kUpgradeCheat);
                theirs = ((CheckFn)kSpecialCodenameCheck)((byte *)code);
            }
            State after_theirs;
            Save(&after_theirs);

            Load(&starts[s]);
            uint64_t ours = Menu_SpecialCodenameCheck((byte *)code);
            State after_ours;
            Save(&after_ours);

            runs++;
            if (theirs != ours || !Same(&after_theirs, &after_ours)) {
                mismatches++;
                printf("[secrets] MISMATCH start %d code \"%s\": original %llu bonus %016llx, ours %llu bonus %016llx\n", s,
                       code, theirs, after_theirs.bonus, ours, after_ours.bonus);
            }
        }
    Load(&saved);
    printf("[secrets] %d codes from the game's table + 4 others, 4 starting states: %d runs, %d mismatches\n",
           count - 4, runs, mismatches);
}
