#include "../../../common/gfx/backendHost.h"

#include "../XboxSettings.h"
#include "../XboxFile.h"   // XboxFile_ReportStreamingIfDue, reported alongside the frame timing

// The action engine's answers to the three questions the shared D3D9 backend asks. Both settings come from
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
