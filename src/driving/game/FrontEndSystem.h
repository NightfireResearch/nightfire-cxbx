#ifndef DRIVING_GAME_FRONTENDSYSTEM_H_
#define DRIVING_GAME_FRONTENDSYSTEM_H_

// ---------------------------------------------------------------------------------------------------------------
// The front end's system helpers (Ghidra: GSystem, GLocale, the CURATOR functions; 0x000e30d0-0x000e3180 and
// 0x000e3af0-0x000e4030): the locale's strings and language, the effects' data path, colour and point maths, the
// mission's gallery and particle files. See FrontEndSystem.cpp.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "../data/CoordConvert.h"         // Coord4

// GLocale::GetLanguage's answer, the languages named after their locale files ("IDL_BRT.loc" ...)
enum GameLanguage : int32_t {
    kLanguageBRT = 0,
    kLanguageFR = 1,
    kLanguageGER = 2,
    kLanguageSPA = 3,
    kLanguageITA = 4,
    kLanguageDUT = 5,
    kLanguageSWE = 6,
    kLanguageENG = 7,
    kLanguageCount = 8,
};

class GSystem {
public:
    // The locale's string, "MISSING" when it has none (the number itself while LocaleShowSymbols is set)
    static const char* LOCALE_Symbol(int symbol);                                               // 0x000e3af0
    // The point rotated by the quaternion and scaled (the scale applied first), then moved by the position
    static void MATH_TransformPoints(Coord4 *point, const Coord4 *position, const Coord4 *orientation,
                                     const Coord4 *scale);                                      // 0x000e3b40
    // The effects' data path before `name`
    static void FILE_FullPathname(const char *name, char *path);                                // 0x000e3c00
    // Each byte from `from` towards `to` by t
    static uint32_t COLOR_Interpolate(uint32_t from, uint32_t to, float t);                     // 0x000e3c30
    // The alpha byte times `alpha`
    static void COLOR_ReduceAlpha(uint32_t *colour, float alpha);                               // 0x000e3cf0
    // The mission is one of the jungle tracks
    static bool IsEARS();                                                                       // 0x000e3d20
    // ... of the snow tracks
    static bool IsSavage();                                                                     // 0x000e3db0
    static const char* GetGalleryName();                                                        // 0x000e3e20
    // The locale's string file, from data/gfx/locale/ (the last one's buffer freed first)
    static void LOCALE_LoadFile(const char *name);                                              // 0x000e3ee0
    static void LOCALE_LoadLanguage(int language);                                              // 0x000e3f30
    // The data path, the effects' random generator seeded, GFX::Init, the language's strings
    static void CURATOR_Init();                                                                 // 0x000e3fd0
};

// The particle file for the mission
const char* GetParticFileForMission();                                                          // 0x000e3e50
// GFX::Shutdown, and the locale's strings freed
void CURATOR_Shutdown();                                                                        // 0x000e3eb0

class GLocale {
public:
    // The launch page's language as a GameLanguage: 6 and anything past 8 are English, 8 Swedish
    static int GetLanguage();                                                                   // 0x000e30d0
    // The movies' folder for the language and the video mode (NULL for a mode with none)
    static const char* GetSubDirectory();                                                       // 0x000e3140
};

extern uint8_t *LocaleFile;             // the language's strings (LOCALE_LoadFile's)
extern int32_t LocaleShowSymbols;       // set: LOCALE_Symbol answers the number (nothing sets it)
extern char LocaleSymbolText[0x80];     // ... written there
extern const char *LocaleMissing;       // "MISSING"
extern char GfxDataPath[0x80];          // CURATOR_Init's "data/gfx/data/"

#endif // DRIVING_GAME_FRONTENDSYSTEM_H_
