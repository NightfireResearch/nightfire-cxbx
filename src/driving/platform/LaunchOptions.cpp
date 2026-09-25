#include "LaunchOptions.h"
#include "../../common/launchInfo.h"
#include "../../common/standalone.h"

#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// Choosing a mission from the command line.
//
// The driving engine is told which mission to run by the launch data page the action engine hands it
// (psiLaunch.bin here, see src/common/launchInfo.cpp). main (0x0005a1b0) reads it through
// ApplicationMemoryHeapConfig, then MissionNumToString (0x000596a0) turns the page's MissionNum into the track
// to load - provided the page's hand-over flag (0x002444f0) is set:
//
//     1 paris_mis01     "Paris Prelude"
//     2 uw_mis11        "Deep Descent"
//     3 junglea_mis13a  "Island Infiltration", first part
//     4 jungleb_mis13b  second part
//     5 snow1a_mis3     "Alpine Escape"
//     6 snow2a_mis4     "Enemies Vanquished"
//     7 junglec_mis13c  "Island Infiltration", third part
//     8 snow2a_race     the Alpine race (a cheat in snow2a_mis4 chains to it)
//     anything else     paris_map
//
// A part of a mission is just another number. Winning one reads CHAIN_NEXT_MISSION and
// CHAIN_NEXT_MISSION_NAME from the level's attributes (SMissionManager::Win, 0x000b8574; junglea's says 4),
// and main relaunches DRIVING.XBE with those in the page. So "-mission 4" starts the jungle at its second part,
// exactly as the chain would have.
//
// The options:
//
//     -mission N | track   the mission (1-8) or its track name from the table above
//     -difficulty N        the page's difficulty, 1-4 as the action engine writes it; MissionNumToString folds
//                          it to 0-2 (3 and 4 both become 2)
//
// Both change the page as it is read and nothing else, so everything downstream - the loading screen, the
// mission scripts, ReturnToAction's hand-back - sees the same page the action engine would have written. With
// no psiLaunch.bin, a clean hand-over page stands in (below). A page left by a between-parts relaunch has
// "LOADER READY" at 0x00244044, which main takes to mean "second boot" and skips its own argument parsing for;
// -mission clears it, since the point is a first boot into the named mission.
//
// Any other argument beginning with '-' or '+' is passed to the game's own main, which knows -ntsc, -pal,
// -pal60, +/-streams and -T<track> (the track to load, bypassing the table - but the mission number stays
// the page's). main parses them only when the page has no "LOADER READY".
//
// The loader's own two arguments, the XBE and the DLL, are positional and come first; the first argument
// beginning with '-' or '+' ends them (src/loader/loadermain.cpp), and everything from there on is read here.
// ---------------------------------------------------------------------------------------------------------------

// Offsets into the page, which the game reads at 0x00243b90 (uberBondScoreBuffer).
static const unsigned PAGE_HANDOVER    = 0x002444f0 - 0x00243b90;   // non-zero: the action engine sent us
static const unsigned PAGE_MISSION     = 0x00244504 - 0x00243b90;   // MissionNum
static const unsigned PAGE_DIFFICULTY  = 0x00244514 - 0x00243b90;
static const unsigned PAGE_SECOND_BOOT = 0x00244044 - 0x00243b90;   // "LOADER READY", 12 bytes
static const unsigned PAGE_SECOND_BOOT_BYTES = 12;

static const char *const MISSION_TRACKS[] = {
    NULL, "paris_mis01", "uw_mis11", "junglea_mis13a", "jungleb_mis13b",
    "snow1a_mis3", "snow2a_mis4", "junglec_mis13c", "snow2a_race",
};
static const int MISSION_COUNT = sizeof(MISSION_TRACKS) / sizeof(MISSION_TRACKS[0]) - 1;

// A clean hand-over, as the action engine wrote one to start "Enemies Vanquished": its profile words (options,
// volumes, progress) and the hand-over block. Only the non-zero dwords; -mission replaces the mission number.
static const struct { unsigned offset, value; } DEFAULT_PAGE[] = {
    {0x000, 0x444e4f42}, {0x004, 0x00000100}, {0x080, 0x00000002}, {0x084, 0x00000001}, {0x088, 0x00000001},
    {0x090, 0x00000001}, {0x094, 0x00000001}, {0x098, 0x00000001}, {0x09c, 0x00000001}, {0x0a0, 0x00000001},
    {0x0a8, 0x00000001}, {0x0b0, 0x00000001}, {0x0c0, 0x0000000a}, {0x0c4, 0x0000000a}, {0x0d4, 0x00000001},
    {0x0d8, 0x00000001}, {0x0dc, 0x00000002}, {0x0e8, 0x0000004b}, {0x0ec, 0x00000064}, {0x0f0, 0x00000211},
    {0x100, 0x0000004b}, {0x104, 0x00000064}, {0x108, 0x00010000}, {0x110, 0x00000007}, {0x114, 0x01000001},
    {0x118, 0x01010101}, {0x11c, 0x02000000}, {0x120, 0x00000001}, {0x130, 0x00000001}, {0x134, 0x00000001},
    {0x138, 0x00000fff}, {0x13c, 0x00000021}, {0x144, 0x00000001}, {0x148, 0x00000001}, {0x14c, 0x09000006},
    {0x150, 0x00000001}, {0x170, 0x00031487}, {0x180, 0x00045d30}, {0x960, 0x00000001}, {0x964, 0x00000064},
    {0x968, 0x0000004b}, {0x974, 0x00000006}, {0x984, 0x00000001}, {0x994, 0x00000001}, {0x998, 0x00000001},
    {0xa18, 0x00000001}, {0xa1c, 0x00000002}, {0xa20, 0x00000002}, {0xa28, 0x00000001}, {0xa40, 0x00000001},
    {0xa48, 0x00000001},
};

