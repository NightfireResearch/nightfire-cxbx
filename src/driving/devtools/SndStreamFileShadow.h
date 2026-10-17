#ifndef DRIVING_DEVTOOLS_SNDSTREAMFILESHADOW_H_
#define DRIVING_DEVTOOLS_SNDSTREAMFILESHADOW_H_

// NIGHTFIRE_SNDSTREAMFILESHADOW=1: EA's STREAM, the file side of sound streaming (sound/snd/Stream.cpp), against
// the originals - scripted sequences on disc stream files behind a recording fake FILESYS. See
// SndStreamFileShadow.cpp.
void SndStreamFileShadow_Run(void);

#endif // DRIVING_DEVTOOLS_SNDSTREAMFILESHADOW_H_
