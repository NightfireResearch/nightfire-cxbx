#ifndef COMMON_LAUNCHINFO_H_
#define COMMON_LAUNCHINFO_H_

int __stdcall XGetLaunchInfo(int *someIdentifier, void* data);
int __stdcall XLaunchNewImageA(char *executableName,void *launchInfo);

// A chance for the engine to change the launch data page before the game sees it - the driving engine's
// -mission option, say (src/driving/platform/LaunchOptions.cpp). Called with the page as read from
// psiLaunch.bin (fromFile true), or, when there is no file, with a zeroed page (fromFile false); returning
// true then means the hook has filled it in and the game should be told there is launch data after all.
typedef bool (*LaunchPageHook)(void *page, bool fromFile);
void LaunchInfo_SetPageHook(LaunchPageHook hook);

#endif // COMMON_LAUNCHINFO_H_
