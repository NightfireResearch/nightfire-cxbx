#ifndef COMMON_GFX_BACKENDHOST_H_
#define COMMON_GFX_BACKENDHOST_H_

// ---------------------------------------------------------------------------------------------------------------
// The three things the D3D9 backend needs from the engine it is compiled into.
//
// The backend itself is engine-agnostic - it translates Xbox D3D8 calls into Direct3D 9 and knows nothing
// about either game - but it prints a frame-timing line, and what belongs on that line, and whether it is
// wanted at all, is the engine's business. Rather than let the shared file include one engine's settings
// header (which is what tied it to the action engine before it was shared), each engine provides these.
// ---------------------------------------------------------------------------------------------------------------

// Whether to print the periodic frame-timing line at all.
bool GfxHost_PerfLogEnabled(void);

// A frame rate to pace to in place of the video mode's own, or 0 for "use the mode's".
int GfxHost_FpsOverride(void);

// Called once per timing report, for whatever the engine wants to say alongside it.
void GfxHost_ReportPeriodic(void);

#endif // COMMON_GFX_BACKENDHOST_H_
