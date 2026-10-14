// The standalone build's entry point (docs/driving/standalone.md): no Driving.xbe mapped, no injection DLL. It does
// what the loader and the XBE's entry did for the injected build - open the window (platform/HostWindow.h), start
// the game's thread at mainXapiStartup (platform/XboxStartup.cpp), and run the window's events.
//
// Not yet done here, and listed in the doc: the kernel the loader provided (src/loader/kernel.cpp, which
// platform/KernelImports.cpp reaches through the import table), and the seams' installation without patching.

#include <stdio.h>

#include "../platform/Host.h"
#include "../platform/HostWindow.h"
#include "../platform/XboxStartup.h"

int main(int argc, char **argv) {
    HostSetCommandLine(argc, argv);
    if (!HostWindow_Open(640, 480, "Nightfire (driving)"))
        return 1;

    HostHandle thread = HostCreateThread(0x10000, mainXapiStartup, NULL, 0, NULL);
    if (thread == NULL) {
        printf("[standalone] the game's thread could not be created (error %lu)\n", HostGetLastError());
        return 1;
    }
    HostCloseHandle(thread);

    HostWindow_Run();
    return 0;
}
