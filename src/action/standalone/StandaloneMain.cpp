// The standalone build's entry point (docs/action-standalone.md): our code alone, no default.xbe mapped or read. It
// does what the loader (src/loader/loadermain.cpp) and the injection's Inject() did for the injected build, by calling
// our code directly - read the launch options, point D: at the disc, load FFmpeg - then opens the render window the
// D3D9 backend presents into (the host's: Win32's, or SDL3's with NIGHTFIRE_SDL), starts the game's thread through
// Xbox_Entry (engine/XboxStartup.cpp), and runs the window's events on this thread. The crash reporter (the driving
// engine's, src/driving/standalone/CrashReport.h) is installed first; closing the window reports every thread and ends
// the process, as the loader does.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "../devtools/Demo.h"
#include "../engine/Fmv.h"
#include "../engine/LaunchOptions.h"
#include "../engine/XboxSettings.h"
#include "../engine/XboxStartup.h"
#include "../memory.h"
#include "../../common/console.h"
#include "../../common/version.h"
#include "../../common/xboxPath.h"
#include "../../driving/platform/Host.h"
#include "../../driving/platform/HostWindow.h"
#include "../../driving/standalone/CrashReport.h"

#ifndef _WIN32
// The C library's start-up has run the static constructors; a MinGW-targeted object would ask for __main to
extern "C" void __main(void) {}
#endif

// NIGHTFIRE_REPORT_AFTER=<seconds>: where every other thread is then, for a run that stops getting anywhere
static HostDword __stdcall ThreadReport(void *seconds) {
    HostSleep((HostDword)(uintptr_t)seconds * 1000);
    CrashReport_Threads();
    return 0;
}

static void StartThreadReport(void) {
    const char *after = getenv("NIGHTFIRE_REPORT_AFTER");
    if (after != NULL)
        HostCloseHandle(HostCreateThread(0x10000, ThreadReport, (void *)(uintptr_t)atoi(after), 0, NULL));
}

int main(int argc, char **argv) {
    CrashReport_Install();
    HostSetCommandLine(argc, argv);
    EnsureConsoleOutput();
    printf("[standalone] Nightfire %s (action engine), no XBE\n", NightfireVersion);
    StartThreadReport();

    // -level and -difficulty; then a time demo's mode (action_standalone_test): a playback moves into its sandbox
    // before settings.ini is read
    LaunchOptions_Read();
    Demo_Init();
    Xbox_SetDiscRoot(Settings_GetDiscPath());
    Fmv_Init();

    Mem_TakeHeap();

    // The client area is the back buffer's size; the backend resizes it to the device's (d3d9Backend.cpp)
    if (!HostWindow_Open(640, 480, "007: Nightfire"))
        return 1;

    Xbox_Entry();

    HostWindow_Run();
    // The game does not know the window has gone and would carry on drawing into nothing
    printf("[standalone] the render window was closed\n");
    fflush(stdout);
    CrashReport_Threads();
    HostExitProcess(0);
    return 0;
}
