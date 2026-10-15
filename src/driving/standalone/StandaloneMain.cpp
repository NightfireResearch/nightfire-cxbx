// The standalone build's entry point (docs/driving/standalone.md): our code alone, no Driving.xbe mapped or read. It
// does what the loader and the injection's Inject() did for the injected build, by calling our code directly - start
// the kernel (platform/KernelImports.cpp), point D: at the disc, tell the graphics seam where D3D8's state is, read the
// launch options, load FFmpeg - then opens the window, starts the game's thread at mainXapiStartup
// (platform/XboxStartup.cpp) with the stack the XBE asked for, and runs the window's events. The crash reporter
// (CrashReport.h) is installed first; closing the window reports every thread and ends the process, as the loader
// does. driving_standalone_test also installs the heap padding, the x87 call counter, the sound call trace and the
// crash test there (the rest of its harness is called from the game's own hooks).

#include <stdio.h>

#include "CrashReport.h"
#include "../devtools/CrashTest.h"
#include "../devtools/HeapPad.h"
#include "../devtools/SndCallTrace.h"
#include "../devtools/X87Count.h"
#include "../gfx/d3dSeam.h"
#include "../platform/Host.h"
#include "../platform/HostWindow.h"
#include "../platform/KernelImports.h"
#include "../platform/LaunchOptions.h"
#include "../platform/XboxStartup.h"
#include "../platform/XboxXapi.h"
#include "../../common/fmv/Ffmpeg.h"

static const size_t kGameStackSize = 0x10000;   // the XBE header's stack commit

#ifndef _WIN32
// The game's sources are compiled for the MinGW target (cmake/xbox-abi-linux.cmake), whose main calls __main to run
// the static constructors; here the C library's start-up has run them already.
extern "C" void __main(void) {}
#endif

int main(int argc, char **argv) {
    CrashReport_Install();
    HostSetCommandLine(argc, argv);
    KernelStart();
    Xbox_ApplyDiscPathSetting();
    D3dSeam_Init();
    Inject_LaunchOptions();
    Ffmpeg_Require();
    HeapPad_Install();        // NIGHTFIRE_HEAP_PAD=<bytes>, driving_standalone_test only
    X87Count_Install();       // NIGHTFIRE_X87COUNT=1, driving_standalone_test only
    SndCallTrace_Install();   // NIGHTFIRE_SNDTRACE=1, driving_standalone_test only
    CrashTest_Start();        // NIGHTFIRE_CRASHTEST=<ms>, the same

    if (!HostWindow_Open(640, 480, "Nightfire (driving)"))
        return 1;

    HostHandle thread = HostCreateThread(kGameStackSize, mainXapiStartup, NULL, 0, NULL);
    if (thread == NULL) {
        printf("[standalone] the game's thread could not be created (error %lu)\n", HostGetLastError());
        return 1;
    }
    HostCloseHandle(thread);

    HostWindow_Run();
    // The game does not know the window has gone; where each thread was is the record of a freeze
    printf("[standalone] the window was closed\n");
    fflush(stdout);
    CrashReport_Threads();
    HostExitProcess(0);
    return 0;
}
