#include "../../../common/gfx/backendHost.h"

#include <stdlib.h>

#include "../XboxSettings.h"
#include "../../devtools/Demo.h"
#include "../XboxFile.h"   // XboxFile_ReportStreamingIfDue, reported alongside the frame timing
#ifdef NIGHTFIRE_SDL
#include "../../../driving/platform/HostSdl.h"
#include "../../../driving/platform/HostWindow.h"
#endif

// The action engine's answers to what the shared D3D9 backend asks of it. Both settings come from
// settings.ini (XboxSettings.cpp), and the periodic report is the big-file streaming summary, which is worth
// seeing next to the frame timing because a hitch is usually one or the other.

bool GfxHost_PerfLogEnabled(void) {
    return Settings_GetPerfLog();
}

int GfxHost_FpsOverride(void) {
    return Settings_GetFPSOverride();
}

void GfxHost_ReportPeriodic(void) {
    XboxFile_ReportStreamingIfDue();
}

int GfxHost_PresentEvery(void) {
    return Demo_PresentEvery();   // a time demo's playback with NIGHTFIRE_DEMO_FAST, otherwise 0
}

// Surfaces the backend hands the game are the C heap's: no run of this engine depends on where they are.
void *GfxHost_Alloc(size_t bytes) {
    return calloc(1, bytes);
}

void GfxHost_Free(void *memory) {
    free(memory);
}

#ifdef NIGHTFIRE_SDL
void *GfxHost_Window(void) {
    return HostWindow_Native();
}

void GfxHost_SizeWindow(uint32_t width, uint32_t height) {
    HostWindow_SetClientSize(width, height);
}

void *GfxHost_SdlWindow(void) {
    return HostWindowSdl_Window();
}
#endif