static int g_mission = 0;       // 0: whatever the page says
static int g_difficulty = 0;    // 0: whatever the page says

#define MAX_GAME_ARGS 16
static char *g_gameArgv[MAX_GAME_ARGS + 1];
static int g_gameArgc = 0;

static void SetWord(void *page, unsigned offset, unsigned value) {
    *(unsigned *)((unsigned char *)page + offset) = value;
}

static bool ApplyToPage(void *page, bool fromFile) {
    if (g_mission == 0 && g_difficulty == 0)
        return false;   // nothing asked of us: the page, or its absence, stands

    if (!fromFile) {
        for (size_t i = 0; i < sizeof(DEFAULT_PAGE) / sizeof(DEFAULT_PAGE[0]); i++)
            SetWord(page, DEFAULT_PAGE[i].offset, DEFAULT_PAGE[i].value);
        printf("[launch] no psiLaunch.bin - starting from a clean hand-over page\n");
    }
    if (g_mission != 0) {
        SetWord(page, PAGE_HANDOVER, 1);
        SetWord(page, PAGE_MISSION, (unsigned)g_mission);
        memset((unsigned char *)page + PAGE_SECOND_BOOT, 0, PAGE_SECOND_BOOT_BYTES);
        printf("[launch] -mission: mission %d, %s\n", g_mission, MISSION_TRACKS[g_mission]);
    }
    if (g_difficulty != 0) {
        SetWord(page, PAGE_DIFFICULTY, (unsigned)g_difficulty);
        printf("[launch] -difficulty: %d\n", g_difficulty);
    }
    fflush(stdout);
    return true;
}

static int ParseMission(const char *text) {
    char *end = NULL;
    long number = strtol(text, &end, 10);
    if (end != text && *end == '\0')
        return (number >= 1 && number <= MISSION_COUNT) ? (int)number : 0;
    for (int i = 1; i <= MISSION_COUNT; i++)
        if (_stricmp(text, MISSION_TRACKS[i]) == 0)
            return i;
    return 0;
}

static void PrintMissions(void) {
    printf("[launch] missions: 1 paris_mis01, 2 uw_mis11, 3 junglea_mis13a, 4 jungleb_mis13b, 5 snow1a_mis3,\n"
           "[launch]           6 snow2a_mis4, 7 junglec_mis13c, 8 snow2a_race\n");
}

static char *Narrow(const wchar_t *wide) {
    int length = WideCharToMultiByte(CP_ACP, 0, wide, -1, NULL, 0, NULL, NULL);
    char *text = (char *)malloc(length > 0 ? length : 1);
    if (length <= 0 || WideCharToMultiByte(CP_ACP, 0, wide, -1, text, length, NULL, NULL) <= 0)
        text[0] = '\0';
    return text;
}

void Inject_LaunchOptions(void) {
    g_gameArgv[0] = (char *)"D:\\DRIVING.XBE";
    g_gameArgc = 1;
    if (!Xbox_RunningStandalone())
        return;

    int count = 0;
    wchar_t **wide = CommandLineToArgvW(GetCommandLineW(), &count);
    if (wide == NULL)
        return;

    int first = 1;
    while (first < count && wide[first][0] != L'-' && wide[first][0] != L'+')
        first++;   // the loader's positional XBE and DLL

    for (int i = first; i < count; i++) {
        char *arg = Narrow(wide[i]);
        if (arg[0] != '-' && arg[0] != '+') {
            printf("[launch] %s: not an option - ignored\n", arg);
            continue;
        }

        if (_stricmp(arg, "-mission") == 0 || _stricmp(arg, "-difficulty") == 0) {
            bool mission = _stricmp(arg, "-mission") == 0;
            const char *value = (i + 1 < count) ? Narrow(wide[++i]) : "";
            if (mission) {
                g_mission = ParseMission(value);
                if (g_mission == 0) {
                    printf("[launch] -mission %s: not a mission number or track - ignored\n", value);
                    PrintMissions();
                }
            } else {
                g_difficulty = atoi(value);
                if (g_difficulty < 1 || g_difficulty > 4) {
                    printf("[launch] -difficulty %s: expected 1 to 4 - ignored\n", value);
                    g_difficulty = 0;
                }
            }
            continue;
        }

        if (g_gameArgc < MAX_GAME_ARGS)
            g_gameArgv[g_gameArgc++] = arg;
        else
            printf("[launch] too many arguments for the game; %s dropped\n", arg);
    }
    g_gameArgv[g_gameArgc] = NULL;
    LocalFree(wide);

    LaunchInfo_SetPageHook(ApplyToPage);
}

int LaunchOptions_GameArgc(void) {
    return g_gameArgc;
}

char **LaunchOptions_GameArgv(void) {
    return g_gameArgv;
}
