#include "LaunchOptions.h"

#include "../actionhelpers.h"
#include "../assets.h"
#include "../game.h"
#include "../input.h"

#include "../../driving/platform/Host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

// ---------------------------------------------------------------------------------------------------------------
// Booting straight into a level, the action engine's counterpart of the driving engine's -mission
// (src/driving/platform/LaunchOptions.cpp).
//
// At boot, GameFlow_Main's state 1 runs bootup_bootup and Boot_LoadPTPData, then Reset_MapLoadSettings loads
// GameState.NextLevelHashcode - the front end (HT_Level_Menu_Pre) unless something set another level. -level sets
// it there, so the level loads as the game's first, through the same ResetMap_LevelToLoad a mission start uses;
// Mission_Init then starts its mission from cold at that part. -difficulty sets GameState.difficultyModifier there
// too, after bootup_bootup's default of 2.
//
// A level asked for is a fresh boot: the launch data a driving mission leaves in psiLaunch.bin is not read
// (ReadLaunchData).
//
// The loader's own two arguments, the XBE and the DLL, are positional and come first; the first argument beginning
// with '-' or '+' ends them (src/loader/loadermain.cpp), and everything from there on is looked at here.
// ---------------------------------------------------------------------------------------------------------------

static const struct { const char *name; uint32_t hash; } kLevels[] = {
    { "HendersonA", HT_Level_HendersonA },
    { "HendersonB", HT_Level_HendersonB },
    { "HendersonC", HT_Level_HendersonC },
    { "HendersonD", HT_Level_HendersonD },
    { "CastleExterior", HT_Level_CastleExterior },
    { "CastleCourtyard", HT_Level_CastleCourtyard },
    { "CastleIndoors1", HT_Level_CastleIndoors1 },
    { "CastleIndoors2", HT_Level_CastleIndoors2 },
    { "TowerA", HT_Level_TowerA },
    { "TowerB", HT_Level_TowerB },
    { "TowerC", HT_Level_TowerC },
    { "PowerStationA1", HT_Level_PowerStationA1 },
    { "PowerStationA2", HT_Level_PowerStationA2 },
    { "Tower2A", HT_Level_Tower2A },
    { "Tower2B", HT_Level_Tower2B },
    { "Tower2C", HT_Level_Tower2C },
    { "Tower2Elevator", HT_Level_Tower2Elevator },
    { "EvilBase", HT_Level_EvilBase },
    { "EvilSilo", HT_Level_EvilSilo },
    { "EvilBaseC", HT_Level_EvilBaseC },
    { "SpaceStationD", HT_Level_SpaceStationD },
#if NIGHTFIRE_DATA_PS2
    // On the PS2's disc only (docs/ps2-data.md)
    { "RefRoom", HT_Level_RefRoom },
    { "AllCharacters", HT_Level_AllCharacters },
    { "TestRoom2", HT_Level_TestRoom2 },
    { "AllCharacters2", HT_Level_AllCharacters2 },
#endif
};

static uint32_t g_level = 0;        // 0: the front end, as usual
static int g_difficulty = 0;        // 0: the game's default
static std::string g_text;

static void ListLevels(void) {
    printf("[launch] -level takes one of:");
    for (size_t i = 0; i < sizeof(kLevels) / sizeof(kLevels[0]); i++)
        printf("%s %s (0x%08x)", i % 4 == 0 ? "\n[launch]  " : ",", kLevels[i].name, (unsigned)kLevels[i].hash);
    printf("\n[launch] or any level's hashcode, as HT_Level_CastleExterior\n");
}

static uint32_t ParseLevel(const char *text) {
    for (size_t i = 0; i < sizeof(kLevels) / sizeof(kLevels[0]); i++) {
        if (_stricmp(text, kLevels[i].name) == 0)
            return kLevels[i].hash;
    }
    char *end;
    unsigned long hash = strtoul(text, &end, 16);
    if (end != text && *end == '\0' && (hash >> 24) == 0x07)
        return (uint32_t)hash;
    return 0;
}

// Takes the options from a list of words, the first being the program
static void ParseWords(int count, char **words) {
    g_level = 0;
    g_difficulty = 0;
    g_text.clear();
    int i = 1;
    while (i < count && words[i][0] != '-' && words[i][0] != '+')
        i++;   // the loader's positional XBE and DLL
    for (; i < count; i++) {
        bool hasValue = i + 1 < count;
        if (_stricmp(words[i], "-level") == 0 && hasValue) {
            const char *value = words[++i];
            g_level = ParseLevel(value);
            if (g_level == 0) {
                if (_stricmp(value, "list") != 0)
                    printf("[launch] -level %s: not a level\n", value);
                ListLevels();
            }
        } else if (_stricmp(words[i], "-difficulty") == 0 && hasValue) {
            g_difficulty = atoi(words[++i]);
            if (g_difficulty < 1 || g_difficulty > 3) {
                printf("[launch] -difficulty %s: 1 (Operative), 2 (Agent) or 3 (00 Agent)\n", words[i]);
                g_difficulty = 0;
            }
        }
    }

    char text[64] = "";
    if (g_level != 0)
        snprintf(text, sizeof(text), "-level 0x%08x", (unsigned)g_level);
    if (g_difficulty != 0)
        snprintf(text + strlen(text), sizeof(text) - strlen(text), "%s-difficulty %d", text[0] ? " " : "",
                 g_difficulty);
    g_text = text;
    if (!g_text.empty())
        printf("[launch] %s\n", text);
}

void LaunchOptions_Read(void) {
    int count = 0;
    char **words = HostCommandLine(&count);
    if (words == NULL)
        return;
    ParseWords(count, words);
}

const char *LaunchOptions_Text(void) {
    return g_text.c_str();
}

void LaunchOptions_SetFromText(const char *text) {
    std::string copy = std::string("action ") + text;
    char *words[16];
    int count = 0;
    for (char *word = strtok(&copy[0], " "); word != NULL && count < 16; word = strtok(NULL, " "))
        words[count++] = word;
    ParseWords(count, words);
}

uint32_t LaunchOptions_Level(void) {
    return g_level;
}

// A level asked for skips the codename, whose settings are the only thing that turns vibration on (Input_Init leaves
// it off), so it is turned on here as a codename would.
void LaunchOptions_Apply(void) {
    if (g_level != 0) {
        GameState.NextLevelHashcode = (HASHCODE)g_level;
        PlayerInputs[0].vibrationEnabled = 1;
    }
    if (g_difficulty != 0)
        GameState.difficultyModifier = g_difficulty;
}
