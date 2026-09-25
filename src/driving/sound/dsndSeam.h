#ifndef DRIVING_SOUND_DSNDSEAM_H_
#define DRIVING_SOUND_DSNDSEAM_H_

// The driving engine's sound seam: every DirectSound entry point in the XBE is patched, so that the library's
// lower half - which programs the MCPX audio registers and spins on them - is never reached. Silent so far,
// but with the bookkeeping EA's voice allocator depends on. See dsndSeam.cpp, and section 6.2 of
// docs/driving-engine-plan.md. Standalone only.
void Inject_DsndSeam(void);

// Which DirectSound entry points have been reached without an implementation, and how often.
void DsndSeam_ReportMissing(void);

#endif // DRIVING_SOUND_DSNDSEAM_H_
