#include "../../common/gfx/backendHost.h"

#include "d3dSeam.h"

// The driving engine's answers to the three questions the shared D3D9 backend asks.
//
// Both settings are the action engine's settings.ini in that engine (XboxSettings.cpp); the driving engine
// has no settings file yet, so these are the defaults that file would have given. The frame-timing line is
// on rather than off, because during bring-up "is it drawing, and how fast" is the question being asked
// every time - when a settings file arrives, this is what it should read.

bool GfxHost_PerfLogEnabled(void) {
    return true;
}

int GfxHost_FpsOverride(void) {
    return 0;   // pace to the video mode's own rate
}

void GfxHost_ReportPeriodic(void) {
    // The action engine reports big-file streaming here. For the driving engine the useful thing to see
    // beside the frame timing, while the graphics seam is being filled in, is which D3D8 entry points the
    // game has reached that the seam does not implement yet.
    D3dSeam_ReportMissing();
}
