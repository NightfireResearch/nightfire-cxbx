#ifndef ACTION_ENGINE_LAUNCHOPTIONS_H_
#define ACTION_ENGINE_LAUNCHOPTIONS_H_

#include <stdint.h>

// action.exe's command line: a level to boot straight into, and the difficulty to play it at. See LaunchOptions.cpp.
//
//     -level NAME | 0xHASH    e.g. -level CastleExterior, -level 0x07000005 (-level list prints the names)
//     -difficulty N           1 Operative, 2 Agent, 3 00 Agent

// Reads the command line. Call once, from Inject.
void LaunchOptions_Read(void);

// The options as text, for a time demo to keep, and setting them from that text instead of the command line
// (devtools/Demo.cpp).
const char *LaunchOptions_Text(void);
void LaunchOptions_SetFromText(const char *text);

// The level asked for, or 0
uint32_t LaunchOptions_Level(void);

// From GameFlow_Main's boot state, just before Reset_MapLoadSettings picks the first level: the level and
// difficulty asked for, in place of the front end and the game's default.
void LaunchOptions_Apply(void);

#endif // ACTION_ENGINE_LAUNCHOPTIONS_H_
