#ifndef DRIVING_PLATFORM_LAUNCHOPTIONS_H_
#define DRIVING_PLATFORM_LAUNCHOPTIONS_H_

// driving.exe's command line: which mission (or part of one) to start, at what difficulty, and the game's
// own arguments passed through to its main. See LaunchOptions.cpp. Standalone only - under CXBX the command
// line is CXBX's.

// Reads the command line and registers the launch page hook. Call once, from Inject.
void Inject_LaunchOptions(void);

// The arguments for the game's own main: an argv[0] and whatever flags were not the loader's or ours.
int LaunchOptions_GameArgc(void);
char **LaunchOptions_GameArgv(void);

#endif // DRIVING_PLATFORM_LAUNCHOPTIONS_H_
