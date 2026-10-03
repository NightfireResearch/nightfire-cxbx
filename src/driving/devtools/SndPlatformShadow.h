#ifndef DRIVING_DEVTOOLS_SNDPLATFORMSHADOW_H_
#define DRIVING_DEVTOOLS_SNDPLATFORMSHADOW_H_

// NIGHTFIRE_SNDPLATFORMSHADOW=1: EA's platform driver (sound/snd/Platform.cpp) against the originals, each function
// run by both on identical snapshots of the driver's state with DirectSound, the mixer and the system calls
// replaced by recorders; the recorded call sequences and the state compared. See SndPlatformShadow.cpp.
void SndPlatformShadow_Run(void);

#endif // DRIVING_DEVTOOLS_SNDPLATFORMSHADOW_H_
