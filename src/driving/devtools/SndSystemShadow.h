#ifndef DRIVING_DEVTOOLS_SNDSYSTEMSHADOW_H_
#define DRIVING_DEVTOOLS_SNDSYSTEMSHADOW_H_

// NIGHTFIRE_SNDSYSSHADOW=1: EA's sound library's system module (sound/snd/System.cpp) against the originals - the
// 64-bit helpers, the random numbers, the lists, the SNDMEMI heap, the server clients, the 100 Hz server and
// init/restore on snapshots with the platform driver replaced by recording fakes. See SndSystemShadow.cpp.
void SndSystemShadow_Run(void);

#endif // DRIVING_DEVTOOLS_SNDSYSTEMSHADOW_H_
